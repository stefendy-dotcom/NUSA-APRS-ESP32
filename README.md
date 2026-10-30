# NUSA APRS ESP32 — iGate + Digipeater + GPS Tracker REV1T

[![Total Firmware Downloads](https://img.shields.io/github/downloads/stefendy-dotcom/NUSA-APRS-ESP32/total?label=Firmware%20Downloads&color=blue)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases)

📦 [Download REV1T firmware (4 BIN ZIP)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases/download/REV1T/NUSA_APRS_ESP32_REV1T_FLASH.zip)

*The counter tracks downloads of GitHub Release assets only; source-code archives downloaded through **Code → Download ZIP** are not included.*

**Classic ESP32 WROOM / DevKit + external VHF radio + UART NMEA GPS — Bell 202 / AX.25 APRS 1200 baud**

🇮🇩 [Bahasa Indonesia](README_ID.md) · 🇬🇧 [English](README_EN.md)

REV1T keeps the proven REV1S RF receive/iGate/digipeater engine frozen and adds a modular, non-blocking GPS APRS tracker. The tracker is disabled by default, so an upgrade from REV1S continues to operate normally even with no GPS connected.

## Highlights

- Bell 202 1200-baud AFSK RX/TX, AX.25/FCS.
- Frozen REV1S receive engine: Decoder A (MicroModem lineage), B-RAW, B-EQ125 and B-EQ130.
- RF → APRS-IS iGate and WIDE digipeater.
- Local APRS-IS → RF message gating (`i/30/0` default).
- GPS tracker with NMEA RMC/GGA parsing for NEO-6M, NEO-M8N, NEO-M10N and compatible receivers.
- Position modes: STATIC, GPS and AUTO (GPS with static fallback).
- Tracker outputs: RF only, APRS-IS only or RF + APRS-IS.
- Fixed interval beaconing and SmartBeaconing with speed-based rate and corner pegging.
- Tracker RF path independent from normal beacon, IS→RF and RF Remote Admin reply paths.
- Standard APRS uncompressed position with optional course/speed and altitude.
- GPS state protection: NO GPS, GPS SEARCH, GPS FIX and GPS STALE; invalid/0,0 fallback is not transmitted.
- RF Remote Admin adds `TRK`, `GPS`, `TMODE`, `TSEC`, `TPATH`, `TOUT` and `TBCN`.
- Web UI live GPS/tracker status and manual tracker beacon.
- OLED GPS Tracker page.
- Wi-Fi AP/STA configuration, live monitor, RF weather, last-heard RF/IS lists and RX WAV diagnostics.

## Default pinout

| Function | ESP32 |
|---|---:|
| RX audio / ADC | GPIO36 / VP |
| TX AFSK DAC | GPIO26 |
| PTT control | GPIO25 |
| RX LED | GPIO2 |
| TX LED | GPIO4 |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| GPS UART RX | GPIO16 |
| GPS UART TX | GPIO17 |

GPS pins are configurable from the Web UI. The firmware refuses to start the GPS UART when the configured GPS pins conflict with active APRS RX/TX, PTT/LED or OLED pins.

## GPS wiring

Default UART wiring:

- GPS TX → ESP32 GPIO16 (GPS RX setting)
- GPS RX → ESP32 GPIO17 (optional; set GPS TX GPIO to `-1` when unused)
- GPS GND → ESP32 GND
- GPS VCC → the voltage required by the actual GPS module/board

Do not assume every GPS board accepts 5 V. Check the module carrier specifications.

Supported selectable baud rates: **9600, 38400 and 115200**.

## Tracker defaults

- Tracker: OFF
- Position mode: AUTO
- Output: RF
- RF path: WIDE2-1
- Symbol: `/>` (car)
- Fixed interval: 120 s
- GPS stale timeout: 15 s
- Minimum automatic tracker TX interval: 15 s
- SmartBeacon slow: 5 km/h / 600 s
- SmartBeacon fast: 80 km/h / 60 s
- Turn angle: 30°
- Turn slope: 255
- Turn time: 15 s
- Stationary threshold: 3 km/h

The legacy REV1S RF/IS site beacon remains available separately from the new GPS tracker.

## RF interface schematic

![NUSA APRS ESP32 RF interface schematic](hardware/skema.jpeg)

The RF interface remains unchanged from REV1S:

- RX: J1 SPK/audio → VR1 10 kΩ → C1 1 µF → R1 1 kΩ → GPIO36/VP.
- RX ADC bias: R2 100 kΩ to 3V3 and R3 100 kΩ to GND; C2 2.2 nF to GND.
- PTT: GPIO25 → R4 1 kΩ → Q1 2N3904 with R5 10 kΩ pulldown.
- TX: GPIO26 → R6 1 kΩ → C3 1 µF → VR2 10 kΩ → radio MIC/audio TX.

## Four BIN flash package

| Offset | File |
|---:|---|
| 0x1000 | bootloader.bin |
| 0x8000 | partitions.bin |
| 0xE000 | boot_app0.bin |
| 0x10000 | firmware.bin |

Target: **ESP32 Dev Module / 4 MB flash / Arduino-ESP32 Core 3.3.11**.

See [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md).

## Documentation

- Indonesian: [User guide](docs/USER_GUIDE_ID.md) · [GPS Tracker](docs/GPS_TRACKER_ID.md) · [Adjustment](docs/ADJUSTMENT_ID.md) · [RF Remote Admin](docs/RF_REMOTE_ADMIN_ID.md)
- English: [User guide](docs/USER_GUIDE_EN.md) · [GPS Tracker](docs/GPS_TRACKER_EN.md) · [Adjustment](docs/ADJUSTMENT_EN.md) · [RF Remote Admin](docs/RF_REMOTE_ADMIN_EN.md)
- Hardware: [RF schematic](hardware/skema.jpeg) · [RF interface BOM](hardware/BOM.md)
- Source: [REV1T sketch folder](source/NUSA_APRS_IGATE_DIGI_REV1T_GPS_TRACKER/)
- Validation: [docs/VALIDATION.md](docs/VALIDATION.md)

## Build validation

REV1T compiles successfully with Arduino CLI 1.5.1 and **Arduino-ESP32 Core 3.3.11** for `esp32:esp32:esp32`.

Verified local build:

- application flash: **1,139,979 bytes (86%)**
- global RAM: **77,184 bytes (23%)**
- free for local variables in the build report: **250,496 bytes**

The AFSK/AX.25/CRC/FIFO/HDLC files in REV1T are byte-identical to REV1S. REV1T adds no new application FreeRTOS task, mutex or semaphore; the existing low-level AFSK implementation is retained unchanged.

> RF Remote Admin uses exact callsign+SSID whitelisting, not cryptographic authentication. APRS source callsigns can be spoofed.
