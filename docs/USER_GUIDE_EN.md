# User Guide — English

## Power and Web UI
Feed ESP32 VIN from a stable 5 V source. After boot, join NUSA-APRS-IGATE and open http://192.168.4.1.

## Station and beacon
Configure legal callsign/SSID, site latitude/longitude, APRS symbol, comment, beacon interval and RF path. Site coordinates are also the reference for OLED bearing/distance.

## Wi-Fi and APRS-IS
Default server is asia.aprs2.net:14580. A blank server filter uses server defaults. The local i/30/0 rule is an ESP32 IS→RF message gate rule, not an APRS-IS server filter.

## iGate and digipeater
Enable iGate for valid RF → APRS-IS forwarding. Enable Digi for RF repeating. Defaults: WIDE1,WIDE2 aliases, 150 ms Digi delay and 30 s duplicate suppression.

## Radio / AFSK
RX audio enters GPIO36 through level control/coupling/bias. TX AFSK leaves GPIO26 through the TX level network to radio MIC. GPIO25 drives the 2N3904 PTT switch.

Start with Decoder Gain 0=AUTO, TX Level 90%, preamble 350 ms and tail 80 ms.

## OLED
SH1106 is field-tested on SDA21/SCL22; SSD1306 is supported. Pages: logo, status, network, RF/decoder, RF heard list, APRS-IS heard list and RF weather.

## RF Remote Admin
Two exact administrator callsign+SSID entries are supported. Non-admin RF requests are silently ignored.

## Operational check
Idle channel should show RF CHANNEL CLEAR. During a packet it should briefly show BUSY and a valid decoder path such as DEC-A, B-RAW, B-EQ125 or B-EQ130.
