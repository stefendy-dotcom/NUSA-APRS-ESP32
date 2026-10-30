#ifndef AFSK_H
#define AFSK_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <pgmspace.h>
#include "FIFO.h"
#include "HDLC.h"
#include "pinout.h"

// Retained from the uploaded modem header: Bell-202 / APRS AFSK.
#define SIN_LEN 192
static const uint8_t sin_table[] = {
    128,132,136,140,145,149,153,157,161,165,169,173,177,180,184,188,
    192,195,199,202,205,209,212,215,218,221,223,226,229,231,234,236,
    238,240,242,244,245,247,248,250,251,252,253,253,254,254,255,255
};

inline static uint8_t sinSample(uint16_t i) {
    uint16_t newI = i % (SIN_LEN / 2);
    newI = (newI >= (SIN_LEN / 4)) ? (SIN_LEN / 2 - newI - 1) : newI;
    uint8_t sine = pgm_read_byte(&sin_table[newI]);
    return (i >= (SIN_LEN / 2)) ? (255 - sine) : sine;
}

#define CONFIG_AFSK_RX_BUFLEN 512
#define CONFIG_AFSK_TX_BUFLEN 512
#define CONFIG_AFSK_PREAMBLE_LEN 350UL
#define CONFIG_AFSK_TRAILER_LEN 80UL
#define CONFIG_AFSK_DAC_SAMPLERATE 38400
#define SAMPLERATE 38400
#define BITRATE 1200
#define SAMPLESPERBIT (SAMPLERATE / BITRATE)
#define BIT_STUFF_LEN 5
#define MARK_FREQ 1200
#define SPACE_FREQ 2200
#define DIV_ROUND(dividend, divisor) (((dividend) + (divisor) / 2) / (divisor))
#define MARK_INC (uint16_t)(DIV_ROUND(SIN_LEN * (uint32_t)MARK_FREQ, CONFIG_AFSK_DAC_SAMPLERATE))
#define SPACE_INC (uint16_t)(DIV_ROUND(SIN_LEN * (uint32_t)SPACE_FREQ, CONFIG_AFSK_DAC_SAMPLERATE))
#define SWITCH_TONE(inc) (((inc) == MARK_INC) ? SPACE_INC : MARK_INC)

typedef struct Hdlc {
    uint8_t demodulatedBits;
    uint8_t bitIndex;
    uint8_t currentByte;
    bool receiving;
} Hdlc;

typedef struct Afsk {
    FILE fd;
    Hdlc hdlc;
    uint16_t preambleLength;
    uint16_t tailLength;
    uint8_t sampleIndex;
    uint8_t currentOutputByte;
    uint8_t txBit;
    bool bitStuff;
    uint8_t bitstuffCount;
    uint16_t phaseAcc;
    uint16_t phaseInc;
    FIFOBuffer txFifo;
    unsigned char txBuf[CONFIG_AFSK_TX_BUFLEN];
    volatile bool sending;
    FIFOBuffer rxFifo;
    unsigned char rxBuf[CONFIG_AFSK_RX_BUFLEN];
    int iirX[2];
    int iirY[2];
    uint16_t sampledBits;
    int16_t currentPhase;
    uint8_t actualBits;
    volatile int status;
} Afsk;

extern bool input_HPF;
extern Afsk *AFSK_modem;
extern bool hw_afsk_dac_isr;

void AFSK_init(Afsk *afsk);
void AFSK_setRxAtt(bool rxAtt);
void AFSK_transmit(char *buffer, size_t size);
void AFSK_poll(Afsk *afsk);
bool getReceive();
void afsk_putchar(char c);
int afsk_getchar(void);
void AFSK_Poll(bool isRF);
void AFSK_TimerEnable(bool sts);
void adcActive(bool sts);

// NUSA extensions. The original public API above is preserved.
bool AFSK_readFrame(uint8_t *out, size_t *len, size_t maxLen);
bool AFSK_channelBusy();
bool AFSK_isTransmitting();
void AFSK_setTxLevel(uint8_t percent);
void AFSK_setRxSquelch(uint16_t adcCounts);
void AFSK_setRxDecoderGain(uint16_t percent); // 0=AUTO, else 20..500%
void AFSK_setPreamble(uint16_t milliseconds);
void AFSK_setTail(uint16_t milliseconds);
void AFSK_setPtt(int pin, bool activeHigh);
void AFSK_setLeds(int rxPin, int txPin);
uint16_t AFSK_getAudioLevel();
uint16_t AFSK_getAudioPeak();
uint16_t AFSK_getAppliedRxGain();
uint32_t AFSK_getAdcClipSamples();
uint16_t AFSK_getAdcRawMin();
uint16_t AFSK_getAdcRawMax();

// REV1L dual-decoder diagnostics. Decoder A = original MicroModem path.
// Decoder B uses three tone-score banks: RAW, SPACE+25%, SPACE+30%,
// all followed by run-length NRZI recovery.
// REV1L retains the on-device 9.6 kHz RX WAV capture so the exact
// Tait->GPIO36 waveform can be analyzed instead of inferred.
uint32_t AFSK_getGoodFrames();
uint32_t AFSK_getBadCrcFrames();
uint32_t AFSK_getPrimaryFlags();
uint32_t AFSK_getPrimaryCandidates();
uint32_t AFSK_getAltGoodFrames();
uint32_t AFSK_getAltBadCrcFrames();
uint32_t AFSK_getAltFlags();
uint32_t AFSK_getAltCandidates();
uint32_t AFSK_getAltTransitions();
uint32_t AFSK_getAltMarkScore();
uint32_t AFSK_getAltSpaceScore();
uint16_t AFSK_getAltConfidence();
uint32_t AFSK_getAltBankGood(uint8_t bank); // bank 0=RAW,1=EQ125,2=EQ130
uint32_t AFSK_getAltBankBad(uint8_t bank);
uint32_t AFSK_getDualDuplicates();
uint8_t AFSK_getLastFrameDecoder(); // 0=none,1=A,2=B-RAW,3=B-EQ125,4=B-EQ130
uint8_t AFSK_getLastReadDecoder();  // decoder of most recently popped RF frame

// REV1L live RX capture / diagnostics. Capture is 9.6 kHz mono PCM16 and includes
// about 250 ms of pre-trigger audio before the next squelch/BUSY event.
bool AFSK_armRxCapture();
bool AFSK_captureArmed();
bool AFSK_captureActive();
bool AFSK_captureReady();
size_t AFSK_getCaptureCount();
const int16_t *AFSK_getCaptureData();
void AFSK_resetDiagnostics();

#endif
