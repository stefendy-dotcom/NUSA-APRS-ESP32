# NUSA APRS ESP32 REV1S — English

Firmware for a **classic ESP32 WROOM/DevKit** interfacing an analog VHF radio with APRS RF and APRS-IS.

REV1S provides RF→IS iGate, RF digipeating, local IS→RF message gating, RF/IS beaconing, web configuration, live monitoring, SH1106/SSD1306 OLED display, RF weather, RF/APRS-IS last-heard lists, and RF Remote Admin.

The established RX engine is retained: Decoder A plus Decoder B RAW/EQ125/EQ130.

## Main pins

- RX audio / ADC: GPIO36 / VP
- TX AFSK DAC: GPIO26
- PTT: GPIO25
- RX LED: GPIO2
- TX LED: GPIO4
- OLED SDA/SCL: GPIO21/GPIO22
- OLED power: 3.3 V

## RF interface schematic

See [hardware/skema.jpeg](hardware/skema.jpeg).

- RX: J1 SPK/audio → VR1 10 kΩ → C1 1 µF → R1 1 kΩ → GPIO36/VP.
- RX ADC bias: R2 100 kΩ to 3V3 and R3 100 kΩ to GND, giving about 1.65 V. C2 2.2 nF connects the GPIO36/bias node to GND.
- PTT: GPIO25 → R4 1 kΩ → Q1 2N3904 base. R5 10 kΩ is the base pulldown. Q1 pulls the radio PTT line to GND, so the radio-side PTT is active LOW.
- TX: GPIO26 → R6 1 kΩ → C3 1 µF → VR2 10 kΩ → J3 radio MIC/audio TX.
- Pin 2 of J1, J2 and J3 is GND.

For C3, the schematic recommends a non-polar 1 µF capacitor when the radio MIC input may carry unknown DC bias. If an electrolytic is used, its positive terminal should face the side with the higher DC voltage.

## Quick start

1. Feed ESP32 VIN from a **stable 5 V source**. If the PCB uses a MINI560, set its output to about 5.0 V before installing the ESP32.
2. Flash the four BIN files using [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md).
3. Join the NUSA-APRS-IGATE access point.
4. Open 192.168.4.1.
5. Configure callsign/SSID, position, Wi-Fi, APRS-IS, iGate/Digi, AFSK, OLED and RF Remote.
6. Save configuration.
7. Adjust RX/TX using [docs/ADJUSTMENT_EN.md](docs/ADJUSTMENT_EN.md).

A field installation reached stable idle-channel CLEAR at **RX Squelch ADC = 135**. This is a proven starting point for that installation, not a universal setting.
