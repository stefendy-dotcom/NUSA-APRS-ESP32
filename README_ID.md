# NUSA APRS ESP32 REV1S — Bahasa Indonesia

[![Total Firmware Downloads](https://img.shields.io/github/downloads/stefendy-dotcom/NUSA-APRS-ESP32/total?label=Firmware%20Downloads&color=blue)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases)

📦 [Unduh firmware REV1S (ZIP berisi 4 BIN)](https://github.com/stefendy-dotcom/NUSA-APRS-ESP32/releases/download/REV1S/NUSA_APRS_ESP32_REV1S_FLASH.zip)

*Counter menghitung unduhan berkas di GitHub Releases, bukan unduhan source code dari menu **Code → Download ZIP**.*

Firmware untuk **ESP32 WROOM/DevKit klasik** yang menghubungkan radio VHF analog dengan APRS RF dan APRS-IS.

REV1S menyediakan iGate RF→IS, digipeater RF, local IS→RF message gating, beacon RF/IS, web configuration, monitor live, OLED SH1106/SSD1306, weather RF, daftar stasiun terakhir terdengar via RF dan IS, serta RF Remote Admin.

Engine RX tetap menggunakan Decoder A dan Decoder B: B-RAW, B-EQ125 dan B-EQ130.

## Pin utama

- RX audio / ADC: GPIO36 / VP
- TX AFSK DAC: GPIO26
- PTT: GPIO25
- RX LED: GPIO2
- TX LED: GPIO4
- OLED SDA/SCL: GPIO21/GPIO22
- OLED VCC: 3.3 V

## Skema antarmuka RF

Lihat [hardware/skema.jpeg](hardware/skema.jpeg).

- RX: J1 SPK/audio → VR1 10K → C1 1µF → R1 1K → GPIO36/VP.
- Bias ADC RX: R2 100K ke 3V3 dan R3 100K ke GND, bias sekitar 1,65 V. C2 2n2 dari node bias/GPIO36 ke GND.
- PTT: GPIO25 → R4 1K → basis Q1 2N3904. R5 10K menjadi pull-down basis. Collector Q1 menarik jalur PTT radio ke GND sehingga PTT radio aktif LOW.
- TX: GPIO26 → R6 1K → C3 1µF → VR2 10K → J3 MIC/audio TX radio.
- Pin 2 J1, J2 dan J3 adalah GND.

Untuk C3, skema menyarankan kapasitor non-polar 1µF bila input MIC radio mempunyai bias DC yang tidak diketahui. Bila memakai elko, kutub + dipasang ke sisi yang mempunyai tegangan DC lebih tinggi.

## Mulai

1. Berikan catu **5 V stabil** ke VIN ESP32. Bila PCB memakai MINI560, set output sekitar 5,0 V sebelum ESP32 dipasang.
2. Flash empat BIN sesuai [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md).
3. Nyalakan unit dan sambungkan ke AP NUSA-APRS-IGATE.
4. Buka 192.168.4.1.
5. Isi callsign/SSID, koordinat, Wi-Fi, APRS-IS, iGate/Digi, radio/AFSK, OLED dan RF Remote.
6. Simpan konfigurasi.
7. Lakukan adjustment RX/TX menurut [docs/ADJUSTMENT_ID.md](docs/ADJUSTMENT_ID.md).

Pada salah satu instalasi nyata, **RX Squelch ADC = 135** menghasilkan RF CHANNEL CLEAR stabil ketika kanal kosong. Nilai ini bukan angka universal.
