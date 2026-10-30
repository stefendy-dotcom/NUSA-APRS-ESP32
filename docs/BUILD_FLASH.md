# Build and Flash — REV1T

Target: **classic ESP32 Dev Module / WROOM-32, 4 MB flash**.

Validated toolchain:

- Arduino CLI 1.5.1
- Arduino-ESP32 Core **3.3.11**
- FQBN `esp32:esp32:esp32`
- Default 4 MB partition scheme

## Build source

REV1T source folder:

```text
source/NUSA_APRS_IGATE_DIGI_REV1T_GPS_TRACKER
```

Rebuild:

```bash
arduino-cli core install esp32:esp32@3.3.11
arduino-cli compile --fqbn esp32:esp32:esp32 source/NUSA_APRS_IGATE_DIGI_REV1T_GPS_TRACKER
```

The repository workflow compiles the same folder and refreshes the four firmware images in `firmware/bin`.

## Four BIN files

| Offset | File |
|---:|---|
| 0x1000 | bootloader.bin |
| 0x8000 | partitions.bin |
| 0xE000 | boot_app0.bin |
| 0x10000 | firmware.bin |

Example:

```bash
esptool --chip esp32 --port COM5 --baud 460800 write-flash -z \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0xE000 boot_app0.bin \
  0x10000 firmware.bin
```

Change `COM5` to the correct port.

## Upgrade from REV1S

Normal flashing of the application/four-BIN package does not intentionally factory-reset NVS. REV1T reads the existing REV1S keys and gives new GPS/tracker keys safe defaults; tracker defaults to OFF.

A full flash erase removes NVS configuration. Erase only when intentionally resetting the unit.

## GPS configuration after flash

Default GPS pins are GPIO16 RX and GPIO17 TX, baud 9600. GPS can remain physically disconnected; the rest of REV1T continues to run.

Use the Web UI GPS Tracker tab to select:

- GPS RX/TX pins
- baud
- stale timeout
- position mode
- tracker output/path
- fixed/SmartBeacon parameters

The firmware does not start the GPS UART when the selected pins conflict with APRS RX/TX, PTT/LED or active OLED pins.

## Verify download

Always verify `SHA256SUMS.txt` located beside the BIN files.
