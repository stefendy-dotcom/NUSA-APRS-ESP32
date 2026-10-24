# Suggested BOM / Hardware Notes

| Item | Typical value / part | Notes |
|---|---|---|
| MCU | ESP32 DevKit / WROOM-32 | classic ESP32, 4 MB |
| Buck converter | MINI560 5A module | set output to 5.0 V before ESP32 |
| Q1 | 2N3904 NPN | verify actual E-B-C pinout |
| PTT base resistor | 1 kΩ | GPIO25 → base |
| PTT pulldown | 10 kΩ | base → GND |
| RX level pot | 10 kΩ | radio SPK/flat audio |
| RX series resistor | 1 kΩ | before GPIO36 |
| RX bias resistors | 47 kΩ + 47 kΩ | 3V3/GND midpoint ≈1.65 V |
| RX RF/noise capacitor | 2.2 nF | GPIO36 bias node → GND |
| RX coupling capacitor | 1 µF | non-polar preferred if DC unknown |
| TX series resistor | 1 kΩ | GPIO26 DAC output |
| TX level pot | 10 kΩ | adjust radio MIC level |
| TX coupling capacitor | 1 µF | non-polar preferred if MIC bias unknown |
| Supply bulk capacitor | 47 µF minimum | at 5 V rail; 220–470 µF optional for harsh sites |
| Supply bypass | 100 nF / 104 | near ESP32 supply |
| OLED | SH1106 128×64 I2C | SSD1306 also supported |

## Critical checks
- MINI560 output must be about 5.0 V before connecting VIN.
- Never apply station 12–14 V directly to ESP32 VIN/3V3.
- Verify every GND connection and ground-pour thermal.
- Verify 2.2 nF capacitor really goes from GPIO36 RX/bias node to GND.
- Verify transistor footprint matches the actual 2N3904 pin order.
- Polarized 1 µF orientation must follow measured DC voltage; use bipolar/non-polar when uncertain.
