# Build, Storage And Verification

## Build

Windows PowerShell, SDCC with MCS-51 support, its sdas8051/packihx helpers,
Python 3, and uCsim `s51` are used. The build creates local helper shims when
a Windows SDCC archive has `cc1` without `.exe`. No newer-carcomputer source,
network download or original firmware binary is required to build.

```powershell
.\firmware\build.ps1 -Sdcc 'C:\Program Files\SDCC\bin\sdcc.exe' -Packihx 'C:\Program Files\SDCC\bin\packihx.exe'
.\tools\verify_all.ps1
```

Use `-SafeBringup` for the loads-disabled image. P-Flash is linked below F000,
with no D-Flash payload. XRAM variables use F600..FBFF; the stack uses internal
RAM. Build output and `build_info.json` state actual programmed/free space and
the image SHA-256. Do not flash the simulator-only test image.

The core protocol and terminal parser workspaces are foreground-only XRAM
buffers, deliberately not large nested IRAM stack arrays. Interrupt handlers
do not call the parser. UART reception has a 128-byte ring and overflow latches
a fault. Main-loop control is serviced ahead of another batch of input bytes.

## Keep Original EEPROM Data

Boot only reads; it does not erase, format or repair either EEPROM.
The lithium supervisor does not reinterpret the old NiMH capacity, chemistry,
temperature-calibration or balancing settings. VIN, battery identifiers,
serial numbers and unowned original bytes remain as they were.

The useful motion records remain in their original formats:

| Field | Display 24C02 | VMS AT25128A | Format |
|---|---|---|---|
| Odometer | 0000..0004 | 0000..0004 and 0100..0104 | u32 big-endian, km x10, then zero-sum checksum |
| Speed calibration | 00CF..00D1 | 00CF..00D1 and 01CF..01D1 | u16 big-endian pulses/100 m, then zero-sum checksum |

For example, 123456 km is `00 12 D6 80 98`. The payload is 1234560 tenths;
all five bytes sum to zero modulo 256. This matches the recorded original
NKM transaction for both normal and Classic layouts. The shorter-looking
odometer range in older overview tables must not be treated as a 24-bit value.

Odometer startup selects the **largest checksum-valid value** among the three
copies. That is an intentional new recovery policy to avoid rollback after
an interrupted save, not a claim of original boot-order equivalence. Ties
prefer VMS primary, then VMS mirror, then dashboard. No valid copy means RAM
zero with source 0; automatic persistence stays disabled so a failed read
cannot overwrite recoverable mileage with zero. There is no terminal command
that changes the odometer in this version.

Changed mileage is saved after 1 km, after 60 seconds with changes, or when
stopped with ignition off. One record is verified per service visit, in original
display -> VMS mirror -> VMS primary order. The committed value advances only
after all three succeed. A failed write waits five seconds before retrying.
An abrupt power cut may lose distance since the last save; this is not a
power-fail journal or capacitor-backed shutdown.

Speed calibration loads dashboard -> primary -> mirror, skipping invalid/zero
records; otherwise it uses the original 1620 pulses/100 m default. SPEEDCAL
explicitly writes and verifies dashboard -> primary -> mirror. Its active RAM
value changes only after all writes verify. Trip remains RAM-only.

`LEGACY` reports odometer source (0 none, 1 primary, 2 mirror, 3 dashboard),
speed-calibration source (0 default, 1 dashboard, 2 primary, 3 mirror),
committed odometer in tenths, pending write phase, and failures.

## Separate Lithium Settings

New 48-byte records are written only by CONFIG,SAVE, at VMS 1000 and 1040.
These addresses do not overlap the odometer, identity or calibration records
above. They are **not claimed universally unused** by original firmware;
back up the original VMS and dashboard EEPROMs before installing/configuring
new firmware. Legacy log preservation is not a goal of this version.

Record bytes: 0..3 `CCLI`, 4 version 1, 5 length 48, 6..9 LE32 sequence,
10 charge mode, 11 drive mode, 12..13 fan-on dC, 14..15 fan-off dC,
16 cell count, 17 reserved, 18..19 cell minimum mV, 20..21 cell maximum mV,
22..23 charge minimum dC, 24..25 charge maximum dC, 26..27 drive minimum dC,
28..29 drive maximum dC, 30..31 charge maximum dA, 32..33 discharge maximum dA,
34..35 maximum charge seconds, 36..43 reserved, 44..45 CRC of bytes 0..43,
46 reserved, 47 commit A5. Multi-byte fields are little-endian here, unlike
the preserved legacy records. Signed temperatures use two's complement.

Save targets the alternate slot: invalidate commit byte, write/verify payload
and CRC, then commit/verify A5 last. The previous slot is untouched. Boot
selects the newer valid sequence modulo 2^32, with the usual half-range rule.
Equal sequences prefer slot A. Invalid settings cannot select SUPERVISED.
Source 0 is erased/default, 1 A, 2 B, 3 unreadable/non-erased invalid storage
with both modes OFF. Boot never automatically writes defaults.

RAM events keep the latest eight state/reason/fault transitions, oldest first.
Legacy history and event formats are not imported or updated. No full EEPROM
erase or raw write command is included.

## What Tests Establish

`verify_all.ps1` builds both flashable profiles and a simulator-only harness.
The harness executes linked SDCC code, not a Python copy of the control logic.
Tests cover FORCE defaults, supervisory limits and inclusive boundaries,
freshness/sequence rollover, completion, fault recovery, CRC corruption,
integer overflow, every byte boundary of interrupted settings saves, corrupt
records, legacy odometer-copy selection, original checksum/byte order,
incremental mileage persistence and speed calibration.

Integration tests execute the actual normal/SAFE linked images with UART
register reads injected and TX captured: output-latch decisions, raw ignition/charger interlocks,
the timer-ISR lease cutoff, direction retention, mode/limit commands, a known
BMS frame and byte-for-byte CC response including CRC. They also check all
four linked interrupt vectors, the UART receive ring and its overflow fault.
A sampled stack-tail
canary is checked; this is not an exhaustive worst-case stack proof.

EEPROM transports are mocked in policy/storage tests. C52 simulation does not
model XC878 paged SFRs, the real watchdog, RS232 electronics, relay contacts,
I2C ACK behavior or physical EEPROM power loss. Those require bench tests.
No successful physical flash, vehicle operation or charge cycle is claimed
by these reports.
