#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <math.h>
#include <strings.h>
#include <Wire.h>

#include "AFSK.h"
#include "AX25.h"
#include "NusaAppTypes.h"
#include "NusaOled.h"
#include "NusaWeather.h"
#include "NusaAprsPosition.h"
#include "NusaLogo.h"

// ============================================================================
// NUSA APRS iGate + Digipeater REV1S RF REMOTE ADMIN / STABLE
// Target : Classic ESP32 DevKit / WROOM-32
// RX     : GPIO36 (ADC1_CH0)
// TX AFSK: GPIO26 (internal DAC2)
// PTT    : configurable, default GPIO25
// Web    : AP + local Wi-Fi simultaneously
// ============================================================================

static const char *FW_NAME = "NUSA APRS iGate + Digipeater";
static const char *FW_VER  = "REV1S RF REMOTE";
static const char *APRS_SW = "NUSAESP32";
static const char *APRS_SW_VER = "1.5";

WebServer web(80);
Preferences prefs;
WiFiClient aprsClient;
Afsk afsk;
AX25Ctx ax25;

struct AppConfig {
  String call = "NOCALL";
  uint8_t ssid = 0;
  String dest = "APZUAG";

  String wifiSsid = "";
  String wifiPass = "";
  String apSsid = "NUSA-APRS-IGATE";
  String apPass = "12345678";

  String isServer = "asia.aprs2.net";
  uint16_t isPort = 14580;
  int32_t isPasscode = 0; // 0 = calculate automatically from callsign
  String isFilter = ""; // APRS-IS server-side filter; blank = server default
  bool igateEnabled = true;

  // Local APRS-IS -> RF message filter.  This is processed by the ESP32,
  // NOT sent to APRS-IS.  i/30/0 = message addressee heard in the last
  // 30 minutes with 0 used digipeater hops (direct RF only).
  bool isToRfEnabled = true;
  String localIsRfFilter = "i/30/0";
  String isToRfPath = ""; // blank = direct RF TX

  bool digiEnabled = false;
  String digiAliases = "WIDE1,WIDE2";
  uint16_t digiDelayMs = 150;
  uint16_t duplicateSec = 30;

  bool rfBeaconEnabled = false;
  bool isBeaconEnabled = false;
  uint32_t beaconSec = 600;
  double lat = 0.0;
  double lon = 0.0;
  char symbolTable = '/';
  char symbolCode = '#';
  String comment = "NUSA ESP32 iGate+Digi";
  String rfPath = "WIDE2-1";

  uint16_t preambleMs = 350;
  uint16_t tailMs = 80;
  uint8_t txLevel = 90;
  uint16_t rxSquelch = 55;
  uint16_t rxDecoderGain = 0; // 0=AUTO, otherwise manual 20..500%

  int pttPin = 25;
  bool pttActiveHigh = true;
  int rxLedPin = 2;
  int txLedPin = 4;

  // Optional SSD1306 128x64 OLED. 0 address = auto detect 0x3C/0x3D.
  bool oledEnabled = true;
  int oledSdaPin = 21;
  int oledSclPin = 22;
  uint8_t oledAddress = 0;
  uint8_t oledController = 2; // 1=SSD1306, 2=SH1106 (default for tested module)
  uint8_t oledContrast = 180;
  bool oledFlip = false;
  uint8_t oledRotateSec = 4;
  uint8_t oledPopupSec = 10;
  uint16_t oledWeatherMaxAgeMin = 30;

  // RF Remote Admin. Admin identity is configured only from the local Web UI.
  // Up to two exact callsign+SSID values are accepted. Empty = unused.
  bool rfAdminEnabled = false;
  String rfAdmin1 = "";
  String rfAdmin2 = "";
  String rfAdminReplyPath = ""; // blank = DIRECT
} cfg;

// Counters
uint32_t rfRxCount = 0;
uint32_t gatedCount = 0;
uint32_t digiTxCount = 0;
uint32_t rfBeaconCount = 0;
uint32_t isBeaconCount = 0;
uint32_t isRxLineCount = 0;
uint32_t isToRfTxCount = 0;
uint32_t isToRfDropCount = 0;
uint32_t droppedTxCount = 0;
uint32_t lastBeaconMs = 0;
uint32_t lastIsConnectAttempt = 0;
uint32_t lastWifiRetry = 0;
uint32_t rfAdminOkCount = 0;
uint32_t rfAdminDeniedCount = 0;
uint32_t rfAdminReplyCount = 0;
bool rfAdminDirty = false;
bool rfAdminRebootPending = false;
uint32_t rfAdminRebootRequestedMs = 0;

// Snapshot of the remote-changeable operational values. CANCEL restores this
// snapshot; SAVE updates NVS and then refreshes the snapshot.
struct RfAdminSavedConfig {
  bool igateEnabled = true;
  bool isToRfEnabled = true;
  bool digiEnabled = false;
  bool rfBeaconEnabled = false;
  bool isBeaconEnabled = false;
  String localIsRfFilter;
  String isToRfPath;
  String digiAliases;
  uint16_t digiDelayMs = 150;
  uint16_t duplicateSec = 30;
  uint32_t beaconSec = 600;
  String rfPath;
  String comment;
  uint8_t txLevel = 90;
  uint16_t rxSquelch = 55;
  uint16_t rxDecoderGain = 0;
  bool oledEnabled = true;
};
RfAdminSavedConfig rfAdminSaved;

// Small log ring.
static const uint8_t LOG_LINES = 50;
String logBuf[LOG_LINES];
uint8_t logHead = 0;
uint8_t logUsed = 0;

// RF transmit queue. All RF TX goes through CSMA/channel-busy checking.
static const uint8_t TX_QUEUE_SIZE = 8;
TxJob txQueue[TX_QUEUE_SIZE];
uint8_t txHead = 0, txTail = 0;

DupEntry dupCache[32];
uint8_t dupPos = 0;

// Separate duplicate cache for APRS-IS -> RF message gating.
DupEntry isToRfDupCache[32];
uint8_t isToRfDupPos = 0;

// RF heard table. One timestamp is retained for each observed used-digi-hop
// count. i/30/0 therefore checks only lastByHops[0].
static const uint8_t HEARD_SLOTS = 48;
HeardEntry rfHeard[HEARD_SLOTS];

// Tracks stations observed as Internet-originated (TCPIP*/TCPXX*).
InternetHeardEntry internetHeard[HEARD_SLOTS];

// IS->RF transmit rate limiter: max 6 packets/1 min and 10 packets/5 min.
uint32_t isToRfTxHistory[10] = {0};
uint8_t isToRfTxHistoryPos = 0;

String isLineBuffer;

// OLED / local RF event state. OLED is display-only and never participates
// in the modem timing path.
NusaOled oled;
NusaWeatherData lastWeather;
String lastRfSource = "-";
String lastRfDestination = "-";
String lastRfSummary = "";
uint32_t lastRfEventMs = 0;
uint32_t oledWeatherPopupUntil = 0;
uint32_t oledPacketPopupUntil = 0;
uint32_t oledLastRefreshMs = 0;
uint32_t oledLastPageMs = 0;

uint8_t oledPage = 0;

// Six newest RF stations that supplied a valid APRS position.
// The bearing/distance reference is cfg.lat/cfg.lon (this iGate/digipeater).
struct OledHeardPos {
  bool used = false;
  String call;
  double lat = 0.0;
  double lon = 0.0;
  uint16_t bearingDeg = 0;
  float distanceKm = 0.0f;
  uint32_t whenMs = 0;
};
static const uint8_t OLED_HEARD_ROWS = 6;
OledHeardPos oledHeard[OLED_HEARD_ROWS];       // newest RF position packets
OledHeardPos oledIsHeard[OLED_HEARD_ROWS];     // newest APRS-IS position packets

static bool queueRf(const uint8_t *raw, size_t len, TxKind kind, uint32_t delayMs);
static void noteOledIsHeardPosition(const IsPacketInfo &p);
static void applyModemConfig();
static void initOled();
static bool processRfAdminMessage(const AX25Msg &m);
static void serviceRfAdmin();

static void addLog(const String &s) {
  String line = String(millis() / 1000UL) + "s  " + s;
  Serial.println(line);
  logBuf[logHead] = line;
  logHead = (uint8_t)((logHead + 1) % LOG_LINES);
  if (logUsed < LOG_LINES) ++logUsed;
}

static String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

static String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\r", "\\r");
  s.replace("\n", "\\n");
  return s;
}

static String sanitizeBaseCall(String in) {
  in.trim(); in.toUpperCase();
  int dash = in.indexOf('-');
  if (dash >= 0) in = in.substring(0, dash);
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 6; ++i) {
    char c = in[i];
    if (isalnum((unsigned char)c)) out += c;
  }
  return out.length() ? out : String("NOCALL");
}

static String singleLine(String s) {
  s.replace("\r", " ");
  s.replace("\n", " ");
  return s;
}

static String stationCall() {
  String s = sanitizeBaseCall(cfg.call);
  if (cfg.ssid > 0) s += "-" + String(cfg.ssid);
  return s;
}

static String callToString(const AX25Call &c, bool star = false) {
  String s(c.call);
  if (c.ssid > 0) s += "-" + String(c.ssid);
  if (star) s += '*';
  return s;
}

static bool parseCallText(String text, AX25Call &out) {
  text.trim(); text.toUpperCase();
  bool repeated = text.endsWith("*");
  if (repeated) text.remove(text.length() - 1);
  int dash = text.indexOf('-');
  String base = dash >= 0 ? text.substring(0, dash) : text;
  int ssid = dash >= 0 ? text.substring(dash + 1).toInt() : 0;
  base = sanitizeBaseCall(base);
  if (base == "NOCALL" || ssid < 0 || ssid > 15) return false;
  out = ax25_makeCall(base.c_str(), (uint8_t)ssid);
  return true;
}

static uint8_t parsePath(const String &pathText, AX25Call out[AX25_MAX_RPT]) {
  String p = pathText;
  p.trim(); p.toUpperCase();
  if (!p.length()) return 0;
  uint8_t n = 0;
  int start = 0;
  while (start < (int)p.length() && n < AX25_MAX_RPT) {
    int comma = p.indexOf(',', start);
    String token = comma < 0 ? p.substring(start) : p.substring(start, comma);
    token.trim();
    AX25Call c{};
    if (token.length() && parseCallText(token, c)) out[n++] = c;
    if (comma < 0) break;
    start = comma + 1;
  }
  return n;
}

static bool sameCall(const AX25Call &a, const AX25Call &b) {
  return strcasecmp(a.call, b.call) == 0 && a.ssid == b.ssid;
}

static String normalizeCallSsid(String s) {
  s.trim(); s.toUpperCase();
  if (!s.length()) return "";
  int dash = s.indexOf('-');
  if (dash >= 0) {
    String ss = s.substring(dash + 1);
    if (!ss.length() || ss.length() > 2) return "";
    for (size_t i = 0; i < ss.length(); ++i) if (!isdigit((unsigned char)ss[i])) return "";
    int n = ss.toInt();
    if (n < 0 || n > 15) return "";
  }
  AX25Call c{};
  if (!parseCallText(s, c)) return "";
  return callToString(c);
}

static bool rfAdminIsAuthorized(const AX25Call &src) {
  String s = callToString(src); s.toUpperCase();
  return (cfg.rfAdmin1.length() && s == cfg.rfAdmin1) ||
         (cfg.rfAdmin2.length() && s == cfg.rfAdmin2);
}

static bool parseOnOff(String s, bool &v) {
  s.trim(); s.toUpperCase();
  if (s == "ON" || s == "1") { v = true; return true; }
  if (s == "OFF" || s == "0") { v = false; return true; }
  return false;
}

static bool validPathText(String p) {
  p.trim(); p.toUpperCase();
  if (!p.length() || p == "DIRECT") return true;
  uint8_t count = 0;
  int start = 0;
  while (start < (int)p.length()) {
    int comma = p.indexOf(',', start);
    String token = comma < 0 ? p.substring(start) : p.substring(start, comma);
    token.trim();
    if (!token.length()) return false;
    AX25Call c{};
    if (!parseCallText(token, c)) return false;
    if (++count > AX25_MAX_RPT) return false;
    if (comma < 0) break;
    start = comma + 1;
  }
  return count > 0;
}

static String remoteDirtySuffix() { return rfAdminDirty ? " *" : ""; }

static void captureRfAdminSavedConfig() {
  rfAdminSaved.igateEnabled = cfg.igateEnabled;
  rfAdminSaved.isToRfEnabled = cfg.isToRfEnabled;
  rfAdminSaved.digiEnabled = cfg.digiEnabled;
  rfAdminSaved.rfBeaconEnabled = cfg.rfBeaconEnabled;
  rfAdminSaved.isBeaconEnabled = cfg.isBeaconEnabled;
  rfAdminSaved.localIsRfFilter = cfg.localIsRfFilter;
  rfAdminSaved.isToRfPath = cfg.isToRfPath;
  rfAdminSaved.digiAliases = cfg.digiAliases;
  rfAdminSaved.digiDelayMs = cfg.digiDelayMs;
  rfAdminSaved.duplicateSec = cfg.duplicateSec;
  rfAdminSaved.beaconSec = cfg.beaconSec;
  rfAdminSaved.rfPath = cfg.rfPath;
  rfAdminSaved.comment = cfg.comment;
  rfAdminSaved.txLevel = cfg.txLevel;
  rfAdminSaved.rxSquelch = cfg.rxSquelch;
  rfAdminSaved.rxDecoderGain = cfg.rxDecoderGain;
  rfAdminSaved.oledEnabled = cfg.oledEnabled;
  rfAdminDirty = false;
}

static void restoreRfAdminSavedConfig() {
  bool oldOled = cfg.oledEnabled;
  cfg.igateEnabled = rfAdminSaved.igateEnabled;
  cfg.isToRfEnabled = rfAdminSaved.isToRfEnabled;
  cfg.digiEnabled = rfAdminSaved.digiEnabled;
  cfg.rfBeaconEnabled = rfAdminSaved.rfBeaconEnabled;
  cfg.isBeaconEnabled = rfAdminSaved.isBeaconEnabled;
  cfg.localIsRfFilter = rfAdminSaved.localIsRfFilter;
  cfg.isToRfPath = rfAdminSaved.isToRfPath;
  cfg.digiAliases = rfAdminSaved.digiAliases;
  cfg.digiDelayMs = rfAdminSaved.digiDelayMs;
  cfg.duplicateSec = rfAdminSaved.duplicateSec;
  cfg.beaconSec = rfAdminSaved.beaconSec;
  cfg.rfPath = rfAdminSaved.rfPath;
  cfg.comment = rfAdminSaved.comment;
  cfg.txLevel = rfAdminSaved.txLevel;
  cfg.rxSquelch = rfAdminSaved.rxSquelch;
  cfg.rxDecoderGain = rfAdminSaved.rxDecoderGain;
  cfg.oledEnabled = rfAdminSaved.oledEnabled;
  applyModemConfig();
  if (oldOled && !cfg.oledEnabled && oled.ready()) oled.end();
  if (!oldOled && cfg.oledEnabled && !oled.ready()) initOled();
  rfAdminDirty = false;
}

static void persistRfAdminOperationalConfig() {
  prefs.begin("nusaaprs", false);
  prefs.putBool("igate", cfg.igateEnabled);
  prefs.putBool("is2rf", cfg.isToRfEnabled);
  prefs.putString("isrfflt", cfg.localIsRfFilter);
  prefs.putString("isrfpath", cfg.isToRfPath);
  prefs.putBool("digi", cfg.digiEnabled);
  prefs.putString("alias", cfg.digiAliases);
  prefs.putUShort("ddelay", cfg.digiDelayMs);
  prefs.putUShort("dupsec", cfg.duplicateSec);
  prefs.putBool("rfbcn", cfg.rfBeaconEnabled);
  prefs.putBool("isbcn", cfg.isBeaconEnabled);
  prefs.putUInt("bsec", cfg.beaconSec);
  prefs.putString("comment", cfg.comment);
  prefs.putString("path", cfg.rfPath);
  prefs.putUChar("txlvl", cfg.txLevel);
  prefs.putUShort("rxsql", cfg.rxSquelch);
  prefs.putUShort("rxgain", cfg.rxDecoderGain);
  prefs.putBool("oled", cfg.oledEnabled);
  prefs.end();
  captureRfAdminSavedConfig();
}

static bool aliasAllowed(const AX25Call &c) {
  String list = cfg.digiAliases;
  list.toUpperCase();
  int start = 0;
  while (start <= (int)list.length()) {
    int comma = list.indexOf(',', start);
    String token = comma < 0 ? list.substring(start) : list.substring(start, comma);
    token.trim();
    int dash = token.indexOf('-');
    if (dash >= 0) token = token.substring(0, dash);
    if (token.equalsIgnoreCase(c.call)) return true;
    if (comma < 0) break;
    start = comma + 1;
  }
  return false;
}

