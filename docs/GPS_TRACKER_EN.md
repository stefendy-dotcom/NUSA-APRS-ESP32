# REV1T GPS Tracker — English

REV1T adds an APRS GPS Tracker above the stable REV1S engine without changing the established AFSK/AX.25 decoder files.

## GPS receivers

Minimum supported devices include:

- u-blox NEO-6M
- u-blox NEO-M8N
- u-blox NEO-M10N
- other receivers emitting standard NMEA RMC/GGA

Selectable baud: 9600, 38400, 115200.

## Default wiring

| GPS | ESP32 |
|---|---|
| TX | GPIO16 / GPS RX |
| RX | GPIO17 / GPS TX, optional |
| GND | GND |
| VCC | According to the module specification |

ESP32 GPS TX can be set to `-1` when unused.

Do not assign GPS to GPIO36, GPIO26, active PTT/LED pins or active OLED pins. The Web UI warns about the conflict and the GPS UART is not started.

## Parsed data

RMC/GGA supplies:

- valid fix
- latitude/longitude
- speed
- course
- altitude
- satellite count
- HDOP
- UTC time

The parser consumes at most 96 UART bytes per main-loop iteration to avoid blocking APRS work.

## GPS states

`NO GPS` — no recent NMEA stream.

`GPS SEARCH` — NMEA is present but there is no fresh valid fix.

`GPS FIX` — a valid fresh fix exists.

`GPS STALE` — NMEA is present but the last valid fix is too old.

Default stale timeout: 15 seconds.

## Position Mode

### STATIC

Use the manually configured Station coordinates.

### GPS

Use GPS only. No fresh fix means no tracker beacon.

### AUTO

Use GPS when valid; otherwise use a configured static position. The untouched 0,0 default is not used as fallback.

## APRS packet format

REV1T transmits standard uncompressed APRS position. While moving with a valid course, `ccc/sss` course/speed is appended. Valid GPS altitude is appended as `/A=xxxxxx` in feet according to APRS convention.

The Destination/ToCall follows station configuration and defaults to `APZUAG`.

## Fixed Interval

Range: 30–3600 seconds.

Default: 120 seconds.

## SmartBeacon

Defaults:

| Parameter | Value |
|---|---:|
| Slow Speed | 5 km/h |
| Slow Rate | 600 s |
| Fast Speed | 80 km/h |
| Fast Rate | 60 s |
| Turn Angle | 30° |
| Turn Slope | 255 |
| Turn Time | 15 s |
| Stationary Speed | 3 km/h |
| Minimum TX Interval | 15 s |

Between Slow and Fast speed, interval is speed-dependent. Corner pegging uses heading change and `Turn Angle + Turn Slope / speed(mph)`.

## Output

Tracker output can be:

- RF
- APRS-IS
- RF + APRS-IS

RF always uses the REV1S TX queue and CSMA. APRS-IS does not wait for RF.

## RF Remote Admin

- `TRK ON/OFF`
- `GPS`
- `TMODE FIX/SMART`
- `TSEC 120`
- `TPATH WIDE2-1`
- `TPATH DIRECT`
- `TOUT RF/IS/BOTH`
- `TBCN`

Use `SAVE` after remote configuration changes that should survive reboot.

## REV1S upgrade

The tracker defaults to OFF. Existing REV1S NVS keys are preserved and there is no automatic factory reset.

The REV1S AFSK/AX.25 engine files are copied byte-for-byte into the REV1T source folder.
