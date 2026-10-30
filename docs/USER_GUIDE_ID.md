# Cara Penggunaan REV1T — Bahasa Indonesia

## Power dan Web UI

Gunakan 5 V stabil ke VIN ESP32. Setelah boot, sambungkan ke AP **NUSA-APRS-IGATE** lalu buka `http://192.168.4.1`.

REV1T mempunyai tab baru **GPS Tracker**. Tracker default OFF sehingga perangkat tetap bekerja seperti REV1S bila GPS belum dipasang.

## Station & Beacon

Isi callsign/SSID yang sah, latitude/longitude site, symbol, comment, interval beacon dan path RF.

Beacon site REV1S tetap tersedia dan terpisah dari GPS Tracker. Latitude/longitude site juga dipakai sebagai posisi STATIC/AUTO fallback dan referensi bearing/jarak OLED heard RF/IS.

## GPS Tracker

### Wiring default

- GPS TX → ESP32 GPIO16
- GPS RX → ESP32 GPIO17 bila diperlukan
- GND → GND
- VCC → sesuai spesifikasi modul GPS

Jika ESP32 tidak perlu mengirim data ke GPS, set **GPS TX GPIO = -1**.

Jangan gunakan pin GPS yang konflik dengan GPIO36, GPIO26, PTT, LED atau OLED. Web UI menampilkan **GPIO CONFLICT** dan GPS UART tidak dijalankan bila konfigurasi tidak aman.

### GPS Baud

Pilih salah satu:

- 9600
- 38400
- 115200

NEO-6M umumnya memakai 9600 pada konfigurasi pabrik. Modul lain dapat berbeda.

### GPS State

- **NO GPS**: stream NMEA tidak terlihat.
- **GPS SEARCH**: NMEA diterima tetapi belum ada fix valid.
- **GPS FIX**: fix valid dan masih fresh.
- **GPS STALE**: NMEA masih diterima tetapi fix terakhir sudah melewati GPS Stale Timeout.

REV1T membaca sentence RMC dan GGA untuk latitude, longitude, speed, course, altitude, satellite, HDOP dan UTC time.

### Position Mode

**STATIC** — selalu menggunakan latitude/longitude manual dari Station.

**GPS** — hanya menggunakan GPS. Bila tidak ada fresh fix, tracker tidak mengirim posisi.

**AUTO** — GPS dipakai bila fresh. Bila GPS tidak valid, posisi STATIC dipakai hanya jika site coordinate memang sudah dikonfigurasi. Default 0,0 tidak dikirim.

### Tracker Output

- **RF ONLY**
- **APRS-IS ONLY**
- **RF + APRS-IS**

APRS-IS tidak menunggu TX RF. RF tetap masuk TX queue dan CSMA REV1S.

### Tracker RF Path

Default `WIDE2-1`.

`DIRECT` atau kosong berarti tanpa digipeater path.

Path tracker terpisah dari:

- RF site beacon
- IS→RF
- RF Remote Admin reply

### Fixed Interval

Pilih **FIXED INTERVAL** dan set **Fixed Interval** 30–3600 detik.

Minimum Tracker TX Interval tetap membatasi beacon otomatis yang terlalu rapat.

### SmartBeaconing

Pilih **SMARTBEACON**.

Default:

- Slow Speed: 5 km/h
- Slow Rate: 600 detik
- Fast Speed: 80 km/h
- Fast Rate: 60 detik
- Turn Angle: 30°
- Turn Slope: 255
- Turn Time: 15 detik
- Stationary Speed: 3 km/h
- Minimum Tracker TX Interval: 15 detik

Saat speed rendah, Slow Rate digunakan. Saat speed tinggi, Fast Rate digunakan. Di antara keduanya interval berubah berdasarkan speed.

Corner pegging mengirim beacon lebih cepat bila perubahan arah melewati threshold dinamis. Tracker tetap menghormati Minimum TX Interval dan CSMA.

### Manual Tracker Beacon

Klik **SEND TRACKER BEACON**.

Manual beacon boleh melewati Minimum Tracker TX Interval, tetapi RF tetap masuk queue dan tetap tunduk pada CSMA/channel busy.

Jika Position Mode GPS tidak mempunyai fix, beacon tidak dikirim. Pada AUTO, static fallback dapat dipakai bila posisi statis valid.

## Wi-Fi dan APRS-IS

Server default `asia.aprs2.net:14580`. Server filter boleh kosong.

Local filter `i/30/0` adalah aturan IS→RF di ESP32, bukan filter server APRS-IS.

## iGate dan Digipeater

Aktifkan iGate untuk frame RF valid → APRS-IS.

Aktifkan Digi bila unit digunakan sebagai digipeater.

Default:

- aliases: WIDE1,WIDE2
- Digi delay: 150 ms
- duplicate window: 30 s

GPS Tracker dapat ON/OFF secara independen dari Digi, iGate dan IS→RF.

## Radio / AFSK

Gunakan [hardware/skema.jpeg](../hardware/skema.jpeg) sebagai acuan antarmuka RF.

### RX

J1 pin 1 menerima SPK/audio RX radio. VR1 10K mengatur level, lalu C1 1µF dan R1 1K menuju GPIO36/VP.

GPIO36 dibias sekitar 1,65 V oleh R2 100K ke 3V3 dan R3 100K ke GND. C2 2n2 ke GND.

### PTT

GPIO25 default mengendalikan Q1 2N3904 melalui R4 1K. R5 10K menjadi pull-down basis. Collector menarik jalur PTT radio ke GND.

### TX

AFSK TX berasal dari GPIO26 melalui R6 1K, C3 1µF dan VR2 10K menuju MIC/audio TX radio.

## OLED

REV1T menambah halaman **GPS TRACKER** yang menampilkan:

- GPS state
- satellite / HDOP
- latitude / longitude
- speed / course
- mode tracker dan next beacon

Jika OLED atau GPS tidak tersedia, iGate/digipeater tetap berjalan.

## RF Remote Admin

Lihat [RF_REMOTE_ADMIN_ID.md](RF_REMOTE_ADMIN_ID.md).

Tracker command:

`TRK ON/OFF`, `GPS`, `TMODE FIX/SMART`, `TSEC`, `TPATH`, `TOUT`, dan `TBCN`.

Perubahan command konfigurasi berlaku di RAM sampai `SAVE`. Gunakan `CANCEL` untuk kembali ke konfigurasi tersimpan.

## Troubleshooting GPS

Jika **NO GPS**:

1. periksa GND bersama;
2. pastikan GPS TX masuk ke GPIO GPS RX;
3. cocokkan baud;
4. periksa GPIO conflict;
5. pastikan GPS benar-benar mengeluarkan NMEA.

Jika **GPS SEARCH** terus:

- pindahkan antena GPS ke area dengan pandangan langit lebih baik;
- tunggu cold start;
- periksa antena aktif/pasif sesuai modul.

Jika **GPS STALE**:

- NMEA masih masuk tetapi fix valid tidak diperbarui;
- cek kualitas satelit/antena;
- naikkan stale timeout hanya bila memang diperlukan.

## Save & Restart

Perubahan Web UI disimpan ke NVS dan diterapkan setelah restart.

Upgrade REV1S → REV1T tidak melakukan factory reset otomatis. Key REV1S lama dipertahankan; key tracker baru memakai default aman.