static uint32_t packetHash(const AX25Msg &m) {
  uint32_t h = 2166136261UL;
  auto mix = [&](uint8_t b) { h ^= b; h *= 16777619UL; };
  for (const char *p = m.src.call; *p; ++p) mix((uint8_t)*p);
  mix(m.src.ssid);
  for (const char *p = m.dst.call; *p; ++p) mix((uint8_t)*p);
  mix(m.dst.ssid);
  for (size_t i = 0; i < m.len; ++i) mix(m.info[i]);
  return h;
}

static bool duplicateRecently(uint32_t h) {
  uint32_t now = millis();
  uint32_t window = (uint32_t)cfg.duplicateSec * 1000UL;
  for (auto &e : dupCache) {
    if (e.hash == h && (uint32_t)(now - e.when) < window) return true;
  }
  dupCache[dupPos] = {h, now};
  dupPos = (uint8_t)((dupPos + 1) % 32);
  return false;
}

static bool digitsOnly(const String &s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); ++i) if (!isdigit((unsigned char)s[i])) return false;
  return true;
}

static LocalIsRfSpec parseLocalIsRfFilter(String f) {
  LocalIsRfSpec spec;
  f.trim(); f.toLowerCase();
  if (!f.startsWith("i/")) return spec;
  int p1 = f.indexOf('/', 2);
  String mins = p1 < 0 ? f.substring(2) : f.substring(2, p1);
  String hops = p1 < 0 ? String("0") : f.substring(p1 + 1);
  if (hops.indexOf('/') >= 0) return spec; // REV1E supports i/time/hops only
  if (!digitsOnly(mins) || !digitsOnly(hops)) return spec;
  int m = mins.toInt();
  int h = hops.toInt();
  if (m < 1 || m > 1440 || h < 0 || h > HEARD_HOPS_MAX) return spec;
  spec.valid = true;
  spec.minutes = (uint16_t)m;
  spec.maxHops = (uint8_t)h;
  return spec;
}

static uint8_t usedDigiHops(const AX25Msg &m) {
  uint8_t hops = 0;
  for (uint8_t i = 0; i < m.rpt_count; ++i) {
    if (m.rpt_flags & BV(i)) ++hops;
  }
  return hops > HEARD_HOPS_MAX ? HEARD_HOPS_MAX : hops;
}

static uint32_t newestHeardMs(const HeardEntry &e) {
  uint32_t newest = 0;
  for (uint8_t h = 0; h <= HEARD_HOPS_MAX; ++h) {
    if (e.lastByHops[h] && (!newest || (int32_t)(e.lastByHops[h] - newest) > 0)) newest = e.lastByHops[h];
  }
  return newest;
}

static int findRfHeardSlot(const String &call, bool create) {
  int freeSlot = -1;
  int oldestSlot = 0;
  uint32_t oldestTime = 0xFFFFFFFFUL;
  for (uint8_t i = 0; i < HEARD_SLOTS; ++i) {
    if (rfHeard[i].used) {
      if (call.equalsIgnoreCase(rfHeard[i].call)) return i;
      uint32_t t = newestHeardMs(rfHeard[i]);
      if (t < oldestTime) { oldestTime = t; oldestSlot = i; }
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }
  if (!create) return -1;
  int slot = freeSlot >= 0 ? freeSlot : oldestSlot;
  rfHeard[slot] = HeardEntry();
  rfHeard[slot].used = true;
  strncpy(rfHeard[slot].call, call.c_str(), sizeof(rfHeard[slot].call) - 1);
  rfHeard[slot].call[sizeof(rfHeard[slot].call) - 1] = 0;
  return slot;
}

static void noteRfHeard(const AX25Msg &m) {
  String c = callToString(m.src); c.toUpperCase();
  if (!c.length() || c.equalsIgnoreCase(stationCall())) return;
  int slot = findRfHeardSlot(c, true);
  if (slot < 0) return;
  uint8_t hops = usedDigiHops(m);
  bool wasEligibleDirect = rfHeard[slot].lastByHops[0] &&
      (uint32_t)(millis() - rfHeard[slot].lastByHops[0]) <= 30UL * 60UL * 1000UL;
  rfHeard[slot].lastByHops[hops] = millis();
  if (hops == 0 && !wasEligibleDirect) addLog("HEARD DIRECT + " + c + " (i/30/0 eligible)");
}

static bool rfHeardWithin(const String &call, uint16_t minutes, uint8_t maxHops, uint32_t *ageMs = nullptr) {
  int slot = findRfHeardSlot(call, false);
  if (slot < 0) return false;
  uint32_t now = millis();
  uint32_t window = (uint32_t)minutes * 60UL * 1000UL;
  uint32_t bestAge = 0xFFFFFFFFUL;
  if (maxHops > (uint8_t)HEARD_HOPS_MAX) maxHops = (uint8_t)HEARD_HOPS_MAX;
  for (uint8_t h = 0; h <= maxHops; ++h) {
    uint32_t t = rfHeard[slot].lastByHops[h];
    if (!t) continue;
    uint32_t age = now - t;
    if (age <= window && age < bestAge) bestAge = age;
  }
  if (bestAge == 0xFFFFFFFFUL) return false;
  if (ageMs) *ageMs = bestAge;
  return true;
}

static int findInternetHeardSlot(const String &call, bool create) {
  int freeSlot = -1;
  int oldestSlot = 0;
  uint32_t oldest = 0xFFFFFFFFUL;
  for (uint8_t i = 0; i < HEARD_SLOTS; ++i) {
    if (internetHeard[i].used) {
      if (call.equalsIgnoreCase(internetHeard[i].call)) return i;
      if (internetHeard[i].lastSeen < oldest) { oldest = internetHeard[i].lastSeen; oldestSlot = i; }
    } else if (freeSlot < 0) freeSlot = i;
  }
  if (!create) return -1;
  int slot = freeSlot >= 0 ? freeSlot : oldestSlot;
  internetHeard[slot] = InternetHeardEntry();
  internetHeard[slot].used = true;
  strncpy(internetHeard[slot].call, call.c_str(), sizeof(internetHeard[slot].call) - 1);
  internetHeard[slot].call[sizeof(internetHeard[slot].call) - 1] = 0;
  return slot;
}

static void noteInternetHeard(const String &call) {
  if (!call.length()) return;
  int slot = findInternetHeardSlot(call, true);
  if (slot >= 0) internetHeard[slot].lastSeen = millis();
}

static bool internetHeardWithin(const String &call, uint16_t minutes) {
  int slot = findInternetHeardSlot(call, false);
  if (slot < 0 || !internetHeard[slot].lastSeen) return false;
  return (uint32_t)(millis() - internetHeard[slot].lastSeen) <= (uint32_t)minutes * 60UL * 1000UL;
}

static uint32_t hashStringFNV1a(const String &s) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < s.length(); ++i) { h ^= (uint8_t)s[i]; h *= 16777619UL; }
  return h;
}

static bool isToRfDuplicate(const String &line) {
  uint32_t h = hashStringFNV1a(line);
  uint32_t now = millis();
  const uint32_t window = 30000UL;
  for (auto &e : isToRfDupCache) {
    if (e.hash == h && (uint32_t)(now - e.when) < window) return true;
  }
  isToRfDupCache[isToRfDupPos] = {h, now};
  isToRfDupPos = (uint8_t)((isToRfDupPos + 1) % 32);
  return false;
}

static bool isToRfRateAllowed() {
  uint32_t now = millis();
  uint8_t oneMin = 0, fiveMin = 0;
  for (uint8_t i = 0; i < 10; ++i) {
    if (!isToRfTxHistory[i]) continue;
    uint32_t age = now - isToRfTxHistory[i];
    if (age < 60000UL) ++oneMin;
    if (age < 300000UL) ++fiveMin;
  }
  return oneMin < 6 && fiveMin < 10;
}

static void noteIsToRfTxNow() {
  isToRfTxHistory[isToRfTxHistoryPos] = millis();
  isToRfTxHistoryPos = (uint8_t)((isToRfTxHistoryPos + 1) % 10);
}

static IsPacketInfo parseAprsIsLine(const String &line) {
  IsPacketInfo p;
  if (!line.length() || line[0] == '#') return p;
  int colon = line.indexOf(':');
  int gt = line.indexOf('>');
  if (colon < 0 || gt <= 0 || gt > colon) return p;

  p.src = line.substring(0, gt); p.src.trim(); p.src.toUpperCase();
  String route = line.substring(gt + 1, colon);
  int comma = route.indexOf(',');
  p.dst = comma < 0 ? route : route.substring(0, comma);
  p.dst.trim(); p.dst.toUpperCase();
  p.info = line.substring(colon + 1); // preserve message text exactly
  if (!p.src.length() || !p.dst.length()) return p;

  if (comma >= 0) {
    int start = comma + 1;
    while (start <= (int)route.length()) {
      int c = route.indexOf(',', start);
      String token = c < 0 ? route.substring(start) : route.substring(start, c);
      token.trim(); token.toUpperCase();
      if (token == "TCPXX" || token == "TCPXX*") { p.internetOrigin = true; p.forbiddenToRf = true; }
      if (token == "TCPIP" || token == "TCPIP*") p.internetOrigin = true;
      if (token == "NOGATE" || token == "NOGATE*" || token == "RFONLY" || token == "RFONLY*") p.forbiddenToRf = true;
      if (c < 0) break;
      start = c + 1;
    }
  }

  if (p.info.length() >= 11 && p.info[0] == ':' && p.info[10] == ':') {
    p.addressee = p.info.substring(1, 10);
    p.addressee.trim(); p.addressee.toUpperCase();
    p.message = p.addressee.length() > 0;
  }
  p.valid = true;
  return p;
}

static bool buildIsToRfThirdParty(const IsPacketInfo &p, uint8_t *raw, size_t *rawLen) {
  if (!raw || !rawLen || !p.valid || !p.message) return false;
  AX25Call src = ax25_makeCall(cfg.call.c_str(), cfg.ssid);
  AX25Call dst{};
  if (!parseCallText(cfg.dest, dst)) dst = ax25_makeCall("APNU01", 0);
  AX25Call path[AX25_MAX_RPT]{};
  uint8_t pathCount = parsePath(cfg.isToRfPath, path);

  // Mandatory third-party Internet->RF representation.  APRS-IS q constructs
  // and server path are intentionally discarded.
  String third = "}" + p.src + ">" + p.dst + ",TCPIP," + stationCall() + "*:" + p.info;
  if (third.length() > AX25_MAX_INFO_LEN) return false;
  return ax25_buildUI(raw, rawLen, &dst, &src, path, pathCount, 0,
                      (const uint8_t *)third.c_str(), third.length());
}

static void processAprsIsLine(const String &line) {
  IsPacketInfo p = parseAprsIsLine(line);
  if (!p.valid) return;

  // OLED APRS-IS heard page is intentionally updated BEFORE any local
  // IS->RF eligibility/filter decision.  Therefore a station position can be
  // displayed even when the packet will never be retransmitted to RF.
  noteOledIsHeardPosition(p);

  // Maintain the Internet-heard state even for non-message packets.
  if (p.internetOrigin) noteInternetHeard(p.src);

  if (!cfg.isToRfEnabled || !p.message) return;
  LocalIsRfSpec spec = parseLocalIsRfFilter(cfg.localIsRfFilter);
  if (!spec.valid) {
    ++isToRfDropCount;
    addLog("IS>RF DROP invalid local filter: " + cfg.localIsRfFilter);
    return;
  }

  if (p.forbiddenToRf) {
    ++isToRfDropCount;
    addLog("IS>RF DROP forbidden path: " + p.src + " > " + p.addressee);
    return;
  }
  if (p.addressee.equalsIgnoreCase(stationCall())) return; // message is for this IGate itself

  uint32_t targetAge = 0;
  if (!rfHeardWithin(p.addressee, spec.minutes, spec.maxHops, &targetAge)) {
    ++isToRfDropCount;
    addLog("IS>RF DROP target not i/" + String(spec.minutes) + "/" + String(spec.maxHops) + ": " + p.addressee);
    return;
  }

  // Standard IGate safeguards in addition to the requested i/time/hops rule.
  if (rfHeardWithin(p.src, spec.minutes, HEARD_HOPS_MAX)) {
    ++isToRfDropCount;
    addLog("IS>RF DROP sender also heard on RF: " + p.src);
    return;
  }
  if (internetHeardWithin(p.addressee, spec.minutes)) {
    ++isToRfDropCount;
    addLog("IS>RF DROP target seen on Internet: " + p.addressee);
    return;
  }
  if (isToRfDuplicate(line)) {
    ++isToRfDropCount;
    addLog("IS>RF duplicate suppressed: " + p.src + " > " + p.addressee);
    return;
  }

  uint8_t raw[AX25_MAX_FRAME_LEN];
  size_t rawLen = 0;
  if (!buildIsToRfThirdParty(p, raw, &rawLen)) {
    ++isToRfDropCount;
    addLog("IS>RF DROP packet too long/invalid: " + p.src + " > " + p.addressee);
    return;
  }

  if (queueRf(raw, rawLen, TX_IS_TO_RF, 0)) {
    addLog("IS>RF queued " + p.src + " > " + p.addressee + "  target age=" + String(targetAge / 1000UL) + "s");
  } else {
    ++isToRfDropCount;
  }
}

static bool queueRf(const uint8_t *raw, size_t len, TxKind kind, uint32_t delayMs) {
  if (!raw || len == 0 || len > AX25_MAX_FRAME_LEN) return false;
  uint8_t next = (uint8_t)((txTail + 1) % TX_QUEUE_SIZE);
  if (next == txHead) {
    ++droppedTxCount;
    addLog("RF TX queue full - packet dropped");
    return false;
  }
  txQueue[txTail].len = (uint16_t)len;
  memcpy(txQueue[txTail].data, raw, len);
  txQueue[txTail].queued = millis();
  txQueue[txTail].due = millis() + delayMs;
  txQueue[txTail].kind = kind;
  txTail = next;
  return true;
}

static bool timeReached(uint32_t now, uint32_t target) {
  return (int32_t)(now - target) >= 0;
}

static int aprsPasscode(String call) {
  call.toUpperCase();
  int dash = call.indexOf('-');
  if (dash >= 0) call = call.substring(0, dash);
  uint16_t hash = 0x73E2;
  for (size_t i = 0; i < call.length(); i += 2) {
    hash ^= ((uint16_t)(uint8_t)call[i]) << 8;
    if (i + 1 < call.length()) hash ^= (uint8_t)call[i + 1];
  }
  return hash & 0x7FFF;
}

static bool hasForbiddenGatePath(const AX25Msg &m) {
  for (uint8_t i = 0; i < m.rpt_count; ++i) {
    String c(m.rpt_list[i].call); c.toUpperCase();
    if (c == "TCPIP" || c == "TCPXX" || c == "NOGATE" || c == "RFONLY") return true;
  }
  return m.len > 0 && m.info[0] == '}'; // do not re-gate third-party encapsulated frames
}

static String msgToTnc2(const AX25Msg &m) {
  String line = callToString(m.src) + ">" + callToString(m.dst);
  for (uint8_t i = 0; i < m.rpt_count; ++i) {
    line += ',';
    line += callToString(m.rpt_list[i], (m.rpt_flags & BV(i)) != 0);
  }
  line += ':';
  for (size_t i = 0; i < m.len; ++i) {
    char c = (char)m.info[i];
    if (c == '\r' || c == '\n' || c == 0) continue;
    line += c;
  }
  return line;
}

static void gateToAprsIs(const AX25Msg &m) {
  if (!cfg.igateEnabled || !aprsClient.connected() || hasForbiddenGatePath(m)) return;
  if (sameCall(m.src, ax25_makeCall(cfg.call.c_str(), cfg.ssid))) return;

  String rf = msgToTnc2(m);
  int colon = rf.indexOf(':');
  if (colon < 0) return;
  String line = rf.substring(0, colon) + ",qAR," + stationCall() + rf.substring(colon) + "\r\n";
  aprsClient.print(line);
  ++gatedCount;
  addLog("IGATE > IS  " + line.substring(0, line.length() - 2));
}

