# Validation — REV1S

## Source lineage
REV1S is based on the stable REV1R OLED firmware. RF Remote Admin was added without intentionally changing the established AFSK/AX.25 decoder behavior.

## Real compile
Validated on server stefendy with:
- Arduino CLI 1.5.1
- Arduino-ESP32 Core 3.3.11
- FQBN esp32:esp32:esp32

Result:
- compile exit code: 0
- application flash: 1,105,807 bytes (84% of 1,310,720)
- global RAM: 76,744 bytes (23% of 327,680)
- free for local variables at build report: 250,936 bytes

The four normal ESP32 images were produced: bootloader, partition table, boot_app0 and application firmware.

## Remote-build SHA256 reference
The independently compiled reference build on stefendy produced:
- bootloader.bin: 427f96e10c620c4f062dab15da54fc45494d897e8397ae6f3aecc98c42d7e379
- partitions.bin: 148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1
- boot_app0.bin: f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0
- firmware.bin: cfbcac9e3791fb2301ee0031ac4acc2bf3550b5d14c9ed287c0ee9f5d0a08e9e

GitHub Actions also writes SHA256SUMS.txt for the binaries produced by the published workflow. Build artifacts may be re-generated; use the checksum file located beside the BIN files when flashing a downloaded copy.

## Field notes
The dual receive engine has previously decoded real RF packets using DEC-A and the tolerant B-EQ125/B-EQ130 paths. One current PCB/radio installation required RX Squelch ADC 135 for stable idle-channel CLEAR.
