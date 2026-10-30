#include "AFSK.h"
#include "AX25.h"
#include "CRC-CCIT.h"

#include <math.h>
#include <string.h>
#include <esp_arduino_version.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "NUSA APRS REV1A modem requires classic ESP32 (WROOM/WROVER/DevKit), not C3/S3/S2."
#endif

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#include "esp_adc/adc_continuous.h"
#include "driver/dac_continuous.h"
#else
#include "driver/i2s.h"
#include "driver/adc.h"
#endif

bool input_HPF = true;
bool hw_afsk_dac_isr = false;
Afsk *AFSK_modem = nullptr;

static uint8_t s_txLevel = 65;
static uint16_t s_rxSquelch = 55;
static uint16_t s_rxDecoderGain = 0; // 0=AUTO, manual range 20..500%
static volatile uint16_t s_appliedRxGain = 100;
static int32_t s_agcEnvQ8 = 64L << 8;
static volatile uint32_t s_adcClipSamples = 0;
static int s_pttPin = PTT_PIN_DEFAULT;
static bool s_pttActiveHigh = true;
static int s_rxLedPin = RX_LED_PIN_DEFAULT;
static int s_txLedPin = TX_LED_PIN_DEFAULT;
static volatile bool s_tx = false;
static volatile bool s_channelBusy = false;
static volatile uint16_t s_audioLevel = 0;
static volatile uint16_t s_audioPeak = 0;
static uint16_t s_audioPeakWork = 0;
static uint16_t s_audioDiagCount = 0;
static volatile uint16_t s_adcRawMin = 4095;
static volatile uint16_t s_adcRawMax = 0;
static uint16_t s_adcRawMinWork = 4095;
static uint16_t s_adcRawMaxWork = 0;
static uint32_t s_adcDiagCount = 0;

// Decoder A = original MicroModem delayed-sample discriminator.
static volatile uint32_t s_goodFrames = 0;
static volatile uint32_t s_badCrcFrames = 0;
static volatile uint32_t s_primaryFlags = 0;
static volatile uint32_t s_primaryCandidates = 0;

// Decoder B = REV1L live-Tait/UV-5RH validated Bell-202 correlation + run-length decoder.
static volatile uint32_t s_altGoodFrames = 0;
static volatile uint32_t s_altBadCrcFrames = 0;
static volatile uint32_t s_altFlags = 0;
static volatile uint32_t s_altCandidates = 0;
static volatile uint32_t s_altTransitions = 0;
static volatile uint32_t s_altMarkScore = 0;
static volatile uint32_t s_altSpaceScore = 0;
static volatile uint16_t s_altConfidence = 0;
// REV1L Decoder-B tone-score banks:
//   B-RAW   = equal MARK/SPACE score.
//   B-EQ125 = SPACE score +25%.
//   B-EQ130 = SPACE score +30%.
// The user's clean live Tait->GPIO36 UV-5RH capture showed MARK around 5.2 dB
// stronger than SPACE, while the original HDSDR reference was only ~1.4 dB
// imbalanced. The fixed equal-score classifier therefore made wrong tone
// decisions even though both tones were present. These compensated banks are
// intentionally limited to Decoder B; Decoder A remains untouched.
// Per-path counters count CRC results before the cross-path duplicate guard.
static volatile uint32_t s_altBankGood[3] = {0,0,0};
static volatile uint32_t s_altBankBad[3] = {0,0,0};
static volatile uint32_t s_dualDuplicates = 0;
static volatile uint8_t s_lastFrameDecoder = 0; // 0=none, 1=A, 2=B-RAW, 3=B-EQ125, 4=B-EQ130
static volatile uint8_t s_lastReadDecoder = 0;  // decoder of frame most recently popped by AX25

static TaskHandle_t s_rxTask = nullptr;
static volatile bool s_pauseRx = false;
static volatile bool s_rxInRead = false;

struct RxFrameSlot {
    uint16_t len;
    uint8_t decoder; // 1=A, 2=B
    uint8_t data[AX25_MAX_FRAME_LEN];
};
static RxFrameSlot s_frameQ[4];
static volatile uint8_t s_qHead = 0;
static volatile uint8_t s_qTail = 0;
static portMUX_TYPE s_qMux = portMUX_INITIALIZER_UNLOCKED;

// Short modem-level duplicate guard. It prevents Decoder A and Decoder B
// from emitting the same RF frame twice.
static uint8_t s_lastQueuedFrame[AX25_MAX_FRAME_LEN];
static uint16_t s_lastQueuedLen = 0;
static uint32_t s_lastQueuedMs = 0;

// RX demodulation follows the proven MicroModem delayed-sample discriminator
// at 9600 samples/s (8 samples per 1200-baud bit).
static const int RX_SAMPLES_PER_BIT = 8;
static const int RX_DELAY_SAMPLES = RX_SAMPLES_PER_BIT / 2;
static const int RX_PHASE_BITS = 8;
static const int RX_PHASE_INC = 1;
static const int RX_PHASE_MAX = RX_SAMPLES_PER_BIT * RX_PHASE_BITS; // 64
static const int RX_PHASE_THRESHOLD = RX_PHASE_MAX / 2;
static int8_t s_delay[RX_DELAY_SAMPLES];
static uint8_t s_delayPos = 0;

// HDLC decoder state. The newest 8 raw bits are withheld so a flag is never
// accidentally delivered as frame data.
static uint8_t s_pendingBits[8];
static uint8_t s_pendingCount = 0;
static bool s_receiving = false;
static uint8_t s_ones = 0;
static uint8_t s_curByte = 0;
static uint8_t s_bitPos = 0;
static uint8_t s_rxFrame[AX25_MAX_FRAME_LEN + 2];
static size_t s_rxFrameLen = 0;

// ---------------------------------------------------------------------------
// REV1L Decoder B: live Tait/UV-5RH validated Bell-202 correlation + run-length RX.
//
// The run-length NRZI recovery remains because it decoded the original HDSDR
// test vector and many live stations. REV1L changes the tone decision itself:
// the clean on-device UV-5RH capture measured ~1167 Hz MARK and ~2208 Hz SPACE,
// but SPACE arrived several dB weaker through the Tait flat-audio chain.
// Equal raw energy therefore biased decisions toward MARK. Three inexpensive
// score hypotheses are evaluated in parallel (RAW, SPACE+25%, SPACE+30%) while
// keeping the same 7.70..8.40 samples/bit run-length timing lanes.
// Decoder A / MicroModem remains untouched.
// ---------------------------------------------------------------------------
static const uint8_t ALT_WINDOW = 8;
static const uint8_t ALT_BANKS = 3;
static const uint8_t ALT_LANES = 8;
// Clock sweep widened around the Python/HDSDR-proven 1200-baud run-length
// range (12 kHz path 9.68..10.32 samples/bit => 9.6 kHz path 7.744..8.256).
static const uint16_t ALT_SPS_Q8[ALT_LANES] = {
    1971U, // 7.70
    1997U, // 7.80
    2022U, // 7.90
    2048U, // 8.00
    2074U, // 8.10
    2099U, // 8.20
    2125U, // 8.30
    2150U  // 8.40
};

