# Validation — REV1T GPS Tracker

## Source lineage

REV1T is built from `NUSA_APRS_IGATE_DIGI_REV1S_RF_REMOTE_ADMIN`.

The following RF receive/decoder files are byte-identical between REV1S and REV1T:

- AFSK.cpp
- AFSK.h
- AX25.cpp
- AX25.h
- CRC-CCIT.cpp
- CRC-CCIT.h
- FIFO.h
- HDLC.h

REV1T adds GPS/tracker logic in the application layer plus `NusaGpsTracker.cpp/.h`.

No new application `xTaskCreate`, `xTaskCreatePinnedToCore`, mutex, semaphore or portMUX was added by REV1T. The low-level AFSK source already contains its pre-existing REV1S receive task and is unchanged.

## Real compile

Validated on server `stefendy` with:

- Arduino CLI 1.5.1
- Arduino-ESP32 Core 3.3.11
- FQBN `esp32:esp32:esp32`

Final result:

- compile exit code: **0**
- application flash: **1,139,979 bytes (86% of 1,310,720)**
- global RAM: **77,184 bytes (23% of 327,680)**
- free for local variables in build report: **250,496 bytes**

## Final four-BIN SHA256

- `bootloader.bin`: `427f96e10c620c4f062dab15da54fc45494d897e8397ae6f3aecc98c42d7e379`
- `partitions.bin`: `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1`
- `boot_app0.bin`: `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0`
- `firmware.bin`: `5a192b76089979aa97ac05639475cd0edbc910a366f4373e8fe5712fde9e8ba4`

The GitHub Actions build may regenerate the application image from the same source; use the `SHA256SUMS.txt` beside the downloaded binaries as the authoritative checksum for that build.

## Automated source/logic validation

A deterministic validation run completed **63/63 PASS**.

Checks included:

- frozen REV1S engine hashes;
- tracker defaults OFF;
- GPS service limited to 96 bytes per main loop iteration;
- no blocking `while (!gpsFix)`;
- RMC and GGA parser presence;
- GPS stale protection;
- default 0,0 static fallback suppression;
- tracker RF transmission through `TX_TRACKER` and the REV1S TX queue;
- dedicated tracker path/output/position modes;
- Fixed and SmartBeacon modes;
- retry throttling when an output is unavailable;
- manual tracker beacon route;
- all seven new RF Remote Admin tracker operations;
- read/write presence of all tracker NVS keys;
- no new FreeRTOS synchronization/task constructs in REV1T application/GPS code;
- known NMEA checksum/coordinate/speed conversion references;
- SmartBeacon stationary/slow/mid/fast interval references;
- SmartBeacon turn-threshold reference;
- presence of all four BIN images, offsets and checksums.

## Prompt test matrix

| Test | Build/source validation | Physical field validation |
|---|---|---|
| GPS not installed, REV1S functions continue | Design/default checks PASS | Required on ESP32 |
| GPS connected without fix, no invalid position TX | Logic checks PASS | Required on ESP32/GPS |
| GPS fix shown in Web/OLED | Code path + compile PASS | Required on ESP32/GPS |
| Fixed RF beacon interval | Scheduler check PASS | Required over RF |
| APRS-IS tracker beacon | Code path + compile PASS | Required with live APRS-IS |
| SmartBeacon stationary | Formula check PASS | Required in motion test |
| SmartBeacon moving | Formula check PASS | Required in motion test |
| Corner pegging | Formula/threshold check PASS | Required in driving test |
| Digi + iGate + Tracker simultaneously | Frozen engine + compile PASS | Required under RF load |
| Heavy APRS RF traffic | RX engine frozen byte-identical | Required RF stress test |
| GPS unplug while running | stale/disconnect logic PASS | Required hardware test |
| GPS returns | restore/fix transition logic PASS | Required hardware test |

## Important validation boundary

The remote build environment can compile and inspect the firmware but does **not** have the target ESP32, VHF radio and GPS receiver physically attached. Therefore the hardware/over-the-air column above is intentionally not marked as passed.

Do not treat compile/static validation as proof of RF field performance. The first REV1T field test should verify GPS UART, AFSK RX sensitivity, RF beacon decode, APRS-IS packet formatting and simultaneous Digi/iGate/Tracker operation on the actual hardware.

## GitHub Actions CI build

GitHub Actions run `41045076034` completed successfully for REV1T source commit `df1aa9dbd42d620826f7d74e1dcb3d1599f87769` and committed the generated four-BIN package as `dd1258f4857ad382c677cdc321c870f4019394db`.

Authoritative checksums of that CI-generated package:

- `bootloader.bin`: `427f96e10c620c4f062dab15da54fc45494d897e8397ae6f3aecc98c42d7e379`
- `partitions.bin`: `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1`
- `boot_app0.bin`: `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0`
- `firmware.bin`: `968c2bf25b97e54d0423509921eb88ebe411233d5033479656e36fcbd5ec95ed`

The CI application binary hash differs from the independently compiled server reference because Arduino/ESP32 builds are not treated as reproducible-byte builds here. The published release uses the **GitHub Actions-generated** BIN package and its accompanying `SHA256SUMS.txt`.
