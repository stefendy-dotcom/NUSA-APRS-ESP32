#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

struct NusaGpsData {
  double lat = 0.0;
  double lon = 0.0;
  float speedKmh = 0.0f;
  float courseDeg = 0.0f;
  float altitudeM = 0.0f;
  float hdop = 99.9f;
  uint8_t satellites = 0;
  bool courseValid = false;
  bool altitudeValid = false;
  bool hasEverFix = false;
  String utc;
  uint32_t lastSentenceMs = 0;
  uint32_t lastFixMs = 0;
  uint32_t sentenceCount = 0;
  uint32_t validFixCount = 0;
  uint32_t invalidFixCount = 0;
};

class NusaGpsTracker {
public:
  NusaGpsTracker();
  void begin(int rxPin, int txPin, uint32_t baud);
  void end();
  void service(uint16_t maxBytesPerLoop = 96);
  bool started() const { return _started; }
  bool connected(uint32_t noDataMs = 5000UL) const;
  bool fixValid(uint32_t staleSec) const;
  bool stale(uint32_t staleSec) const;
  String statusText(uint32_t staleSec) const;
  const NusaGpsData &data() const { return _data; }

private:
  HardwareSerial _serial;
  bool _started = false;
  char _line[128]{};
  uint8_t _lineLen = 0;
  NusaGpsData _data;

  static bool checksumOk(const char *line);
  static bool parseCoordinate(const char *value, const char *hemisphere, bool latitude, double &out);
  static bool isSentenceType(const char *type, const char *suffix);
  void parseLine(char *line);
  void parseRmc(char **f, int count);
  void parseGga(char **f, int count);
};