static void maybeDigipeat(const AX25Msg &m) {
  if (!cfg.digiEnabled || m.rpt_count == 0) return;

  AX25Call own = ax25_makeCall(cfg.call.c_str(), cfg.ssid);
  if (sameCall(m.src, own)) return;
  for (uint8_t i = 0; i < m.rpt_count; ++i) {
    if (sameCall(m.rpt_list[i], own) && (m.rpt_flags & BV(i))) return;
  }

  int idx = -1;
  for (uint8_t i = 0; i < m.rpt_count; ++i) {
    if (!(m.rpt_flags & BV(i))) { idx = i; break; }
  }
  if (idx < 0) return;

  bool directOwn = sameCall(m.rpt_list[idx], own);
  bool wideAlias = aliasAllowed(m.rpt_list[idx]) && m.rpt_list[idx].ssid > 0;
  if (!directOwn && !wideAlias) return;

  uint32_t h = packetHash(m);
  if (duplicateRecently(h)) {
    addLog("DIGI duplicate suppressed: " + callToString(m.src));
    return;
  }

  AX25Call newPath[AX25_MAX_RPT]{};
  uint8_t newFlags = 0;
  uint8_t n = 0;

  for (int i = 0; i < idx && n < AX25_MAX_RPT; ++i) {
    newPath[n] = m.rpt_list[i];
    if (m.rpt_flags & BV(i)) newFlags |= BV(n);
    ++n;
  }

  if (directOwn) {
    if (n >= AX25_MAX_RPT) return;
    newPath[n] = own;
    newFlags |= BV(n);
    ++n;
  } else {
    uint8_t hops = m.rpt_list[idx].ssid;
    if (n >= AX25_MAX_RPT) return;
    newPath[n] = own;
    newFlags |= BV(n);
    ++n;
    if (hops > 1) {
      if (n >= AX25_MAX_RPT) return;
      newPath[n] = m.rpt_list[idx];
      newPath[n].ssid = hops - 1;
      ++n;
    }
  }

  for (uint8_t i = (uint8_t)(idx + 1); i < m.rpt_count && n < AX25_MAX_RPT; ++i) {
    newPath[n] = m.rpt_list[i];
    if (m.rpt_flags & BV(i)) newFlags |= BV(n);
    ++n;
  }

  uint8_t raw[AX25_MAX_FRAME_LEN];
  size_t rawLen = 0;
  if (!ax25_buildUI(raw, &rawLen, &m.dst, &m.src, newPath, n, newFlags, m.info, m.len)) return;

  uint32_t dly = cfg.digiDelayMs + (uint32_t)random(0, 51);
  if (queueRf(raw, rawLen, TX_DIGI, dly)) {
    addLog("DIGI queued: " + callToString(m.src) + ">" + callToString(m.dst));
  }
}


static String clipText(String x, uint8_t n) {
  x.replace("\r", " "); x.replace("\n", " "); x.toUpperCase();
  if (x.length() > n) x = x.substring(0, n);
  return x;
}

static String decoderShortName() {
  uint8_t d = AFSK_getLastFrameDecoder();
  if (d == 1) return "A";
  if (d == 2) return "B-RAW";
  if (d == 3) return "B-EQ125";
  if (d == 4) return "B-EQ130";
  return "-";
}

static bool oledPinsSafe() {
  if (cfg.oledSdaPin < 0 || cfg.oledSdaPin > 33 || cfg.oledSclPin < 0 || cfg.oledSclPin > 33) return false;
  if (cfg.oledSdaPin == cfg.oledSclPin) return false;
  if (cfg.oledSdaPin == 26 || cfg.oledSclPin == 26 || cfg.oledSdaPin == 36 || cfg.oledSclPin == 36) return false;
  if (cfg.oledSdaPin == cfg.pttPin || cfg.oledSclPin == cfg.pttPin) return false;
  if (cfg.oledSdaPin == cfg.rxLedPin || cfg.oledSclPin == cfg.rxLedPin) return false;
  if (cfg.oledSdaPin == cfg.txLedPin || cfg.oledSclPin == cfg.txLedPin) return false;
  return true;
}

static void oledHeader(const String &title) {
  oled.fillRect(0, 0, 128, 10, true);
  oled.text(3, 1, title, 1, true);
}


static void oledDrawBitmap128x64(const uint8_t *bmp) {
  if (!bmp) return;
  for (int page = 0; page < 8; ++page) {
    for (int x = 0; x < 128; ++x) {
      uint8_t b = pgm_read_byte(bmp + page * 128 + x);
      for (int bit = 0; bit < 8; ++bit) {
        oled.pixel(x, page * 8 + bit, (b >> bit) & 0x01);
      }
    }
  }
}

static String weatherAgeText() {
  if (!lastWeather.valid) return "-";
  uint32_t age = (millis() - lastWeather.whenMs) / 1000UL;
  if (age < 60) return String(age) + "S";
  return String(age / 60UL) + "M";
}

static void drawOledLogo() {
  oled.clear();
  oledDrawBitmap128x64(NUSA_APRS_LOGO_128X64);
}

static void drawOledHome() {
  oled.clear(); oledHeader("NUSA APRS " + String(FW_VER));
  String call = stationCall();
  oled.centered(14, call, call.length() <= 8 ? 2 : 1);
  bool isok = aprsClient.connected();
  oled.text(1, 34, "IS:" + String(isok ? "OK" : "--") + " IG:" + String(cfg.igateEnabled ? "ON" : "OFF"));
  oled.text(1, 43, "DGI:" + String(cfg.digiEnabled ? "ON" : "OFF") + " RF:" + String(AFSK_channelBusy() ? "BUSY" : "CLEAR"));
  oled.text(1, 53, "RX:" + String(rfRxCount) + " TX:" + String(digiTxCount));
}

static void drawOledRf() {
  oled.clear(); oledHeader("RF / DECODER");
  oled.text(1, 13, "LAST:" + decoderShortName());
  oled.text(1, 22, "AUDIO:" + String(AFSK_getAudioLevel()) + " G:" + String(AFSK_getAppliedRxGain()) + "%");
  oled.text(1, 31, "RX VALID:" + String(rfRxCount));
  oled.text(1, 40, "RF>IS:" + String(gatedCount) + " DIGI:" + String(digiTxCount));
  oled.text(1, 49, "SRC:" + clipText(lastRfSource, 14));
  oled.text(1, 58, "DST:" + clipText(lastRfDestination, 14));
}

static void drawOledNetwork() {
  oled.clear(); oledHeader("NETWORK");
  String sta = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "NO STA";
  oled.text(1, 13, "AP:" + WiFi.softAPIP().toString());
  oled.text(1, 22, "STA:" + clipText(sta, 16));
  oled.text(1, 31, "APRS-IS:" + String(aprsClient.connected() ? "CONNECTED" : "OFFLINE"));
  oled.text(1, 40, "IS>RF:" + String(cfg.isToRfEnabled ? "ON" : "OFF"));
  oled.text(1, 49, "HEAP:" + String(ESP.getFreeHeap()));
  oled.text(1, 58, "UP:" + String(millis()/60000UL) + " MIN");
}

static void drawOledLastPacket() {
  oled.clear(); oledHeader("PACKET RF TERAKHIR");
  oled.text(1, 13, "DARI:" + clipText(lastRfSource, 14));
  oled.text(1, 22, "KE  :" + clipText(lastRfDestination, 14));
  String a = lastRfSummary;
  oled.text(1, 33, clipText(a.substring(0, min((int)a.length(), 20)), 20));
  if (a.length() > 20) oled.text(1, 42, clipText(a.substring(20, min((int)a.length(), 40)), 20));
  if (a.length() > 40) oled.text(1, 51, clipText(a.substring(40, min((int)a.length(), 60)), 20));
}

static String fmt1(float v) { return String(v, 1); }

static void drawOledWeather() {
  oled.clear(); oledHeader("WEATHER RF");
  if (!lastWeather.valid) {
    oled.centered(26, "BELUM ADA DATA", 1);
    oled.centered(39, "WEATHER RF", 1);
    return;
  }
  oled.text(1, 12, "DARI:" + clipText(String(lastWeather.source), 13) + " " + weatherAgeText());
  String l1 = "T:";
  l1 += lastWeather.tempValid ? fmt1(lastWeather.tempC) + "C" : "--";
  l1 += " H:";
  l1 += lastWeather.humidityValid ? String(lastWeather.humidity) + "%" : "--";
  oled.text(1, 23, l1);
  String l2 = "P:";
  l2 += lastWeather.pressureValid ? fmt1(lastWeather.pressureHpa) + "HPA" : "--";
  oled.text(1, 33, l2);
  String l3 = "W:";
  l3 += lastWeather.windDirValid ? String(lastWeather.windDirDeg) : "---";
  l3 += "/";
  l3 += lastWeather.windSpeedValid ? fmt1(lastWeather.windSpeedMph * 0.868976f) + "KT" : "--";
  if (lastWeather.gustValid) l3 += " G" + fmt1(lastWeather.gustMph * 0.868976f);
  oled.text(1, 43, l3);
  String l4 = "RAIN:";
  if (lastWeather.rain1hValid) l4 += fmt1(lastWeather.rain1hMm) + "MM/H";
  else if (lastWeather.rain24hValid) l4 += fmt1(lastWeather.rain24hMm) + "MM/24H";
  else l4 += "--";
  oled.text(1, 53, l4);
}


static double degToRad(double d) { return d * 0.017453292519943295769; }

static float distanceKmBetween(double lat1, double lon1, double lat2, double lon2) {
  const double r = 6371.0088;
  double p1 = degToRad(lat1), p2 = degToRad(lat2);
  double dp = degToRad(lat2 - lat1);
  double dl = degToRad(lon2 - lon1);
  double a = sin(dp * 0.5) * sin(dp * 0.5) +
             cos(p1) * cos(p2) * sin(dl * 0.5) * sin(dl * 0.5);
  a = constrain(a, 0.0, 1.0);
  return (float)(r * 2.0 * atan2(sqrt(a), sqrt(1.0 - a)));
}

static uint16_t bearingDegBetween(double lat1, double lon1, double lat2, double lon2) {
  double p1 = degToRad(lat1), p2 = degToRad(lat2);
  double dl = degToRad(lon2 - lon1);
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = atan2(y, x) * 57.295779513082320876;
  if (b < 0.0) b += 360.0;
  int v = (int)lround(b);
  if (v >= 360) v -= 360;
  if (v < 0) v += 360;
  return (uint16_t)v;
}

static bool stationReferencePositionReady() {
  if (cfg.lat < -90.0 || cfg.lat > 90.0 || cfg.lon < -180.0 || cfg.lon > 180.0) return false;
  // 0,0 is the factory default and normally means the station position has
  // not been configured yet. Avoid showing meaningless range/bearing.
  if (fabs(cfg.lat) < 0.000001 && fabs(cfg.lon) < 0.000001) return false;
  return true;
}

static void noteOledHeardPosition(const AX25Msg &msg) {
  if (!stationReferencePositionReady()) return;

  double lat = 0.0, lon = 0.0;
  if (!NusaAprsPosition_parse(msg, lat, lon)) return;

  String call = callToString(msg.src);
  call.trim(); call.toUpperCase();
  if (!call.length() || call.equalsIgnoreCase(stationCall())) return;

  OledHeardPos e;
  e.used = true;
  e.call = call;
  e.lat = lat;
  e.lon = lon;
  e.bearingDeg = bearingDegBetween(cfg.lat, cfg.lon, lat, lon);
  e.distanceKm = distanceKmBetween(cfg.lat, cfg.lon, lat, lon);
  e.whenMs = millis();

  int old = -1;
  for (uint8_t i = 0; i < OLED_HEARD_ROWS; ++i) {
    if (oledHeard[i].used && oledHeard[i].call.equalsIgnoreCase(call)) { old = i; break; }
  }

  if (old >= 0) {
    for (int i = old; i > 0; --i) oledHeard[i] = oledHeard[i - 1];
  } else {
    for (int i = OLED_HEARD_ROWS - 1; i > 0; --i) oledHeard[i] = oledHeard[i - 1];
  }
  oledHeard[0] = e;
}

static void noteOledIsHeardPosition(const IsPacketInfo &p) {
  if (!p.valid || !stationReferencePositionReady()) return;

  // Reuse exactly the same APRS position parser used by the RF heard page.
  // Only the source and information field are needed for position decoding.
  AX25Msg msg{};
  if (!parseCallText(p.src, msg.src)) return;
  size_t n = p.info.length();
  if (n == 0) return;
  if (n > AX25_MAX_INFO_LEN) n = AX25_MAX_INFO_LEN;
  memcpy(msg.info, p.info.c_str(), n);
  msg.len = n;

  double lat = 0.0, lon = 0.0;
  if (!NusaAprsPosition_parse(msg, lat, lon)) return;

  String call = p.src;
  call.trim(); call.toUpperCase();
  if (!call.length() || call.equalsIgnoreCase(stationCall())) return;

  OledHeardPos e;
  e.used = true;
  e.call = call;
  e.lat = lat;
  e.lon = lon;
  e.bearingDeg = bearingDegBetween(cfg.lat, cfg.lon, lat, lon);
  e.distanceKm = distanceKmBetween(cfg.lat, cfg.lon, lat, lon);
  e.whenMs = millis();

  int old = -1;
  for (uint8_t i = 0; i < OLED_HEARD_ROWS; ++i) {
    if (oledIsHeard[i].used && oledIsHeard[i].call.equalsIgnoreCase(call)) { old = i; break; }
  }

  if (old >= 0) {
    for (int i = old; i > 0; --i) oledIsHeard[i] = oledIsHeard[i - 1];
  } else {
    for (int i = OLED_HEARD_ROWS - 1; i > 0; --i) oledIsHeard[i] = oledIsHeard[i - 1];
  }
  oledIsHeard[0] = e;
}

static String oledDistanceText(float km) {
  if (!isfinite(km) || km < 0.0f) return "--";
  if (km < 100.0f) return String(km, 1);
  if (km < 1000.0f) return String((int)lroundf(km));
  if (km < 10000.0f) return String((int)lroundf(km));
  return "9999+";
}

static void drawOledHeardStations() {
  oled.clear();
  oledHeader("TERDENGAR     AZ  KM");

  bool any = false;
  for (uint8_t i = 0; i < OLED_HEARD_ROWS; ++i) {
    if (!oledHeard[i].used) continue;
    any = true;

    String call = clipText(oledHeard[i].call, 9);
    while (call.length() < 9) call += ' ';

    char az[5];
    snprintf(az, sizeof(az), "%03u", (unsigned)oledHeard[i].bearingDeg);
    String d = oledDistanceText(oledHeard[i].distanceKm);
    if (d.length() > 5) d = d.substring(0, 5);
    while (d.length() < 5) d = " " + d;

    // 9 call + 1 + 3 azimuth + 1 + 5 km = 19 characters.
    oled.text(1, 11 + (int)i * 9, call + " " + String(az) + " " + d);
  }

  if (!any) {
    oled.centered(27, stationReferencePositionReady() ? "BELUM ADA POSISI RF" : "SET LAT/LON IGATE", 1);
    oled.centered(39, stationReferencePositionReady() ? "DARI STASIUN" : "DI WEB CONFIG", 1);
  }
}

static void drawOledIsHeardStations() {
  oled.clear();
  oledHeader("VIA IS       AZ  KM");

  bool any = false;
  for (uint8_t i = 0; i < OLED_HEARD_ROWS; ++i) {
    if (!oledIsHeard[i].used) continue;
    any = true;

    String call = clipText(oledIsHeard[i].call, 9);
    while (call.length() < 9) call += ' ';

    char az[5];
    snprintf(az, sizeof(az), "%03u", (unsigned)oledIsHeard[i].bearingDeg);
    String d = oledDistanceText(oledIsHeard[i].distanceKm);
    if (d.length() > 5) d = d.substring(0, 5);
    while (d.length() < 5) d = " " + d;

    oled.text(1, 11 + (int)i * 9, call + " " + String(az) + " " + d);
  }

  if (!any) {
    oled.centered(27, stationReferencePositionReady() ? "BELUM ADA POSISI IS" : "SET LAT/LON IGATE", 1);
    oled.centered(39, stationReferencePositionReady() ? "DARI APRS-IS" : "DI WEB CONFIG", 1);
  }
}

static void initOled() {
  if (!cfg.oledEnabled) { addLog("OLED disabled"); return; }
  if (!oledPinsSafe()) { addLog("OLED disabled: GPIO conflict/invalid"); return; }
  bool ok = oled.begin(cfg.oledSdaPin, cfg.oledSclPin, cfg.oledAddress, cfg.oledContrast, cfg.oledFlip, cfg.oledController);
  if (!ok) { addLog("OLED not detected on 0x3C/0x3D"); return; }
  addLog(String("OLED ") + (cfg.oledController==2 ? "SH1106" : "SSD1306") + " ready I2C=0x" + String(oled.address(), HEX) + " SDA=" + String(cfg.oledSdaPin) + " SCL=" + String(cfg.oledSclPin));
  drawOledLogo();
  oled.display();
}

