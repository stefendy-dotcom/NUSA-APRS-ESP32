# GPS Tracker REV1T — Bahasa Indonesia

REV1T menambahkan APRS GPS Tracker di atas engine REV1S tanpa mengubah decoder AFSK/AX.25 yang sudah stabil.

## Modul GPS

Didukung minimal:

- u-blox NEO-6M
- u-blox NEO-M8N
- u-blox NEO-M10N
- receiver lain yang mengeluarkan NMEA RMC/GGA standar

Baud selectable: 9600, 38400, 115200.

## Wiring default

| GPS | ESP32 |
|---|---|
| TX | GPIO16 / GPS RX |
| RX | GPIO17 / GPS TX, opsional |
| GND | GND |
| VCC | Sesuai spesifikasi modul |

GPS TX GPIO ESP32 dapat diatur `-1` bila tidak dipakai.

Jangan gunakan GPIO36, GPIO26, pin PTT, LED, atau OLED aktif sebagai pin GPS. Web UI memberi warning dan GPS UART tidak dimulai bila pin konflik.

## Data yang dibaca

RMC/GGA digunakan untuk:

- valid fix
- latitude/longitude
- speed
- course
- altitude
- satellite count
- HDOP
- UTC time

Parser hanya mengonsumsi maksimum 96 byte UART per loop agar tidak menahan pekerjaan APRS.

## GPS state

`NO GPS` — tidak ada stream NMEA baru.

`GPS SEARCH` — NMEA masuk tetapi belum ada fresh fix.

`GPS FIX` — fix valid.

`GPS STALE` — NMEA masih masuk tetapi fix terakhir terlalu tua.

Default stale timeout: 15 detik.

## Position Mode

### STATIC

Gunakan koordinat Station manual.

### GPS

Gunakan GPS saja. Tidak ada fresh fix = tidak ada tracker beacon.

### AUTO

Gunakan GPS bila valid. Jika tidak, gunakan posisi statis yang sudah dikonfigurasi. Default 0,0 tidak digunakan sebagai fallback.

## Format packet

REV1T mengirim APRS position uncompressed standar. Saat bergerak dan course valid, packet menambahkan `ccc/sss` untuk course/speed. Jika altitude GPS valid, altitude ditambahkan sebagai `/A=xxxxxx` dalam feet sesuai format APRS.

ToCall mengikuti Destination konfigurasi, default `APZUAG`.

## Fixed Interval

Range 30–3600 detik.

Default: 120 detik.

## SmartBeacon

Default:

| Parameter | Nilai |
|---|---:|
| Slow Speed | 5 km/h |
| Slow Rate | 600 s |
| Fast Speed | 80 km/h |
| Fast Rate | 60 s |
| Turn Angle | 30° |
| Turn Slope | 255 |
| Turn Time | 15 s |
| Stationary Speed | 3 km/h |
| Minimum TX Interval | 15 s |

Pada speed antara Slow dan Fast, interval dihitung proporsional berdasarkan speed. Corner pegging memakai perubahan course dan threshold `Turn Angle + Turn Slope / speed(mph)`.

## Output

Tracker dapat mengirim:

- RF
- APRS-IS
- RF + APRS-IS

RF selalu masuk queue dan CSMA. APRS-IS tidak menunggu RF.

## RF Remote Admin

- `TRK ON/OFF`
- `GPS`
- `TMODE FIX/SMART`
- `TSEC 120`
- `TPATH WIDE2-1`
- `TPATH DIRECT`
- `TOUT RF/IS/BOTH`
- `TBCN`

Gunakan `SAVE` setelah perubahan remote yang ingin dibuat permanen.

## Upgrade dari REV1S

Tracker default OFF. Key NVS REV1S tetap dipertahankan. Tidak ada factory reset otomatis.

Engine AFSK/AX.25 REV1S disalin byte-identik ke folder source REV1T.
