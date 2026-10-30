#include "NusaGpsTracker.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

NusaGpsTracker::NusaGpsTracker() : _serial(2) {}

void NusaGpsTracker::begin(int rxPin, int txPin, uint32_t baud) {
  end();
  if (baud != 9600UL && baud != 38400UL && baud != 115200UL) baud = 9600UL;
  _serial.begin(baud, SERIAL_8N1, rxPin, txPin);
  _started = true;
  _lineLen = 0;
}

void NusaGpsTracker::end() {
  if (_started) _serial.end();
  _started = false;
  _lineLen = 0;
}

bool NusaGpsTracker::connected(uint32_t noDataMs) const {
  if (!_started || !_data.lastSentenceMs) return false;
  return (uint32_t)(millis() - _data.lastSentenceMs) <= noDataMs;
}

bool NusaGpsTracker::fixValid(uint32_t staleSec) const {
  if (!_started || !_data.lastFixMs || !connected()) return false;
  uint32_t limit = staleSec * 1000UL;
  return (uint32_t)(millis() - _data.lastFixMs) <= limit;
}

bool NusaGpsTracker::stale(uint32_t staleSec) const {
  return _data.hasEverFix && connected() && !fixValid(staleSec);
}

String NusaGpsTracker::statusText(uint32_t staleSec) const {
  if (!connected()) return "NO GPS";
  if (fixValid(staleSec)) return "GPS FIX";
  if (stale(staleSec)) return "GPS STALE";
  return "GPS SEARCH";
}

bool NusaGpsTracker::checksumOk(const char *line) {
  if (!line || line[0] != '$') return false;
  const char *star = strchr(line, '*');
  if (!star) return true;
  uint8_t sum = 0;
  for (const char *p = line + 1; p < star; ++p) sum ^= (uint8_t)*p;
  if (!star[1] || !star[2]) return false;
  char hex[3] = {star[1], star[2], 0};
  char *end = nullptr;
  long expected = strtol(hex, &end, 16);
  return end && *end == 0 && expected >= 0 && expected <= 255 && sum == (uint8_t)expected;
}

bool NusaGpsTracker::parseCoordinate(const char *value, const char *hemisphere, bool latitude, double &out) {
  if (!value || !*value || !hemisphere || !*hemisphere) return false;
  double raw = atof(value);
  if (!isfinite(raw) || raw < 0.0) return false;
  int deg = (int)(raw / 100.0);
  double minutes = raw - ((double)deg * 100.0);
  if (minutes < 0.0 || minutes >= 60.0) return false;
  double v = (double)deg + minutes / 60.0;
  char h = hemisphere[0];
  if (h == 'S' || h == 's' || h == 'W' || h == 'w') v = -v;
  if (latitude) {
    if (fabs(v) > 90.0 || (h != 'N' && h != 'n' && h != 'S' && h != 's')) return false;
  } else {
    if (fabs(v) > 180.0 || (h != 'E' && h != 'e' && h != 'W' && h != 'w')) return false;
  }
  out = v;
  return true;
}

bool NusaGpsTracker::isSentenceType(const char *type, const char *suffix) {
  if (!type || !suffix) return false;
  size_t n = strlen(type), s = strlen(suffix);
  return n >= s && strcmp(type + n - s, suffix) == 0;
}

void NusaGpsTracker::parseRmc(char **f, int count) {
  if (count < 9) return;
  if (f[1] && *f[1]) _data.utc = String(f[1]);
  bool valid = f[2] && (f[2][0] == 'A' || f[2][0] == 'a');
  double lat = 0.0, lon = 0.0;
  bool posOk = valid && parseCoordinate(f[3], f[4], true, lat) &&
                        parseCoordinate(f[5], f[6], false, lon);
  if (!posOk) {
    ++_data.invalidFixCount;
    return;
  }

  _data.lat = lat;
  _data.lon = lon;
  _data.speedKmh = (f[7] && *f[7]) ? (float)(atof(f[7]) * 1.852) : 0.0f;
  if (!isfinite(_data.speedKmh) || _data.speedKmh < 0.0f) _data.speedKmh = 0.0f;

  if (f[8] && *f[8]) {
    float c = (float)atof(f[8]);
    if (isfinite(c) && c >= 0.0f && c < 360.0f) {
      _data.courseDeg = c;
      _data.courseValid = true;
    } else {
      _data.courseValid = false;
    }
  } else {
    _data.courseValid = false;
  }

  _data.lastFixMs = millis();
  _data.hasEverFix = true;
  ++_data.validFixCount;
}

void NusaGpsTracker::parseGga(char **f, int count) {
  if (count < 10) return;
  if (f[1] && *f[1]) _data.utc = String(f[1]);
  int quality = (f[6] && *f[6]) ? atoi(f[6]) : 0;
  _data.satellites = (uint8_t)constrain((f[7] && *f[7]) ? atoi(f[7]) : 0, 0, 99);
  if (f[8] && *f[8]) {
    float h = (float)atof(f[8]);
    if (isfinite(h) && h >= 0.0f && h < 99.9f) _data.hdop = h;
  }

  double lat = 0.0, lon = 0.0;
  bool posOk = quality > 0 && parseCoordinate(f[2], f[3], true, lat) &&
                              parseCoordinate(f[4], f[5], false, lon);
  if (!posOk) {
    ++_data.invalidFixCount;
  } else {
    _data.lat = lat;
    _data.lon = lon;
    _data.lastFixMs = millis();
    _data.hasEverFix = true;
    ++_data.validFixCount;
  }

  if (quality > 0 && f[9] && *f[9]) {
    float a = (float)atof(f[9]);
    if (isfinite(a) && a > -1000.0f && a < 60000.0f) {
      _data.altitudeM = a;
      _data.altitudeValid = true;
    } else {
      _data.altitudeValid = false;
    }
  } else {
    _data.altitudeValid = false;
  }
}

void NusaGpsTracker::parseLine(char *line) {
  if (!line || !*line || !checksumOk(line)) return;
  ++_data.sentenceCount;
  _data.lastSentenceMs = millis();

  char *star = strchr(line, '*');
  if (star) *star = 0;

  char *fields[20] = {};
  int count = 0;
  char *p = line;
  fields[count++] = p;
  while (*p && count < 20) {
    if (*p == ',') {
      *p = 0;
      fields[count++] = p + 1;
    }
    ++p;
  }
  if (!count) return;

  if (isSentenceType(fields[0], "RMC")) parseRmc(fields, count);
  else if (isSentenceType(fields[0], "GGA")) parseGga(fields, count);
}

void NusaGpsTracker::service(uint16_t maxBytesPerLoop) {
  if (!_started) return;
  uint16_t used = 0;
  while (_serial.available() > 0 && used < maxBytesPerLoop) {
    int v = _serial.read();
    if (v < 0) break;
    ++used;
    char c = (char)v;
    if (c == '\r') continue;
    if (c == '\n') {
      if (_lineLen > 0) {
        _line[_lineLen] = 0;
        parseLine(_line);
      }
      _lineLen = 0;
      continue;
    }
    if (_lineLen < sizeof(_line) - 1) {
      _line[_lineLen++] = c;
    } else {
      _lineLen = 0;
    }
  }
}
