#include "NusaAprsPosition.h"
#include <math.h>

static bool validLatLon(double lat, double lon) {
  return isfinite(lat) && isfinite(lon) && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

static int decDigit(char c, bool midpointForAmbiguity) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c == ' ' && midpointForAmbiguity) return 5; // APRS position ambiguity midpoint
  return -1;
}

static bool parseUncompressed(const uint8_t *p, size_t n, double &lat, double &lon) {
  // DDMM.hhN T DDDMM.hhE S = 19 chars (T/S are APRS symbol table/code).
  if (!p || n < 19) return false;
  int d0=decDigit((char)p[0], true), d1=decDigit((char)p[1], true);
  int m0=decDigit((char)p[2], true), m1=decDigit((char)p[3], true);
  int h0=decDigit((char)p[5], true), h1=decDigit((char)p[6], true);
  int od0=decDigit((char)p[9], true), od1=decDigit((char)p[10], true), od2=decDigit((char)p[11], true);
  int om0=decDigit((char)p[12], true), om1=decDigit((char)p[13], true);
  int oh0=decDigit((char)p[15], true), oh1=decDigit((char)p[16], true);
  if (d0<0||d1<0||m0<0||m1<0||h0<0||h1<0||od0<0||od1<0||od2<0||om0<0||om1<0||oh0<0||oh1<0) return false;
  if (p[4] != '.' || p[14] != '.') return false;
  char ns = (char)p[7], ew = (char)p[17];
  if (ns!='N' && ns!='S' && ns!='n' && ns!='s') return false;
  if (ew!='E' && ew!='W' && ew!='e' && ew!='w') return false;
  int ld = d0*10+d1;
  double lm = (m0*10+m1) + (h0*10+h1)/100.0;
  int gd = od0*100+od1*10+od2;
  double gm = (om0*10+om1) + (oh0*10+oh1)/100.0;
  if (ld > 90 || lm >= 60.0 || gd > 180 || gm >= 60.0) return false;
  lat = ld + lm/60.0;
  lon = gd + gm/60.0;
  if (ns=='S' || ns=='s') lat = -lat;
  if (ew=='W' || ew=='w') lon = -lon;
  return validLatLon(lat, lon);
}

static bool base91Char(uint8_t c) { return c >= 33 && c <= 123; }

static uint32_t base91_4(const uint8_t *p) {
  uint32_t v = 0;
  for (int i=0;i<4;++i) v = v*91U + (uint32_t)(p[i]-33U);
  return v;
}

static bool parseCompressed(const uint8_t *p, size_t n, double &lat, double &lon) {
  // Symbol table + YYYY latitude + XXXX longitude + symbol code.
  if (!p || n < 10) return false;
  for (int i=1;i<=8;++i) if (!base91Char(p[i])) return false;
  uint32_t y = base91_4(p+1);
  uint32_t x = base91_4(p+5);
  lat = 90.0 - ((double)y / 380926.0);
  lon = -180.0 + ((double)x / 190463.0);
  return validLatLon(lat, lon);
}

bool NusaAprsPosition_parse(const AX25Msg &msg, double &lat, double &lon) {
  lat = lon = 0.0;
  if (!msg.len) return false;
  const uint8_t *info = msg.info;
  size_t n = msg.len;
  size_t pos = 0;
  char dti = (char)info[0];
  if (dti == '!' || dti == '=') pos = 1;
  else if (dti == '/' || dti == '@') {
    // APRS timestamp is seven characters following the data type identifier.
    if (n < 9) return false;
    pos = 8;
  } else return false;
  if (pos >= n) return false;

  // Uncompressed begins with DDMM.hhN; compressed begins with symbol table.
  if (n - pos >= 19 && (info[pos] == ' ' || (info[pos] >= '0' && info[pos] <= '9'))) {
    if (parseUncompressed(info + pos, n - pos, lat, lon)) return true;
  }
  return parseCompressed(info + pos, n - pos, lat, lon);
}
