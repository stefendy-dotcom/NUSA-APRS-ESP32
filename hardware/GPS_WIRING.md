# GPS Wiring — NUSA APRS ESP32 REV1T

## Default connection

```text
GPS / GNSS module                 ESP32 WROOM / DevKit
-----------------                 ---------------------
TX  ----------------------------> GPIO16  (GPS UART RX)
RX  <---------------------------- GPIO17  (GPS UART TX, optional)
GND ----------------------------- GND
VCC ----------------------------- Voltage required by the GPS board/module
```

When the ESP32 only receives NMEA, set **GPS TX GPIO = -1** and the GPS RX connection may be omitted.

## Important

- GPIO36 is reserved for APRS RX audio ADC.
- GPIO26 is reserved for APRS AFSK TX DAC.
- Default PTT is GPIO25.
- Default RX/TX LEDs are GPIO2/GPIO4.
- Default OLED is GPIO21/GPIO22.
- Do not share these active pins with GPS.
- Web UI reports a GPS GPIO conflict and the GPS UART is not started when a conflict is detected.
- ESP32 GPIO logic is 3.3 V. Verify the logic-level and supply-voltage requirements of the GPS carrier board.
- Do not assume every NEO-6M/M8N/M10N board can be powered from 5 V; carrier boards differ.

## Default serial settings

- RX GPIO16
- TX GPIO17
- 9600 baud
- 8 data bits, no parity, 1 stop bit
- NMEA RMC/GGA