static void serviceOled() {
  if (!oled.ready()) return;
  uint32_t now = millis();
  if ((uint32_t)(now - oledLastRefreshMs) < 1000U) return;
  oledLastRefreshMs = now;

  // Weather has the highest event priority.
  if ((int32_t)(oledWeatherPopupUntil - now) > 0) { drawOledWeather(); oled.display(); return; }
  if ((int32_t)(oledPacketPopupUntil - now) > 0) { drawOledLastPacket(); oled.display(); return; }

  uint32_t rotateSec = cfg.oledRotateSec < 2 ? 2 : cfg.oledRotateSec;
  uint32_t rotateMs = rotateSec * 1000UL;
  if ((uint32_t)(now - oledLastPageMs) >= rotateMs) {
    oledLastPageMs = now;
    oledPage = (uint8_t)((oledPage + 1) % 7);
  }

  // Skip weather page if no fresh weather.
  bool freshWeather = lastWeather.valid &&
    (uint32_t)(now - lastWeather.whenMs) <= (uint32_t)cfg.oledWeatherMaxAgeMin * 60000UL;
  if (oledPage == 6 && !freshWeather) oledPage = 0;

  if (oledPage == 0) drawOledLogo();
  else if (oledPage == 1) drawOledHome();
  else if (oledPage == 2) drawOledNetwork();
  else if (oledPage == 3) drawOledRf();
  else if (oledPage == 4) drawOledHeardStations();
  else if (oledPage == 5) drawOledIsHeardStations();
  else drawOledWeather();
  oled.display();
}

static bool extractAprsMessageToThisStation(const AX25Msg &m, String &body, String &msgId) {
  body = ""; msgId = "";
  if (m.len < 11 || m.info[0] != ':' || m.info[10] != ':') return false;
  String to;
  for (uint8_t i = 1; i <= 9; ++i) to += (char)m.info[i];
  to.trim(); to.toUpperCase();
  if (!to.equalsIgnoreCase(stationCall())) return false;
  for (size_t i = 11; i < m.len; ++i) {
    char c = (char)m.info[i];
    if (c && c != '\r' && c != '\n') body += c;
  }
  body.trim();
  int brace = body.lastIndexOf('{');
  if (brace >= 0) {
    String candidate = body.substring(brace + 1); candidate.trim();
    if (candidate.length() > 0 && candidate.length() <= 5 && candidate.indexOf(' ') < 0) {
      msgId = candidate;
      body = body.substring(0, brace); body.trim();
    }
  }
  return body.length() > 0;
}

static bool buildRfAdminReplyRaw(const String &targetCall, const String &reply, uint8_t *raw, size_t *rawLen) {
  if (!raw || !rawLen) return false;
  AX25Call dst{};
  if (!parseCallText(cfg.dest, dst)) dst = ax25_makeCall("APNU01", 0);
  AX25Call src = ax25_makeCall(cfg.call.c_str(), cfg.ssid);
  AX25Call path[AX25_MAX_RPT]{};
  uint8_t pathCount = parsePath(cfg.rfAdminReplyPath, path);
  String a = normalizeCallSsid(targetCall);
  if (!a.length() || a.length() > 9) return false;
  while (a.length() < 9) a += ' ';
  String info = ":" + a + ":" + reply;
  if (info.length() > AX25_MAX_INFO_LEN) info = info.substring(0, AX25_MAX_INFO_LEN);
  return ax25_buildUI(raw, rawLen, &dst, &src, path, pathCount, 0,
                      (const uint8_t *)info.c_str(), info.length());
}

static bool queueRfAdminReply(const String &targetCall, String reply) {
  reply = singleLine(reply); reply.trim();
  if (reply.length() > 67) reply = reply.substring(0, 67);
  uint8_t raw[AX25_MAX_FRAME_LEN]; size_t len = 0;
  if (!buildRfAdminReplyRaw(targetCall, reply, raw, &len)) return false;
  if (!queueRf(raw, len, TX_REMOTE_ADMIN, 150)) return false;
  ++rfAdminReplyCount;
  return true;
}

static String rfAdminGetText() {
  String s = "D=" + String(cfg.digiEnabled ? 1 : 0) +
             " I=" + String(cfg.igateEnabled ? 1 : 0) +
             " R=" + String(cfg.isToRfEnabled ? 1 : 0) +
             " Q=" + String(cfg.rxSquelch) +
             " G=" + String(cfg.rxDecoderGain) +
             " T=" + String(cfg.txLevel) +
             " B=" + String(cfg.beaconSec);
  if (rfAdminDirty) s += " *";
  return s;
}

static String rfAdminStatText() {
  uint32_t upMin = millis() / 60000UL;
  String up = upMin >= 1440UL ? String(upMin / 1440UL) + "D" : String(upMin / 60UL) + "H";
  String s = "UP=" + up + " RX=" + String(rfRxCount) +
             " D=" + String(cfg.digiEnabled ? 1 : 0) +
             " I=" + String(cfg.igateEnabled ? 1 : 0) +
             " IS=" + String(aprsClient.connected() ? 1 : 0) +
             " H=" + String(ESP.getFreeHeap() / 1024UL) + "K";
  return s;
}

static bool setRemoteOnOff(String arg, bool &field) {
  bool v = false;
  if (!parseOnOff(arg, v)) return false;
  field = v; rfAdminDirty = true; return true;
}

static void applyRemoteOledState(bool previous) {
  if (previous && !cfg.oledEnabled && oled.ready()) oled.end();
  if (!previous && cfg.oledEnabled && !oled.ready()) initOled();
}

static bool processRfAdminMessage(const AX25Msg &m) {
  String cmd, msgId;
  if (!extractAprsMessageToThisStation(m, cmd, msgId)) return false;
  if (!cfg.rfAdminEnabled) return false;

  String src = callToString(m.src); src.toUpperCase();
  if (!rfAdminIsAuthorized(m.src)) {
    ++rfAdminDeniedCount;
    addLog("RF ADMIN DENIED SRC=" + src + " (silent)");
    return true; // consumed as a local admin attempt; intentionally no RF reply
  }

  ++rfAdminOkCount;
  // If the admin client attached a standard APRS message number, acknowledge
  // it so normal APRS apps/radios stop retrying. Non-admin never reaches here.
  if (msgId.length()) queueRfAdminReply(src, "ack" + msgId);

  String original = cmd;
  String upper = cmd; upper.toUpperCase(); upper.trim();
  int sp = upper.indexOf(' ');
  String op = sp < 0 ? upper : upper.substring(0, sp);
  String arg = sp < 0 ? String("") : original.substring(sp + 1);
  arg.trim();
  String uarg = arg; uarg.toUpperCase();
  String reply;

  if (op == "STAT") reply = rfAdminStatText();
  else if (op == "GET") reply = rfAdminGetText();
  else if (op == "DIGI") {
    if (!setRemoteOnOff(uarg, cfg.digiEnabled)) reply = "ERR VALUE";
    else reply = "OK DIGI " + String(cfg.digiEnabled ? "ON" : "OFF") + remoteDirtySuffix();
  } else if (op == "IGATE") {
    if (!setRemoteOnOff(uarg, cfg.igateEnabled)) reply = "ERR VALUE";
    else reply = "OK IGATE " + String(cfg.igateEnabled ? "ON" : "OFF") + remoteDirtySuffix();
  } else if (op == "ISRF") {
    if (!setRemoteOnOff(uarg, cfg.isToRfEnabled)) reply = "ERR VALUE";
    else reply = "OK ISRF " + String(cfg.isToRfEnabled ? "ON" : "OFF") + remoteDirtySuffix();
  } else if (op == "RFBCN") {
    if (!setRemoteOnOff(uarg, cfg.rfBeaconEnabled)) reply = "ERR VALUE";
    else reply = "OK RFBCN " + String(cfg.rfBeaconEnabled ? "ON" : "OFF") + remoteDirtySuffix();
  } else if (op == "ISBCN") {
    if (!setRemoteOnOff(uarg, cfg.isBeaconEnabled)) reply = "ERR VALUE";
    else reply = "OK ISBCN " + String(cfg.isBeaconEnabled ? "ON" : "OFF") + remoteDirtySuffix();
  } else if (op == "RXSQL") {
    int v = arg.toInt();
    if (!digitsOnly(arg) || v < 5 || v > 1000) reply = "ERR VALUE";
    else { cfg.rxSquelch = (uint16_t)v; applyModemConfig(); rfAdminDirty = true; reply = "OK RXSQL " + String(v) + remoteDirtySuffix(); }
  } else if (op == "RXGAIN") {
    int v = arg.toInt();
    if (!digitsOnly(arg) || !((v == 0) || (v >= 20 && v <= 500))) reply = "ERR VALUE";
    else { cfg.rxDecoderGain = (uint16_t)v; applyModemConfig(); rfAdminDirty = true; reply = "OK RXGAIN " + String(v) + remoteDirtySuffix(); }
  } else if (op == "TXLVL") {
    int v = arg.toInt();
    if (!digitsOnly(arg) || v < 10 || v > 100) reply = "ERR VALUE";
    else { cfg.txLevel = (uint8_t)v; applyModemConfig(); rfAdminDirty = true; reply = "OK TXLVL " + String(v) + remoteDirtySuffix(); }
  } else if (op == "BSEC") {
    long v = arg.toInt();
    if (!digitsOnly(arg) || v < 30 || v > 86400) reply = "ERR VALUE";
    else { cfg.beaconSec = (uint32_t)v; rfAdminDirty = true; reply = "OK BSEC " + String(v) + remoteDirtySuffix(); }
  } else if (op == "DDELAY") {
    int v = arg.toInt();
    if (!digitsOnly(arg) || v < 0 || v > 3000) reply = "ERR VALUE";
    else { cfg.digiDelayMs = (uint16_t)v; rfAdminDirty = true; reply = "OK DDELAY " + String(v) + remoteDirtySuffix(); }
  } else if (op == "DUPSEC") {
    int v = arg.toInt();
    if (!digitsOnly(arg) || v < 5 || v > 300) reply = "ERR VALUE";
    else { cfg.duplicateSec = (uint16_t)v; rfAdminDirty = true; reply = "OK DUPSEC " + String(v) + remoteDirtySuffix(); }
  } else if (op == "FILTER") {
    String f = uarg; f.toLowerCase();
    if (!parseLocalIsRfFilter(f).valid) reply = "ERR FILTER";
    else { cfg.localIsRfFilter = f; rfAdminDirty = true; reply = "OK FILTER " + f + remoteDirtySuffix(); }
  } else if (op == "ALIAS") {
    String v = uarg; v.trim();
    if (!v.length() || v.length() > 40) reply = "ERR VALUE";
    else { cfg.digiAliases = v; rfAdminDirty = true; reply = "OK ALIAS" + remoteDirtySuffix(); }
  } else if (op == "RFPATH" || op == "ISPATH") {
    String v = uarg; v.trim();
    if (v == "DIRECT") v = "";
    if (!validPathText(v)) reply = "ERR PATH";
    else {
      if (op == "RFPATH") cfg.rfPath = v; else cfg.isToRfPath = v;
      rfAdminDirty = true;
      reply = "OK " + op + " " + (v.length() ? v : String("DIRECT")) + remoteDirtySuffix();
    }
  } else if (op == "COMMENT") {
    String v = singleLine(arg); v.trim();
    if (v.length() > 100) v = v.substring(0, 100);
    cfg.comment = v; rfAdminDirty = true; reply = "OK COMMENT" + remoteDirtySuffix();
  } else if (op == "OLED") {
    bool old = cfg.oledEnabled;
    if (!setRemoteOnOff(uarg, cfg.oledEnabled)) reply = "ERR VALUE";
    else { applyRemoteOledState(old); reply = "OK OLED " + String(cfg.oledEnabled ? "ON" : "OFF") + remoteDirtySuffix(); }
  } else if (op == "SAVE") {
    persistRfAdminOperationalConfig();
    reply = "OK SAVED";
  } else if (op == "CANCEL") {
    restoreRfAdminSavedConfig();
    reply = "OK CANCEL";
  } else if (op == "REBOOT") {
    reply = "OK REBOOT";
    rfAdminRebootPending = true;
    rfAdminRebootRequestedMs = millis();
  } else {
    reply = "ERR CMD";
  }

  addLog("RF ADMIN " + src + " CMD=" + upper + " -> " + reply);
  queueRfAdminReply(src, reply);
  return true;
}

static void serviceRfAdmin() {
  if (!rfAdminRebootPending) return;
  // Cooperative/non-blocking reboot: give the queued APRS reply time to TX.
  uint32_t age = millis() - rfAdminRebootRequestedMs;
  if (age < 1200UL) return;
  if ((txHead == txTail && !AFSK_isTransmitting()) || age > 10000UL) {
    rfAdminRebootPending = false;
    ESP.restart();
  }
}

static void onAx25Frame(AX25Msg *msg) {
  if (!msg) return;
  ++rfRxCount;
  String tnc = msgToTnc2(*msg);
  uint8_t dec = AFSK_getLastReadDecoder();
  String decName;
  if (dec == 1) decName = "A";
  else if (dec == 2) decName = "B-RAW";
  else if (dec == 3) decName = "B-EQ125";
  else if (dec == 4) decName = "B-EQ130";
  else decName = "?";
  addLog("RF RX [DEC-" + decName + "] " + tnc);

  lastRfSource = callToString(msg->src);
  lastRfDestination = callToString(msg->dst);
  lastRfSummary = "";
  for (size_t i = 0; i < msg->len && lastRfSummary.length() < 90; ++i) {
    char c = (char)msg->info[i];
    if (c && c != '\r' && c != '\n') lastRfSummary += c;
  }
  lastRfEventMs = millis();
  if (cfg.oledEnabled && oled.ready()) oledPacketPopupUntil = millis() + 3000UL;

  // Update the six-row local-station OLED page only when the RF packet
  // contains a valid APRS position.
  noteOledHeardPosition(*msg);

  NusaWeatherData wx;
  if (NusaWeather_parse(*msg, wx)) {
    lastWeather = wx;
    addLog("WEATHER RF < " + String(lastWeather.source) +
           (lastWeather.tempValid ? " T=" + String(lastWeather.tempC,1) + "C" : "") +
           (lastWeather.humidityValid ? " H=" + String(lastWeather.humidity) + "%" : ""));
    if (cfg.oledEnabled && oled.ready()) oledWeatherPopupUntil = millis() + (uint32_t)cfg.oledPopupSec * 1000UL;
  }

  // Learn RF reachability before any iGate/digipeater processing.  A packet
  // with no used repeater H bits is hop 0 and qualifies for i/30/0.
  noteRfHeard(*msg);

  gateToAprsIs(*msg);
  maybeDigipeat(*msg);

  // Additive RF Remote Admin runs after the legacy iGate/digipeater path so
  // existing forwarding/queue order remains unchanged. Non-admin gets no reply.
  processRfAdminMessage(*msg);
}

static String aprsPositionPayload() {
  double alat = fabs(cfg.lat), alon = fabs(cfg.lon);
  int latDeg = (int)alat;
  int lonDeg = (int)alon;
  double latMin = (alat - latDeg) * 60.0;
  double lonMin = (alon - lonDeg) * 60.0;
  char b[128];
  char ns = cfg.lat >= 0 ? 'N' : 'S';
  char ew = cfg.lon >= 0 ? 'E' : 'W';
  snprintf(b, sizeof(b), "!%02d%05.2f%c%c%03d%05.2f%c%c",
           latDeg, latMin, ns, cfg.symbolTable,
           lonDeg, lonMin, ew, cfg.symbolCode);
  String s(b);
  if (cfg.comment.length()) s += cfg.comment;
  return s;
}

static bool buildBeaconRaw(uint8_t *raw, size_t *rawLen) {
  if (!raw || !rawLen || sanitizeBaseCall(cfg.call) == "NOCALL") return false;
  AX25Call src = ax25_makeCall(cfg.call.c_str(), cfg.ssid);
  AX25Call dst{};
  if (!parseCallText(cfg.dest, dst)) dst = ax25_makeCall("APNU01", 0);
  AX25Call path[AX25_MAX_RPT]{};
  uint8_t pathCount = parsePath(cfg.rfPath, path);
  String payload = aprsPositionPayload();
  return ax25_buildUI(raw, rawLen, &dst, &src, path, pathCount, 0,
                      (const uint8_t *)payload.c_str(), payload.length());
}

