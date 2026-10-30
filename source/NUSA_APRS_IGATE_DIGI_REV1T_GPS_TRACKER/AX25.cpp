#include "AX25.h"
#include "AFSK.h"
#include <ctype.h>
#include <string.h>

static void normalizeCall(const char *src, char out[7]) {
    memset(out, 0, 7);
    if (!src) return;
    size_t n = 0;
    while (*src && *src != '-' && n < 6) {
        char c = *src++;
        if (isalnum((unsigned char)c)) out[n++] = (char)toupper((unsigned char)c);
    }
}

AX25Call ax25_makeCall(const char *call, uint8_t ssid) {
    AX25Call c{};
    normalizeCall(call, c.call);
    c.ssid = ssid & 0x0F;
    return c;
}

static void encodeAddress(uint8_t *dst, const AX25Call &c, bool last, bool repeated) {
    char norm[7];
    normalizeCall(c.call, norm);
    for (int i = 0; i < 6; ++i) {
        char ch = norm[i] ? norm[i] : ' ';
        dst[i] = ((uint8_t)ch) << 1;
    }
    dst[6] = 0x60 | ((c.ssid & 0x0F) << 1);
    if (repeated) dst[6] |= 0x80;
    if (last) dst[6] |= 0x01;
}

static void decodeAddress(const uint8_t *src, AX25Call *c) {
    memset(c, 0, sizeof(*c));
    int end = 5;
    while (end >= 0 && ((char)(src[end] >> 1) == ' ')) --end;
    for (int i = 0; i <= end && i < 6; ++i) {
        c->call[i] = (char)(src[i] >> 1);
    }
    c->call[end + 1 < 7 ? end + 1 : 6] = 0;
    c->ssid = (src[6] >> 1) & 0x0F;
}

bool ax25_buildUI(uint8_t *out, size_t *outLen,
                  const AX25Call *dst, const AX25Call *src,
                  const AX25Call *repeaters, uint8_t rptCount, uint8_t rptFlags,
                  const uint8_t *info, size_t infoLen) {
    if (!out || !outLen || !dst || !src || !info) return false;
    if (rptCount > AX25_MAX_RPT || infoLen > AX25_MAX_INFO_LEN) return false;
    const size_t needed = (2U + rptCount) * 7U + 2U + infoLen;
    if (needed > AX25_MAX_FRAME_LEN) return false;

    size_t p = 0;
    encodeAddress(out + p, *dst, false, false); p += 7;
    encodeAddress(out + p, *src, rptCount == 0, false); p += 7;
    for (uint8_t i = 0; i < rptCount; ++i) {
        bool last = (i == rptCount - 1);
        bool repeated = (rptFlags & BV(i)) != 0;
        encodeAddress(out + p, repeaters[i], last, repeated);
        p += 7;
    }
    out[p++] = AX25_CTRL_UI;
    out[p++] = AX25_PID_NOLAYER3;
    memcpy(out + p, info, infoLen);
    p += infoLen;
    *outLen = p;
    return true;
}

bool ax25_parseRaw(const uint8_t *raw, size_t rawLen, AX25Msg *out) {
    if (!raw || !out || rawLen < 16) return false;
    memset(out, 0, sizeof(*out));

    size_t p = 0;
    int addressIndex = 0;
    bool last = false;
    while (!last) {
        if (p + 7 > rawLen) return false;
        const uint8_t *a = raw + p;
        last = (a[6] & 0x01) != 0;
        if (addressIndex == 0) {
            decodeAddress(a, &out->dst);
        } else if (addressIndex == 1) {
            decodeAddress(a, &out->src);
        } else {
            if (out->rpt_count >= AX25_MAX_RPT) return false;
            decodeAddress(a, &out->rpt_list[out->rpt_count]);
            if (a[6] & 0x80) out->rpt_flags |= BV(out->rpt_count);
            out->rpt_count++;
        }
        ++addressIndex;
        p += 7;
        if (addressIndex > 2 + AX25_MAX_RPT) return false;
    }
    if (addressIndex < 2 || p >= rawLen) return false;

    out->ctrl = raw[p++];
    if ((out->ctrl & 0x01) == 0) {
        // This firmware intentionally handles APRS UI frames only.
        return false;
    }
    if (p >= rawLen) return false;
    out->pid = raw[p++];
    if (out->ctrl != AX25_CTRL_UI || out->pid != AX25_PID_NOLAYER3) return false;

    size_t infoLen = rawLen - p;
    if (infoLen > AX25_MAX_INFO_LEN) return false;
    memcpy(out->info, raw + p, infoLen);
    out->len = infoLen;
    return true;
}

void ax25_init(AX25Ctx *ctx, ax25_callback_t hook) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->hook = hook;
}

void ax25_poll(AX25Ctx *ctx) {
    if (!ctx) return;
    uint8_t raw[AX25_MAX_FRAME_LEN];
    size_t len = 0;
    while (AFSK_readFrame(raw, &len, sizeof(raw))) {
        AX25Msg msg{};
        if (ax25_parseRaw(raw, len, &msg) && ctx->hook) {
            ctx->hook(&msg);
        }
    }
}

void ax25_sendRaw(AX25Ctx *ctx, void *_buf, size_t len) {
    (void)ctx;
    if (!_buf || len == 0 || len > AX25_MAX_FRAME_LEN) return;
    AFSK_transmit((char *)_buf, len);
}

void ax25_sendVia(AX25Ctx *ctx, const AX25Call *path, size_t path_len, const void *_buf, size_t len) {
    if (!ctx || !path || path_len < 2 || !_buf || len > AX25_MAX_INFO_LEN) return;
    if (path_len > (size_t)(2 + AX25_MAX_RPT)) return;
    uint8_t raw[AX25_MAX_FRAME_LEN];
    size_t rawLen = 0;
    uint8_t rptCount = (uint8_t)(path_len - 2);
    if (!ax25_buildUI(raw, &rawLen, &path[0], &path[1],
                      rptCount ? &path[2] : nullptr, rptCount, 0,
                      (const uint8_t *)_buf, len)) return;
    ax25_sendRaw(ctx, raw, rawLen);
}

char ax25_encode(ax25frame &frame, char *txt, int size) {
    if (!txt || size <= 0) return 0;
    size_t p = 0;
    for (int i = 0; i < 10; ++i) {
        if (!frame.header[i].addr[0]) break;
        String a(frame.header[i].addr);
        if (frame.header[i].ssid > 0) a += "-" + String((int)frame.header[i].ssid);
        if (p && p < (size_t)size - 1) txt[p++] = ',';
        for (size_t j = 0; j < a.length() && p < (size_t)size - 1; ++j) txt[p++] = a[j];
    }
    if (p < (size_t)size - 1) txt[p++] = ':';
    for (size_t j = 0; frame.data[j] && p < (size_t)size - 1; ++j) txt[p++] = frame.data[j];
    txt[p] = 0;
    return (char)p;
}

void ax25sendFrame(AX25Ctx *ctx, ax25frame *pkg) {
    if (!ctx || !pkg) return;
    AX25Call addrs[2 + AX25_MAX_RPT]{};
    size_t count = 0;
    for (int i = 0; i < 10 && count < 2 + AX25_MAX_RPT; ++i) {
        if (!pkg->header[i].addr[0]) break;
        addrs[count++] = ax25_makeCall(pkg->header[i].addr, (uint8_t)pkg->header[i].ssid);
    }
    if (count < 2) return;
    size_t infoLen = strnlen(pkg->data, AX25_MAX_INFO_LEN);
    ax25_sendVia(ctx, addrs, count, pkg->data, infoLen);
}
