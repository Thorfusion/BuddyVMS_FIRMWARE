# Desktop Report Schema

Lithium firmware uses the existing Buddy structured report system, not the old
custom `$CC` status packet. Reports are described by `protocol/protocol.json`
and the byte-identical copy shipped with the matching HEX release.

Native commands are uppercase ASCII terminated by LF; CRLF is accepted. Input
capacity is 143 characters excluding delimiters, with a 250 ms frame timeout.
Overflow/timeout discards the remainder through LF/hash or a fresh dollar,
never executing a suffix as a new command. `$COMMAND#` wrappers are compatibility
aliases. Only the wrapped `$ID#` retains `$ID,BUDDY-CC2-LITHIUM,<build_id>#`; native
`ID` is an alias for structured `IDENTIFY`. Normal report output never uses
that wrapper. There is no boot banner that can be mistaken for report data.

## Identify First

```text
IDENTIFY
@OK,IDENTIFY,1,CC2_LITHIUM,XC878,LITHIUM,<build_id>,<protocol_sha256>
```

This is a read-only text-valued command. Identity schema 1, product profile,
chemistry, MCU, build identifier and the 64-digit protocol digest are separate
from report schema, EEPROM version and persistent log version. Build IDs use
Europe/Oslo `YYYYMMDD-HHmmss.GITHUB_RUN_NUMBER`; local builds use run 0.
The build generates the digest from exact protocol-file bytes and embeds it.
Do not substitute a latest DA64 definition or infer chemistry from USB IDs.

The build ID is also the firmware version. Native ID/IDENTIFY, wrapped `$ID#`,
the release manifest and R2 versioned directory use the same value, matching
the newer firmware and PC software's dated release format. There is no separate
semantic version in new builds. Old v0.2.x releases remain readable: the PC
prefers their IDENTIFY build ID over their historical semantic `$ID#` value.
If both replies use dated build IDs but disagree, the PC rejects the identity.

The existing generic desktop validator/parser/presenter can load this JSON.
The firmware tests run actual compiled transcripts through those components.
When available, tests additionally use the desktop's profile-bound decoder,
live SOC presentation and both identification probe orders. GUI routing and
actual serial/update operations are not exercised. Do not run stock NiMH repair/SOC-setting policies on
this replacement or fall back to stock just because identification times out.

## Envelope And Capabilities

```text
@B,<report_id>,<schema_version>
@D,<report_id>,<field_id>,<values...>
@E,<report_id>,<data_record_count>
```

Only @D rows count toward @E. Integers are decimal except `_hex` masks, which
use hex without 0x. Booleans are 0/1. No labels, localized numbers or quoted CSV
occur inside machine records. Outcomes are `@OK,<command>,0` or
`@ERR,<command>,<numeric_error>`, never a substitute for a completed report.

| Command | Report | Schema | Actual available content |
|---|---:|---:|---|
| STATUS | 1 | 129 | Vehicle/charge state, speed, fault masks, permissions, age; SOC only when known |
| VERSION | 2 | 1 | Product 3 (CC2 lithium), config schema 1, persistent log schema 0 |
| TEMPS | 3 | 128 | Adapter pack min/max temperatures, not front/rear/box ADC data |
| ELECTRICAL | 4 | 128 | Adapter pack/cell extrema, current, SOC and freshness |
| CHARGEREPORT | 6 | 128 | Lithium modes, inhibit reasons, request and MCU latch |
| CONFIG | 7 | 128 | Effective settings, dirty/source state and configured limits |
| RELAYS | 8 | 128 | Requests/latches and raw digital inputs; feedback unavailable |
| FAULTS / EVENTS | 9 | 128 | Faults and eight-entry RAM session history |
| STORAGE | 13 | 128 | Separate VMS/display observations, settings slots and legacy records |
| LIFETIME | 15 | 128 | Preserved mileage, RAM trip and speed calibration |

REPORT emits exactly `2,1,3,4,6,7,8,9,13,15`, in that order, using one frozen
snapshot. Version schema 1 is reused unchanged. Lithium layouts 128/129 explicitly
does not reuse DA64 charge states, fault assignments or ADC units. Product 3
is distinct from DA64 product 1. Always key decoding by CC2_LITHIUM profile as
well as report/schema. Relay-counter field 1 and DA64 odometer-slot field 2
are not repurposed: desktop special handling must not relabel other data.

