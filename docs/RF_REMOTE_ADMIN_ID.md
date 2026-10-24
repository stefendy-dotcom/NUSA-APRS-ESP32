# RF Remote Admin — Bahasa Indonesia

REV1S dapat menerima command konfigurasi melalui **APRS Message yang diterima langsung dari RF**.

## Otorisasi
Di Web UI tersedia dua field admin:
- Admin Callsign 1
- Admin Callsign 2

Pencocokan dilakukan **persis callsign+SSID**. Contoh: bila admin adalah YF9UAG-9, maka YF9UAG dan YF9UAG-10 tidak dianggap admin.

Packet dari callsign selain dua admin:
- tidak menjalankan command;
- tidak mendapat ACK admin;
- tidak mendapat ERR;
- tidak memicu TX balasan RF;
- hanya dapat dicatat di log lokal sebagai denied/silent.

Command dari APRS-IS tidak dipakai untuk remote admin; jalur ini khusus frame RF yang berhasil didecode.

## Command pendek

- STAT
- GET
- DIGI ON / DIGI OFF
- IGATE ON / IGATE OFF
- ISRF ON / ISRF OFF
- RFBCN ON / RFBCN OFF
- ISBCN ON / ISBCN OFF
- RXSQL 135
- RXGAIN 0
- TXLVL 90
- BSEC 600
- DDELAY 150
- DUPSEC 30
- FILTER i/30/0
- ALIAS WIDE1,WIDE2
- RFPATH WIDE2-1
- RFPATH DIRECT
- ISPATH WIDE2-1
- ISPATH DIRECT
- COMMENT teks
- OLED ON / OLED OFF
- SAVE
- CANCEL
- REBOOT

Perubahan remote berlaku di RAM. Gunakan SAVE agar permanen di NVS. CANCEL mengembalikan nilai remote-configurable ke konfigurasi tersimpan.

Admin callsign, callsign perangkat, Wi-Fi credential dan GPIO kritis tidak dapat diganti melalui RF Remote.

## Security
Whitelist callsign+SSID adalah kontrol operasional, **bukan autentikasi kriptografi**. Source callsign APRS dapat dipalsukan oleh pemancar lain.
