# RX/TX Adjustment — English

## RX
1. Set RX Decoder Gain = 0 (AUTO).
2. Start RX pot around 75%.
3. Ensure the channel is truly idle.
4. Watch RF CHANNEL in Web Monitor.
5. Raise RX Squelch ADC gradually until CLEAR is stable.
6. Test real APRS packets: BUSY during packet and valid decode.
7. If weak packets disappear, reduce squelch slightly; if false BUSY remains, increase slightly.

Field result: one NUSA installation reached stable CLEAR at **135**. This is not universal.

Digital decoder gain acts after ADC and cannot recover analog clipping.

## TX
Start at TX Level 90% with conservative TX pot. Verify with an independent APRS decoder. Increase only enough for reliable decoding without over-deviation. Confirm actual Digi retransmission at another receiver/iGate.

## Permanently BUSY
Adjust RX Squelch ADC first, not Decoder Gain. If BUSY remains at unusually high values, disconnect RX audio. CLEAR after disconnection points to radio/audio noise; continued BUSY points to the GPIO36 bias/ground network.