static void sendRfBeacon() {
  uint8_t raw[AX25_MAX_FRAME_LEN];
  size_t len = 0;
  if (!buildBeaconRaw(raw, &len)) {
    addLog("RF beacon skipped: invalid callsign/config");
    return;
  }
  if (queueRf(raw, len, TX_BEACON, 0)) addLog("RF beacon queued");
}

static void sendIsBeacon() {
  // Automatic scheduling is controlled by cfg.isBeaconEnabled in loop().
  // Keeping this function independent allows the WEB "BEACON IS NOW" button
  // to work even when automatic APRS-IS beaconing is disabled.
  if (!aprsClient.connected() || sanitizeBaseCall(cfg.call) == "NOCALL") return;
  String line = stationCall() + ">" + cfg.dest + ",TCPIP*:" + aprsPositionPayload() + "\r\n";
  aprsClient.print(line);
  ++isBeaconCount;
  addLog("BEACON > IS  " + line.substring(0, line.length() - 2));
}

static void serviceRfTx() {
  if (txHead == txTail || AFSK_isTransmitting()) return;
  TxJob &j = txQueue[txHead];
  uint32_t now = millis();
  if (!timeReached(now, j.due)) return;

  if (AFSK_channelBusy()) {
    if ((uint32_t)(now - j.queued) > 5000U) {
      addLog("RF TX dropped: channel busy >5s");
      ++droppedTxCount;
      if (j.kind == TX_IS_TO_RF) ++isToRfDropCount;
      txHead = (uint8_t)((txHead + 1) % TX_QUEUE_SIZE);
    }
    return;
  }

  if (j.kind == TX_IS_TO_RF && !isToRfRateAllowed()) {
    addLog("IS>RF DROP rate limit (6/min, 10/5min)");
    ++isToRfDropCount;
    txHead = (uint8_t)((txHead + 1) % TX_QUEUE_SIZE);
    return;
  }

  if (j.kind == TX_DIGI) addLog("RF TX DIGI");
  else if (j.kind == TX_IS_TO_RF) addLog("RF TX IS>RF MESSAGE");
  else if (j.kind == TX_REMOTE_ADMIN) addLog("RF TX ADMIN REPLY");
  else addLog("RF TX BEACON");

  AFSK_transmit((char *)j.data, j.len);
  if (j.kind == TX_DIGI) ++digiTxCount;
  else if (j.kind == TX_IS_TO_RF) { ++isToRfTxCount; noteIsToRfTxNow(); }
  else if (j.kind == TX_REMOTE_ADMIN) { /* counted when queued */ }
  else ++rfBeaconCount;
  txHead = (uint8_t)((txHead + 1) % TX_QUEUE_SIZE);
}

static void loadConfig() {
  prefs.begin("nusaaprs", true);
  cfg.call = prefs.getString("call", cfg.call);
  cfg.ssid = prefs.getUChar("ssid", cfg.ssid);
  cfg.dest = prefs.getString("dest", cfg.dest);
  cfg.wifiSsid = prefs.getString("wssid", cfg.wifiSsid);
  cfg.wifiPass = prefs.getString("wpass", cfg.wifiPass);
  cfg.apSsid = prefs.getString("apssid", cfg.apSsid);
  cfg.apPass = prefs.getString("appass", cfg.apPass);
  cfg.isServer = prefs.getString("server", cfg.isServer);
  cfg.isPort = prefs.getUShort("port", cfg.isPort);
  cfg.isPasscode = prefs.getInt("ispass", cfg.isPasscode);
  cfg.isFilter = prefs.getString("filter", cfg.isFilter);
  cfg.igateEnabled = prefs.getBool("igate", cfg.igateEnabled);
  cfg.isToRfEnabled = prefs.getBool("is2rf", cfg.isToRfEnabled);
  cfg.localIsRfFilter = prefs.getString("isrfflt", cfg.localIsRfFilter);
  cfg.isToRfPath = prefs.getString("isrfpath", cfg.isToRfPath);
  cfg.digiEnabled = prefs.getBool("digi", cfg.digiEnabled);
  cfg.digiAliases = prefs.getString("alias", cfg.digiAliases);
  cfg.digiDelayMs = prefs.getUShort("ddelay", cfg.digiDelayMs);
  cfg.duplicateSec = prefs.getUShort("dupsec", cfg.duplicateSec);
  cfg.rfBeaconEnabled = prefs.getBool("rfbcn", cfg.rfBeaconEnabled);
  cfg.isBeaconEnabled = prefs.getBool("isbcn", cfg.isBeaconEnabled);
  cfg.beaconSec = prefs.getUInt("bsec", cfg.beaconSec);
  cfg.lat = prefs.getDouble("lat", cfg.lat);
  cfg.lon = prefs.getDouble("lon", cfg.lon);
  String st = prefs.getString("symtbl", String(cfg.symbolTable)); if (st.length()) cfg.symbolTable = st[0];
  String sc = prefs.getString("sym", String(cfg.symbolCode)); if (sc.length()) cfg.symbolCode = sc[0];
  cfg.comment = prefs.getString("comment", cfg.comment);
  cfg.rfPath = prefs.getString("path", cfg.rfPath);
  cfg.preambleMs = prefs.getUShort("pre", cfg.preambleMs);
  cfg.tailMs = prefs.getUShort("tail", cfg.tailMs);
  cfg.txLevel = prefs.getUChar("txlvl", cfg.txLevel);
  cfg.rxSquelch = prefs.getUShort("rxsql", cfg.rxSquelch);
  cfg.rxDecoderGain = prefs.getUShort("rxgain", cfg.rxDecoderGain);
  uint8_t audioTuneRev = prefs.getUChar("audrev", 0);
  cfg.pttPin = prefs.getInt("pttpin", cfg.pttPin);
  cfg.pttActiveHigh = prefs.getBool("ptthi", cfg.pttActiveHigh);
  cfg.rxLedPin = prefs.getInt("rxled", cfg.rxLedPin);
  cfg.txLedPin = prefs.getInt("txled", cfg.txLedPin);
  cfg.oledEnabled = prefs.getBool("oled", cfg.oledEnabled);
  cfg.oledSdaPin = prefs.getInt("osda", cfg.oledSdaPin);
  cfg.oledSclPin = prefs.getInt("oscl", cfg.oledSclPin);
  cfg.oledAddress = prefs.getUChar("oaddr", cfg.oledAddress);
  cfg.oledController = prefs.getUChar("octrl", cfg.oledController);
  if (cfg.oledController != 1 && cfg.oledController != 2) cfg.oledController = 2;
  cfg.oledContrast = prefs.getUChar("ocon", cfg.oledContrast);
  cfg.oledFlip = prefs.getBool("oflip", cfg.oledFlip);
  cfg.oledRotateSec = prefs.getUChar("orot", cfg.oledRotateSec);
  cfg.oledPopupSec = prefs.getUChar("opop", cfg.oledPopupSec);
  cfg.oledWeatherMaxAgeMin = prefs.getUShort("owxage", cfg.oledWeatherMaxAgeMin);
  cfg.rfAdminEnabled = prefs.getBool("rfadm", cfg.rfAdminEnabled);
  cfg.rfAdmin1 = prefs.getString("rfadm1", cfg.rfAdmin1);
  cfg.rfAdmin2 = prefs.getString("rfadm2", cfg.rfAdmin2);
  cfg.rfAdminReplyPath = prefs.getString("rfadmpath", cfg.rfAdminReplyPath);
  prefs.end();

  cfg.call = sanitizeBaseCall(cfg.call);

  // REV1F keeps the REV1E i/time/hops migration: if it was typed into the old
  // Server filter field, move it to the local device filter automatically.
  String migratedFilter = cfg.isFilter; migratedFilter.trim(); migratedFilter.toLowerCase();
  if (migratedFilter.startsWith("i/")) {
    cfg.localIsRfFilter = cfg.isFilter;
    cfg.isFilter = "";
    cfg.isToRfEnabled = true;
  }
  if (!parseLocalIsRfFilter(cfg.localIsRfFilter).valid) cfg.localIsRfFilter = "i/30/0";
  cfg.isToRfPath.trim(); cfg.isToRfPath.toUpperCase();

  cfg.ssid = constrain(cfg.ssid, (uint8_t)0, (uint8_t)15);
  cfg.lat = constrain(cfg.lat, -90.0, 90.0);
  cfg.lon = constrain(cfg.lon, -180.0, 180.0);
  cfg.txLevel = constrain(cfg.txLevel, (uint8_t)10, (uint8_t)100);
  cfg.rxSquelch = constrain(cfg.rxSquelch, (uint16_t)5, (uint16_t)1000);
  if (cfg.rxDecoderGain != 0) cfg.rxDecoderGain = constrain(cfg.rxDecoderGain, (uint16_t)20, (uint16_t)500);

  // REV1F one-time audio migration. REV1A-E used 65% as the default TX DAC
  // level. If the stored value is still exactly that old default, move it to
  // 90%. Any user-tuned value is preserved.
  if (audioTuneRev < 1) {
    if (cfg.txLevel == 65) cfg.txLevel = 90;
    prefs.begin("nusaaprs", false);
    prefs.putUChar("audrev", 1);
    prefs.putUChar("txlvl", cfg.txLevel);
    prefs.end();
  }
  cfg.beaconSec = constrain(cfg.beaconSec, 30UL, 86400UL);
  if (cfg.apPass.length() < 8) cfg.apPass = "12345678";
  cfg.oledSdaPin = constrain(cfg.oledSdaPin, 0, 33);
  cfg.oledSclPin = constrain(cfg.oledSclPin, 0, 33);
  if (cfg.oledAddress != 0x3C && cfg.oledAddress != 0x3D) cfg.oledAddress = 0;
  cfg.oledRotateSec = constrain(cfg.oledRotateSec, (uint8_t)2, (uint8_t)30);
  cfg.oledPopupSec = constrain(cfg.oledPopupSec, (uint8_t)3, (uint8_t)30);
  cfg.oledWeatherMaxAgeMin = constrain(cfg.oledWeatherMaxAgeMin, (uint16_t)1, (uint16_t)1440);
  cfg.rfAdmin1 = normalizeCallSsid(cfg.rfAdmin1);
  cfg.rfAdmin2 = normalizeCallSsid(cfg.rfAdmin2);
  if (cfg.rfAdmin2.length() && cfg.rfAdmin2 == cfg.rfAdmin1) cfg.rfAdmin2 = "";
  cfg.rfAdminReplyPath.trim(); cfg.rfAdminReplyPath.toUpperCase();
  if (cfg.rfAdminReplyPath == "DIRECT") cfg.rfAdminReplyPath = "";
  if (!validPathText(cfg.rfAdminReplyPath)) cfg.rfAdminReplyPath = "";
  if (!cfg.rfAdmin1.length() && !cfg.rfAdmin2.length()) cfg.rfAdminEnabled = false;
}

static void saveConfigFromWeb() {
  cfg.call = sanitizeBaseCall(web.arg("call"));
  cfg.ssid = constrain(web.arg("ssid").toInt(), 0, 15);
  cfg.dest = singleLine(web.arg("dest")); cfg.dest.trim(); cfg.dest.toUpperCase();
  AX25Call destCheck{}; if (!parseCallText(cfg.dest, destCheck)) cfg.dest = "APNU01";
  cfg.wifiSsid = web.arg("wssid");
  if (web.arg("wpass").length()) cfg.wifiPass = web.arg("wpass");
  cfg.apSsid = web.arg("apssid");
  if (cfg.apSsid.length() < 1) cfg.apSsid = "NUSA-APRS-IGATE";
  if (web.arg("appass").length() >= 8) cfg.apPass = web.arg("appass");

  cfg.isServer = singleLine(web.arg("server")); cfg.isServer.trim();
  cfg.isPort = constrain(web.arg("port").toInt(), 1, 65535);
  cfg.isPasscode = web.arg("ispass").toInt();
  cfg.isFilter = singleLine(web.arg("filter")); cfg.isFilter.trim();
  cfg.igateEnabled = web.hasArg("igate");

  cfg.isToRfEnabled = web.hasArg("is2rf");
  cfg.localIsRfFilter = singleLine(web.arg("localfilter")); cfg.localIsRfFilter.trim(); cfg.localIsRfFilter.toLowerCase();
  cfg.isToRfPath = singleLine(web.arg("isrfpath")); cfg.isToRfPath.trim(); cfg.isToRfPath.toUpperCase();

  // Friendly migration: i/30/0 typed into Server filter is treated as the
  // local device filter instead of being sent to APRS-IS.
  String serverFilterLower = cfg.isFilter; serverFilterLower.toLowerCase();
  if (serverFilterLower.startsWith("i/")) {
    cfg.localIsRfFilter = cfg.isFilter; cfg.localIsRfFilter.toLowerCase();
    cfg.isFilter = "";
    cfg.isToRfEnabled = true;
  }
  if (!parseLocalIsRfFilter(cfg.localIsRfFilter).valid) cfg.localIsRfFilter = "i/30/0";

  cfg.digiEnabled = web.hasArg("digi");
  cfg.digiAliases = web.arg("alias"); cfg.digiAliases.trim(); cfg.digiAliases.toUpperCase();
  cfg.digiDelayMs = constrain(web.arg("ddelay").toInt(), 0, 3000);
  cfg.duplicateSec = constrain(web.arg("dupsec").toInt(), 5, 300);

  cfg.rfBeaconEnabled = web.hasArg("rfbcn");
  cfg.isBeaconEnabled = web.hasArg("isbcn");
  cfg.beaconSec = constrain((uint32_t)web.arg("bsec").toInt(), 30UL, 86400UL);
  cfg.lat = constrain(web.arg("lat").toDouble(), -90.0, 90.0);
  cfg.lon = constrain(web.arg("lon").toDouble(), -180.0, 180.0);
  String st = web.arg("symtbl"); cfg.symbolTable = st.length() ? st[0] : '/';
  String sc = web.arg("sym"); cfg.symbolCode = sc.length() ? sc[0] : '#';
  cfg.comment = singleLine(web.arg("comment"));
  cfg.rfPath = singleLine(web.arg("path")); cfg.rfPath.trim(); cfg.rfPath.toUpperCase();

  cfg.preambleMs = constrain(web.arg("pre").toInt(), 50, 1500);
  cfg.tailMs = constrain(web.arg("tail").toInt(), 20, 1000);
  cfg.txLevel = constrain(web.arg("txlvl").toInt(), 10, 100);
  cfg.rxSquelch = constrain(web.arg("rxsql").toInt(), 5, 1000);
  {
    int rg = web.arg("rxgain").toInt();
    cfg.rxDecoderGain = (rg <= 0) ? 0 : (uint16_t)constrain(rg, 20, 500);
  }
  cfg.pttPin = constrain(web.arg("pttpin").toInt(), -1, 39);
  cfg.pttActiveHigh = web.hasArg("ptthi");
  cfg.rxLedPin = constrain(web.arg("rxled").toInt(), -1, 39);
  cfg.txLedPin = constrain(web.arg("txled").toInt(), -1, 39);

  cfg.oledEnabled = web.hasArg("oled");
  cfg.oledSdaPin = constrain(web.arg("osda").toInt(), 0, 33);
  cfg.oledSclPin = constrain(web.arg("oscl").toInt(), 0, 33);
  {
    String oa = web.arg("oaddr"); oa.trim(); oa.toLowerCase();
    cfg.oledAddress = (oa == "0x3c" || oa == "3c") ? 0x3C : ((oa == "0x3d" || oa == "3d") ? 0x3D : 0);
  }
  cfg.oledController = (web.arg("octrl") == "ssd1306") ? 1 : 2;
  cfg.oledContrast = (uint8_t)constrain(web.arg("ocon").toInt(), 1, 255);
  cfg.oledFlip = web.hasArg("oflip");
  cfg.oledRotateSec = (uint8_t)constrain(web.arg("orot").toInt(), 2, 30);
  cfg.oledPopupSec = (uint8_t)constrain(web.arg("opop").toInt(), 3, 30);
  cfg.oledWeatherMaxAgeMin = (uint16_t)constrain(web.arg("owxage").toInt(), 1, 1440);

  cfg.rfAdminEnabled = web.hasArg("rfadm");
  cfg.rfAdmin1 = normalizeCallSsid(singleLine(web.arg("rfadm1")));
  cfg.rfAdmin2 = normalizeCallSsid(singleLine(web.arg("rfadm2")));
  if (cfg.rfAdmin2.length() && cfg.rfAdmin2 == cfg.rfAdmin1) cfg.rfAdmin2 = "";
  cfg.rfAdminReplyPath = singleLine(web.arg("rfadmpath")); cfg.rfAdminReplyPath.trim(); cfg.rfAdminReplyPath.toUpperCase();
  if (cfg.rfAdminReplyPath == "DIRECT") cfg.rfAdminReplyPath = "";
  if (!validPathText(cfg.rfAdminReplyPath)) cfg.rfAdminReplyPath = "";
  if (!cfg.rfAdmin1.length() && !cfg.rfAdmin2.length()) cfg.rfAdminEnabled = false;

  prefs.begin("nusaaprs", false);
  prefs.putString("call", cfg.call); prefs.putUChar("ssid", cfg.ssid); prefs.putString("dest", cfg.dest);
  prefs.putString("wssid", cfg.wifiSsid); prefs.putString("wpass", cfg.wifiPass);
  prefs.putString("apssid", cfg.apSsid); prefs.putString("appass", cfg.apPass);
  prefs.putString("server", cfg.isServer); prefs.putUShort("port", cfg.isPort); prefs.putInt("ispass", cfg.isPasscode);
  prefs.putString("filter", cfg.isFilter); prefs.putBool("igate", cfg.igateEnabled);
  prefs.putBool("is2rf", cfg.isToRfEnabled); prefs.putString("isrfflt", cfg.localIsRfFilter); prefs.putString("isrfpath", cfg.isToRfPath);
  prefs.putBool("digi", cfg.digiEnabled); prefs.putString("alias", cfg.digiAliases);
  prefs.putUShort("ddelay", cfg.digiDelayMs); prefs.putUShort("dupsec", cfg.duplicateSec);
  prefs.putBool("rfbcn", cfg.rfBeaconEnabled); prefs.putBool("isbcn", cfg.isBeaconEnabled); prefs.putUInt("bsec", cfg.beaconSec);
  prefs.putDouble("lat", cfg.lat); prefs.putDouble("lon", cfg.lon); prefs.putString("symtbl", String(cfg.symbolTable)); prefs.putString("sym", String(cfg.symbolCode));
  prefs.putString("comment", cfg.comment); prefs.putString("path", cfg.rfPath);
  prefs.putUShort("pre", cfg.preambleMs); prefs.putUShort("tail", cfg.tailMs); prefs.putUChar("txlvl", cfg.txLevel); prefs.putUShort("rxsql", cfg.rxSquelch); prefs.putUShort("rxgain", cfg.rxDecoderGain); prefs.putUChar("audrev", 1);
  prefs.putInt("pttpin", cfg.pttPin); prefs.putBool("ptthi", cfg.pttActiveHigh); prefs.putInt("rxled", cfg.rxLedPin); prefs.putInt("txled", cfg.txLedPin);
  prefs.putBool("oled", cfg.oledEnabled); prefs.putInt("osda", cfg.oledSdaPin); prefs.putInt("oscl", cfg.oledSclPin);
  prefs.putUChar("oaddr", cfg.oledAddress); prefs.putUChar("octrl", cfg.oledController); prefs.putUChar("ocon", cfg.oledContrast); prefs.putBool("oflip", cfg.oledFlip);
  prefs.putUChar("orot", cfg.oledRotateSec); prefs.putUChar("opop", cfg.oledPopupSec); prefs.putUShort("owxage", cfg.oledWeatherMaxAgeMin);
  prefs.putBool("rfadm", cfg.rfAdminEnabled); prefs.putString("rfadm1", cfg.rfAdmin1); prefs.putString("rfadm2", cfg.rfAdmin2); prefs.putString("rfadmpath", cfg.rfAdminReplyPath);
  prefs.end();
}