static const int8_t ALT_MARK_COS[8] = {127,90,0,-90,-127,-90,0,90};
static const int8_t ALT_MARK_SIN[8] = {0,90,127,90,0,-90,-127,-90};
static const int8_t ALT_SPACE_COS[48] = {
    127,17,-123,-49,110,77,-90,-101,63,117,-33,-126,
    0,126,33,-117,-64,101,90,-77,-110,49,123,-17,
    -127,-17,123,49,-110,-77,90,101,-63,-117,33,126,
    0,-126,-33,117,64,-101,-90,77,110,-49,-123,17
};

static const int8_t ALT_SPACE_SIN[48] = {
    0,126,33,-117,-64,101,90,-77,-110,49,123,-17,
    -127,-17,123,49,-110,-77,90,101,-63,-117,33,126,
    0,-126,-33,117,64,-101,-90,77,110,-49,-123,17,
    127,17,-123,-49,110,77,-90,-101,63,117,-33,-126
};

struct AltLane {
    uint16_t spsQ8;
    bool haveRun;
    uint8_t pendingBits[8];
    uint8_t pendingCount;
    bool receiving;
    uint8_t ones;
    uint8_t curByte;
    uint8_t bitPos;
    uint8_t frame[AX25_MAX_FRAME_LEN + 2];
    size_t frameLen;
};

struct AltBank {
    AltLane lane[ALT_LANES];
    int8_t samples[ALT_WINDOW];
    uint8_t writePos;
    uint8_t filled;
    uint32_t sampleNo;
    uint8_t toneHist;
    bool haveTone;
    uint8_t prevTone;
    uint16_t runLen;
    int32_t deempQ8;
};

static AltBank s_altBank[ALT_BANKS];

// SPACE score weighting for the three Decoder-B hypotheses.
// 100 = RAW, 125 = +25%, 130 = +30%.
// Offline replay of the clean live UV-5RH GPIO36 capture decoded successfully
// with 125% and 130%; RAW did not.
static const uint16_t ALT_SPACE_WEIGHT_PCT[ALT_BANKS] = {100U, 125U, 130U};

static uint8_t s_altLastGoodFrame[AX25_MAX_FRAME_LEN];
static uint16_t s_altLastGoodLen = 0;
static uint32_t s_altLastGoodMs = 0;
static uint8_t s_altBankLastGoodFrame[ALT_BANKS][AX25_MAX_FRAME_LEN];
static uint16_t s_altBankLastGoodLen[ALT_BANKS] = {0,0,0};
static uint32_t s_altBankLastGoodMs[ALT_BANKS] = {0,0,0};

// Demodulator state after 4:1 decimation (38.4 kHz -> 9.6 kHz).
static int32_t s_dc = 2048;
static int32_t s_decimSum = 0;
static uint8_t s_decimCount = 0;
static uint32_t s_lastBusyMs = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static adc_continuous_handle_t s_adcHandle = nullptr;
#else
static bool s_i2sRxInstalled = false;
#endif

// ---------------------------------------------------------------------------
// REV1L on-device RX capture. The goal is to capture the exact audio that the
// decoder receives from the Tait flat/discriminator output, including 250 ms
// BEFORE the BUSY trigger. This removes guesswork when a particular HT (UV-5RH)
// is not decoded even though other stations are.
// ---------------------------------------------------------------------------
static const size_t RXCAP_PRE_SAMPLES = 2400U;   // 0.25 s @ 9600 Hz
static const size_t RXCAP_TOTAL_SAMPLES = 24000U; // 2.50 s @ 9600 Hz
static int16_t s_rxCapPre[RXCAP_PRE_SAMPLES];
static size_t s_rxCapPreWrite = 0;
static bool s_rxCapPreFull = false;
static int16_t *s_rxCapBuf = nullptr;
static volatile size_t s_rxCapCount = 0;
static volatile bool s_rxCapArmed = false;
static volatile bool s_rxCapActive = false;
static volatile bool s_rxCapReady = false;

static inline int16_t rxCapScale(int16_t x) {
    int32_t v = (int32_t)x * 64L;
    if (v > 32767L) v = 32767L;
    if (v < -32768L) v = -32768L;
    return (int16_t)v;
}

static void rxCapFeed(int16_t x, bool trigger) {
    // Always keep a short pre-trigger history, even when the decoder is idle.
    s_rxCapPre[s_rxCapPreWrite] = rxCapScale(x);
    s_rxCapPreWrite = (s_rxCapPreWrite + 1U) % RXCAP_PRE_SAMPLES;
    if (s_rxCapPreWrite == 0U) s_rxCapPreFull = true;

    if (s_rxCapArmed && !s_rxCapActive && trigger && s_rxCapBuf) {
        size_t preCount = s_rxCapPreFull ? RXCAP_PRE_SAMPLES : s_rxCapPreWrite;
        size_t start = s_rxCapPreFull ? s_rxCapPreWrite : 0U;
        for (size_t i = 0; i < preCount && i < RXCAP_TOTAL_SAMPLES; ++i) {
            size_t src = (start + i) % RXCAP_PRE_SAMPLES;
            s_rxCapBuf[i] = s_rxCapPre[src];
        }
        s_rxCapCount = preCount;
        s_rxCapActive = true;
        s_rxCapArmed = false;
        s_rxCapReady = false;
    } else if (s_rxCapActive && s_rxCapBuf) {
        size_t n = s_rxCapCount;
        if (n < RXCAP_TOTAL_SAMPLES) {
            s_rxCapBuf[n] = rxCapScale(x);
            s_rxCapCount = n + 1U;
        }
        if (s_rxCapCount >= RXCAP_TOTAL_SAMPLES) {
            s_rxCapActive = false;
            s_rxCapReady = true;
        }
    }
}

static inline void setLed(int pin, bool on) {
    if (pin >= 0) digitalWrite(pin, on ? HIGH : LOW);
}

static inline void setRxBusy(bool busy) {
    if (s_channelBusy != busy) {
        s_channelBusy = busy;
        setLed(s_rxLedPin, busy);
    }
}

static void resetHdlc() {
    s_pendingCount = 0;
    s_receiving = false;
    s_ones = 0;
    s_curByte = 0;
    s_bitPos = 0;
    s_rxFrameLen = 0;
}

