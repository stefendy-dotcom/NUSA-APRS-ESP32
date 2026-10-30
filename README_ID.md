# NUSA APRS ESP32 REV1T — Bahasa Indonesia

[![Total Firmware Downloads](https://img.shields.io/github/downloads/stefendy-dotcom/NUSA-APRS-ESP32/total?label=Firmware%20Downloads&color=blue)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases)

📦 [Unduh firmware REV1T (ZIP berisi 4 BIN)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases/download/REV1T/NUSA_APRS_ESP32_REV1T_FLASH.zip)

Firmware untuk **ESP32 WROOM/DevKit klasik** yang menghubungkan radio VHF analog dengan APRS RF/APRS-IS dan sekarang juga dapat berfungsi sebagai **GPS APRS Tracker**.

REV1T mempertahankan engine RX/iGate/digipeater REV1S tanpa perubahan dan menambahkan tracker GPS UART NMEA secara modular dan non-blocking. Tracker default **OFF**, sehingga upgrade dari REV1S tetap dapat digunakan walaupun GPS belum dipasang.

## Fungsi utama

- APRS RF → APRS-IS iGate.
- APRS RF digipeater.
- APRS-IS → RF message gate lokal.
- Beacon site RF/IS dari REV1S.
- GPS APRS Tracker baru.
- NMEA RMC/GGA untuk NEO-6M, NEO-M8N, NEO-M10N dan GPS NMEA kompatibel.
- Mode posisi STATIC / GPS / AUTO.
- Output tracker RF / APRS-IS / RF+IS.
- Fixed Interval dan SmartBeaconing.
- Corner pegging berdasarkan perubahan arah.
- GPS stale protection dan validasi fix.
- Web UI live GPS/tracker.
- Halaman OLED GPS.
- RF Remote Admin tracker.
- RX WAV diagnostic, RF weather dan last-heard RF/IS.

## Pin utama

- RX audio / ADC: GPIO36 / VP
- TX AFSK DAC: GPIO26
- PTT: GPIO25
- RX LED: GPIO2
- TX LED: GPIO4
- OLED SDA/SCL: GPIO21/GPIO22
- GPS RX UART default: GPIO16
- GPS TX UART default: GPIO17

GPIO GPS dapat diganti dari Web UI. GPS UART tidak dijalankan bila terjadi konflik dengan GPIO36, GPIO26, PTT, LED atau OLED yang sedang aktif.

## Wiring GPS default

- GPS TX → ESP32 GPIO16
- GPS RX → ESP32 GPIO17 bila diperlukan
- GPS GND → ESP32 GND
- GPS VCC → sesuai spesifikasi modul GPS

Jika ESP32 hanya menerima NMEA, GPS TX GPIO pada ESP32 dapat diatur ke `-1`.

**Jangan menganggap semua board GPS aman langsung ke 5 V.** Periksa spesifikasi carrier/modul yang digunakan.

Baud GPS yang dapat dipilih: **9600 / 38400 / 115200**.

## Default tracker

- Tracker: OFF
- Position Mode: AUTO
- Tracker Output: RF
- Tracker RF Path: WIDE2-1
- Symbol: `/>` (mobil)
- Fixed Interval: 120 detik
- GPS Stale Timeout: 15 detik
- Minimum Tracker TX Interval: 15 detik
- Slow Speed / Rate: 5 km/h / 600 detik
- Fast Speed / Rate: 80 km/h / 60 detik
- Turn Angle: 30°
- Turn Slope: 255
- Turn Time: 15 detik
- Stationary Speed: 3 km/h

## Mode posisi

**STATIC** menggunakan latitude/longitude manual seperti beacon REV1S.

**GPS** hanya mengirim tracker bila GPS mempunyai fix yang masih fresh.

**AUTO** memakai GPS bila fix valid; jika GPS tidak valid, posisi statis dipakai hanya bila latitude/longitude statis sudah benar-benar dikonfigurasi. Default 0,0 tidak digunakan sebagai fallback.

## SmartBeaconing

Pada kecepatan rendah tracker memakai Slow Rate. Pada kecepatan tinggi memakai Fast Rate. Di antara keduanya interval dihitung berdasarkan kecepatan.

Corner pegging menggunakan perubahan heading dengan threshold dinamis. Semua beacon otomatis tetap tunduk pada Minimum Tracker TX Interval dan semua TX RF tetap masuk queue/CSMA REV1S.

## RF Remote Admin tracker

Command tambahan REV1T:

- `TRK ON` / `TRK OFF`
- `GPS`
- `TMODE FIX`
- `TMODE SMART`
- `TSEC 120`
- `TPATH WIDE2-1`
- `TPATH DIRECT`
- `TOUT RF`
- `TOUT IS`
- `TOUT BOTH`
- `TBCN`

Lihat [docs/RF_REMOTE_ADMIN_ID.md](docs/RF_REMOTE_ADMIN_ID.md).

## Dokumentasi

- [Panduan penggunaan](docs/USER_GUIDE_ID.md)
- [Panduan GPS Tracker](docs/GPS_TRACKER_ID.md)
- [RF Remote Admin](docs/RF_REMOTE_ADMIN_ID.md)
- [Build & Flash](docs/BUILD_FLASH.md)
- [Validation](docs/VALIDATION.md)

## Build

Target: **ESP32 Dev Module / WROOM-32 klasik / flash 4 MB / Arduino-ESP32 Core 3.3.11**.

Build tervalidasi:

- flash aplikasi: **1.139.979 byte (86%)**
- global RAM: **77.184 byte (23%)**

File AFSK/AX.25 dan engine decoder REV1S dipertahankan byte-identik pada REV1T.