static void applyModemConfig() {
  AFSK_setPtt(cfg.pttPin, cfg.pttActiveHigh);
  AFSK_setLeds(cfg.rxLedPin, cfg.txLedPin);
  AFSK_setPreamble(cfg.preambleMs);
  AFSK_setTail(cfg.tailMs);
  AFSK_setTxLevel(cfg.txLevel);
  AFSK_setRxSquelch(cfg.rxSquelch);
  AFSK_setRxDecoderGain(cfg.rxDecoderGain);
}

static void startWifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("nusa-aprs-igate");
  bool apOk = WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str());
  addLog(String("AP ") + (apOk ? "ready " : "FAILED ") + cfg.apSsid + "  IP=" + WiFi.softAPIP().toString());
  if (cfg.wifiSsid.length()) {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    addLog("WiFi STA connecting to " + cfg.wifiSsid);
  }
}

static void disconnectAprsIs() {
  if (aprsClient.connected()) aprsClient.stop();
  isLineBuffer = "";
}

static void serviceWifi() {
  if (!cfg.wifiSsid.length()) return;
  if (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - lastWifiRetry) > 30000U) {
    lastWifiRetry = millis();
    WiFi.disconnect();
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  }
}

static void serviceAprsIs() {
  bool needIs = cfg.igateEnabled || cfg.isToRfEnabled || cfg.isBeaconEnabled;
  if (!needIs || WiFi.status() != WL_CONNECTED || sanitizeBaseCall(cfg.call) == "NOCALL") {
    disconnectAprsIs();
    return;
  }

  if (!aprsClient.connected()) {
    if ((uint32_t)(millis() - lastIsConnectAttempt) < 15000U) return;
    lastIsConnectAttempt = millis();
    addLog("APRS-IS connecting " + cfg.isServer + ":" + String(cfg.isPort));
    if (!aprsClient.connect(cfg.isServer.c_str(), cfg.isPort)) {
      addLog("APRS-IS connect failed");
      return;
    }
    aprsClient.setNoDelay(true);
    int pass = cfg.isPasscode > 0 ? cfg.isPasscode : aprsPasscode(stationCall());
    String login = "user " + stationCall() + " pass " + String(pass) + " vers " + APRS_SW + " " + APRS_SW_VER;
    if (cfg.isFilter.length()) login += " filter " + cfg.isFilter;
    aprsClient.print(login + "\r\n");
    addLog("APRS-IS connected / login sent; local IS>RF=" + cfg.localIsRfFilter);
  }

  while (aprsClient.available()) {
    char c = (char)aprsClient.read();
    if (c == '\n') {
      ++isRxLineCount;
      String line = isLineBuffer;
      isLineBuffer = "";
      if (line.length()) processAprsIsLine(line);
    } else if (c != '\r' && isLineBuffer.length() < 600) {
      isLineBuffer += c;
    }
  }
}

static String checked(bool v) { return v ? " checked" : ""; }

