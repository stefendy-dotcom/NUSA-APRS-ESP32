#ifndef NUSA_APP_TYPES_H
#define NUSA_APP_TYPES_H

#include <Arduino.h>
#include "AX25.h"

#define HEARD_HOPS_MAX 8

enum TxKind : uint8_t { TX_DIGI = 1, TX_BEACON = 2, TX_IS_TO_RF = 3, TX_REMOTE_ADMIN = 4, TX_TRACKER = 5 };

struct TxJob {
  uint16_t len = 0;
  uint8_t data[AX25_MAX_FRAME_LEN];
  uint32_t due = 0;
  uint32_t queued = 0;
  TxKind kind = TX_DIGI;
};

struct DupEntry {
  uint32_t hash = 0;
  uint32_t when = 0;
};

struct LocalIsRfSpec {
  bool valid = false;
  uint16_t minutes = 30;
  uint8_t maxHops = 0;
};

struct HeardEntry {
  bool used = false;
  char call[10] = {0};
  uint32_t lastByHops[HEARD_HOPS_MAX + 1] = {0};
};

struct InternetHeardEntry {
  bool used = false;
  char call[10] = {0};
  uint32_t lastSeen = 0;
};

struct IsPacketInfo {
  bool valid = false;
  bool message = false;
  bool internetOrigin = false;
  bool forbiddenToRf = false;
  String src;
  String dst;
  String addressee;
  String info;
};

#endif
