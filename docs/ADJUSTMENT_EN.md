# RX/TX Adjustment — English

## RX
1. Set RX Decoder Gain = 0 (AUTO).
2. Start RX pot VR1 around 75%.
3. Ensure the channel is truly idle.
4. Watch RF CHANNEL in Web Monitor.
5. Raise RX Squelch ADC gradually until CLEAR is stable.
6. Test real APRS packets: BUSY during packet and valid decode.
7. If weak packets disappear, reduce squelch slightly; if false BUSY remains, increase slightly.

Field result: one NUSA installation reached stable CLEAR at **135**. This is not universal.

Digital decoder gain acts after ADC and cannot recover analog clipping.

According to the [RF schematic](../hardware/skema.jpeg), the RX path is J1 → VR1 10 kΩ → C1 1 µF → R1 1 kΩ → GPIO36. GPIO36 bias uses **R2 100 kΩ to 3V3 + R3 100 kΩ to GND**, with C2 2.2 nF to GND and a bias point of about 1.65 V.

## TX
The schematic TX path is GPIO26 → R6 1 kΩ → C3 1 µF → VR2 10 kΩ → radio MIC.

Start at TX Level 90% with conservative VR2 setting. Verify with an independent APRS decoder. Increase only enough for reliable decoding without over-deviation. Confirm actual Digi retransmission at another receiver/iGate.

## Permanently BUSY
Adjust RX Squelch ADC first, not Decoder Gain. If BUSY remains at unusually high values, disconnect RX audio. CLEAR after disconnection points to radio/audio noise; continued BUSY points to the GPIO36 bias/ground network. Check **R2/R3 100 kΩ/100 kΩ**, C2 2.2 nF, C1 and R1.
