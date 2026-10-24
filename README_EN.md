# NUSA APRS ESP32 REV1S — English

Firmware for a **classic ESP32 WROOM/DevKit** interfacing an analog VHF radio with APRS RF and APRS-IS.

REV1S provides RF→IS iGate, RF digipeating, local IS→RF message gating, RF/IS beaconing, web configuration, live monitoring, SH1106/SSD1306 OLED display, RF weather, RF/APRS-IS last-heard lists, and RF Remote Admin.

The established RX engine is retained: Decoder A plus Decoder B RAW/EQ125/EQ130.

## Main pins

- RX audio: GPIO36 / VP
- TX AFSK DAC: GPIO26
- PTT: GPIO25
- RX LED: GPIO2
- TX LED: GPIO4
- OLED SDA/SCL: GPIO21/GPIO22
- OLED power: 3.3 V

The NPN PTT interface is radio-side active LOW. GPIO25 HIGH turns the 2N3904 on and grounds the radio PTT line.

## Quick start

1. Set MINI560 output to **5.0 V** before connecting ESP32.
2. Flash the four BIN files using [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md).
3. Join the NUSA-APRS-IGATE access point.
4. Open 192.168.4.1.
5. Configure callsign/SSID, position, Wi-Fi, APRS-IS, iGate/Digi, AFSK, OLED and RF Remote.
6. Save configuration.
7. Adjust RX/TX using [docs/ADJUSTMENT_EN.md](docs/ADJUSTMENT_EN.md).

A field installation reached stable idle-channel CLEAR at **RX Squelch ADC = 135**. This is a proven starting point for that installation, not a universal setting.