static void handleRoot() {
  String h; h.reserve(43000);
  h += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta charset='utf-8'><meta name='theme-color' content='#0b1220'><title>NUSA APRS</title><style>"
         "*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#09111f;color:#eaf1fb;font-family:Arial,Helvetica,sans-serif;font-size:14px}"
         ".top{position:sticky;top:0;z-index:20;background:rgba(12,22,39,.96);border-bottom:1px solid #263957;backdrop-filter:blur(8px)}"
         ".topin{max-width:1180px;margin:auto;padding:14px 18px;display:flex;align-items:center;justify-content:space-between;gap:12px}.brand{font-weight:800;font-size:20px;letter-spacing:.2px}.tag{display:inline-block;margin-left:7px;padding:3px 8px;border-radius:999px;background:#18355e;color:#9fc5ff;font-size:11px;vertical-align:2px}.sub{color:#7f93af;font-size:12px;margin-top:3px}"
         ".wrap{max-width:1180px;margin:auto;padding:16px}.nav{display:flex;gap:7px;overflow:auto;padding:2px 0 14px;scrollbar-width:none}.nav::-webkit-scrollbar{display:none}.tabbtn{white-space:nowrap;border:1px solid #2a3d5c;background:#111d31;color:#aebdd3;padding:9px 13px;border-radius:9px;font-weight:700;cursor:pointer}.tabbtn.active{background:#2563a9;border-color:#4385d2;color:#fff}"
         ".panel{display:none}.panel.active{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}.grid2{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}.card,.box,fieldset{background:#101c30;border:1px solid #253a59;border-radius:13px}.card{padding:13px 14px;min-height:76px}.card small{color:#8da0ba;display:block;margin-bottom:5px}.card strong{font-size:19px;line-height:1.2}.box{padding:15px;margin-bottom:12px}fieldset{padding:16px;margin:0 0 12px}legend{font-weight:800;color:#a9c9ff;padding:0 7px}"
         ".sectiontitle{font-size:17px;font-weight:800;margin:4px 0 12px}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}label{display:block;color:#aebdd0;font-size:13px;margin-top:8px}.lbl{display:flex;justify-content:space-between;gap:6px}input,select{width:100%;margin-top:5px;padding:10px 11px;border:1px solid #344c70;border-radius:8px;background:#091424;color:#fff;outline:none}input:focus,select:focus{border-color:#5a9cf0;box-shadow:0 0 0 2px rgba(90,156,240,.14)}input[type=checkbox]{width:auto;margin:0 8px 0 0}.ck{display:flex;align-items:center;color:#eaf1fb;margin-top:13px}.hint{display:block;color:#7287a4;font-size:11px;margin-top:4px;line-height:1.35}"
         ".note{background:#101b2c;border-left:3px solid #d9a441;color:#d9c18d;padding:10px 12px;border-radius:7px;margin-top:11px;line-height:1.45}.note.blue{border-left-color:#4f91e5;color:#a9c7ee}.note.green{border-left-color:#42b983;color:#9fd9be}.ok{color:#66dda4}.bad{color:#ff7b87}.muted{color:#8496ae}.mono,.fixed,pre{font-family:Consolas,Monaco,monospace}"
         ".actions{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0}.btn{border:0;border-radius:9px;padding:10px 15px;font-weight:800;cursor:pointer;background:#347fe0;color:#fff;text-decoration:none;display:inline-block}.btn.secondary{background:#203654;color:#dbe8fa;border:1px solid #315078}.btn.danger{background:#8f3340}.btn.small{padding:8px 11px;font-size:12px}.savebar{position:sticky;bottom:10px;z-index:12;display:flex;align-items:center;justify-content:space-between;gap:10px;background:#101d31;border:1px solid #2b4569;border-radius:12px;padding:10px 12px;margin:15px 0;box-shadow:0 8px 28px rgba(0,0,0,.35)}.savebar span{color:#8ea2bd;font-size:12px}"
         ".metricline{display:flex;flex-wrap:wrap;gap:8px 18px;line-height:1.8}.metricline b{color:#f0f6ff}details{background:#0d1829;border:1px solid #233b5b;border-radius:10px;padding:10px 12px;margin-top:10px}summary{cursor:pointer;font-weight:700;color:#a9c9ff}pre{white-space:pre-wrap;word-break:break-word;background:#07101d;border:1px solid #1d304a;padding:12px;border-radius:9px;max-height:390px;overflow:auto;margin:8px 0 0;font-size:12px;line-height:1.42}.foot{text-align:center;color:#657b99;padding:17px 4px 4px;font-size:12px}"
         ".toast{position:fixed;right:16px;bottom:16px;z-index:50;max-width:360px;background:#17263d;border:1px solid #365577;color:#eef5ff;padding:12px 14px;border-radius:10px;box-shadow:0 10px 30px rgba(0,0,0,.4);display:none}.pill{display:inline-block;padding:3px 8px;border-radius:999px;background:#152943;color:#9db9dd;font-size:11px;font-weight:700}"
         "@media(max-width:640px){.topin{padding:12px 13px}.brand{font-size:17px}.wrap{padding:12px}.card strong{font-size:17px}.savebar{bottom:5px}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.card{min-height:72px}.btn{flex:1;text-align:center}.actions .btn{min-width:45%}}"
         "</style></head><body>");

  h += "<div class='top'><div class='topin'><div><div class='brand'>NUSA APRS iGate + Digipeater <span class='tag'>" + String(FW_VER) + "</span></div><div class='sub'>Classic ESP32 • GPIO36 RX • GPIO26 TX DAC</div></div><span class='pill' id='topstate'>STARTING</span></div></div><div class='wrap'>";
  h += F("<div class='nav'>"
         "<button type='button' class='tabbtn active' data-tab='overview' onclick=tab('overview',this)>Overview</button>"
         "<button type='button' class='tabbtn' data-tab='station' onclick=tab('station',this)>Station & Beacon</button>"
         "<button type='button' class='tabbtn' data-tab='network' onclick=tab('network',this)>Wi-Fi</button>"
         "<button type='button' class='tabbtn' data-tab='aprs' onclick=tab('aprs',this)>iGate & Digi</button>"
         "<button type='button' class='tabbtn' data-tab='radio' onclick=tab('radio',this)>Radio / AFSK</button>"
         "<button type='button' class='tabbtn' data-tab='remote' onclick=tab('remote',this)>RF Remote</button>"
         "<button type='button' class='tabbtn' data-tab='oled' onclick=tab('oled',this)>OLED</button>"
         "<button type='button' class='tabbtn' data-tab='monitor' onclick=tab('monitor',this)>Monitor</button>"
         "</div>");

  // OVERVIEW ---------------------------------------------------------------
  h += F("<section id='overview' class='panel active'>"
         "<div class='grid'>"
         "<div class='card'><small>Station</small><strong id='stcall'>-</strong></div>"
         "<div class='card'><small>Wi-Fi STA</small><strong id='wifi'>-</strong></div>"
         "<div class='card'><small>APRS-IS</small><strong id='is'>-</strong></div>"
         "<div class='card'><small>RF Channel</small><strong id='rf'>-</strong></div>"
         "<div class='card'><small>RX Valid</small><strong id='rx'>0</strong></div>"
         "<div class='card'><small>RF→IS / Digi TX</small><strong id='counts'>0 / 0</strong></div>"
         "<div class='card'><small>IS→RF TX / Drop</small><strong id='is2rfcounts'>0 / 0</strong></div>"
         "<div class='card'><small>Free Heap</small><strong><span id='heap'>0</span></strong><small>bytes</small></div>"
         "</div>");
  h += "<div class='box' style='margin-top:12px'><div class='sectiontitle'>Radio & Network at a glance</div><div class='metricline'>";
  h += "<span>AP IP <b class='mono'>" + WiFi.softAPIP().toString() + "</b></span><span>Local IP <b class='mono' id='ip'>-</b></span>";
  h += F("<span>Audio <b id='audio'>0</b></span><span>Peak <b id='audiopeak'>0</b></span><span>RX Gain <b id='rxgainnow'>100</b>%</span><span>ADC Clips <b id='adcclips'>0</b></span><span>ADC Min/Max <b id='adcraw'>-</b></span></div>"
         "<div class='actions'><button type='button' class='btn secondary' onclick=cmd('/beaconrf')>Beacon RF Now</button><button type='button' class='btn secondary' onclick=cmd('/beaconis')>Beacon IS Now</button><button type='button' class='btn danger' onclick=rebootNow()>Restart ESP32</button></div>"
         "<div class='note green'><b>Stable RX profile:</b> Decoder REV1L yang sudah terbukti menerima UV-5RH dipertahankan tanpa perubahan: MicroModem + B-RAW + B-EQ125 + B-EQ130.</div></div></section>");

  h += "<form method='POST' action='/save' id='cfgform'>";

  // STATION / BEACON --------------------------------------------------------
  h += F("<section id='station' class='panel'><fieldset><legend>Station Identity</legend><div class='row'>");
  h += "<label>Callsign<input name='call' maxlength='6' value='" + htmlEscape(cfg.call) + "'><span class='hint'>Base callsign tanpa SSID.</span></label>";
  h += "<label>SSID<input name='ssid' type='number' min='0' max='15' value='" + String(cfg.ssid) + "'><span class='hint'>0–15.</span></label>";
  h += "<label>Destination / ToCall<input name='dest' maxlength='9' value='" + htmlEscape(cfg.dest) + "'></label></div>";
  h += "<div class='row'><label>Latitude<input name='lat' value='" + String(cfg.lat, 6) + "'><span class='hint'>Decimal degrees.</span></label><label>Longitude<input name='lon' value='" + String(cfg.lon, 6) + "'><span class='hint'>Decimal degrees.</span></label><label>Symbol table<input name='symtbl' maxlength='1' value='" + htmlEscape(String(cfg.symbolTable)) + "'></label><label>Symbol code<input name='sym' maxlength='1' value='" + htmlEscape(String(cfg.symbolCode)) + "'></label></div>";
  h += "<label>Beacon comment<input name='comment' maxlength='100' value='" + htmlEscape(cfg.comment) + "'></label></fieldset>";
  h += F("<fieldset><legend>Beacon</legend><div class='row'>");
  h += "<label>RF path<input name='path' value='" + htmlEscape(cfg.rfPath) + "'><span class='hint'>Contoh WIDE2-1. Kosong = direct.</span></label><label>Interval<input name='bsec' type='number' min='30' value='" + String(cfg.beaconSec) + "'><span class='hint'>Detik.</span></label></div>";
  h += "<div class='grid2'><label class='ck'><input type='checkbox' name='rfbcn'" + checked(cfg.rfBeaconEnabled) + ">Automatic RF beacon</label><label class='ck'><input type='checkbox' name='isbcn'" + checked(cfg.isBeaconEnabled) + ">Automatic APRS-IS beacon</label></div></fieldset></section>";

  // NETWORK -----------------------------------------------------------------
  h += F("<section id='network' class='panel'><fieldset><legend>Local Wi-Fi / STA</legend><div class='row'>");
  h += "<label>Wi-Fi SSID<input name='wssid' value='" + htmlEscape(cfg.wifiSsid) + "'></label><label>Wi-Fi password<input type='password' name='wpass' placeholder='blank = keep current'><span class='hint'>Tidak ditampilkan kembali demi keamanan.</span></label></div></fieldset>";
  h += F("<fieldset><legend>ESP32 Access Point</legend><div class='row'>");
  h += "<label>AP SSID<input name='apssid' value='" + htmlEscape(cfg.apSsid) + "'></label><label>AP password<input type='password' name='appass' placeholder='blank = keep current'><span class='hint'>Minimal 8 karakter jika diganti.</span></label></div><div class='note blue'>Access Point tetap aktif bersamaan dengan koneksi Wi-Fi lokal. Semua perubahan jaringan diterapkan setelah Save & Restart.</div></fieldset></section>";

  // APRS / IGATE / DIGI ------------------------------------------------------
  h += F("<section id='aprs' class='panel'><fieldset><legend>APRS-IS iGate</legend><div class='row'>");
  h += "<label>Server<input name='server' value='" + htmlEscape(cfg.isServer) + "'></label><label>Port<input type='number' name='port' min='1' max='65535' value='" + String(cfg.isPort) + "'></label><label>Passcode<input type='number' name='ispass' value='" + String(cfg.isPasscode) + "'><span class='hint'>0 = hitung otomatis dari callsign.</span></label></div>";
  h += "<label>APRS-IS server filter<input name='filter' value='" + htmlEscape(cfg.isFilter) + "' placeholder='blank = server default; contoh m/100'><span class='hint'>Gunakan filter resmi APRS-IS. i/30/0 bukan server filter.</span></label>";
  h += "<label class='ck'><input type='checkbox' name='igate'" + checked(cfg.igateEnabled) + ">Enable RF → APRS-IS iGate</label></fieldset>";

  h += F("<fieldset><legend>APRS-IS → RF Message Gate</legend>");
  h += "<label class='ck'><input type='checkbox' name='is2rf'" + checked(cfg.isToRfEnabled) + ">Enable APRS-IS → RF messages</label>";
  h += "<div class='row'><label>Local IS→RF filter<input name='localfilter' value='" + htmlEscape(cfg.localIsRfFilter) + "' placeholder='i/30/0'></label><label>IS→RF path<input name='isrfpath' value='" + htmlEscape(cfg.isToRfPath) + "' placeholder='blank = DIRECT'></label></div>";
  h += F("<div class='note'><b>i/30/0:</b> hanya APRS message ke target yang terdengar langsung di RF dalam 30 menit terakhir dan maksimum 0 digi hop. Filter ini dijalankan lokal di ESP32. Path kosong = transmit direct.</div></fieldset>");

  h += F("<fieldset><legend>RF Digipeater</legend>");
  h += "<label class='ck'><input type='checkbox' name='digi'" + checked(cfg.digiEnabled) + ">Enable digipeater</label>";
  h += "<div class='row'><label>Aliases<input name='alias' value='" + htmlEscape(cfg.digiAliases) + "'><span class='hint'>Default WIDE1,WIDE2.</span></label><label>Digi delay<input name='ddelay' type='number' min='0' max='3000' value='" + String(cfg.digiDelayMs) + "'><span class='hint'>ms</span></label><label>Duplicate window<input name='dupsec' type='number' min='5' max='300' value='" + String(cfg.duplicateSec) + "'><span class='hint'>detik</span></label></div></fieldset></section>";

  // RADIO / AFSK ------------------------------------------------------------
  h += F("<section id='radio' class='panel'><fieldset><legend>AFSK / Radio Interface</legend><div class='row'>");
  h += "<label>TX preamble<input name='pre' type='number' min='50' max='1500' value='" + String(cfg.preambleMs) + "'><span class='hint'>ms</span></label><label>TX tail<input name='tail' type='number' min='20' max='1000' value='" + String(cfg.tailMs) + "'><span class='hint'>ms</span></label><label>TX audio level<input name='txlvl' type='number' min='10' max='100' value='" + String(cfg.txLevel) + "'><span class='hint'>Persen DAC, default 90%.</span></label><label>RX decoder gain<input name='rxgain' type='number' min='0' max='500' value='" + String(cfg.rxDecoderGain) + "'><span class='hint'>0 = AUTO. Manual 20–500%.</span></label><label>RX squelch ADC<input name='rxsql' type='number' min='5' max='1000' value='" + String(cfg.rxSquelch) + "'><span class='hint'>Set sedikit di atas noise idle.</span></label></div>";
  h += "<div class='row'><label>PTT GPIO<input name='pttpin' type='number' min='-1' max='39' value='" + String(cfg.pttPin) + "'></label><label>RX LED GPIO<input name='rxled' type='number' min='-1' max='39' value='" + String(cfg.rxLedPin) + "'></label><label>TX LED GPIO<input name='txled' type='number' min='-1' max='39' value='" + String(cfg.txLedPin) + "'></label></div>";
  h += "<label class='ck'><input type='checkbox' name='ptthi'" + checked(cfg.pttActiveHigh) + ">PTT active HIGH</label>";
  h += F("<div class='note blue'>RX audio tetap <b>GPIO36</b> dan TX AFSK DAC tetap <b>GPIO26</b> pada ESP32 klasik. Decoder UV-5RH REV1L dibekukan pada FINAL ini.</div>"
         "<details><summary>Panduan adjustment RX/TX</summary><div class='note blue'><b>RX:</b> mulai dengan RX Decoder Gain = 0/AUTO. Saat channel kosong Audio harus di bawah squelch. Saat packet masuk Audio harus naik jelas dan ADC Clips sebaiknya tidak bertambah. Jika clips bertambah, kecilkan pot/level analog RX; gain digital tidak dapat memperbaiki ADC yang sudah clipping.<br><br><b>Squelch:</b> set sedikit di atas noise tertinggi. Contoh noise 15–25, mulai 35–50. Jika BUSY tanpa packet naikkan sedikit; jika packet terdengar tetapi tetap CLEAR turunkan sedikit.<br><br><b>TX:</b> mulai 90%. Naikkan sampai 100% bila perlu. Di atas full-scale DAC tidak digunakan karena hanya menambah distorsi.</div></details></fieldset></section>");

  // RF REMOTE ADMIN ---------------------------------------------------------
  h += F("<section id='remote' class='panel'><fieldset><legend>RF Remote Admin</legend>");
  h += "<label class='ck'><input type='checkbox' name='rfadm'" + checked(cfg.rfAdminEnabled) + ">Enable remote konfigurasi melalui APRS Message RF</label>";
  h += "<div class='row'><label>Admin Callsign 1<input name='rfadm1' maxlength='9' value='" + htmlEscape(cfg.rfAdmin1) + "' placeholder='contoh YF9UAG-9'><span class='hint'>Harus cocok persis callsign + SSID pengirim.</span></label>";
  h += "<label>Admin Callsign 2<input name='rfadm2' maxlength='9' value='" + htmlEscape(cfg.rfAdmin2) + "' placeholder='opsional'><span class='hint'>Admin kedua; kosong jika tidak digunakan.</span></label>";
  h += "<label>Reply path<input name='rfadmpath' value='" + htmlEscape(cfg.rfAdminReplyPath) + "' placeholder='blank = DIRECT'><span class='hint'>Contoh WIDE2-1. DIRECT/kosong = tanpa digi.</span></label></div>";
  h += F("<div class='note green'><b>Silent reject:</b> message command dari callsign selain dua Admin di atas tidak pernah dibalas lewat RF. Admin identity, enable Remote, Wi-Fi, callsign station dan GPIO hanya dapat diubah dari Web lokal.</div>"
         "<div class='note blue'><b>Command singkat:</b> STAT, GET, DIGI ON/OFF, IGATE ON/OFF, ISRF ON/OFF, RFBCN ON/OFF, ISBCN ON/OFF, RXSQL n, RXGAIN n, TXLVL n, BSEC n, DDELAY n, DUPSEC n, FILTER i/30/0, ALIAS ..., RFPATH ..., ISPATH ..., COMMENT ..., OLED ON/OFF, SAVE, CANCEL, REBOOT.<br><b>*</b> pada reply berarti perubahan belum SAVE. Contoh: <span class='mono'>DIGI OFF</span> lalu <span class='mono'>SAVE</span>.</div></fieldset></section>");

  // OLED --------------------------------------------------------------------
  h += F("<section id='oled' class='panel'><fieldset><legend>OLED 128x64 I2C</legend>");
  h += "<label class='ck'><input type='checkbox' name='oled'" + checked(cfg.oledEnabled) + ">Aktifkan OLED</label>";
  h += "<div class='row'><label>SDA GPIO<input name='osda' type='number' min='0' max='33' value='" + String(cfg.oledSdaPin) + "'><span class='hint'>Default GPIO21.</span></label>";
  h += "<label>SCL GPIO<input name='oscl' type='number' min='0' max='33' value='" + String(cfg.oledSclPin) + "'><span class='hint'>Default GPIO22.</span></label>";
  h += "<label>Controller OLED<select name='octrl'><option value='sh1106'" + String(cfg.oledController==2 ? " selected" : "") + ">SH1106 (default modul foto)</option><option value='ssd1306'" + String(cfg.oledController==1 ? " selected" : "") + ">SSD1306</option></select><span class='hint'>Jika layar bergaris/acak, pilih SH1106.</span></label>";
  h += "<label>Alamat I2C<select name='oaddr'><option value='auto'" + String(cfg.oledAddress==0 ? " selected" : "") + ">AUTO (0x3C/0x3D)</option><option value='0x3c'" + String(cfg.oledAddress==0x3C ? " selected" : "") + ">0x3C</option><option value='0x3d'" + String(cfg.oledAddress==0x3D ? " selected" : "") + ">0x3D</option></select></label>";
  h += "<label>Contrast<input name='ocon' type='number' min='1' max='255' value='" + String(cfg.oledContrast) + "'></label>";
  h += "<label>Ganti halaman<input name='orot' type='number' min='2' max='30' value='" + String(cfg.oledRotateSec) + "'><span class='hint'>detik</span></label>";
  h += "<label>Durasi popup<input name='opop' type='number' min='3' max='30' value='" + String(cfg.oledPopupSec) + "'><span class='hint'>Popup weather, detik. Packet biasa tampil 3 detik.</span></label>";
  h += "<label>Weather berlaku<input name='owxage' type='number' min='1' max='1440' value='" + String(cfg.oledWeatherMaxAgeMin) + "'><span class='hint'>menit sebelum halaman weather dianggap stale.</span></label></div>";
  h += "<label class='ck'><input type='checkbox' name='oflip'" + checked(cfg.oledFlip) + ">Balik orientasi OLED 180 derajat</label>";
  h += F("<div class='note blue'>Wiring default: <b>VCC→3V3, GND→GND, SDA→GPIO21, SCL→GPIO22</b>. Jika OLED tidak terdeteksi, iGate/Digipeater tetap beroperasi normal. Weather berasal dari packet APRS yang benar-benar diterima lewat RF.</div></fieldset>"
         "<fieldset><legend>Tampilan OLED</legend><div class='grid'>"
         "<div class='card'><small>Status OLED</small><strong id='oledstate'>-</strong></div>"
         "<div class='card'><small>Alamat</small><strong id='oledaddr'>-</strong></div>"
         "<div class='card'><small>Weather terakhir</small><strong id='wxsrc'>-</strong><small id='wxage'>-</small></div>"
         "<div class='card'><small>Stasiun terdengar</small><strong>6 terbaru</strong><small>Callsign • arah • km</small></div>"
         "</div><div class='note green'>Halaman otomatis: Status Utama → Network → RF/Decoder → Packet RF Terakhir → 6 Stasiun Terdengar → Weather. Arah/jarak dihitung dari Latitude/Longitude station pada konfigurasi. Packet baru tampil sebagai popup; packet weather mendapat prioritas popup weather.</div></fieldset></section>");

  h += F("<div id='savebar' class='savebar' style='display:none'><span>Perubahan konfigurasi baru aktif setelah restart.</span><button class='btn' type='submit'>SAVE & RESTART</button></div></form>");

  // MONITOR / DIAGNOSTIC -----------------------------------------------------
  h += F("<section id='monitor' class='panel'><div class='box'><div class='sectiontitle'>RX Decoder Diagnostic</div><div class='grid'>"
         "<div class='card'><small>Last accepted decoder</small><strong id='lastdec'>NONE</strong></div>"
         "<div class='card'><small>Decoder A / MicroModem</small><strong id='deca'>0 / 0</strong><small>CRC OK / FAIL</small></div>"
         "<div class='card'><small>Decoder B / Tolerant</small><strong id='decb'>0 / 0</strong><small>Accepted OK / FAIL attempts</small></div>"
         "<div class='card'><small>B-RAW CRC</small><strong id='braw'>0 / 0</strong><small>OK / FAIL</small></div>"
         "<div class='card'><small>B-EQ125 CRC</small><strong id='bd75'>0 / 0</strong><small>SPACE +25%</small></div>"
         "<div class='card'><small>B-EQ130 CRC</small><strong id='bd220'>0 / 0</strong><small>SPACE +30%</small></div>"
         "</div><details><summary>Advanced decoder counters</summary><div class='grid' style='margin-top:10px'>"
         "<div class='card'><small>A Flags / Candidates</small><strong id='afc'>0 / 0</strong></div>"
         "<div class='card'><small>B Flags / Candidates</small><strong id='bfc'>0 / 0</strong></div>"
         "<div class='card'><small>B Mark / Space</small><strong id='tones'>0 / 0</strong></div>"
         "<div class='card'><small>B Tone confidence</small><strong id='conf'>0%</strong></div>"
         "<div class='card'><small>B Tone transitions</small><strong id='trans'>0</strong></div>"
         "<div class='card'><small>Dual decoder duplicates</small><strong id='dualdup'>0</strong></div>"
         "</div></details>"
         "<details><summary>RX WAV diagnostic capture</summary><div class='grid' style='margin-top:10px'><div class='card'><small>Capture state</small><strong id='capstat'>IDLE</strong><small id='capsamples'>0 samples</small></div></div><div class='actions'><button type='button' class='btn secondary small' onclick=caparm()>ARM NEXT RX WAV</button><button type='button' class='btn secondary small' onclick=diagreset()>RESET RX DIAGNOSTICS</button><a id='caplink' class='btn secondary small' style='display:none' href='/rxcapture.wav'>GET RX WAV</a></div><div class='note blue'>Capture 9.6 kHz mono tetap dipertahankan hanya sebagai alat service/diagnostic dan tidak perlu dipakai pada operasi normal.</div></details>"
         "</div>"
         "<div class='grid2'><fieldset><legend>RF Heard / Local Message Eligibility</legend><pre id='heard'>loading...</pre></fieldset><fieldset><legend>Live Log</legend><pre id='log'>loading...</pre></fieldset></div></section>");

  h += F("<div class='foot'>NUSA APRS ESP32 • FINAL based on proven REV1L UV-5RH decoder</div><div id='toast' class='toast'></div></div><script>"
         "function tab(id,b){document.querySelectorAll('.panel').forEach(x=>x.classList.remove('active'));let p=document.getElementById(id);if(p)p.classList.add('active');document.querySelectorAll('.tabbtn').forEach(x=>x.classList.remove('active'));if(b)b.classList.add('active');let sb=document.getElementById('savebar');if(sb)sb.style.display=(id=='station'||id=='network'||id=='aprs'||id=='radio'||id=='remote'||id=='oled')?'flex':'none';try{localStorage.setItem('nusaTab',id)}catch(e){}}"
         "function toast(t){let x=document.getElementById('toast');x.textContent=t;x.style.display='block';clearTimeout(window._tt);window._tt=setTimeout(()=>x.style.display='none',3200)}"
         "async function stat(){try{let r=await fetch('/status.json',{cache:'no-store'});let s=await r.json();document.getElementById('stcall').textContent=s.call;let w=document.getElementById('wifi');w.textContent=s.wifi;w.className=s.wifiok?'ok':'bad';let i=document.getElementById('is');i.textContent=s.is;i.className=s.isok?'ok':'bad';let rf=document.getElementById('rf');rf.textContent=s.busy?'BUSY':'CLEAR';rf.className=s.busy?'bad':'ok';document.getElementById('rx').textContent=s.rx;document.getElementById('counts').textContent=s.gated+' / '+s.digi;document.getElementById('is2rfcounts').textContent=s.is2rftx+' / '+s.is2rfdrop;document.getElementById('ip').textContent=s.ip;document.getElementById('audio').textContent=s.audio;document.getElementById('audiopeak').textContent=s.audiopeak;document.getElementById('rxgainnow').textContent=s.rxgain;document.getElementById('adcclips').textContent=s.adcclips;document.getElementById('adcraw').textContent=s.adcmin+' / '+s.adcmax;document.getElementById('heap').textContent=s.heap;document.getElementById('topstate').textContent=(s.wifiok?'WIFI ':'')+(s.isok?'IS OK':'LOCAL');document.getElementById('topstate').className='pill '+(s.isok?'ok':'');document.getElementById('lastdec').textContent=s.lastdec==1?'A / MicroModem':(s.lastdec==2?'B-RAW':(s.lastdec==3?'B-EQ125':(s.lastdec==4?'B-EQ130':'NONE')));document.getElementById('deca').textContent=s.aok+' / '+s.abadu;document.getElementById('decb').textContent=s.bok+' / '+s.bbadu;document.getElementById('braw').textContent=s.brawok+' / '+s.brawbad;document.getElementById('bd75').textContent=s.bd75ok+' / '+s.bd75bad;document.getElementById('bd220').textContent=s.bd220ok+' / '+s.bd220bad;document.getElementById('afc').textContent=s.aflags+' / '+s.acand;document.getElementById('bfc').textContent=s.bflags+' / '+s.bcand;document.getElementById('tones').textContent=s.mark+' / '+s.space;document.getElementById('conf').textContent=s.conf+'%';document.getElementById('trans').textContent=s.trans;document.getElementById('dualdup').textContent=s.dualdup;let cs=s.capready?'READY':(s.capactive?'CAPTURING':(s.caparmed?'ARMED':'IDLE'));document.getElementById('capstat').textContent=cs;document.getElementById('capsamples').textContent=s.capsamples+' samples';document.getElementById('caplink').style.display=s.capready?'inline-block':'none';let os=document.getElementById('oledstate');if(os){os.textContent=s.oledok?'READY':'OFF/NO OLED';os.className=s.oledok?'ok':'bad';document.getElementById('oledaddr').textContent=s.oledaddr;document.getElementById('wxsrc').textContent=s.wxsrc;document.getElementById('wxage').textContent=s.wxage;}}catch(e){document.getElementById('topstate').textContent='OFFLINE'}}"
         "async function logs(){try{document.getElementById('log').textContent=await (await fetch('/log.txt',{cache:'no-store'})).text()}catch(e){}}async function heard(){try{document.getElementById('heard').textContent=await (await fetch('/heard.txt',{cache:'no-store'})).text()}catch(e){}}"
         "async function cmd(u){try{let r=await fetch(u,{method:'POST'});toast(await r.text())}catch(e){toast('Command failed')}}function rebootNow(){if(confirm('Restart ESP32 sekarang?'))cmd('/reboot')}async function caparm(){try{let r=await fetch('/capturearm',{method:'POST'});toast(await r.text());stat()}catch(e){toast('Capture arm failed')}}async function diagreset(){try{let r=await fetch('/diagreset',{method:'POST'});toast(await r.text());stat()}catch(e){toast('Reset failed')}}"
         "try{let t=localStorage.getItem('nusaTab');if(t&&document.getElementById(t)){let b=document.querySelector('[data-tab=\"'+t+'\"]');tab(t,b)}}catch(e){}setInterval(stat,1500);setInterval(logs,2500);setInterval(heard,2500);stat();logs();heard();</script></body></html>");
  web.send(200, "text/html", h);
}

