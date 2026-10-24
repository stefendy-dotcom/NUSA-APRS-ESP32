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
Use [hardware/skema.jpeg](../hardware/skema.jpeg) as the RF interface reference.

### RX
J1 pin 1 accepts the radio SPK/RX audio. VR1 10 kΩ sets the RX level, followed by C1 1 µF and R1 1 kΩ into GPIO36/VP.

GPIO36 is biased to about 1.65 V by:
- R2 100 kΩ to 3V3
- R3 100 kΩ to GND
- C2 2.2 nF from the GPIO36/bias node to GND

J1 pin 2 is GND.

### PTT
GPIO25 drives Q1 2N3904 through R4 1 kΩ. R5 10 kΩ is the base pulldown. Q1 collector connects to the radio PTT line and the emitter goes to GND. The radio-side PTT is active LOW through the NPN open-collector switch.

J2 pin 1 = radio PTT, J2 pin 2 = GND.

### TX
TX AFSK comes from GPIO26, through R6 1 kΩ and C3 1 µF to the top of VR2 10 kΩ. The VR2 wiper feeds J3 pin 1 radio MIC/audio TX and the lower VR2 terminal goes to GND.

For general use, the schematic recommends a non-polar 1 µF capacitor if the radio MIC input may have unknown DC bias. If an electrolytic is used, the positive terminal should face the side with the higher DC voltage.

Start with Decoder Gain 0=AUTO, TX Level 90%, preamble 350 ms and tail 80 ms.

## OLED
SH1106 is field-tested on SDA21/SCL22; SSD1306 is supported. Pages: logo, status, network, RF/decoder, RF heard list, APRS-IS heard list and RF weather.

## RF Remote Admin
Two exact administrator callsign+SSID entries are supported. Non-admin RF requests are silently ignored.

## Operational check
Idle channel should show RF CHANNEL CLEAR. During a packet it should briefly show BUSY and a valid decoder path such as DEC-A, B-RAW, B-EQ125 or B-EQ130.
