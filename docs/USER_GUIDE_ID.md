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
Gunakan [hardware/skema.jpeg](../hardware/skema.jpeg) sebagai acuan antarmuka RF.

### RX
J1 pin 1 menerima SPK/audio RX radio. Level diatur VR1 10K, lalu audio melewati C1 1µF dan R1 1K menuju GPIO36/VP.

GPIO36 dibias sekitar 1,65 V oleh:
- R2 100K ke 3V3
- R3 100K ke GND
- C2 2n2 dari node bias/GPIO36 ke GND

J1 pin 2 adalah GND.

### PTT
GPIO25 mengendalikan Q1 2N3904 melalui R4 1K. R5 10K menjadi pull-down basis. Collector Q1 terhubung ke jalur PTT radio, emitter ke GND. Jalur PTT radio aktif LOW melalui NPN open collector.

J2 pin 1 = PTT radio, J2 pin 2 = GND.

### TX
AFSK TX berasal dari GPIO26, melewati R6 1K dan C3 1µF menuju bagian atas VR2 10K. Wiper VR2 menuju J3 pin 1 MIC/audio TX radio dan kaki bawah VR2 ke GND.

Untuk penggunaan umum, skema menyarankan kapasitor non-polar 1µF bila input MIC radio memiliki bias DC yang tidak diketahui. Bila menggunakan elko, kutub + menghadap sisi yang tegangan DC-nya lebih tinggi.

Decoder Gain awal 0=AUTO. TX Level default 90%. Preamble 350 ms; tail 80 ms.

## OLED
Default controller SH1106, SDA21/SCL22; SSD1306 juga didukung. Halaman: logo, status, network, RF/decoder, heard RF, heard APRS-IS dan weather RF.

## RF Remote Admin
Dua callsign+SSID admin dapat disimpan. Hanya source yang cocok persis yang diproses. Callsign lain tidak mendapat reply RF.

## Verifikasi
Kanal kosong: RF CHANNEL harus CLEAR. Saat packet masuk: BUSY sesaat, lalu Live Log menunjukkan DEC-A, B-RAW, B-EQ125 atau B-EQ130 bila frame valid. Untuk Digi, konfirmasikan retransmit di receiver/iGate lain.
