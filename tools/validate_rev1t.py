#!/usr/bin/env python3
from pathlib import Path
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]
REV1S = ROOT / "source" / "NUSA_APRS_IGATE_DIGI_REV1S_RF_REMOTE_ADMIN"
REV1T = ROOT / "source" / "NUSA_APRS_IGATE_DIGI_REV1T_GPS_TRACKER"
INO = (REV1T / "NUSA_APRS_IGATE_DIGI_REV1T_GPS_TRACKER.ino").read_text()
GPSH = (REV1T / "NusaGpsTracker.h").read_text()
GPSC = (REV1T / "NusaGpsTracker.cpp").read_text()
TYPES = (REV1T / "NusaAppTypes.h").read_text()

checks = []

def check(name, ok):
    checks.append((name, bool(ok)))

def contains(name, needle, hay=INO):
    check(name, needle in hay)

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

# Frozen engine: exact byte identity.
for name in ["AFSK.cpp","AFSK.h","AX25.cpp","AX25.h","CRC-CCIT.cpp","CRC-CCIT.h","FIFO.h","HDLC.h"]:
    check("frozen engine " + name, sha(REV1S/name) == sha(REV1T/name))

# Core REV1T identity/defaults.
contains("REV1T version string", 'FW_VER  = "REV1T GPS TRACKER"')
contains("TOCALL default APZUAG", 'String dest = "APZUAG"')
contains("tracker default OFF", "bool trackerEnabled = false")
contains("auto mode default ON", "bool autoModeSwitchEnabled = true")
contains("default tracker path WIDE2-1", 'String trackerRfPath = "WIDE2-1"')
contains("default GPS RX GPIO16", "int gpsRxPin = 16")
contains("default GPS TX GPIO17", "int gpsTxPin = 17")
contains("default GPS baud 9600", "uint32_t gpsBaud = 9600")
contains("default stale timeout 15s", "uint16_t gpsStaleSec = 15")
contains("default min tracker TX 15s", "uint16_t trackerMinTxSec = 15")

# GPS parser and bounded service.
contains("GPS RMC parser", 'isSentenceType(fields[0], "RMC")', GPSC)
contains("GPS GGA parser", 'isSentenceType(fields[0], "GGA")', GPSC)
contains("GPS checksum validation", "checksumOk", GPSC)
contains("GPS kmh conversion", "* 1.852", GPSC)
contains("GPS fix requires active stream", "|| !connected()) return false", GPSC)
contains("GPS service byte budget parameter", "maxBytesPerLoop", GPSH + GPSC)
contains("main loop GPS budget 96", "gps.service(96)")
check("no blocking wait for GPS fix", "while (!gps" not in INO and "while(!gps" not in INO)

# GPS state protection.
contains("NO GPS state", '"NO GPS"', GPSC)
contains("GPS SEARCH state", '"GPS SEARCH"', GPSC)
contains("GPS FIX state", '"GPS FIX"', GPSC)
contains("GPS STALE state", '"GPS STALE"', GPSC)
contains("static untouched 0,0 rejected", 'fabs(cfg.lat) > 0.000001 || fabs(cfg.lon) > 0.000001')
contains("GPS GPIO36 conflict protection", "pin == 36")
contains("GPS GPIO26 conflict protection", "pin == 26")

# Position/output modes and tracker queue.
contains("STATIC position mode", "TRACK_POS_STATIC")
contains("GPS position mode", "TRACK_POS_GPS")
contains("AUTO position mode", "TRACK_POS_AUTO")
contains("RF tracker output", "TRACK_OUT_RF")
contains("IS tracker output", "TRACK_OUT_IS")
contains("BOTH tracker output", "TRACK_OUT_BOTH")
check("TX_TRACKER enum present", "TX_TRACKER = 5" in TYPES)
contains("tracker RF uses queue", "queueRf(raw, len, TX_TRACKER, 0)")
check("GPS parser never directly transmits AFSK", "AFSK_transmit" not in GPSC)
contains("tracker RF path independent", "cfg.trackerRfPath")
contains("tracker comment independent", "cfg.trackerComment")

# Beacon formats and SmartBeacon.
contains("course speed APRS format", '"%03d/%03d"')
contains("altitude APRS A=", '"/A=%06ld"')
contains("fixed interval scheduler", "elapsedSec >= cfg.trackerFixedSec")
contains("SmartBeacon interval function", "smartBeaconIntervalSec")
contains("stationary slow rate", "speedKmh <= (float)cfg.stationarySpeedKmh")
contains("turn threshold angle", "cfg.smartTurnAngleDeg")
contains("turn slope", "cfg.smartTurnSlope / mph")
contains("turn minimum time", "elapsedSec >= cfg.smartTurnTimeSec")
contains("minimum auto TX interval", "cfg.trackerMinTxSec")

