# RF Remote Admin — English

REV1S accepts short configuration commands through **APRS Messages received directly over RF**.

## Authorization
The Web UI stores two administrator identities:
- Admin Callsign 1
- Admin Callsign 2

Matching is **exact callsign+SSID**. If YF9UAG-9 is configured, YF9UAG and YF9UAG-10 are not administrators.

Packets from any other source:
- do not execute the command;
- receive no admin ACK;
- receive no ERR response;
- cause no RF reply transmission;
- may only be logged locally as denied/silent.

Commands arriving through APRS-IS are not used as RF Remote Admin commands.

## Short commands

STAT, GET, DIGI ON/OFF, IGATE ON/OFF, ISRF ON/OFF, RFBCN ON/OFF, ISBCN ON/OFF, RXSQL n, RXGAIN n, TXLVL n, BSEC n, DDELAY n, DUPSEC n, FILTER text, ALIAS text, RFPATH path/DIRECT, ISPATH path/DIRECT, COMMENT text, OLED ON/OFF, SAVE, CANCEL, REBOOT.

Remote changes take effect in RAM. SAVE commits them to NVS. CANCEL restores the last saved remote-configurable values.

Administrator identities, station callsign, Wi-Fi credentials and critical GPIO assignments cannot be changed through RF Remote.

## Security
Exact callsign+SSID whitelisting is an operational control, **not cryptographic authentication**. APRS source callsigns can be spoofed.
