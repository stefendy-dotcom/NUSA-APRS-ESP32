# NUSA APRS ESP32 REV1T — English

[![Total Firmware Downloads](https://img.shields.io/github/downloads/stefendy-dotcom/NUSA-APRS-ESP32/total?label=Firmware%20Downloads&color=blue)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases)

📦 [Download REV1T firmware (4 BIN ZIP)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases/download/REV1T/NUSA_APRS_ESP32_REV1T_FLASH.zip)

Firmware for a **classic ESP32 WROOM/DevKit** interfacing an analog VHF radio with APRS RF/APRS-IS and now providing a **GPS APRS Tracker**.

REV1T keeps the established REV1S RX/iGate/digipeater engine unchanged and adds a modular, non-blocking UART NMEA tracker. The tracker defaults to **OFF**, so an upgrade from REV1S remains operational even with no GPS connected.

## Main functions

- APRS RF → APRS-IS iGate.
- APRS RF digipeater.
- Local APRS-IS → RF message gate.
- Legacy REV1S RF/IS site beacon.
- New GPS APRS Tracker.
- NMEA RMC/GGA support for NEO-6M, NEO-M8N, NEO-M10N and compatible GPS receivers.
- STATIC / GPS / AUTO position modes.
- RF / APRS-IS / RF+IS tracker output.
- Fixed Interval and SmartBeaconing.
- Corner pegging based on heading change.
- GPS stale protection and fix validation.
- Live GPS/tracker Web UI.
- GPS OLED page.
- Tracker commands through RF Remote Admin.
- RX WAV diagnostics, RF weather and RF/IS last-heard lists.

## Main pins

- RX audio / ADC: GPIO36 / VP
- TX AFSK DAC: GPIO26
- PTT: GPIO25
- RX LED: GPIO2
- TX LED: GPIO4
- OLED SDA/SCL: GPIO21/GPIO22
- Default GPS UART RX: GPIO16
- Default GPS UART TX: GPIO17

GPS GPIOs are configurable in the Web UI. The GPS UART is not started if the selected pins conflict with GPIO36, GPIO26, active PTT/LED pins or active OLED pins.

## Default GPS wiring

- GPS TX → ESP32 GPIO16
- GPS RX → ESP32 GPIO17 when required
- GPS GND → ESP32 GND
- GPS VCC → the voltage required by the GPS module

When the ESP32 only needs to receive NMEA, its GPS TX GPIO can be set to `-1`.

**Do not assume every GPS carrier board accepts 5 V.** Check the actual module/carrier specification.

Selectable GPS baud rates: **9600 / 38400 / 115200**.

## Tracker defaults

- Tracker: OFF
- Position Mode: AUTO
- Tracker Output: RF
- Tracker RF Path: WIDE2-1
- Symbol: `/>` (car)
- Fixed Interval: 120 s
- GPS Stale Timeout: 15 s
- Minimum Tracker TX Interval: 15 s
- Slow Speed / Rate: 5 km/h / 600 s
- Fast Speed / Rate: 80 km/h / 60 s
- Turn Angle: 30°
- Turn Slope: 255
- Turn Time: 15 s
- Stationary Speed: 3 km/h

## Position modes

**STATIC** uses the manually configured site latitude/longitude.

**GPS** transmits tracker beacons only while a fresh GPS fix exists.

**AUTO** uses GPS when valid and falls back to the configured static position only when that static position is actually configured. The untouched 0,0 default is not transmitted as a fallback.

## SmartBeaconing

At low speed the tracker uses the Slow Rate. At high speed it uses the Fast Rate. Between those thresholds the interval is speed-dependent.

Corner pegging uses heading change with a dynamic threshold. All automatic tracker beacons remain subject to the Minimum Tracker TX Interval, and every RF tracker transmission still goes through the REV1S TX queue and CSMA logic.

## RF Remote Admin tracker commands

REV1T adds:

- `TRK ON` / `TRK OFF`
- `GPS`
- `TMODE FIX`
- `TMODE SMART`
- `TSEC 120`
- `TPATH WIDE2-1`
- `TPATH DIRECT`
- `TOUT RF`
- `TOUT IS`
- `TOUT BOTH`
- `TBCN`

See [docs/RF_REMOTE_ADMIN_EN.md](docs/RF_REMOTE_ADMIN_EN.md).

## Documentation

- [User guide](docs/USER_GUIDE_EN.md)
- [GPS Tracker guide](docs/GPS_TRACKER_EN.md)
- [RF Remote Admin](docs/RF_REMOTE_ADMIN_EN.md)
- [Build & Flash](docs/BUILD_FLASH.md)
- [Validation](docs/VALIDATION.md)

## Build

Target: **classic ESP32 Dev Module / WROOM-32 / 4 MB flash / Arduino-ESP32 Core 3.3.11**.

Validated build:

- application flash: **1,139,979 bytes (86%)**
- global RAM: **77,184 bytes (23%)**

REV1T retains the REV1S AFSK/AX.25/decoder files byte-for-byte.
