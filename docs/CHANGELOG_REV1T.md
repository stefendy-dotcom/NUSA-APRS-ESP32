# Changelog — REV1S → REV1T

## Added

- UART NMEA GPS tracker.
- NEO-6M / NEO-M8N / NEO-M10N compatible RMC/GGA parsing.
- Configurable GPS RX/TX GPIO and baud 9600/38400/115200.
- GPS states: NO GPS, GPS SEARCH, GPS FIX, GPS STALE.
- GPS stale timeout.
- Position modes STATIC, GPS and AUTO.
- Dedicated tracker APRS symbol/comment/path.
- Tracker output RF / APRS-IS / BOTH.
- Fixed Interval tracker scheduler.
- SmartBeacon speed-dependent interval.
- Corner pegging.
- Stationary-speed protection.
- Minimum automatic tracker TX interval.
- Manual tracker beacon from Web UI and RF Remote Admin.
- Standard APRS course/speed and optional altitude.
- GPS Tracker OLED page.
- Live GPS/tracker Web status and counters.
- Tracker diagnostics: RF/IS count, drop count, NMEA/fix counters and trigger reason.
- RF Remote commands: TRK, GPS, TMODE, TSEC, TPATH, TOUT, TBCN.
- NVS migration keys for REV1T with tracker default OFF.
- GPS documentation in Indonesian and English.

## Preserved

- REV1S AFSK receive engine.
- REV1S AX.25 implementation.
- RF → APRS-IS iGate.
- Digipeater.
- IS→RF message gating.
- RF/IS site beacon.
- Web configuration.
- OLED SH1106/SSD1306 support.
- RF weather and last-heard pages.
- RX WAV diagnostics.
- Existing RF Remote Admin commands and whitelist behavior.

## Explicitly unchanged

AFSK.cpp/h, AX25.cpp/h, CRC-CCIT.cpp/h, FIFO.h and HDLC.h are byte-identical to REV1S.

## Safety / compatibility defaults

- Tracker OFF after upgrade unless explicitly enabled.
- AUTO position never treats untouched static 0,0 as a valid fallback.
- GPS UART is disabled on GPIO conflict.
- No GPS fix waiting loop.
- GPS processing is bounded to 96 serial bytes per main-loop iteration.
- Tracker RF always uses the existing TX queue and CSMA.
