# Build and Flash

Target: **classic ESP32 Dev Module / WROOM-32, 4 MB flash**.

Validated toolchain:
- Arduino CLI 1.5.1
- Arduino-ESP32 Core **3.3.11**
- FQBN esp32:esp32:esp32
- Default 4 MB partition scheme

## Four BIN files

    0x1000  bootloader.bin
    0x8000  partitions.bin
    0xE000  boot_app0.bin
    0x10000 firmware.bin

Example:

    esptool --chip esp32 --port COM5 --baud 460800 write-flash -z 0x1000 bootloader.bin 0x8000 partitions.bin 0xE000 boot_app0.bin 0x10000 firmware.bin

Change COM5 to the correct port.

A full flash erase removes stored NVS configuration. Erase only when intentionally resetting the unit.

## Rebuild

    arduino-cli core install esp32:esp32@3.3.11
    arduino-cli compile --fqbn esp32:esp32:esp32 source/NUSA_APRS_IGATE_DIGI_REV1S_RF_REMOTE_ADMIN

The GitHub workflow rebuilds all four images and writes SHA256 checksums.
