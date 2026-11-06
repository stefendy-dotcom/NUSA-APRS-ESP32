# Four-file firmware package — REV1T

This directory is generated from the published **REV1T GPS Tracker** source by the GitHub Actions workflow.

Flash offsets:

- 0x1000 bootloader.bin
- 0x8000 partitions.bin
- 0xE000 boot_app0.bin
- 0x10000 firmware.bin

Target: classic ESP32 Dev Module / WROOM-32, 4 MB flash, Arduino-ESP32 Core 3.3.11.

The GPS tracker is disabled by default. Default GPS UART is RX GPIO16 / TX GPIO17 at 9600 baud.

Auto Mode Switch defaults ON: APRS-IS online = iGate ON/Digi OFF; Internet/APRS-IS unavailable = iGate OFF/Digi ON; reconnect returns automatically to iGate.

Always check `SHA256SUMS.txt` after download.