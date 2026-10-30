# RF Remote Admin — Bahasa Indonesia

REV1T dapat menerima command konfigurasi melalui **APRS Message yang diterima langsung dari RF**. Semua command REV1S tetap dipertahankan dan REV1T menambahkan kontrol GPS Tracker.

## Otorisasi

Di Web UI tersedia dua field admin:

- Admin Callsign 1
- Admin Callsign 2

Pencocokan dilakukan **persis callsign+SSID**. Contoh: bila admin adalah `YF9UAG-9`, maka `YF9UAG` dan `YF9UAG-10` tidak dianggap admin.

Packet dari callsign selain dua admin:

- tidak menjalankan command;
- tidak mendapat ACK admin;
- tidak mendapat ERR;
- tidak memicu TX balasan RF;
- hanya dapat dicatat di log lokal sebagai denied/silent.

Command dari APRS-IS tidak dipakai untuk remote admin; jalur ini khusus frame RF yang berhasil didecode.

## Cara mengirim command

Kirim command sebagai **APRS Message ke callsign perangkat NUSA APRS**. Isi message cukup berupa command pendek di bawah.

Contoh:

```text
STAT
DIGI ON
RXGAIN 0
RFPATH WIDE2-1
SAVE
```

Bila aplikasi/radio pengirim menggunakan APRS message number, perangkat juga mengirim ACK standar APRS terlebih dahulu.

## Command pendek dan fungsinya

| Command | Fungsi |
|---|---|
| `STAT` | Menampilkan status singkat perangkat: uptime, jumlah packet RF yang diterima, status DIGI, IGATE, koneksi APRS-IS, dan free heap. |
| `GET` | Menampilkan konfigurasi operasional utama saat ini dalam bentuk singkat: DIGI, IGATE, IS→RF, RX squelch, RX decoder gain, TX level, dan interval beacon. Tanda `*` berarti ada perubahan yang belum di-`SAVE`. |
| `DIGI ON` / `DIGI OFF` | Mengaktifkan atau mematikan fungsi **APRS digipeater**. |
| `IGATE ON` / `IGATE OFF` | Mengaktifkan atau mematikan fungsi **RF → APRS-IS iGate**. |
| `ISRF ON` / `ISRF OFF` | Mengaktifkan atau mematikan fungsi **APRS-IS → RF**. |
| `RFBCN ON` / `RFBCN OFF` | Mengaktifkan atau mematikan **beacon posisi melalui RF**. |
| `ISBCN ON` / `ISBCN OFF` | Mengaktifkan atau mematikan **beacon posisi langsung ke APRS-IS**. |
| `RXSQL 135` | Mengatur **RX Squelch ADC**. Nilai yang diterima: **5–1000**. Contoh `RXSQL 55`. |
| `RXGAIN 0` | Mengatur **RX Decoder Gain**. `0` = AUTO. Nilai manual yang diterima: **20–500**. |
| `TXLVL 90` | Mengatur level audio/modulasi AFSK TX. Nilai yang diterima: **10–100**. |
| `BSEC 600` | Mengatur interval beacon dalam **detik**. Nilai yang diterima: **30–86400 detik**. Contoh `600` = 10 menit. |
| `DDELAY 150` | Mengatur **delay digipeater sebelum retransmit**, dalam milidetik. Nilai: **0–3000 ms**. |
| `DUPSEC 30` | Mengatur waktu penyaringan **packet duplikat**, dalam detik. Nilai: **5–300 detik**. |
| `FILTER i/30/0` | Mengatur filter lokal untuk **IS → RF**. Format `i/menit/hop`. Contoh `i/30/0` berarti station harus pernah terdengar melalui RF dalam 30 menit terakhir dengan maksimum 0 hop, yaitu terdengar langsung. |
| `ALIAS WIDE1,WIDE2` | Mengatur alias yang diterima oleh digipeater. Contoh umum: `WIDE1,WIDE2`. Panjang maksimum konfigurasi alias 40 karakter. |
| `RFPATH WIDE2-1` | Mengatur path untuk **beacon RF perangkat**. |
| `RFPATH DIRECT` | Mengatur beacon RF dikirim **tanpa digipeater path**. |
| `ISPATH WIDE2-1` | Mengatur path RF yang digunakan saat perangkat meneruskan packet **APRS-IS → RF**. |
| `ISPATH DIRECT` | Mengatur packet **APRS-IS → RF** dikirim langsung tanpa digipeater path. |
| `COMMENT teks` | Mengubah comment APRS perangkat. Maksimum **100 karakter**. Contoh: `COMMENT NUSA APRS IGATE`. |
| `TRK ON` / `TRK OFF` | Mengaktifkan atau mematikan **GPS Tracker REV1T** tanpa mengubah status Digi/iGate/IS→RF. |
| `GPS` | Meminta status GPS singkat. Contoh balasan: `FIX=1 SAT=10 SPD=45 CRS=128`; tanpa fix: `FIX=0 SAT=3`. |
| `TMODE FIX` | Menggunakan mode tracker **Fixed Interval**. |
| `TMODE SMART` | Menggunakan mode **SmartBeaconing**. |
| `TSEC 120` | Mengatur interval Fixed tracker. Range **30–3600 detik**. |
| `TPATH WIDE2-1` | Mengatur path RF khusus tracker. |
| `TPATH DIRECT` | Mengatur tracker RF direct tanpa digipeater path. |
| `TOUT RF` | Tracker mengirim ke RF saja. |
| `TOUT IS` | Tracker mengirim ke APRS-IS saja. |
| `TOUT BOTH` | Tracker mengirim ke RF dan APRS-IS. |
| `TBCN` | Meminta **manual tracker beacon**. Position Mode GPS memerlukan fresh fix; AUTO boleh memakai static fallback yang valid. RF tetap masuk TX queue/CSMA. |
| `OLED ON` / `OLED OFF` | Mengaktifkan atau mematikan display OLED. |
| `SAVE` | Menyimpan semua perubahan remote ke **NVS**, sehingga tetap berlaku setelah restart/power-off. |
| `CANCEL` | Membatalkan perubahan remote yang belum di-`SAVE` dan mengembalikan konfigurasi tersimpan terakhir. |
| `REBOOT` | Me-restart ESP32 secara remote setelah balasan `OK REBOOT` diantrikan untuk dikirim. |