static void resetAltLaneHdlc(AltLane &d) {
    d.pendingCount = 0;
    d.receiving = false;
    d.ones = 0;
    d.curByte = 0;
    d.bitPos = 0;
    d.frameLen = 0;
    d.haveRun = false;
}

static void resetAltDemod() {
    for (uint8_t b = 0; b < ALT_BANKS; ++b) {
        AltBank &bk = s_altBank[b];
        memset(bk.samples, 0, sizeof(bk.samples));
        bk.writePos = 0;
        bk.filled = 0;
        bk.sampleNo = 0;
        bk.toneHist = 0;
        bk.haveTone = false;
        bk.prevTone = 0;
        bk.runLen = 0;
        bk.deempQ8 = 0;
        for (uint8_t i = 0; i < ALT_LANES; ++i) {
            resetAltLaneHdlc(bk.lane[i]);
            bk.lane[i].spsQ8 = ALT_SPS_Q8[i];
        }
    }
}

static void resetDemod() {
    memset(s_delay, 0, sizeof(s_delay));
    s_delayPos = 0;
    s_decimSum = 0;
    s_decimCount = 0;
    s_agcEnvQ8 = 64L << 8;
    s_appliedRxGain = 100;
    if (AFSK_modem) {
        AFSK_modem->iirX[0] = AFSK_modem->iirX[1] = 0;
        AFSK_modem->iirY[0] = AFSK_modem->iirY[1] = 0;
        AFSK_modem->sampledBits = 0;
        AFSK_modem->actualBits = 0;
        AFSK_modem->currentPhase = 0;
    }
    resetHdlc();
    resetAltDemod();
}

static bool enqueueFrame(const uint8_t *data, size_t len, uint8_t decoder) {
    if (!data || len == 0 || len > AX25_MAX_FRAME_LEN) return false;
    const uint32_t now = millis();
    if (s_lastQueuedLen == len &&
        (uint32_t)(now - s_lastQueuedMs) <= 300U &&
        memcmp(s_lastQueuedFrame, data, len) == 0) {
        ++s_dualDuplicates;
        return false;
    }

    bool queued = false;
    portENTER_CRITICAL(&s_qMux);
    uint8_t next = (uint8_t)((s_qTail + 1) % 4);
    if (next != s_qHead) {
        s_frameQ[s_qTail].len = (uint16_t)len;
        s_frameQ[s_qTail].decoder = decoder;
        memcpy(s_frameQ[s_qTail].data, data, len);
        s_qTail = next;
        queued = true;
    }
    portEXIT_CRITICAL(&s_qMux);

    if (queued) {
        memcpy(s_lastQueuedFrame, data, len);
        s_lastQueuedLen = (uint16_t)len;
        s_lastQueuedMs = now;
        s_lastFrameDecoder = decoder;
    }
    return queued;
}

bool AFSK_readFrame(uint8_t *out, size_t *len, size_t maxLen) {
    if (!out || !len) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_qMux);
    if (s_qHead != s_qTail) {
        size_t n = s_frameQ[s_qHead].len;
        if (n <= maxLen) {
            memcpy(out, s_frameQ[s_qHead].data, n);
            *len = n;
            s_lastReadDecoder = s_frameQ[s_qHead].decoder;
            ok = true;
        }
        s_qHead = (uint8_t)((s_qHead + 1) % 4);
    }
    portEXIT_CRITICAL(&s_qMux);
    return ok;
}

static void finishFrameOnFlag() {
    ++s_primaryFlags;
    if (s_receiving && s_bitPos == 0 && s_rxFrameLen >= AX25_MIN_FRAME_LEN) {
        ++s_primaryCandidates;
        uint16_t crc = CRC_CCIT_INIT_VAL;
        for (size_t i = 0; i < s_rxFrameLen; ++i) crc = update_crc_ccit(s_rxFrame[i], crc);
        if (crc == AX25_CRC_CORRECT) {
            if (s_rxFrameLen >= 2) {
                enqueueFrame(s_rxFrame, s_rxFrameLen - 2, 1); // Decoder A, remove FCS
                ++s_goodFrames;
            }
        } else {
            ++s_badCrcFrames;
        }
    }
    // A flag closes the previous frame and opens the next one.
    s_receiving = true;
    s_ones = 0;
    s_curByte = 0;
    s_bitPos = 0;
    s_rxFrameLen = 0;
}

static void emitDataBit(uint8_t bit) {
    if (!s_receiving) return;

    if (bit) {
        ++s_ones;
        if (s_ones >= 6) {
            // HDLC abort / malformed stream. A legal flag never reaches here.
            s_receiving = false;
            s_curByte = 0;
            s_bitPos = 0;
            s_rxFrameLen = 0;
            return;
        }
    } else {
        if (s_ones == 5) {
            // Stuffed zero after five consecutive 1 bits.
            s_ones = 0;
            return;
        }
        s_ones = 0;
    }

    if (bit) s_curByte |= (uint8_t)(1U << s_bitPos);
    ++s_bitPos;
    if (s_bitPos == 8) {
        if (s_rxFrameLen < sizeof(s_rxFrame)) {
            s_rxFrame[s_rxFrameLen++] = s_curByte;
        } else {
            s_receiving = false;
            s_rxFrameLen = 0;
        }
        s_curByte = 0;
        s_bitPos = 0;
    }
}

static bool pendingIsFlag() {
    if (s_pendingCount != 8) return false;
    static const uint8_t flagBits[8] = {0,1,1,1,1,1,1,0};
    for (int i = 0; i < 8; ++i) if (s_pendingBits[i] != flagBits[i]) return false;
    return true;
}

static void feedHdlcBit(uint8_t bit) {
    bit &= 1;
    if (s_pendingCount < 8) {
        s_pendingBits[s_pendingCount++] = bit;
    } else {
        emitDataBit(s_pendingBits[0]);
        memmove(&s_pendingBits[0], &s_pendingBits[1], 7);
        s_pendingBits[7] = bit;
    }

    if (pendingIsFlag()) {
        finishFrameOnFlag();
        s_pendingCount = 0;
    }
}

static inline bool signalTransitioned(uint16_t bits) {
    // Two consecutive discriminator samples must change together. This avoids
    // re-phasing on a single noisy sample.
    return ((((bits) ^ (bits >> 2)) & 0x03U) == 0x03U);
}

static inline bool transitionFound(uint8_t bits) {
    return (((bits ^ (bits >> 1)) & 0x01U) != 0);
}

static bool altGoodIsNew(const uint8_t *data, size_t len) {
    const uint32_t now = millis();
    if (s_altLastGoodLen == len &&
        (uint32_t)(now - s_altLastGoodMs) <= 500U &&
        memcmp(s_altLastGoodFrame, data, len) == 0) return false;
    memcpy(s_altLastGoodFrame, data, len);
    s_altLastGoodLen = (uint16_t)len;
    s_altLastGoodMs = now;
    return true;
}

