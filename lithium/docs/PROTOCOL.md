# PoldenTEK Universal BMS Adapter Protocol v1

The future adapter translates its particular BMS into this common CC2 format.
No vendor-specific BMS protocol is built into the carcomputer. Link is full
duplex 19200 baud, 8N1 on the existing service UART. P1.0 RX/P1.1 TX are MCU
signals; use the board's verified service-level interface or a proper RS232
transceiver. Never connect RS232 voltage directly to MCU pins. Only one
transmitter may drive CC RX: an adapter/service host must arbitrate access.

## Framing And CRC

```text
$BODY*HHHH#
```

BODY is ASCII, comma-separated, case-sensitive. HHHH is four uppercase hex
digits of CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, no reflection,
no final XOR. CRC covers BODY only, excluding dollar, star, CRC and hash.
Check vector `123456789` produces `29B1`. Optional CR/LF is outside the frame.
Integers use decimal without spaces or plus signs. Leading minus is allowed
only on signed fields. The complete input frame must fit 145 bytes including
delimiters, with no more than 143 characters between dollar and hash.
Finish a frame within 250 ms; dollar restarts input framing.

CRC detects accidental corruption, not malicious commands or spoofing. This
is a trusted local service link, not authenticated remote control.

## Adapter To CC

```text
$BMS,1,seq,flags,cells,pack_cV,cell_min_mV,cell_max_mV,0,temp_min_dC,temp_max_dC,current_dA,soc10*HHHH#
```

| Field | Encoding |
|---|---|
| 1 | Protocol version, exactly 1 |
| seq | Unsigned 16-bit sequence, increment once for each fresh sample |
| flags | Decimal bit mask below |
| cells | Series cell count, 1..96 |
| pack_cV | Unsigned pack voltage in 0.01 V |
| cell_min_mV, cell_max_mV | Actual lowest/highest cell measurements in mV |
| 0 | Reserved field; must be zero in v1 |
| temp_min_dC, temp_max_dC | Signed lowest/highest relevant pack temperatures, 0.1 C |
| current_dA | Signed 0.1 A; positive charging, negative discharging |
| soc10 | SOC in 0.1 percent, 0..1000; 65535 if unavailable |

Flags: 1 charge permitted, 2 drive permitted, 4 charge complete, 8 BMS fault,
16 all electrical/temperature measurement fields valid. SOC may separately be
unknown (65535). Bit 16 is required; unknown bits are rejected.
Permission must reflect all pack-level BMS protections, not just SOC. Do not
invent cell extrema from a grouped voltage tap or a pack average.

Known CRC example, using synthetic bench data, not recommended pack limits:

```text
$BMS,1,1,19,24,7920,3290,3310,0,200,250,100,500*4EDB#
```

This represents 24 cells, 79.20 V, 3.290..3.310 V cells, 20.0..25.0 C,
10.0 A into the battery, 50.0% SOC and both permissions.

Send fresh samples every 200 ms; freshness expires at **age >=1500 ms**.
Duplicates and backwards sequences do not refresh the timer. Sequence rollover
65535 to 0 is valid; a forward difference must be 1..32767 modulo 65536.
After an adapter sequence reset, use the idle-only BMS,RESET service command.
Do not keep incrementing sequence numbers on old BMS measurements.

The CC rejects impossible field ordering/ranges and pack voltage inconsistent
with `cells * min..max` (20 mV rounding allowance). These are packet checks,
not a substitute for BMS protection or configured operating limits.

OFF and EXTERNAL do not become SUPERVISED merely because packets arrive.
EXTERNAL deliberately ignores their charge/drive permissions and completion.

## CC To Adapter

Every accepted BMS packet, and STATUS/REPORT requests, returns:

```text
$CC,1,seq,uptime_ms,charge_mode,drive_mode,charge_state,charge_reason,faults,drive_state,outputs,speed_kmh,soc10,bms_age_ms,drive_reason*HHHH#
```

Sequence echoes the last accepted packet; uptime is unsigned 32-bit ms.
Modes/states/reasons/fault bits are in [control behavior](BEHAVIOR.md).
Outputs are MCU output-latch requests: 1 charge, 2 motor, 4 footswitch,
8 reverse-direction relay, 16 fan. They are not contactor-feedback measurements.
SOC 65535 means unknown/stale; age 4294967295 means no packet received.
Drive states are 0 OFF, 1 start delay, 2 motor wait, 3 running, 4 wait stop,
5 direction deadtime. There is no unsolicited automatic stream before an
adapter starts sending packets; each fresh packet gives one status response.

The adapter should also treat a missing CC response as a link failure in its
own policy. No claim of end-to-end safety certification is made by this CRC
and heartbeat scheme.

## Tools

`tools/bms_protocol.py` exports `BmsStatus.encode()` and `decode_cc()` for an
adapter implementation. It also encodes frames and decodes status at a terminal:

```powershell
python .\tools\bms_protocol.py --sequence 1 --flags 19 --cells 24 --pack-cv 7920 --cell-min-mv 3290 --cell-max-mv 3310 --temp-min-dc 200 --temp-max-dc 250 --current-da 100 --soc10 500
```

That command prints a frame only; it does not connect to or energize a device.
Ordinary service commands are unchecksummed `$COMMAND#` frames, separate from
the checksummed BMS telemetry schema. Unknown/malformed input gets an ERR
response and does not update freshness.