No motor CAN/INA229 hardware, individual on-board lithium cell measurement,
original six-block sample, persistent journal or full-charge log is advertised.
LOG, BMSREPORT, READJOURNAL and unimplemented histories return error 2, not a
plausible zero-filled report. EVENTS is RAM-only; uptime and its wrapping
16-bit sequence reset at reboot. Ordering metadata identifies oldest/newest
within this captured session. It does not imply calendar timestamps.

## Validity And Storage

With no fresh adapter sample, TEMPS contains availability only; ELECTRICAL
contains availability only. Their measurement rows are omitted. Status 129
omits its optional SOC row when unknown; it never publishes 255 as a percentage.
Valid SOC also appears as centipercent in Electrical
to preserve 0.1-percent adapter precision. No artificial front/rear/box values
or group voltages are substituted. Current is positive while charging and uses
0.1 A; cell values are mV and pack voltage is 0.01 V. No ADS8688 conversion is
used. The release JSON provides labels, enums, fault meanings and units.

Relay-applied bits are MCU output latches, not physical contact feedback. The
feedback-valid mask is zero and presentation metadata says unavailable rather
than Off. A CHG request cannot prove current flow or successful charging.

Storage reports never probe or write EEPROM. They expose cached boot-read
observations separately for the two devices. Slot states distinguish unknown,
erased, valid, invalid and I/O failure. Source and dirty flags distinguish
effective defaults/unsaved edits from a valid saved record. An I/O failure does
not assert that the EEPROM is blank. Reading reports does not initialize or
normalize settings, migrate records or alter SOC/mileage.

## Bounded Output And Recovery

The emitter stores one 160-byte line, two short outcome messages and a small
snapshot in XRAM. It advances using non-blocking UART transmission, at most
eight attempted characters per poll; no report waits for UART completion.
Control remains ahead of report/terminal work in the foreground loop.

One report request is accepted at a time. A second read receives BUSY (error 20)
on a complete line boundary. Service acknowledgements can interleave on line
boundaries without changing report counts. STOP and OFF actions execute even
while a report is printing. Fresh adapter safety packets remain accepted.
The fixed two-entry reply queue is not an unbounded command backlog; service
hosts must keep one outstanding request and avoid flooding commands.

After 1000 ms with no transmit progress, the partial report is abandoned. It
does not receive a success @E. LF resynchronizes the next output. Hosts must
time out and discard partial frames, not treat an ACK as report completion.
Use 500 ms polling for individual live STATUS reads, one outstanding request;
allow a longer timeout (at least 5 s on initial bench tests) for REPORT.

Errors: 20 busy, 2 unsupported command/report, 3 syntax/frame, 4 interlock,
5 invalid/unconfigured limits, 6 storage operation failed, 7 active fault or
unsafe fault-clear state. Mode changes remain RAM-only until CONFIG,SAVE.
No report command enters BSL, calibrates, writes EEPROM or requests a relay.

Recognized service commands blocked by key, charger, motion or drive outputs
return interlock error 4, not unsupported error 2. Malformed fan, speed or raw
EEPROM-read parameters return syntax/limit errors without accessing EEPROM.
An RX-ring overflow latches the UART fault and discards the damaged frame's
remaining bytes until LF, hash or a fresh dollar delimiter. A fault-clear
suffix inside that damaged frame cannot clear the fault.

## Verification Scope

The test suite checks identity digest/build matching, exact aggregate list and
record counts, LF/CRLF and overflow recovery, valid/stale/unknown measurements,
latched faults and 16-bit event wrap, unsupported commands, feedback validity,
odometer metre scaling beyond 32 bits, interleaved outcomes and blocked TX.
Actual linked SDCC output is checked by the existing BuddySoftware validator,
serial parser and presenter. No second human-text desktop parser is introduced.

Those tests are not proof of XC878 timing, hardware watchdog reset, live GUI
autodetection, BSL recovery or charging performance on a vehicle. Those remain
explicit bench/integration work.
