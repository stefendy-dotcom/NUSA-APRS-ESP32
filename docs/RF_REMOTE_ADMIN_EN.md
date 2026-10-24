# RF Remote Admin — English

REV1S accepts short configuration commands through **APRS Messages received directly over RF**.

## Authorization

The Web UI stores two administrator identities:

- Admin Callsign 1
- Admin Callsign 2

Matching is **exact callsign+SSID**. If `YF9UAG-9` is configured, `YF9UAG` and `YF9UAG-10` are not administrators.

Packets from any other source:

- do not execute the command;
- receive no admin ACK;
- receive no ERR response;
- cause no RF reply transmission;
- may only be logged locally as denied/silent.

Commands arriving through APRS-IS are not used as RF Remote Admin commands. This control path only uses APRS messages successfully decoded from RF.

## How to send a command

Send the command as an **APRS Message addressed to the NUSA APRS device callsign**. The message body only needs to contain one of the short commands below.

Examples:

```text
STAT
DIGI ON
RXGAIN 0
RFPATH WIDE2-1
SAVE
```

If the transmitting APRS application or radio includes a standard APRS message number, the device also sends the normal APRS ACK first.

## Short commands and their functions

| Command | Function |
|---|---|
| `STAT` | Shows a compact device status: uptime, number of received RF packets, DIGI status, IGATE status, APRS-IS connection status, and free heap. |
| `GET` | Shows the current main operational settings in compact form: DIGI, IGATE, IS→RF, RX squelch, RX decoder gain, TX level, and beacon interval. A trailing `*` means there are changes that have not yet been saved. |
| `DIGI ON` / `DIGI OFF` | Enables or disables the **APRS digipeater** function. |
| `IGATE ON` / `IGATE OFF` | Enables or disables the **RF → APRS-IS iGate** function. |
| `ISRF ON` / `ISRF OFF` | Enables or disables **APRS-IS → RF** gating. |
| `RFBCN ON` / `RFBCN OFF` | Enables or disables the device's **position beacon over RF**. |
| `ISBCN ON` / `ISBCN OFF` | Enables or disables the device's **position beacon directly to APRS-IS**. |
| `RXSQL 135` | Sets the **RX Squelch ADC** threshold. Accepted range: **5–1000**. Example: `RXSQL 55`. |
| `RXGAIN 0` | Sets **RX Decoder Gain**. `0` = AUTO. Accepted manual values: **20–500**. |
| `TXLVL 90` | Sets the AFSK TX audio/modulation level. Accepted range: **10–100**. |
| `BSEC 600` | Sets the beacon interval in **seconds**. Accepted range: **30–86400 seconds**. Example: `600` = 10 minutes. |
| `DDELAY 150` | Sets the **digipeater retransmit delay** in milliseconds. Accepted range: **0–3000 ms**. |
| `DUPSEC 30` | Sets the **duplicate-packet suppression window** in seconds. Accepted range: **5–300 seconds**. |
| `FILTER i/30/0` | Sets the local **IS → RF heard filter**. Format: `i/minutes/hops`. Example: `i/30/0` means the station must have been heard over RF within the last 30 minutes with a maximum of 0 used digipeater hops, i.e. heard directly. |
| `ALIAS WIDE1,WIDE2` | Sets the aliases accepted by the digipeater. A common example is `WIDE1,WIDE2`. Maximum alias configuration length is 40 characters. |
| `RFPATH WIDE2-1` | Sets the path used by the device's **RF beacon**. |
| `RFPATH DIRECT` | Sends the RF beacon **without a digipeater path**. |
| `ISPATH WIDE2-1` | Sets the RF path used when forwarding packets from **APRS-IS → RF**. |
| `ISPATH DIRECT` | Sends **APRS-IS → RF** packets directly without a digipeater path. |
| `COMMENT text` | Changes the device APRS comment. Maximum length: **100 characters**. Example: `COMMENT NUSA APRS IGATE`. |
| `OLED ON` / `OLED OFF` | Enables or disables the OLED display. |
| `SAVE` | Saves all remote changes to **NVS**, so they remain active after restart or power-off. |
| `CANCEL` | Cancels unsaved remote changes and restores the last saved configuration. |
| `REBOOT` | Remotely restarts the ESP32 after the `OK REBOOT` reply has been queued for RF transmission. |

### Meaning of the `STAT` reply

Example:

```text
UP=12H RX=245 D=1 I=1 IS=1 H=143K
```

- `UP` = device uptime.
- `RX` = number of RF frames received.
- `D=1` = digipeater ON, `D=0` = OFF.
- `I=1` = RF→IS iGate ON, `I=0` = OFF.
- `IS=1` = connected to APRS-IS, `IS=0` = disconnected.
- `H` = ESP32 free heap in KB.

### Meaning of the `GET` reply

Example:

```text
D=1 I=1 R=1 Q=55 G=0 T=90 B=600 *
```

- `D` = DIGI.
- `I` = RF→IS IGATE.
- `R` = IS→RF.
- `Q` = RX Squelch ADC.
- `G` = RX Decoder Gain.
- `T` = TX Level.
- `B` = beacon interval in seconds.
- `*` = configuration changes are present but have **not yet been saved**.

## Usage examples

Temporarily disable the digipeater:

```text
DIGI OFF
```

If the result is correct and should be made permanent:

```text
SAVE
```

Discard unsaved remote changes:

```text
CANCEL
```

Set the beacon interval to 10 minutes, enable the RF beacon, and use WIDE2-1:

```text
BSEC 600
RFBCN ON
RFPATH WIDE2-1
SAVE
```

Remote changes take effect in RAM first. Use `SAVE` to commit them to NVS. `CANCEL` restores the last saved remote-configurable values.

Administrator identities, station callsign, Wi-Fi credentials, and critical GPIO assignments cannot be changed through RF Remote.

## Security

Exact callsign+SSID whitelisting is an operational control, **not cryptographic authentication**. APRS source callsigns can be spoofed.
