# Build, Storage And Verification

## Build

Windows PowerShell or PowerShell 7 on Linux, SDCC with MCS-51 support, its
sdas8051/packihx helpers, Python 3, and uCsim `s51` 0.9.9 are used. Ubuntu's
older bundled simulator is not suitable for these test commands; CI installs
the upstream 0.9.9 package with a pinned SHA-256. Schema integration tests additionally use
the neighboring BuddySoftware checkout's pure protocol module, supplied with
`-DesktopSource` if it is elsewhere. This is a test dependency, not a firmware
source/build dependency. The build creates local helper shims when
a Windows SDCC archive has `cc1` without `.exe`. No newer-carcomputer source,
network download or original firmware binary is required to build.

```powershell
.\firmware\build.ps1 -Sdcc 'C:\Program Files\SDCC\bin\sdcc.exe' -Packihx 'C:\Program Files\SDCC\bin\packihx.exe'
.\tools\verify_all.ps1 -DesktopSource F:\Github\BuddySoftware
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
BMS input frame with CRC and the byte-for-byte schema Status response. They also check all
four linked interrupt vectors, the UART receive ring and its overflow fault.
A sampled stack-tail
canary is checked; this is not an exhaustive worst-case stack proof.

Version 0.2.0 adds native LF/CRLF parsing, IDENTIFY build/digest binding,
non-blocking output, aggregate record counts, stale-data omission, unknown
feedback, session event wrap, read-only EEPROM-transaction checks, and the
actual BuddySoftware validator/parser/presenter. Reports cache storage status
without reading either EEPROM. Slot diagnostics distinguish erased, invalid
and I/O-failed storage without changing the persisted layout or boot policy.
The matching immutable protocol.json and transcripts accompany each new HEX.
Use `tools/make_release.ps1` to generate `releases/<build_id>` with the same
dated version used by ID/IDENTIFY and R2. An existing release's HEX/protocol
bytes cannot be replaced with different bytes under the same build ID.
Existing v0.1.0, v0.2.0 and v0.2.1 artifacts are not overwritten. `-OutputDirectory PATH`
keeps CI output separate from committed releases.

Version 0.2.2 adds regression checks for RX-ring overflow tails, deliberate
recovery at LF/hash/dollar boundaries, and valid fault clearing only after a
fresh boundary. Service-command tests exercise charger, ignition, pending
speed pulses and active drive-output interlocks, checking the numeric error
and zero EEPROM transactions. Actual PC live decoding is checked for lithium
pack voltage, current and the large original-format odometer, not just SOC.
The report schemas, settings layout and charging/drive defaults are unchanged.

## GitHub Build And R2 Publishing

`.github/workflows/lithium.yml` compiles and tests on GitHub-hosted Ubuntu 24.04;
it does not wait for a self-hosted runner. Pushes to main, lithium-tag pushes,
pull requests touching lithium, and manual runs produce downloadable artifacts.
Artifacts include the operational and SAFE HEX images, byte-matched protocol,
flash-space report, source hashes and simulator/contract verification results.

CI uses `-SkipDesktop` explicitly because the PC's new CC2 integration may not
yet be published. Linked core/integration tests, offline schema tests and host
release tests still run. Desktop checks are marked skipped, never passed.
Locally, omit that switch and supply `-DesktopSource` to test the actual PC
parser, presenter, identity merge and profile decoder as well.

Publish only by pushing a `lithium-v*` tag, or running the workflow on main with
`publish_r2` checked. Main pushes alone do not publish. There are no GitHub
Releases. Configure these repository Actions secrets:

| Secret | Purpose |
|---|---|
| R2_ACCESS_KEY_ID | R2 S3 API access key |
| R2_SECRET_ACCESS_KEY | R2 S3 API secret |
| R2_BUCKET | Existing firmware bucket |
| R2_ENDPOINT_URL | HTTPS S3 endpoint for the R2 account |

The only writable prefix is `buddy/old-cc-firmware/lithium`. The PC reads
`https://filer.poldentek.no/buddy/old-cc-firmware/lithium/manifest.json`.
The publisher creates a schema-2 channel manifest with matching dated build
IDs, `<build-id>.hex`, protocol SHA-256 and XC878/LITHIUM identity. Firmware
version, IDENTIFY, `$ID#`, build metadata and the download version are identical.
The format is `YYYYMMDD-HHmmss.GITHUB_RUN_NUMBER` in Europe/Oslo, matching the
newer firmware and PC software publisher; local builds use run 0. No separate
semantic firmware version is assigned to new builds. The development/audit HEX
names remain `CC2_LITHIUM.hex` and `CC2_LITHIUM_SAFE.hex`; R2 uses the dated
normal download filename.

Versioned HEX, protocol and release metadata are immutable: an existing build
ID with different bytes aborts. Every object is downloaded again and compared
before latest is published. Previous release history is merged from R2; only
HTTP 404 means an absent manifest, not access failure. Uploads never delete
objects or touch NiMH/new-carcomputer channels. The latest manifest is written
last, after its immutable assets. SAFE remains an engineering artifact, not
the automatic normal-firmware download. A deliberately older build cannot
silently roll the channel backwards.

Publication jobs are serialized by channel and latest-manifest writes use an
atomic ETag precondition, so a concurrent history change aborts publication
instead of overwriting it. Missing credentials or upload
failures leave the build artifact available; they do not claim a successful
R2 publication. Actual cloud uploads and vehicle testing are not simulator tests.

EEPROM transports are mocked in policy/storage tests. C52 simulation does not
model XC878 paged SFRs, the real watchdog, RS232 electronics, relay contacts,
I2C ACK behavior or physical EEPROM power loss. Those require bench tests.
No successful physical flash, vehicle operation or charge cycle is claimed
by these reports.
