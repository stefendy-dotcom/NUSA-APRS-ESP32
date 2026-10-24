# Four-file firmware package

This directory is generated from the published REV1S source by the GitHub Actions workflow.

Flash offsets:
- 0x1000 bootloader.bin
- 0x8000 partitions.bin
- 0xE000 boot_app0.bin
- 0x10000 firmware.bin

Target: classic ESP32 Dev Module / WROOM-32, 4 MB flash, Arduino-ESP32 Core 3.3.11.

Always check SHA256SUMS.txt after download.
