# NUSA APRS ESP32 REV1S — Bahasa Indonesia

Firmware untuk **ESP32 WROOM/DevKit klasik** yang menghubungkan radio VHF analog dengan APRS RF dan APRS-IS.

REV1S menyediakan iGate RF→IS, digipeater RF, local IS→RF message gating, beacon RF/IS, web configuration, monitor live, OLED SH1106/SSD1306, weather RF, daftar stasiun terakhir terdengar via RF dan IS, serta RF Remote Admin.

Engine RX tetap menggunakan Decoder A dan Decoder B: B-RAW, B-EQ125 dan B-EQ130.

## Pin utama

- RX audio: GPIO36 / VP
- TX AFSK DAC: GPIO26
- PTT: GPIO25
- RX LED: GPIO2
- TX LED: GPIO4
- OLED SDA/SCL: GPIO21/GPIO22
- OLED VCC: 3.3 V

PTT memakai NPN 2N3904: GPIO25 HIGH menyalakan transistor dan menarik jalur PTT radio ke GND.

## Mulai

1. Set MINI560 ke **5.0 V** sebelum menyambungkan ESP32.
2. Flash empat BIN sesuai [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md).
3. Nyalakan unit dan sambungkan ke AP NUSA-APRS-IGATE.
4. Buka 192.168.4.1.
5. Isi callsign/SSID, koordinat, Wi-Fi, APRS-IS, iGate/Digi, radio/AFSK, OLED dan RF Remote.
6. Simpan konfigurasi.
7. Lakukan adjustment RX/TX menurut [docs/ADJUSTMENT_ID.md](docs/ADJUSTMENT_ID.md).

Pada salah satu instalasi nyata, **RX Squelch ADC = 135** menghasilkan RF CHANNEL CLEAR stabil ketika kanal kosong. Nilai ini bukan angka universal.
