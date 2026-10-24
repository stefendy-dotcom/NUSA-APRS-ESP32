# Adjustment RX/TX — Bahasa Indonesia

## RX
1. Set RX Decoder Gain = 0 (AUTO).
2. Pot RX sekitar 75% sebagai titik awal.
3. Pastikan frekuensi benar-benar sepi.
4. Lihat RF CHANNEL pada Web Monitor.
5. Naikkan RX Squelch ADC perlahan sampai CLEAR stabil.
6. Uji packet APRS nyata: harus BUSY saat packet dan tetap terdecode.
7. Bila packet lemah hilang, turunkan sedikit; bila false BUSY saat kosong, naikkan sedikit.

Data lapangan: pada satu instalasi NUSA, **135** memberi CLEAR stabil. Ini bukan nilai universal.

Digital decoder gain bekerja setelah ADC dan tidak memperbaiki clipping analog.

## TX
Mulai TX Level 90% dan pot TX rendah-menengah. Pantau dengan decoder APRS independen. Naikkan hanya sampai decode konsisten tanpa over-deviation. Konfirmasi retransmit Digi dari receiver/iGate lain.

## Bila BUSY terus
Atur RX Squelch ADC terlebih dahulu, bukan Decoder Gain. Jika tetap BUSY pada squelch sangat tinggi, cabut audio SPK. Bila menjadi CLEAR, sumber noise berasal dari radio/audio. Bila tetap BUSY, cek bias GPIO36, grounding, 47K/47K dan 2n2.