### Contoh command Tracker REV1T

```text
GPS
TRK ON
TMODE SMART
TPATH WIDE2-1
TOUT BOTH
SAVE
```

`TRK`, `TMODE`, `TSEC`, `TPATH` dan `TOUT` adalah perubahan konfigurasi remote: tanda `*` pada reply berarti perubahan belum `SAVE`. `TBCN` hanya meminta beacon saat itu dan tidak mengubah konfigurasi.

### Arti singkatan pada balasan `STAT`

Contoh bentuk balasan:

```text
UP=12H RX=245 D=1 I=1 IS=1 H=143K
```

- `UP` = uptime perangkat.
- `RX` = jumlah frame RF yang sudah diterima.
- `D=1` = digipeater ON, `D=0` = OFF.
- `I=1` = iGate RF→IS ON, `I=0` = OFF.
- `IS=1` = sedang terhubung ke APRS-IS, `IS=0` = tidak terhubung.
- `H` = free heap ESP32 dalam KB.

### Arti singkatan pada balasan `GET`

Contoh:

```text
D=1 I=1 R=1 Q=55 G=0 T=90 B=600 *
```

- `D` = DIGI.
- `I` = IGATE RF→IS.
- `R` = IS→RF.
- `Q` = RX Squelch ADC.
- `G` = RX Decoder Gain.
- `T` = TX Level.
- `B` = beacon interval dalam detik.
- `*` = ada perubahan konfigurasi yang **belum disimpan**.

## Contoh penggunaan

Mematikan digipeater sementara:

```text
DIGI OFF
```

Jika hasil sudah sesuai dan ingin dibuat permanen:

```text
SAVE
```

Mengembalikan perubahan yang belum disimpan:

```text
CANCEL
```

Mengubah beacon menjadi setiap 10 menit, beacon RF aktif, dan path WIDE2-1:

```text
BSEC 600
RFBCN ON
RFPATH WIDE2-1
SAVE
```

Perubahan remote berlaku di RAM terlebih dahulu. Gunakan `SAVE` agar permanen di NVS. `CANCEL` mengembalikan nilai remote-configurable ke konfigurasi tersimpan.

Admin callsign, callsign perangkat, Wi-Fi credential dan GPIO kritis tidak dapat diganti melalui RF Remote.

## Security

Whitelist callsign+SSID adalah kontrol operasional, **bukan autentikasi kriptografi**. Source callsign APRS dapat dipalsukan oleh pemancar lain.
