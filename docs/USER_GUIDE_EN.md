# REV1T User Guide — English

## Power and Web UI

Feed ESP32 VIN from a stable 5 V source. After boot, join **NUSA-APRS-IGATE** and open `http://192.168.4.1`.

REV1T adds a **GPS Tracker** tab. The tracker defaults to OFF, so the unit continues to work like REV1S when no GPS is installed.

## Station and beacon

Configure a legal callsign/SSID, site latitude/longitude, APRS symbol, comment, site beacon interval and RF path.

The REV1S site beacon remains separate from the new GPS tracker. Site latitude/longitude is also used by STATIC/AUTO fallback and as the reference for OLED RF/IS bearing and distance.

## GPS Tracker

### Default wiring

- GPS TX → ESP32 GPIO16
- GPS RX → ESP32 GPIO17 when required
- GND → GND
- VCC → the voltage required by the actual GPS module

If the ESP32 does not need to transmit to the GPS, set **GPS TX GPIO = -1**.

Do not select GPS pins that conflict with GPIO36, GPIO26, PTT, LEDs or active OLED pins. The Web UI shows **GPIO CONFLICT** and the GPS UART is not started when the configuration is unsafe.

### GPS baud

Selectable values:

- 9600
- 38400
- 115200

A factory NEO-6M commonly uses 9600 baud; other modules may differ.

### GPS states

- **NO GPS**: no recent NMEA stream.
- **GPS SEARCH**: NMEA is present but no valid fix is available.
- **GPS FIX**: a fresh valid fix exists.
- **GPS STALE**: NMEA is still present, but the last valid fix exceeded the configured stale timeout.

REV1T parses RMC and GGA for latitude, longitude, speed, course, altitude, satellites, HDOP and UTC time.

### Position Mode

**STATIC** — use the manually configured site coordinates.

**GPS** — GPS only. No fresh fix means no tracker position transmission.

**AUTO** — use GPS while fresh and fall back to the static site position only if that static position is actually configured. The untouched 0,0 default is not transmitted.

### Tracker Output

- **RF ONLY**
- **APRS-IS ONLY**
- **RF + APRS-IS**

APRS-IS transmission does not wait for RF. RF tracker frames still use the REV1S TX queue and CSMA logic.

### Tracker RF Path

Default: `WIDE2-1`.

`DIRECT` or blank means no digipeater path.

The tracker path is independent from the normal site beacon, IS→RF and RF Remote Admin reply paths.

### Fixed Interval

Choose **FIXED INTERVAL** and set 30–3600 seconds.

The Minimum Tracker TX Interval still protects automatic beaconing from becoming too frequent.

### SmartBeaconing

Choose **SMARTBEACON**.

Defaults:

- Slow Speed: 5 km/h
- Slow Rate: 600 s
- Fast Speed: 80 km/h
- Fast Rate: 60 s
- Turn Angle: 30°
- Turn Slope: 255
- Turn Time: 15 s
- Stationary Speed: 3 km/h
- Minimum Tracker TX Interval: 15 s

Low speed uses the Slow Rate, high speed uses the Fast Rate, and speeds in between use a speed-dependent interval.

Corner pegging can trigger an earlier beacon when the heading change exceeds the dynamic threshold. The minimum TX interval and RF CSMA still apply.

### Manual Tracker Beacon

Press **SEND TRACKER BEACON**.

A manual beacon may bypass the minimum tracker interval, but an RF frame still enters the TX queue and remains subject to CSMA/channel busy.

In GPS mode, no fresh fix means the manual tracker beacon is rejected. AUTO may use the static fallback if valid.

## Wi-Fi and APRS-IS

Default server: `asia.aprs2.net:14580`.

The local `i/30/0` rule is an ESP32 IS→RF message-gating rule, not an APRS-IS server filter.

## iGate and digipeater

Enable iGate for valid RF → APRS-IS forwarding.

Enable Digi when the device should repeat APRS RF frames.

Defaults:

- aliases: WIDE1,WIDE2
- Digi delay: 150 ms
- duplicate suppression: 30 s

The GPS tracker operates independently from Digi, iGate and IS→RF enable states.

## Radio / AFSK

Use [hardware/skema.jpeg](../hardware/skema.jpeg) as the RF interface reference.

### RX

Radio SPK/RX audio enters J1 pin 1, passes VR1 10 kΩ, C1 1 µF and R1 1 kΩ, then reaches GPIO36/VP.

### PTT

Default GPIO25 drives Q1 2N3904 through R4 1 kΩ, with R5 10 kΩ as the base pulldown. The transistor pulls the radio PTT line to GND.

### TX

AFSK TX leaves GPIO26 through R6 1 kΩ, C3 1 µF and VR2 10 kΩ to the radio MIC/audio input.

## OLED

REV1T adds a **GPS TRACKER** page showing GPS state, satellites/HDOP, position, speed/course and next tracker beacon.

The iGate/digipeater continues to run when the OLED or GPS is unavailable.

## RF Remote Admin

See [RF_REMOTE_ADMIN_EN.md](RF_REMOTE_ADMIN_EN.md).

Tracker commands are `TRK ON/OFF`, `GPS`, `TMODE FIX/SMART`, `TSEC`, `TPATH`, `TOUT` and `TBCN`.

Configuration changes remain in RAM until `SAVE`; `CANCEL` restores the last saved remote-configurable values.

## GPS troubleshooting

For **NO GPS**:

1. verify common GND;
2. verify GPS TX goes to ESP32 GPS RX;
3. match the baud rate;
4. check the Web UI for GPIO conflicts;
5. verify the GPS actually emits NMEA.

For persistent **GPS SEARCH**:

- move the antenna to a clearer sky view;
- allow time for a cold start;
- verify the antenna/module arrangement.

For **GPS STALE**:

- NMEA still arrives but valid fixes are no longer updating;
- check satellite/antenna conditions;
- increase stale timeout only when justified.

## Save & Restart

Web UI changes are stored in NVS and applied after restart.

REV1S → REV1T upgrade does not automatically factory-reset NVS. Existing REV1S keys are preserved and new tracker keys use safe defaults.