static bool altBankGoodIsNew(uint8_t bank, const uint8_t *data, size_t len) {
    if (bank >= ALT_BANKS) return false;
    const uint32_t now = millis();
    if (s_altBankLastGoodLen[bank] == len &&
        (uint32_t)(now - s_altBankLastGoodMs[bank]) <= 500U &&
        memcmp(s_altBankLastGoodFrame[bank], data, len) == 0) return false;
    memcpy(s_altBankLastGoodFrame[bank], data, len);
    s_altBankLastGoodLen[bank] = (uint16_t)len;
    s_altBankLastGoodMs[bank] = now;
    return true;
}

static bool altPendingIsFlag(const AltLane &d) {
    if (d.pendingCount != 8) return false;
    static const uint8_t flagBits[8] = {0,1,1,1,1,1,1,0};
    for (uint8_t i = 0; i < 8; ++i) if (d.pendingBits[i] != flagBits[i]) return false;
    return true;
}

static void altFinishFrameOnFlag(AltLane &d, uint8_t bank) {
    ++s_altFlags;
    if (d.receiving && d.bitPos == 0 && d.frameLen >= AX25_MIN_FRAME_LEN) {
        ++s_altCandidates;
        uint16_t crc = CRC_CCIT_INIT_VAL;
        for (size_t i = 0; i < d.frameLen; ++i) crc = update_crc_ccit(d.frame[i], crc);
        if (crc == AX25_CRC_CORRECT && d.frameLen >= 2) {
            const size_t payloadLen = d.frameLen - 2;
            if (altBankGoodIsNew(bank, d.frame, payloadLen)) ++s_altBankGood[bank];
            if (altGoodIsNew(d.frame, payloadLen)) {
                ++s_altGoodFrames;
                // 2=B-RAW, 3=B-EQ125, 4=B-EQ130.
                enqueueFrame(d.frame, payloadLen, (uint8_t)(2U + bank));
            }
        } else {
            if (bank < ALT_BANKS) ++s_altBankBad[bank];
            ++s_altBadCrcFrames;
        }
    }
    d.receiving = true;
    d.ones = 0;
    d.curByte = 0;
    d.bitPos = 0;
    d.frameLen = 0;
}

static void altEmitDataBit(AltLane &d, uint8_t bit) {
    if (!d.receiving) return;
    if (bit) {
        ++d.ones;
        if (d.ones >= 6) {
            d.receiving = false;
            d.curByte = 0;
            d.bitPos = 0;
            d.frameLen = 0;
            return;
        }
    } else {
        if (d.ones == 5) { d.ones = 0; return; }
        d.ones = 0;
    }
    if (bit) d.curByte |= (uint8_t)(1U << d.bitPos);
    if (++d.bitPos == 8) {
        if (d.frameLen < sizeof(d.frame)) d.frame[d.frameLen++] = d.curByte;
        else { d.receiving = false; d.frameLen = 0; }
        d.curByte = 0;
        d.bitPos = 0;
    }
}

static void altFeedHdlcBit(AltLane &d, uint8_t bit, uint8_t bank) {
    bit &= 1U;
    if (d.pendingCount < 8) d.pendingBits[d.pendingCount++] = bit;
    else {
        altEmitDataBit(d, d.pendingBits[0]);
        memmove(&d.pendingBits[0], &d.pendingBits[1], 7);
        d.pendingBits[7] = bit;
    }
    if (altPendingIsFlag(d)) {
        altFinishFrameOnFlag(d, bank);
        d.pendingCount = 0;
    }
}

static inline uint8_t majority3(uint8_t bits) {
    bits &= 0x07U;
    return (bits == 0x07U || bits == 0x06U || bits == 0x05U || bits == 0x03U) ? 1U : 0U;
}

static void altEmitRun(AltLane &d, uint16_t runLen, uint8_t bank) {
    if (runLen == 0U || d.spsQ8 == 0U) return;

    uint32_t scaled = ((uint32_t)runLen << 8);
    uint16_t k = (uint16_t)((scaled + (d.spsQ8 >> 1)) / d.spsQ8);
    if (k < 1U) k = 1U;

    if (k > 12U) {
        const uint16_t keepSps = d.spsQ8;
        resetAltLaneHdlc(d);
        d.spsQ8 = keepSps ? keepSps : 2048U;
        d.haveRun = false;
        return;
    }

    if (!d.haveRun) {
        d.haveRun = true;
        for (uint16_t j = 0; j < k; ++j) altFeedHdlcBit(d, 1U, bank);
        return;
    }

    altFeedHdlcBit(d, 0U, bank);
    for (uint16_t j = 1; j < k; ++j) altFeedHdlcBit(d, 1U, bank);
}

static void processAltBank(AltBank &bk, uint8_t bank, int8_t sample) {
    const uint32_t n = bk.sampleNo++;
    bk.samples[bk.writePos] = sample;
    bk.writePos = (uint8_t)((bk.writePos + 1U) % ALT_WINDOW);
    if (bk.filled < ALT_WINDOW) { ++bk.filled; return; }

    int32_t mi = 0, mq = 0, si = 0, sq = 0;
    for (uint8_t k = 0; k < ALT_WINDOW; ++k) {
        uint8_t pos = (uint8_t)((bk.writePos + ALT_WINDOW - 1U - k) % ALT_WINDOW);
        int16_t v = bk.samples[pos];
        uint32_t sampleIndex = n - k;
        uint8_t mp = (uint8_t)(sampleIndex & 0x07U);
        uint8_t sp = (uint8_t)(sampleIndex % 48U);
        mi += v * ALT_MARK_COS[mp]; mq += v * ALT_MARK_SIN[mp];
        si += v * ALT_SPACE_COS[sp]; sq += v * ALT_SPACE_SIN[sp];
    }

    uint32_t mark = (uint32_t)(abs(mi) + abs(mq)) >> 3;
    uint32_t spaceRaw = (uint32_t)(abs(si) + abs(sq)) >> 3;
    uint32_t space = (uint32_t)(((uint64_t)spaceRaw * (uint64_t)ALT_SPACE_WEIGHT_PCT[bank] + 50ULL) / 100ULL);
    uint32_t total = mark + space;
    uint32_t diff = (mark > space) ? (mark - space) : (space - mark);
    uint16_t confidence = total ? (uint16_t)min(100UL, (diff * 100UL) / total) : 0U;

    // Preserve the familiar dashboard tone metrics from the RAW path only.
    if (bank == 0U) {
        s_altMarkScore = (s_altMarkScore * 7U + mark) / 8U;
        s_altSpaceScore = (s_altSpaceScore * 7U + spaceRaw) / 8U;
        s_altConfidence = confidence;
    }

    uint8_t rawTone = (mark >= space) ? 1U : 0U;
    if (bk.haveTone && confidence < 4U) rawTone = bk.prevTone;
    bk.toneHist = (uint8_t)((bk.toneHist << 1) | rawTone);
    uint8_t tone = majority3(bk.toneHist);

    if (!bk.haveTone) {
        bk.haveTone = true;
        bk.prevTone = tone;
        bk.runLen = 1U;
    } else if (tone == bk.prevTone) {
        if (bk.runLen < 65535U) ++bk.runLen;
    } else {
        const uint16_t completedRun = bk.runLen;
        bk.prevTone = tone;
        bk.runLen = 1U;
        if (bank == 0U) ++s_altTransitions;
        for (uint8_t i = 0; i < ALT_LANES; ++i) altEmitRun(bk.lane[i], completedRun, bank);
    }
}