static void handleStatus() {
  String j; j.reserve(1450);
  bool w = WiFi.status() == WL_CONNECTED;
  bool i = aprsClient.connected();
  j = "{\"call\":\"" + jsonEscape(stationCall()) + "\",";
  j += "\"wifi\":\"" + String(w ? "CONNECTED" : (cfg.wifiSsid.length() ? "DISCONNECTED" : "NOT SET")) + "\",";
  j += "\"wifiok\":" + String(w ? "true" : "false") + ",";
  j += "\"is\":\"" + String(i ? "CONNECTED" : "DISCONNECTED") + "\",";
  j += "\"isok\":" + String(i ? "true" : "false") + ",";
  j += "\"busy\":" + String(AFSK_channelBusy() ? "true" : "false") + ",";
  j += "\"rx\":" + String(rfRxCount) + ",\"gated\":" + String(gatedCount) + ",\"digi\":" + String(digiTxCount) + ",";
  j += "\"is2rftx\":" + String(isToRfTxCount) + ",\"is2rfdrop\":" + String(isToRfDropCount) + ",";
  j += "\"ip\":\"" + String(w ? WiFi.localIP().toString() : "-") + "\",";
  j += "\"audio\":" + String(AFSK_getAudioLevel()) + ",\"audiopeak\":" + String(AFSK_getAudioPeak()) + ",";
  j += "\"rxgain\":" + String(AFSK_getAppliedRxGain()) + ",\"adcclips\":" + String(AFSK_getAdcClipSamples()) + ",";
  j += "\"adcmin\":" + String(AFSK_getAdcRawMin()) + ",\"adcmax\":" + String(AFSK_getAdcRawMax()) + ",";
  j += "\"aok\":" + String(AFSK_getGoodFrames()) + ",\"abadu\":" + String(AFSK_getBadCrcFrames()) + ",";
  j += "\"aflags\":" + String(AFSK_getPrimaryFlags()) + ",\"acand\":" + String(AFSK_getPrimaryCandidates()) + ",";
  j += "\"bok\":" + String(AFSK_getAltGoodFrames()) + ",\"bbadu\":" + String(AFSK_getAltBadCrcFrames()) + ",";
  j += "\"brawok\":" + String(AFSK_getAltBankGood(0)) + ",\"brawbad\":" + String(AFSK_getAltBankBad(0)) + ",";
  j += "\"bd75ok\":" + String(AFSK_getAltBankGood(1)) + ",\"bd75bad\":" + String(AFSK_getAltBankBad(1)) + ",";
  j += "\"bd220ok\":" + String(AFSK_getAltBankGood(2)) + ",\"bd220bad\":" + String(AFSK_getAltBankBad(2)) + ",";
  j += "\"bflags\":" + String(AFSK_getAltFlags()) + ",\"bcand\":" + String(AFSK_getAltCandidates()) + ",";
  j += "\"mark\":" + String(AFSK_getAltMarkScore()) + ",\"space\":" + String(AFSK_getAltSpaceScore()) + ",";
  j += "\"conf\":" + String(AFSK_getAltConfidence()) + ",\"trans\":" + String(AFSK_getAltTransitions()) + ",";
  j += "\"dualdup\":" + String(AFSK_getDualDuplicates()) + ",\"lastdec\":" + String(AFSK_getLastFrameDecoder()) + ",";
  j += "\"caparmed\":" + String(AFSK_captureArmed() ? "true" : "false") + ",";
  j += "\"capactive\":" + String(AFSK_captureActive() ? "true" : "false") + ",";
  j += "\"capready\":" + String(AFSK_captureReady() ? "true" : "false") + ",";
  j += "\"capsamples\":" + String((uint32_t)AFSK_getCaptureCount()) + ",";
  j += "\"oledok\":" + String(oled.ready() ? "true" : "false") + ",";
  String oa = oled.ready() ? (String("0x") + String(oled.address(), HEX)) : String("-");
  j += "\"oledaddr\":\"" + oa + "\",";
  j += "\"wxsrc\":\"" + jsonEscape(lastWeather.valid ? String(lastWeather.source) : String("-")) + "\",";
  j += "\"wxage\":\"" + jsonEscape(lastWeather.valid ? weatherAgeText() : String("belum ada weather RF")) + "\",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + "}";
  web.send(200, "application/json", j);
}

static void handleLog() {
  String out; out.reserve(7000);
  uint8_t start = (uint8_t)((logHead + LOG_LINES - logUsed) % LOG_LINES);
  for (uint8_t i = 0; i < logUsed; ++i) {
    out += logBuf[(start + i) % LOG_LINES];
    out += '\n';
  }
  web.send(200, "text/plain", out);
}

static void handleHeard() {
  LocalIsRfSpec spec = parseLocalIsRfFilter(cfg.localIsRfFilter);
  String out; out.reserve(6000);
  out += "Local filter: " + cfg.localIsRfFilter + "\n";
  out += "Meaning     : APRS messages only; target heard <= " + String(spec.valid ? spec.minutes : 0) +
         " min; max used digi hops = " + String(spec.valid ? spec.maxHops : 0) + "\n\n";
  out += "CALLSIGN    LAST-HOP  LAST-RF    DIRECT-AGE   STATUS\n";
  out += "-------------------------------------------------------\n";

  uint32_t now = millis();
  bool any = false;
  for (uint8_t i = 0; i < HEARD_SLOTS; ++i) {
    if (!rfHeard[i].used) continue;
    uint32_t newest = newestHeardMs(rfHeard[i]);
    if (!newest) continue;
    uint32_t ageNewest = now - newest;
    if (ageNewest > 60UL * 60UL * 1000UL) continue; // hide stale entries older than 1 hour

    uint8_t lastHop = 255;
    uint32_t lastAge = 0xFFFFFFFFUL;
    for (uint8_t h = 0; h <= HEARD_HOPS_MAX; ++h) {
      uint32_t t = rfHeard[i].lastByHops[h];
      if (!t) continue;
      uint32_t age = now - t;
      if (age < lastAge) { lastAge = age; lastHop = h; }
    }

    uint32_t eligibleAge = 0;
    bool eligible = spec.valid && rfHeardWithin(String(rfHeard[i].call), spec.minutes, spec.maxHops, &eligibleAge);
    uint32_t directAge = rfHeard[i].lastByHops[0] ? (now - rfHeard[i].lastByHops[0]) : 0xFFFFFFFFUL;

    String call = String(rfHeard[i].call);
    while (call.length() < 12) call += ' ';
    out += call;
    out += (lastHop == 255 ? String("-") : String(lastHop));
    out += "         ";
    out += String(lastAge / 60000UL) + "m" + String((lastAge / 1000UL) % 60UL) + "s";
    out += "     ";
    if (directAge == 0xFFFFFFFFUL) out += "never";
    else out += String(directAge / 60000UL) + "m" + String((directAge / 1000UL) % 60UL) + "s";
    out += eligible ? "       ELIGIBLE\n" : "       BLOCKED\n";
    any = true;
  }
  if (!any) out += "(belum ada station RF yang terdengar)\n";
  web.send(200, "text/plain", out);
}

static void putLe16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFFU); p[1] = (uint8_t)(v >> 8); }
static void putLe32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

static void handleRxCaptureWav() {
  if (!AFSK_captureReady() || !AFSK_getCaptureData() || AFSK_getCaptureCount() == 0) {
    web.send(404, "text/plain", "RX capture not ready. ARM it, then transmit one UV-5RH beacon.");
    return;
  }
  const size_t samples = AFSK_getCaptureCount();
  const uint32_t dataBytes = (uint32_t)(samples * sizeof(int16_t));
  uint8_t wh[44]; memset(wh, 0, sizeof(wh));
  memcpy(wh + 0, "RIFF", 4); putLe32(wh + 4, 36U + dataBytes);
  memcpy(wh + 8, "WAVEfmt ", 8); putLe32(wh + 16, 16U); putLe16(wh + 20, 1U);
  putLe16(wh + 22, 1U); putLe32(wh + 24, 9600U); putLe32(wh + 28, 19200U);
  putLe16(wh + 32, 2U); putLe16(wh + 34, 16U); memcpy(wh + 36, "data", 4); putLe32(wh + 40, dataBytes);
  web.sendHeader("Content-Disposition", "attachment; filename=UV5RH_TAIT_GPIO36_9600.wav");
  web.setContentLength(44U + dataBytes);
  web.send(200, "audio/wav", "");
  WiFiClient c = web.client();
  c.write(wh, sizeof(wh));
  const uint8_t *raw = (const uint8_t *)AFSK_getCaptureData();
  size_t left = dataBytes;
  while (left && c.connected()) {
    size_t n = left > 1024U ? 1024U : left;
    c.write(raw, n); raw += n; left -= n; delay(0);
  }
}

static void setupWeb() {
  web.on("/", HTTP_GET, handleRoot);
  web.on("/status.json", HTTP_GET, handleStatus);
  web.on("/log.txt", HTTP_GET, handleLog);
  web.on("/heard.txt", HTTP_GET, handleHeard);
  web.on("/rxcapture.wav", HTTP_GET, handleRxCaptureWav);
  web.on("/capturearm", HTTP_POST, [](){ bool ok = AFSK_armRxCapture(); web.send(ok ? 200 : 500, "text/plain", ok ? "RX capture ARMED. Transmit ONE UV-5RH beacon now." : "RX capture allocation failed"); });
  web.on("/diagreset", HTTP_POST, [](){ AFSK_resetDiagnostics(); web.send(200, "text/plain", "RX decoder diagnostics reset"); });
  web.on("/save", HTTP_POST, [](){
    saveConfigFromWeb();
    web.send(200, "text/html", "<html><body style='font-family:Arial;background:#101827;color:white;padding:30px'><h2>Configuration saved.</h2><p>ESP32 is restarting...</p></body></html>");
    delay(700);
    ESP.restart();
  });
  web.on("/beaconrf", HTTP_POST, [](){ sendRfBeacon(); web.send(200, "text/plain", "RF beacon queued"); });
  web.on("/beaconis", HTTP_POST, [](){ sendIsBeacon(); web.send(200, "text/plain", aprsClient.connected() ? "APRS-IS beacon sent" : "APRS-IS not connected"); });
  web.on("/reboot", HTTP_POST, [](){ web.send(200, "text/plain", "Restarting"); delay(300); ESP.restart(); });
  web.onNotFound([](){ web.send(404, "text/plain", "Not found"); });
  web.begin();
  addLog("Web server started on port 80");
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("====================================================");
  Serial.println(" NUSA APRS IGATE + DIGIPEATER REV1S RF REMOTE ADMIN");
  Serial.println("====================================================");

  randomSeed(esp_random());
  loadConfig();
  captureRfAdminSavedConfig();

  // Configure dynamic pins first, then initialize the modem.
  AFSK_setPtt(cfg.pttPin, cfg.pttActiveHigh);
  AFSK_setLeds(cfg.rxLedPin, cfg.txLedPin);
  AFSK_init(&afsk);
  applyModemConfig();
  ax25_init(&ax25, onAx25Frame);

  initOled();
  startWifi();
  setupWeb();
  lastBeaconMs = millis();
  addLog("Station=" + stationCall() + "  iGate=" + String(cfg.igateEnabled ? "ON" : "OFF") + "  Digi=" + String(cfg.digiEnabled ? "ON" : "OFF"));
  addLog("Local IS>RF=" + String(cfg.isToRfEnabled ? "ON " : "OFF ") + cfg.localIsRfFilter + "  path=" + (cfg.isToRfPath.length() ? cfg.isToRfPath : String("DIRECT")));
  addLog("RF Remote=" + String(cfg.rfAdminEnabled ? "ON" : "OFF") + " Admin1=" + (cfg.rfAdmin1.length() ? cfg.rfAdmin1 : String("-")) + " Admin2=" + (cfg.rfAdmin2.length() ? cfg.rfAdmin2 : String("-")) + " Reply=" + (cfg.rfAdminReplyPath.length() ? cfg.rfAdminReplyPath : String("DIRECT")));
  addLog("Radio pins: RX=GPIO36 TX-DAC=GPIO26 PTT=GPIO" + String(cfg.pttPin));
  addLog("REV1S RF REMOTE: REV1R engine frozen; A=MicroModem; B=RAW+EQ125+EQ130 UV5RH-tuned");
}

void loop() {
  web.handleClient();
  serviceWifi();
  serviceAprsIs();
  serviceOled();

  // AX25 poll consumes validated RF frames produced by the AFSK/HDLC task.
  ax25_poll(&ax25);
  serviceRfTx();
  serviceRfAdmin();

  uint32_t now = millis();
  if ((uint32_t)(now - lastBeaconMs) >= cfg.beaconSec * 1000UL) {
    lastBeaconMs = now;
    if (cfg.rfBeaconEnabled) sendRfBeacon();
    if (cfg.isBeaconEnabled) sendIsBeacon();
  }
  delay(1);
}
