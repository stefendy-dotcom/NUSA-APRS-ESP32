# Adjustment RX/TX — Bahasa Indonesia

## RX
1. Set RX Decoder Gain = 0 (AUTO).
2. Pot RX VR1 sekitar 75% sebagai titik awal.
3. Pastikan frekuensi benar-benar sepi.
4. Lihat RF CHANNEL pada Web Monitor.
5. Naikkan RX Squelch ADC perlahan sampai CLEAR stabil.
6. Uji packet APRS nyata: harus BUSY saat packet dan tetap terdecode.
7. Bila packet lemah hilang, turunkan sedikit; bila false BUSY saat kosong, naikkan sedikit.

Data lapangan: pada satu instalasi NUSA, **135** memberi CLEAR stabil. Ini bukan nilai universal.

Digital decoder gain bekerja setelah ADC dan tidak memperbaiki clipping analog.

Sesuai [skema RF](../hardware/skema.jpeg), jalur RX adalah J1 → VR1 10K → C1 1µF → R1 1K → GPIO36. Bias GPIO36 menggunakan **R2 100K ke 3V3 + R3 100K ke GND**, dengan C2 2n2 ke GND dan titik bias sekitar 1,65 V.

## TX
Jalur TX sesuai skema adalah GPIO26 → R6 1K → C3 1µF → VR2 10K → MIC radio.

Mulai TX Level 90% dan VR2 rendah-menengah. Pantau dengan decoder APRS independen. Naikkan hanya sampai decode konsisten tanpa over-deviation. Konfirmasi retransmit Digi dari receiver/iGate lain.

## Bila BUSY terus
Atur RX Squelch ADC terlebih dahulu, bukan Decoder Gain. Jika tetap BUSY pada squelch sangat tinggi, cabut audio SPK. Bila menjadi CLEAR, sumber noise berasal dari radio/audio. Bila tetap BUSY, cek bias GPIO36, grounding, **R2/R3 100K/100K**, C2 2n2, C1 dan R1.