# Auto Mode Switch.
contains("auto network end-to-end helper", "autoNetworkAvailable")
contains("auto requires WiFi connected", "WiFi.status() == WL_CONNECTED && aprsClient.connected()")
contains("effective iGate helper", "effectiveIgateEnabled")
contains("effective Digi helper", "effectiveDigiEnabled")
contains("online makes iGate effective", "cfg.autoModeSwitchEnabled ? autoNetworkAvailable() : cfg.igateEnabled")
contains("offline makes Digi effective", "cfg.autoModeSwitchEnabled ? !autoNetworkAvailable() : cfg.digiEnabled")
contains("gate path uses effective iGate", "!effectiveIgateEnabled()")
contains("digi path uses effective Digi", "!effectiveDigiEnabled()")
contains("auto mode service in main loop", "serviceAutoModeSwitch();")
contains("APRS-IS reconnect remains requested in auto", "cfg.autoModeSwitchEnabled || cfg.igateEnabled")
contains("auto mode NVS key", '"automode"')
contains("auto mode Web field", 'name=\'automode\'')
contains("auto mode live status JSON", '\\"mode\\":\\"')
contains("manual DIGI blocked in auto", 'reply = "ERR AUTO"')
contains("AMODE remote command", 'op == "AMODE" || op == "AUTO"')

# Remote tracker commands.
for op in ["TRK","GPS","TMODE","TSEC","TPATH","TOUT","TBCN"]:
    contains("RF Remote " + op, 'op == "' + op + '"')

# NVS tracker keys.
for key in ["trken","posmode","gpsrx","gpstx","gpsbaud","gpsstale","trout","trpath","trsymt","trsym","trcomm","trmode","trsec","sslow","srslow","sfast","srfast","sturn","sslope","sturnt","sstat","trmin"]:
    contains("NVS key " + key, '"' + key + '"')

# Web/OLED live features.
for field in ["gpsstate","gpssats","gpspos","gpsmove","gpsalt","trlast","trnext","trcounts","gpscounts"]:
    contains("Web live field " + field, "id='" + field + "'")
contains("manual tracker web endpoint", 'web.on("/trackerbeacon"')
contains("OLED GPS page", "drawOledGps")
contains("OLED page rotation includes GPS", "oledPage == 6")

# No new explicit RTOS/synchronization constructs in application/GPS layer.
app_text = INO + GPSH + GPSC
for token in ["xTaskCreate(", "xTaskCreatePinnedToCore(", "xSemaphore", "SemaphoreHandle", "portMUX", "mutex"]:
    check("no new app RTOS token " + token, token not in app_text)

# Four binary package and offsets.
bin_dir = ROOT / "firmware" / "bin"
for name in ["bootloader.bin","partitions.bin","boot_app0.bin","firmware.bin","FLASH_OFFSETS.txt","SHA256SUMS.txt"]:
    check("package file " + name, (bin_dir/name).exists())
offs = (bin_dir/"FLASH_OFFSETS.txt").read_text()
for value, name in [("0x1000","bootloader.bin"),("0x8000","partitions.bin"),("0xE000","boot_app0.bin"),("0x10000","firmware.bin")]:
    check("offset " + value + " " + name, value in offs and name in offs)

# Deterministic algorithm references.
def smart_interval(speed, stationary=3, slow_speed=5, slow_rate=600, fast_speed=80, fast_rate=60):
    if speed <= stationary or speed <= slow_speed:
        return slow_rate
    if speed >= fast_speed:
        return fast_rate
    sec = round(fast_rate * fast_speed / speed)
    return max(fast_rate, min(slow_rate, sec))

check("SmartBeacon stationary reference", smart_interval(0) == 600)
check("SmartBeacon slow reference", smart_interval(5) == 600)
check("SmartBeacon mid reference", smart_interval(40) == 120)
check("SmartBeacon fast reference", smart_interval(100) == 60)

def heading_delta(a,b):
    d=abs(a-b)
    while d >= 360:
        d -= 360
    return 360-d if d > 180 else d
check("heading wrap 350->10", heading_delta(350,10) == 20)
check("heading wrap 10->350", heading_delta(10,350) == 20)

failed=[name for name,ok in checks if not ok]
for name,ok in checks:
    print(("PASS " if ok else "FAIL ") + name)
print()
print(f"RESULT {len(checks)-len(failed)}/{len(checks)} PASS")
if failed:
    print("FAILED:")
    for x in failed:
        print(" -",x)
    sys.exit(1)

[executed on device: stefendy (0b0a0c21-e827-4729-9ace-14c4d03fbbf8)]