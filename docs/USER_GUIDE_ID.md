# Cara Penggunaan — Bahasa Indonesia

## Power dan Web UI
Gunakan 5 V stabil ke VIN ESP32. Setelah boot, sambungkan ke AP NUSA-APRS-IGATE lalu buka http://192.168.4.1.

## Station & Beacon
Isi callsign/SSID yang sah, latitude/longitude lokasi, symbol, comment, interval beacon dan path RF. Koordinat site juga menjadi referensi bearing/jarak OLED heard RF/IS.

## Wi-Fi dan APRS-IS
Server default asia.aprs2.net:14580. Server filter boleh kosong. Local filter i/30/0 adalah aturan IS→RF di ESP32, bukan filter server APRS-IS.

## iGate dan Digipeater
Aktifkan iGate untuk frame RF valid → APRS-IS. Aktifkan Digi bila unit digunakan sebagai digipeater. Default alias WIDE1,WIDE2, Digi delay 150 ms, duplicate window 30 s.

## Radio / AFSK
RX masuk GPIO36 dari flat/discriminator/speaker audio melalui level control, coupling dan bias. TX AFSK keluar GPIO26 melalui level pot ke MIC. PTT GPIO25 menggerakkan 2N3904.

Decoder Gain awal 0=AUTO. TX Level default 90%. Preamble 350 ms; tail 80 ms.

## OLED
Default controller SH1106, SDA21/SCL22; SSD1306 juga didukung. Halaman: logo, status, network, RF/decoder, heard RF, heard APRS-IS dan weather RF.

## RF Remote Admin
Dua callsign+SSID admin dapat disimpan. Hanya source yang cocok persis yang diproses. Callsign lain tidak mendapat reply RF.

## Verifikasi
Kanal kosong: RF CHANNEL harus CLEAR. Saat packet masuk: BUSY sesaat, lalu Live Log menunjukkan DEC-A, B-RAW, B-EQ125 atau B-EQ130 bila frame valid. Untuk Digi, konfirmasikan retransmit di receiver/iGate lain.