static void processAltBell202(int8_t sample) {
    // Same audio is classified by three MARK/SPACE score hypotheses.
    // This is intentionally limited to Decoder B; Decoder A is unchanged.
    processAltBank(s_altBank[0], 0U, sample);
    processAltBank(s_altBank[1], 1U, sample);
    processAltBank(s_altBank[2], 2U, sample);
}

static void process9600Sample(int16_t x) {
    const uint32_t now = millis();
    uint16_t amp = (uint16_t)abs((int)x);
    rxCapFeed(x, (amp >= s_rxSquelch && !s_tx));
    s_audioLevel = (uint16_t)((s_audioLevel * 7U + amp) / 8U);
    if (amp > s_audioPeakWork) s_audioPeakWork = amp;
    if (++s_audioDiagCount >= 9600U) {
        s_audioPeak = s_audioPeakWork;
        s_audioPeakWork = 0;
        s_audioDiagCount = 0;
    }

    if (amp >= s_rxSquelch && !s_tx) {
        s_lastBusyMs = now;
        setRxBusy(true);
    } else if ((uint32_t)(now - s_lastBusyMs) > 80U) {
        setRxBusy(false);
        if ((uint32_t)(now - s_lastBusyMs) > 180U) resetDemod();
        return;
    }

    if (!AFSK_modem || s_tx) return;

    // REV1F RX normalizer/AGC.  The squelch and dashboard Audio level above
    // intentionally use the ORIGINAL signal amplitude.  Only the sample fed
    // into the Bell-202 discriminator is normalized.  This keeps the existing
    // squelch calibration meaningful while making the decoder much more
    // tolerant of low-deviation, high-deviation and differently equalized HTs.
    uint16_t absx = (uint16_t)abs((int)x);
    int32_t envTargetQ8 = ((int32_t)absx) << 8;
    if (envTargetQ8 > s_agcEnvQ8) {
        s_agcEnvQ8 += (envTargetQ8 - s_agcEnvQ8) >> 3;  // fast attack
    } else {
        s_agcEnvQ8 += (envTargetQ8 - s_agcEnvQ8) >> 9;  // slow release
    }
    uint16_t env = (uint16_t)(s_agcEnvQ8 >> 8);
    if (env < 1) env = 1;

    uint16_t gainPct = s_rxDecoderGain;
    if (gainPct == 0) {
        // Aim for about +/-96 discriminator counts.  20..500% prevents both
        // excessive boost of noise and hard clipping of strong RX audio.
        uint32_t autoGain = 9600UL / env;
        if (autoGain < 20UL) autoGain = 20UL;
        if (autoGain > 500UL) autoGain = 500UL;
        gainPct = (uint16_t)autoGain;
    }
    s_appliedRxGain = gainPct;
    int32_t normalized = ((int32_t)x * (int32_t)gainPct) / 100L;
    if (normalized > 127) normalized = 127;
    if (normalized < -127) normalized = -127;
    int8_t currentSample = (int8_t)normalized;

    // Decoder B consumes the same normalized sample with independent tone and timing recovery.
    processAltBell202(currentSample);

    // Decoder A / MicroModem discriminator: multiply the current normalized sample by one
    // delayed by half a bit (4 samples at 9600 Hz), then low-pass the product.
    int8_t delayedSample = s_delay[s_delayPos];
    s_delay[s_delayPos] = currentSample;
    s_delayPos = (uint8_t)((s_delayPos + 1U) % RX_DELAY_SAMPLES);

    AFSK_modem->iirX[0] = AFSK_modem->iirX[1];
    AFSK_modem->iirX[1] = ((int16_t)delayedSample * (int16_t)currentSample) >> 2;
    AFSK_modem->iirY[0] = AFSK_modem->iirY[1];
    AFSK_modem->iirY[1] = AFSK_modem->iirX[0] + AFSK_modem->iirX[1] + (AFSK_modem->iirY[0] >> 1);

    AFSK_modem->sampledBits <<= 1;
    AFSK_modem->sampledBits |= (AFSK_modem->iirY[1] > 0) ? 1U : 0U;

    // Digital PLL: nudge the 8-sample bit window toward real tone transitions.
    if (signalTransitioned(AFSK_modem->sampledBits)) {
        if (AFSK_modem->currentPhase < RX_PHASE_THRESHOLD) {
            AFSK_modem->currentPhase += RX_PHASE_INC;
        } else {
            AFSK_modem->currentPhase -= RX_PHASE_INC;
        }
    }

    AFSK_modem->currentPhase += RX_PHASE_BITS;
    if (AFSK_modem->currentPhase >= RX_PHASE_MAX) {
        AFSK_modem->currentPhase %= RX_PHASE_MAX;
        AFSK_modem->actualBits <<= 1;

        // Majority decision over the last three discriminator samples.
        uint8_t bits = (uint8_t)(AFSK_modem->sampledBits & 0x07U);
        if (bits == 0x07 || bits == 0x06 || bits == 0x05 || bits == 0x03) {
            AFSK_modem->actualBits |= 1U;
        }

        // NRZI: no tone transition means data 1; transition means data 0.
        feedHdlcBit(transitionFound(AFSK_modem->actualBits) ? 0U : 1U);
    }
}

