# BOM — Antarmuka RF sesuai skema.jpeg

Daftar berikut mengikuti komponen yang terlihat pada [skema.jpeg](skema.jpeg).

| Ref | Nilai / part | Fungsi |
|---|---|---|
| U1 | ESP32 DevKit 30-pin | MCU |
| J1 | Connector 2-pin | SPK / AUDIO RX masuk; pin 2 GND |
| J2 | Connector 2-pin | PTT radio; pin 2 GND |
| J3 | Connector 2-pin | MIC / AUDIO TX keluar; pin 2 GND |
| VR1 | Pot 10 kΩ | Level audio RX |
| VR2 | Pot 10 kΩ | Deviasi / level audio TX |
| R1 | 1 kΩ | Series RX menuju GPIO36 |
| R2 | 100 kΩ | Bias RX dari 3V3 |
| R3 | 100 kΩ | Bias RX ke GND |
| R4 | 1 kΩ | GPIO25 ke basis Q1 |
| R5 | 10 kΩ | Pull-down basis Q1 |
| R6 | 1 kΩ | Series TX dari GPIO26 |
| C1 | 1 µF elko | Coupling audio RX |
| C2 | 2.2 nF / 2n2 | Filter node RX/GPIO36 ke GND |
| C3 | 1 µF | Coupling audio TX |
| Q1 | 2N3904 NPN | Open-collector PTT |

## Jalur sesuai skema

**RX:** J1 → VR1 → C1 → R1 → GPIO36/VP. Node GPIO36 dibias sekitar **1,65 V** oleh R2 100K ke 3V3 dan R3 100K ke GND; C2 2n2 ke GND.

**PTT:** GPIO25 → R4 1K → basis Q1. R5 10K ke GND. Collector Q1 ke jalur PTT radio dan emitter ke GND. PTT radio aktif LOW melalui open collector.

**TX:** GPIO26 → R6 1K → C3 1µF → bagian atas VR2 10K; wiper VR2 → J3 pin 1 MIC OUT; kaki bawah VR2 → GND.

Untuk penggunaan umum pada TX, gambar menyarankan **kapasitor non-polar 1µF** bila input MIC radio mempunyai bias DC. Bila memakai elko, kutub + harus menghadap sisi dengan tegangan DC lebih tinggi.

## Di luar skema RF ini

Catu 5 V, MINI560, kapasitor supply, OLED, RX LED dan TX LED tidak digambar pada skema.jpeg; bagian tersebut mengikuti wiring/PCB unit masing-masing.