static void processAdcRaw(uint16_t raw) {
    if (raw <= 24U || raw >= 4071U) ++s_adcClipSamples;
    if (raw < s_adcRawMinWork) s_adcRawMinWork = raw;
    if (raw > s_adcRawMaxWork) s_adcRawMaxWork = raw;
    if (++s_adcDiagCount >= 38400UL) {
        s_adcRawMin = s_adcRawMinWork;
        s_adcRawMax = s_adcRawMaxWork;
        s_adcRawMinWork = 4095;
        s_adcRawMaxWork = 0;
        s_adcDiagCount = 0;
    }
    // Slow DC tracker: removes the 1.65 V bias while preserving 1200/2200 Hz.
    s_dc += ((int32_t)raw - s_dc) >> 6;
    int16_t centered = (int16_t)((int32_t)raw - s_dc);
    s_decimSum += centered;
    if (++s_decimCount >= 4) {
        int16_t v = (int16_t)(s_decimSum / 4);
        s_decimCount = 0;
        s_decimSum = 0;
        process9600Sample(v);
    }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static bool startAdcDriver() {
    if (s_adcHandle) return true;

    adc_continuous_handle_cfg_t hc{};
    hc.max_store_buf_size = 2048;
    hc.conv_frame_size = 256;
    if (adc_continuous_new_handle(&hc, &s_adcHandle) != ESP_OK) {
        s_adcHandle = nullptr;
        return false;
    }

    adc_digi_pattern_config_t pattern{};
    pattern.atten = ADC_ATTEN_DB_12;
    pattern.channel = ADC_CHANNEL_0; // GPIO36 on classic ESP32
    pattern.unit = ADC_UNIT_1;
    pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    adc_continuous_config_t cfg{};
    cfg.sample_freq_hz = SAMPLERATE;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
    cfg.pattern_num = 1;
    cfg.adc_pattern = &pattern;

    if (adc_continuous_config(s_adcHandle, &cfg) != ESP_OK ||
        adc_continuous_start(s_adcHandle) != ESP_OK) {
        adc_continuous_deinit(s_adcHandle);
        s_adcHandle = nullptr;
        return false;
    }
    return true;
}

static void stopAdcDriver() {
    s_pauseRx = true;
    uint32_t start = millis();
    while (s_rxInRead && (uint32_t)(millis() - start) < 100U) delay(1);
    if (s_adcHandle) {
        adc_continuous_stop(s_adcHandle);
        adc_continuous_deinit(s_adcHandle);
        s_adcHandle = nullptr;
    }
}

static void rxTask(void *) {
    uint8_t buf[512];
    for (;;) {
        s_rxInRead = true;
        if (s_pauseRx || !s_adcHandle || s_tx) {
            s_rxInRead = false;
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        adc_continuous_handle_t localHandle = s_adcHandle;
        uint32_t got = 0;
        esp_err_t err = adc_continuous_read(localHandle, buf, sizeof(buf), &got, 20);
        s_rxInRead = false;
        if (err == ESP_OK && got) {
            for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buf[i];
                uint16_t raw = p->type1.data;
                processAdcRaw(raw);
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

struct DacWriter {
    dac_continuous_handle_t handle = nullptr;
    uint8_t buf[512];
    size_t used = 0;
    uint16_t phase = 0;
    uint16_t phaseInc = MARK_INC;

    bool begin() {
        dac_continuous_config_t cfg{};
        cfg.chan_mask = DAC_CHANNEL_MASK_CH1; // GPIO26 on classic ESP32
        cfg.desc_num = 6;
        cfg.buf_size = 1024;
        cfg.freq_hz = CONFIG_AFSK_DAC_SAMPLERATE;
        cfg.offset = 0;
        cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
        cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
        if (dac_continuous_new_channels(&cfg, &handle) != ESP_OK) return false;
        if (dac_continuous_enable(handle) != ESP_OK) {
            dac_continuous_del_channels(handle);
            handle = nullptr;
            return false;
        }
        return true;
    }

    bool flush() {
        if (!handle || used == 0) return true;
        size_t loaded = 0;
        esp_err_t e = dac_continuous_write(handle, buf, used, &loaded, -1);
        used = 0;
        return e == ESP_OK;
    }

    bool sample(uint8_t v) {
        buf[used++] = v;
        return used < sizeof(buf) ? true : flush();
    }

    bool writeNrziBit(uint8_t nrziDataBit) {
        if (!nrziDataBit) phaseInc = SWITCH_TONE(phaseInc);
        for (uint8_t i = 0; i < SAMPLESPERBIT; ++i) {
            uint8_t raw = sinSample(phase);
            int16_t centered = (int16_t)raw - 128;
            uint8_t out = (uint8_t)constrain(128 + (centered * s_txLevel) / 100, 0, 255);
            if (!sample(out)) return false;
            phase = (uint16_t)((phase + phaseInc) % SIN_LEN);
        }
        return true;
    }

    bool byteNoStuff(uint8_t b) {
        for (uint8_t i = 0; i < 8; ++i) if (!writeNrziBit((b >> i) & 1U)) return false;
        return true;
    }

    bool frameBytes(const uint8_t *data, size_t len) {
        uint8_t ones = 0;
        for (size_t n = 0; n < len; ++n) {
            uint8_t b = data[n];
            for (uint8_t i = 0; i < 8; ++i) {
                uint8_t db = (b >> i) & 1U;
                if (!writeNrziBit(db)) return false;
                if (db) {
                    if (++ones == 5) {
                        if (!writeNrziBit(0)) return false;
                        ones = 0;
                    }
                } else {
                    ones = 0;
                }
            }
        }
        return true;
    }

    void end() {
        flush();
        if (handle) {
            dac_continuous_disable(handle);
            dac_continuous_del_channels(handle);
            handle = nullptr;
        }
    }
};

#else // Arduino-ESP32 2.x fallback using the legacy I2S ADC/DAC driver

static bool startAdcDriver() {
    if (s_i2sRxInstalled) return true;
    i2s_config_t cfg{};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
    cfg.sample_rate = SAMPLERATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_I2S_MSB;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    if (i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_0) != ESP_OK || i2s_adc_enable(I2S_NUM_0) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }
    s_i2sRxInstalled = true;
    return true;
}

static void stopAdcDriver() {
    s_pauseRx = true;
    uint32_t start = millis();
    while (s_rxInRead && (uint32_t)(millis() - start) < 100U) delay(1);
    if (s_i2sRxInstalled) {
        i2s_adc_disable(I2S_NUM_0);
        i2s_driver_uninstall(I2S_NUM_0);
        s_i2sRxInstalled = false;
    }
}

static void rxTask(void *) {
    uint16_t buf[256];
    for (;;) {
        s_rxInRead = true;
        if (s_pauseRx || !s_i2sRxInstalled || s_tx) {
            s_rxInRead = false;
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        size_t got = 0;
        esp_err_t err = i2s_read(I2S_NUM_0, buf, sizeof(buf), &got, pdMS_TO_TICKS(20));
        s_rxInRead = false;
        if (err == ESP_OK && got) {
            size_t samples = got / sizeof(uint16_t);
            for (size_t i = 0; i < samples; ++i) processAdcRaw(buf[i] & 0x0FFF);
        }
    }
}

struct DacWriter {
    uint8_t buf8[512];
    uint16_t buf16[512];
    size_t used = 0;
    uint16_t phase = 0;
    uint16_t phaseInc = MARK_INC;
    bool installed = false;

    bool begin() {
        i2s_config_t cfg{};
        cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
        cfg.sample_rate = CONFIG_AFSK_DAC_SAMPLERATE;
        cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_I2S_MSB;
        cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        cfg.dma_buf_count = 6;
        cfg.dma_buf_len = 256;
        cfg.use_apll = false;
        cfg.tx_desc_auto_clear = true;
        if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
        i2s_set_pin(I2S_NUM_0, nullptr);
        i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN); // DAC2 / GPIO26
        installed = true;
        return true;
    }
    bool flush() {
        if (!installed || used == 0) return true;
        for (size_t i = 0; i < used; ++i) buf16[i] = ((uint16_t)buf8[i]) << 8;
        size_t written = 0;
        esp_err_t e = i2s_write(I2S_NUM_0, buf16, used * sizeof(uint16_t), &written, portMAX_DELAY);
        used = 0;
        return e == ESP_OK;
    }
    bool sample(uint8_t v) {
        buf8[used++] = v;
        return used < 512 ? true : flush();
    }
    bool writeNrziBit(uint8_t nrziDataBit) {
        if (!nrziDataBit) phaseInc = SWITCH_TONE(phaseInc);
        for (uint8_t i = 0; i < SAMPLESPERBIT; ++i) {
            uint8_t raw = sinSample(phase);
            int16_t centered = (int16_t)raw - 128;
            uint8_t out = (uint8_t)constrain(128 + (centered * s_txLevel) / 100, 0, 255);
            if (!sample(out)) return false;
            phase = (uint16_t)((phase + phaseInc) % SIN_LEN);
        }
        return true;
    }
    bool byteNoStuff(uint8_t b) {
        for (uint8_t i = 0; i < 8; ++i) if (!writeNrziBit((b >> i) & 1U)) return false;
        return true;
    }
    bool frameBytes(const uint8_t *data, size_t len) {
        uint8_t ones = 0;
        for (size_t n = 0; n < len; ++n) {
            uint8_t b = data[n];
            for (uint8_t i = 0; i < 8; ++i) {
                uint8_t db = (b >> i) & 1U;
                if (!writeNrziBit(db)) return false;
                if (db) {
                    if (++ones == 5) {
                        if (!writeNrziBit(0)) return false;
                        ones = 0;
                    }
                } else ones = 0;
            }
        }
        return true;
    }
    void end() {
        flush();
        if (installed) {
            delay(20);
            i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
            i2s_driver_uninstall(I2S_NUM_0);
            installed = false;
        }
    }
};
#endif

void AFSK_init(Afsk *afsk) {
    AFSK_modem = afsk;
    if (!AFSK_modem) return;
    memset(AFSK_modem, 0, sizeof(*AFSK_modem));
    AFSK_modem->preambleLength = CONFIG_AFSK_PREAMBLE_LEN;
    AFSK_modem->tailLength = CONFIG_AFSK_TRAILER_LEN;
    fifo_init(&AFSK_modem->txFifo, AFSK_modem->txBuf, CONFIG_AFSK_TX_BUFLEN);
    fifo_init(&AFSK_modem->rxFifo, AFSK_modem->rxBuf, CONFIG_AFSK_RX_BUFLEN);

    pinMode(s_pttPin, OUTPUT);
    digitalWrite(s_pttPin, s_pttActiveHigh ? LOW : HIGH);
    if (s_rxLedPin >= 0) { pinMode(s_rxLedPin, OUTPUT); setLed(s_rxLedPin, false); }
    if (s_txLedPin >= 0) { pinMode(s_txLedPin, OUTPUT); setLed(s_txLedPin, false); }

    resetDemod();
    s_pauseRx = false;
    if (!startAdcDriver()) AFSK_modem->status = -1;
    if (!s_rxTask) xTaskCreatePinnedToCore(rxTask, "nusa_afsk_rx", 4096, nullptr, 3, &s_rxTask, 0);
}

void AFSK_transmit(char *buffer, size_t size) {
    if (!buffer || size == 0 || size > AX25_MAX_FRAME_LEN || s_tx) return;
    s_tx = true;
    if (AFSK_modem) AFSK_modem->sending = true;
    setRxBusy(false);
    setLed(s_txLedPin, true);

    stopAdcDriver();
    resetDemod();

    DacWriter w;
    if (!w.begin()) {
        if (AFSK_modem) AFSK_modem->status = -2;
        goto tx_done;
    }

    digitalWrite(s_pttPin, s_pttActiveHigh ? HIGH : LOW);
    delay(20);

    {
        uint16_t crc = CRC_CCIT_INIT_VAL;
        for (size_t i = 0; i < size; ++i) crc = update_crc_ccit((uint8_t)buffer[i], crc);
        uint16_t fcs = crc ^ 0xFFFF;

        uint16_t preMs = AFSK_modem ? AFSK_modem->preambleLength : CONFIG_AFSK_PREAMBLE_LEN;
        uint16_t tailMs = AFSK_modem ? AFSK_modem->tailLength : CONFIG_AFSK_TRAILER_LEN;
        uint16_t preFlags = (uint16_t)((preMs * BITRATE + 7999UL) / 8000UL);
        uint16_t tailFlags = (uint16_t)((tailMs * BITRATE + 7999UL) / 8000UL);
        if (preFlags < 2) preFlags = 2;
        if (tailFlags < 2) tailFlags = 2;

        // Bit stuffing must continue across the data/FCS boundary, so send them
        // as one continuous HDLC data field.
        uint8_t frameWithFcs[AX25_MAX_FRAME_LEN + 2];
        memcpy(frameWithFcs, buffer, size);
        frameWithFcs[size] = (uint8_t)(fcs & 0xFF);
        frameWithFcs[size + 1] = (uint8_t)(fcs >> 8);

        bool ok = true;
        for (uint16_t i = 0; i < preFlags && ok; ++i) ok = w.byteNoStuff(HDLC_FLAG);
        if (ok) ok = w.frameBytes(frameWithFcs, size + 2);
        for (uint16_t i = 0; i < tailFlags && ok; ++i) ok = w.byteNoStuff(HDLC_FLAG);
        if (!ok && AFSK_modem) AFSK_modem->status = -3;
    }

    w.end();
    delay(10);
    digitalWrite(s_pttPin, s_pttActiveHigh ? LOW : HIGH);

tx_done:
    setLed(s_txLedPin, false);
    s_tx = false;
    if (AFSK_modem) AFSK_modem->sending = false;
    delay(10);
    if (!startAdcDriver() && AFSK_modem) AFSK_modem->status = -4;
    s_pauseRx = false;
}

void AFSK_poll(Afsk *afsk) { (void)afsk; }
void AFSK_Poll(bool isRF) { (void)isRF; }
void AFSK_TimerEnable(bool sts) { (void)sts; }
void adcActive(bool sts) {
    if (sts) {
        if (!startAdcDriver() && AFSK_modem) AFSK_modem->status = -4;
        s_pauseRx = false;
    } else stopAdcDriver();
}
void AFSK_setRxAtt(bool rxAtt) { (void)rxAtt; }
bool getReceive() { return s_channelBusy; }
void afsk_putchar(char c) {
    if (AFSK_modem && !fifo_isfull_locked(&AFSK_modem->txFifo)) fifo_push_locked(&AFSK_modem->txFifo, (unsigned char)c);
}
int afsk_getchar(void) {
    if (!AFSK_modem || fifo_isempty_locked(&AFSK_modem->rxFifo)) return -1;
    return (int)fifo_pop_locked(&AFSK_modem->rxFifo);
}

bool AFSK_channelBusy() { return s_channelBusy || s_tx; }
bool AFSK_isTransmitting() { return s_tx; }
void AFSK_setTxLevel(uint8_t percent) { s_txLevel = constrain(percent, 10, 100); }
void AFSK_setRxSquelch(uint16_t adcCounts) { s_rxSquelch = constrain(adcCounts, (uint16_t)5, (uint16_t)1000); }
void AFSK_setRxDecoderGain(uint16_t percent) {
    if (percent == 0) s_rxDecoderGain = 0;
    else s_rxDecoderGain = constrain(percent, (uint16_t)20, (uint16_t)500);
}
void AFSK_setPreamble(uint16_t milliseconds) { if (AFSK_modem) AFSK_modem->preambleLength = constrain(milliseconds, (uint16_t)50, (uint16_t)1500); }
void AFSK_setTail(uint16_t milliseconds) { if (AFSK_modem) AFSK_modem->tailLength = constrain(milliseconds, (uint16_t)20, (uint16_t)1000); }
void AFSK_setPtt(int pin, bool activeHigh) {
    if (s_pttPin >= 0) digitalWrite(s_pttPin, s_pttActiveHigh ? LOW : HIGH);
    s_pttPin = pin;
    s_pttActiveHigh = activeHigh;
    if (s_pttPin >= 0) {
        pinMode(s_pttPin, OUTPUT);
        digitalWrite(s_pttPin, s_pttActiveHigh ? LOW : HIGH);
    }
}
void AFSK_setLeds(int rxPin, int txPin) {
    s_rxLedPin = rxPin;
    s_txLedPin = txPin;
    if (s_rxLedPin >= 0) { pinMode(s_rxLedPin, OUTPUT); setLed(s_rxLedPin, false); }
    if (s_txLedPin >= 0) { pinMode(s_txLedPin, OUTPUT); setLed(s_txLedPin, false); }
}
uint16_t AFSK_getAudioLevel() { return s_audioLevel; }
uint16_t AFSK_getAudioPeak() { return s_audioPeak; }
uint16_t AFSK_getAppliedRxGain() { return s_appliedRxGain; }
uint32_t AFSK_getAdcClipSamples() { return s_adcClipSamples; }
uint16_t AFSK_getAdcRawMin() { return s_adcRawMin; }
uint16_t AFSK_getAdcRawMax() { return s_adcRawMax; }
uint32_t AFSK_getGoodFrames() { return s_goodFrames; }
uint32_t AFSK_getBadCrcFrames() { return s_badCrcFrames; }
uint32_t AFSK_getPrimaryFlags() { return s_primaryFlags; }
uint32_t AFSK_getPrimaryCandidates() { return s_primaryCandidates; }
uint32_t AFSK_getAltGoodFrames() { return s_altGoodFrames; }
uint32_t AFSK_getAltBadCrcFrames() { return s_altBadCrcFrames; }
uint32_t AFSK_getAltFlags() { return s_altFlags; }
uint32_t AFSK_getAltCandidates() { return s_altCandidates; }
uint32_t AFSK_getAltTransitions() { return s_altTransitions; }
uint32_t AFSK_getAltMarkScore() { return s_altMarkScore; }
uint32_t AFSK_getAltSpaceScore() { return s_altSpaceScore; }
uint16_t AFSK_getAltConfidence() { return s_altConfidence; }
uint32_t AFSK_getAltBankGood(uint8_t bank) { return (bank < ALT_BANKS) ? s_altBankGood[bank] : 0U; }
uint32_t AFSK_getAltBankBad(uint8_t bank) { return (bank < ALT_BANKS) ? s_altBankBad[bank] : 0U; }
uint32_t AFSK_getDualDuplicates() { return s_dualDuplicates; }
uint8_t AFSK_getLastFrameDecoder() { return s_lastFrameDecoder; }
uint8_t AFSK_getLastReadDecoder() { return s_lastReadDecoder; }


bool AFSK_armRxCapture() {
    if (!s_rxCapBuf) {
        s_rxCapBuf = (int16_t *)malloc(RXCAP_TOTAL_SAMPLES * sizeof(int16_t));
        if (!s_rxCapBuf) return false;
    }
    s_rxCapCount = 0;
    s_rxCapReady = false;
    s_rxCapActive = false;
    s_rxCapArmed = true;
    return true;
}
bool AFSK_captureArmed() { return s_rxCapArmed; }
bool AFSK_captureActive() { return s_rxCapActive; }
bool AFSK_captureReady() { return s_rxCapReady; }
size_t AFSK_getCaptureCount() { return s_rxCapCount; }
const int16_t *AFSK_getCaptureData() { return s_rxCapBuf; }

void AFSK_resetDiagnostics() {
    s_goodFrames = 0;
    s_badCrcFrames = 0;
    s_primaryFlags = 0;
    s_primaryCandidates = 0;
    s_altGoodFrames = 0;
    s_altBadCrcFrames = 0;
    s_altFlags = 0;
    s_altCandidates = 0;
    s_altTransitions = 0;
    s_altMarkScore = 0;
    s_altSpaceScore = 0;
    s_altConfidence = 0;
    for (uint8_t i = 0; i < ALT_BANKS; ++i) {
        s_altBankGood[i] = 0;
        s_altBankBad[i] = 0;
    }
    s_dualDuplicates = 0;
    s_audioPeak = 0;
    s_audioPeakWork = 0;
    s_adcClipSamples = 0;
    s_adcRawMin = 4095;
    s_adcRawMax = 0;
    s_lastFrameDecoder = 0;
}
