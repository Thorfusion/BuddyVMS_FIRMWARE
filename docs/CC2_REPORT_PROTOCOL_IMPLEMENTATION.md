# XC878 Replacement Firmware: Desktop Report Integration

Implementation guide for the NiMH and lithium replacement firmwares running on
the **original Buddy XC878 carcomputer**. Both should use the structured report
system already used by the new DA64 carcomputer, not a second human-text parser.

Status: design and integration requirements, recorded 2026-11-24. Proposed
commands and metadata below are **not implemented merely by adding this file**.
Firmware and desktop support must be delivered together and tested before use.

## Scope And Current State

| Project | Firmware location | Current identification |
|---|---|---|
| NiMH replacement | `Buddy/src/CarComputer/cc2_nimh/` | Native `IDENTIFY` and structured reports; compatibility `$ID#` also exists |
| Lithium replacement | `BuddyBVS_VMS_FIRMWARE/lithium/firmware/` | Native `IDENTIFY`, structured reports, compatibility `$ID,BUDDY-CC2-LITHIUM,0.2.1#` |
| Existing new carcomputer | `Buddy/src/CarComputer/da64/` | Existing structured DA64/BMS reports |

Both XC878 projects currently configure service UART as **19200 8N1**. The
DA64 service console uses **230400 8N1**. Sharing report formats must not change
the XC878 baud rate or make it a DA64/UPDI target.

The lithium v0.2.1 contract is
`BuddyBVS_VMS_FIRMWARE/lithium/protocol/protocol.json`. The older root
`firmware/` scaffold and its generic `CC2-CUSTOM-SDCC` identity are not this
profile. Lithium is bench firmware, not a claim of vehicle/hardware validation.
The NiMH directory is also work in progress. Its report contract is
`Buddy/protocol/cc2-nimh-protocol.json`, not the DA64 protocol file.
Most initial NiMH reports use schema 100; electrical schema
101 adds current age, scan sequence, and scan-current bounds. Voltage acquisition
metadata declares its slower multiplexed scan age allowance.

### Lithium Schema Checked Against v0.2.1

Lithium uses Status **129**, unchanged Version **1**, and schema **128** for
Temperatures, Electrical, Charging, Configuration, Relays, Faults, Storage and
Lifetime. Its aggregate is exactly `2,1,3,4,6,7,8,9,13,15`; product ID is **3**.
There is no legacy LOG, separate BMS bridge, persistent journal or NiMH SOC setter.

- Status SOC is an optional record, omitted when unknown. Electrical SOC is
  centipercent and preserves adapter precision; never display a sentinel as SOC.
- Electrical pack voltage is centivolts, cell extrema are millivolts, and
  signed current is deciamperes, positive charging. Do not invent six block
  voltages or use cell spread as a NiMH group-balancing trigger.
- Temperatures are BMS pack extrema in deci-Celsius, not front/rear/box probes.
  Their spread has its own live/graph/CSV fields.
- `bms_fresh`, `soc_valid`, omitted records and adapter sample age determine
  availability. CONFIG declares `bms_timeout_ms` (1500 in this release).
  Host transport age is included; a newer unavailable adapter status hides
  older measurements without hiding digital vehicle/charge state.
- Lifetime odometer values are metres and can exceed 32 bits after scaling;
  `odometer_valid` and source must be checked before displaying distance.
- Relay-applied bits are MCU latches. A zero feedback-valid mask means
  unavailable physical feedback, not confirmed Off. Fault transitions are
  an eight-entry RAM session history; its 16-bit sequence wraps and resets at
  reboot. It is not a persistent/calendar journal.
- Error meanings come from this profile, including 20 busy, 5 invalid limits
  and 7 active fault preventing clear. Do not borrow DA64/NiMH meanings.

BuddySoftware bundles the byte-identical current lithium JSON, uses it only for
an exact identity digest match, and tests its parser, presenter and live values
with the released compiled SDCC transcripts. Older Status 128 is decoded only
with that older release's matching protocol, not a guessed alternate layout.
Live panels/graphs use lithium labels and dedicated cell/temperature extrema;
CSV preserves these fields without fabricating stock sensor values.

## Authoritative References

- `Buddy/docs/PC_SERIAL_PROTOCOL.md`: existing serial grammar and commands.
- `Buddy/protocol/serial-protocol.json`: existing report layouts, units, enum
  labels, bitfields, conversion factors, and historical schemas.
- `Buddy/src/CarComputer/da64/da64_pc_protocol.h`: reference report emitter.
- `BuddySoftware/buddy_software/protocols.py`: current parser and JSON validator.
- `BuddySoftware/docs/REPORT_PROTOCOL_METADATA.md`: chronology and validity
  relationships understood by the desktop.
- `BuddySoftware/docs/BUDDY_PC_INTEGRATION.md`: link ownership and flashing.
- Each XC878 project's hardware, EEPROM, and charging documentation.

Paths above are relative to the parent directory containing the repositories.
Keep an identical copy of this guide in each firmware repository's `docs/`.
Do not copy DA64-specific hardware assumptions into XC878 firmware.

## Identify Before Using Any Service Commands

USB descriptors identify the adapter, **not the firmware or chemistry**.
The same Elbil / BUDDY Interface adapter can connect to stock firmware, the
NiMH replacement, or the lithium replacement.

The desktop currently has legacy automatic service behavior that unlocks the
original controller, reads `$PRT#`, and may adjust NiMH capacity/SOC. That
behavior must not be selected merely because an original adapter was found.

### Proposed Common Identity Command

Implement the read-only, uppercase, LF-terminated command `IDENTIFY` in both
replacement firmwares. It must work without unlock, EEPROM writes, calibration,
relay changes, or a battery-capacity adjustment. Do not require legacy login.

Reply with exactly these comma-separated tokens:

```text
@OK,IDENTIFY,1,<profile>,XC878,<chemistry>,<build_id>,<protocol_sha256>
```

| Token | Meaning |
|---|---|
| `1` | Identity response schema, independent of report/config/EEPROM schemas |
| `profile` | `CC2_NIMH` or `CC2_LITHIUM` |
| `chemistry` | `NIMH` or `LITHIUM`, matching the profile |
| `build_id` | Exact firmware release identifier, not the EEPROM schema |
| `protocol_sha256` | 64 hexadecimal characters; SHA-256 of the release's exact `protocol.json` bytes |

Use `YYYYMMDD-HHmmss.GITHUB_RUN_NUMBER` for release build IDs, with the timestamp
in `Europe/Oslo`, consistent with the current firmware release scheme. Existing
development strings such as `0.3` are not dates; do not compare them as dates.
Identity strings must contain no commas, CR, LF, or embedded whitespace.

Preserve compatibility `$ID#` responses while older tools need them, but do not
mistake the old generic scaffold's product name for a chemistry declaration.
The new `IDENTIFY` response is the shared integration contract.
The desktop prefers its release build ID over semantic versions printed by
compatibility ID/BOOT banners. Contradictory explicit profiles or native build
identities are still rejected.

### Detection Available In The Desktop

BuddySoftware now probes `IDENTIFY`, falling back to `$ID#`, on the existing
19200-baud serial worker. The original firmware page also has a **Detect
firmware** button and a **Detected firmware** field. Matching `$BOOT,...#`
banners are accepted as additional evidence. Outgoing echoes are not identities.

| Response | Desktop classification |
|---|---|
| Valid `@OK,IDENTIFY,1,CC2_NIMH,XC878,NIMH,...` | Replacement NiMH with declared build and protocol digest |
| Valid `@OK,IDENTIFY,1,CC2_LITHIUM,XC878,LITHIUM,...` | Replacement lithium with declared build and protocol digest |
| `$ID,BUDDY-CC2-NIMH,<version>#` | Replacement NiMH; no protocol digest declared |
| `$ID,BUDDY-CC2-LITHIUM,<version>#` | Replacement lithium; no protocol digest declared |
| `$ID,CC2-CUSTOM-SDCC,<version>#` | Custom XC878; chemistry not declared, never guessed from the repository name |
| Stock report line `Software version : CC2_<six digits>` | Original Buddy firmware, provided no contradictory replacement identity was received |
| Missing, malformed, unknown or conflicting identity | Unknown; legacy automatic repairs disabled |

Released lithium uses the explicit `BUDDY-CC2-LITHIUM` compatibility name.
The v0.2.0 alias `PoldenTEK-CC2-LITHIUM` is also recognized. Native IDENTIFY
binds the dated release build and protocol; compatibility replies use the
semantic version. Both must declare the same firmware family.

If neither identity command yields a replacement response, the desktop may
send the existing `$KEY,123BUDDY#` access unlock followed by the native read-only
`$PRT#` to obtain stock-version evidence. Unlock is an access-state change, not
an EEPROM/battery-setting write. Timeout alone still does not identify stock:
automatic stock capacity/SOC changes require a recognized stock version and a
complete service report. A malformed/unknown explicit identity stops this
fallback rather than being treated as an unsupported command.

Recognizing identity now selects a matching profile-specific report decoder.
Its JSON must match the device's protocol digest, using a cached copy, the
bundled NiMH definition when identical, or that installed build's versioned
protocol from its dedicated channel. A different/latest schema is not silently
substituted. The original-hardware sidebar switches to replacement reports,
commands, live status and graphs; stock EEPROM/backup/diagnosis tools are hidden.
Lithium firmware still needs to implement its identity and publish its own
protocol before structured reports become available; generic legacy text alone
does not enable them.

### Desktop Connection Rules

1. Detect a supported adapter, open one serial session at 19200, and perform
   bounded read-only identification before enabling any legacy service policy.
2. A valid replacement identity selects its matching protocol/profile. Verify
   profile, MCU, chemistry, build, and protocol digest together.
3. An unknown, malformed, timed-out, or contradictory identity means **unknown**,
   not stock firmware. Keep raw diagnostics available without automatic writes.
4. Legacy stock detection needs its own positively recognized stock response.
   A replacement identity timeout alone is not sufficient. Never probe with
   setters, raw output commands, or boot-entry commands.
5. Do not run the stock NAH/VG normalization, error-1 repair, automatic balancing,
   SOC correction, or stock-2019 update prompt for either replacement profile.
   Any replacement-specific service action must be explicitly implemented and
   authorized for that profile, not inherited from a similar report field.

Opening a serial port must not intentionally reset the controller: preserve
normal FTDI line/CBUS states and use a dedicated workflow for BSL entry.

## Command And Record Grammar

Use native uppercase commands terminated by LF; accept CRLF as well. These are
not `$...#` commands. Legacy wrappers may remain as explicitly documented
compatibility aliases, but cannot be the only route to structured reports.

All structured reports use the existing envelope:

```text
@B,<report_id>,<schema_version>
@D,<report_id>,<field_id>,<value0>[,<value1>...]
@E,<report_id>,<record_count>
```

Example using existing Status report 1, schema 1, field 1:

```text
@B,1,1
@D,1,1,0,0,10,0,0,0
@E,1,1
```

Its fields are `vehicle_state,charge_state,soc_pct,speed_kmh,fault_mask_lo_hex,
fault_mask_hi_hex`. State names must come from the selected protocol registry.
Do not adopt schema 1 if the firmware uses different state or fault meanings.

Rules:

- `record_count` counts only `@D` records for that report, not CSV values,
  acknowledgements, or events. An empty supported history can have count zero.
- Repeat the report ID in every record. Interleave different reports only on
  complete line boundaries; never open two frames of the same ID concurrently.
- Do not finish a partial frame with a success count after an internal failure.
  Document abort behavior and require bounded host timeouts/recovery.
- Numeric fields are decimal integers unless their declared type says otherwise.
  Hex fields use hexadecimal digits without `0x`; keep `_hex` column names.
- No localized numbers, human labels, CSV quoting, or multiline values. The
  existing parser splits on commas rather than interpreting quoted CSV.
- Encode booleans as `0`/`1`; document current direction and all fixed-point
  units. Preserve enough signed width for current, Ah, time, and counters.
- Keep command results separate from report data:

```text
@OK,<COMMAND>[,<returned_value>...]
@WARN,<COMMAND>,<numeric_warning_id>[,<value>...]
@ERR,<COMMAND>,<numeric_error_id>[,<value>...]
@EV,<source_id>,<numeric_event_id>[,<value>...]
```

Identity is an explicitly text-valued command. Other text-valued commands must
also be declared. An acknowledgement is not a completed report or proof that
an asynchronous operation succeeded. Do not assign XC878 events to the existing
DA64/BMS sources without agreeing a source ID and adding its registry entries.

## Reports And Commands To Implement

Reuse these established report IDs **only when their meanings are preserved**:

| ID | Existing purpose | XC878 guidance |
|---:|---|---|
| 1 | Status | Live vehicle/charge state, SOC, speed, faults |
| 2 | Version | Preserve existing product/config/log schema meanings; identity supplies actual build version |
| 3 | Temperatures | Front, rear, box, validity/freshness where supported |
| 4 | Electrical | Calibrated voltages, current, SOC, supply, validity |
| 5 | Motor | Only motor information actually observed on this hardware |
| 6 | Charging | Actual chemistry-specific charge logic and stop reasons |
| 7 | Configuration | Stored/effective settings, board/display selection, chemistry |
| 8 | Relay Counters | Counts and requested/applied/feedback states, only where available |
| 9 | Faults | Active/session/persistent faults, masks, statistics |
| 10 | Balance Voltage Log | Only if meaningful and implemented |
| 11 | Temperature Error Log | Retained temperature events and snapshots |
| 12 | Voltage Error Log | Retained voltage events and snapshots |
| 13 | Storage Integrity | VMS and display integrity/version/availability separately |
| 14 | BMS SOC Snapshot | Only if its exact SOC snapshot meaning is implemented |
| 15 | Lifetime | Odometer, operating time, charge/cycle/Ah counters |
| 16 | Detailed BMS / INA229 | Do not advertise a separate BMS/INA229 that is not fitted |
| 17 | Full-Charge History | Real full-charge reasons and valid measurement snapshots |
| 18 | Persistent Event Journal | Sequence-ordered state/fault events with validity |

Start with read-only `IDENTIFY`, `VERSION`, `STATUS`, `CONFIG`, `TEMPS`, `FAULTS`,
and `REPORT`. Add electrical data to `REPORT` using the agreed layout. Then add
applicable history commands: `CHARGEREPORT`, `RELAYS`, `EVENTS`, `READTEMPERR`,
`READVOLTERR`, `READFULLCHARGE`, `READJOURNAL`, and `LOG` as implemented.

`REPORT` must emit every report advertised for the aggregate read, including
Version, and no unsupported report. Record its exact list/order in the profile.
Do not copy DA64's aggregate list blindly. The current desktop additionally
requests `BMSREPORT` after its DA64 aggregate read; disable that behavior for
XC878 profiles without a real supported bridge. Do not return invented report
16 data just to satisfy an unconditional desktop request.

Unsupported report commands return a documented error, not a plausible-looking
zero-filled report. An implemented history with no retained events is different
from unsupported hardware or unavailable EEPROM.

Allocate distinct product IDs before release: DA64 currently emits product ID
`1` in Version report 2, field 1. Neither XC878 profile should identify itself
as that product. Config/log schema numbers are not firmware version numbers.

### Live Status And Graphs

If implementing the existing `LOG` compatibility command, return exactly:

```text
vehicle_state,charge_state,odometer_km,rear_C,front_C,box_C,soc_pct,current_dA,b1_dV,b2_dV,b3_dV,b4_dV,b5_dV,b6_dV,fan_on,speed_kmh
```

This line is unframed and has no header in the actual response. Match signs,
units, field order, and enum values to the existing LOG contract. The desktop
only treats an unprefixed line as LOG while that request is outstanding.

- Use 500 ms host polling by default, one outstanding request at a time.
- The ADC multiplexer may update voltages more slowly; expose measurement age
  in structured electrical data rather than pretending every poll is fresh.
- Total voltage is the sum of the six **valid, calibrated group voltages**;
  never sum cumulative taps or double-count repeated display rows.
- Show box temperature but exclude it from front/rear temperature delta.
- LOG has no validity mask. Do not advertise it until a complete meaningful
  sample can be emitted; invalid measurements need a documented error or an
  agreed versioned live-report extension, not fabricated zeros or sentinels.
- Six grouped voltages are not six individual lithium cell voltages. Do not
  reuse NiMH charge/balance heuristics or claim cell-level protection from them.

## Release Protocol Definition And Compatibility

Publish a **separate validated `protocol.json` for each firmware profile**.
Start from `Buddy/protocol/serial-protocol.json`, retaining only applicable
definitions and supported historical schemas; do not ship every DA64 capability.

For reuse of today's generic decoder, keep the existing root values:

```json
{
  "format": "buddy.da64-report-protocol",
  "schema": 1
}
```

This is only a fragment, not a complete valid protocol file. The historical
`da64` name is the current JSON **format identifier**, not a hardware identity.
Renaming it requires a coordinated desktop validator update.

Each report definition needs its name, supported `schema_versions`, field IDs,
exact column order, titles, and applicable overrides. Include enum/bitfield/
fault labels, numeric conversions, boolean/text declarations, and presentation
metadata. A changed field order, unit, enum meaning, fault assignment, or validity
rule needs a new agreed report schema; do not reinterpret an existing one.
Coordinate new schema numbers across both profiles and the desktop, and key the
active decoder by profile as well as report/schema ID.

Do not use DA64's ADS8688 count-to-voltage conversion for the XC878 ADC/shunt.
Prefer calibrated fixed-point engineering values with explicit column names,
or publish the actual XC878 conversion. If using a DA64-compatible ADC-count
representation, define it as a normalized wire quantity and test conversion
accuracy; it must not masquerade as a native XC878 ADC reading.

The existing decoder has special handling for several known report/field/column
names. Reusing the envelope does not automatically remove those assumptions.
Review those paths before giving different data an existing ID or column name.

### Proposed Read-Only Profile Metadata

Add a `cc2_profile` root object to the profile's protocol file. For example:

```json
{
  "cc2_profile": {
    "schema": 1,
    "id": "CC2_NIMH",
    "mcu": "XC878",
    "chemistry": "NIMH",
    "service_baud": 19200,
    "supported_reports": {"1": 1, "2": 1},
    "aggregate_reports": [2, 1],
    "legacy_log": false,
    "bms_bridge": false
  }
}
```

This is a **minimal illustrative profile**, not the finished report set. For
lithium use `CC2_LITHIUM` and `LITHIUM`; include only implemented report/schema
pairs in each release. Historical layouts can remain in `reports` without being
advertised as current capabilities. Validate that every advertised pair exists
and that aggregate IDs are supported and unique.

The desktop validates this object, matches the protocol digest, and uses an
independent per-profile parser/formatter rather than replacing DA64/CAN globals.
Report buttons are limited to supported report IDs using the desktop's known
command map. Read All follows the profile's aggregate without guessing a DA64
BMS bridge. An old desktop ignoring extra JSON properties is not sufficient
compatibility. Profile metadata must
not contain executable expressions, command sequences, or button definitions.
New actions still require deliberate desktop implementation and safety review.

Attach each immutable protocol file to the matching firmware release and include
its URL/SHA-256 in the release manifest. Identity must embed the digest of those
same bytes. If the matching definition cannot be validated, preserve the last
validated compatible definition or use raw read-only diagnostics; never silently
load the latest DA64 schema for an unidentified XC878 image. A digest checks
integrity/binding, not publisher authenticity; use trusted release transport.

## Publishing Firmware Files

Both replacement firmwares belong under the existing **old-carcomputer**
download area, not under the DA64/new-carcomputer firmware area. Give each
chemistry its own directory, latest manifest, and immutable release history.

| Publisher | Profile | Object-storage prefix | Public latest manifest |
|---|---|---|---|
| `Buddy` XC878 NiMH build | `CC2_NIMH` | `buddy/old-cc-firmware/nimh/` | `https://filer.poldentek.no/buddy/old-cc-firmware/nimh/manifest.json` |
| `BuddyBVS_VMS_FIRMWARE` lithium build | `CC2_LITHIUM` | `buddy/old-cc-firmware/lithium/` | `https://filer.poldentek.no/buddy/old-cc-firmware/lithium/manifest.json` |

The desktop supports these channels and selects the detected replacement type
as the initial flashing target. This is not a claim that firmware files have
already been uploaded. Users can deliberately select a different firmware type.
Object keys are relative to the configured bucket; do not add a second bucket
name or a second `buddy/` prefix when uploading them.

### Required Server Layout

```text
buddy/
  old-cc-firmware/
    manifest.json                  legacy clients only; new desktop never uses it
    ...                            existing legacy releases; leave untouched
    nimh/
      manifest.json                latest NiMH plus NiMH version history
      releases/
        20261124-120000.1/
          20261124-120000.1.hex
          protocol.json
    lithium/
      manifest.json                latest lithium plus lithium version history
      releases/
        20261124-120000.1/
          20261124-120000.1.hex
          protocol.json
```

Use the exact firmware build ID as the release directory, HEX basename, and
manifest `version`, including the GitHub run number. The example IDs are not
real releases. The two workflows can produce the same ID: their separate
profile prefixes keep the objects distinct. Do not rename these builds to a
stock `CC2_YYMMDD` identifier or publish them as DA64/BMS images.

The NiMH workflow packages its XC878 build from `src/CarComputer/cc2_nimh/`;
the lithium workflow packages its XC878 build from `lithium/firmware/`. Rename/copy
the selected production HEX into the release layout during packaging. Do not
publish development safe/output-test images as the normal customer firmware.
Set the firmware's embedded build ID before compilation; renaming an artifact
afterward must not make its reported version disagree with its manifest.

The NiMH firmware repository can continue publishing its DA64/AVR products to
`buddy/firmware/` through their existing workflow. Its **XC878 NiMH** artifact
and protocol must instead go to the dedicated old-firmware prefix above.

### NiMH Manifest Example

Save this as `buddy/old-cc-firmware/nimh/manifest.json` after replacing both hash
placeholders with real SHA-256 values for the exact uploaded bytes:

```json
{
  "schema": 2,
  "profile": "CC2_NIMH",
  "hardware": "XC878",
  "chemistry": "NIMH",
  "latest": "20261124-120000.1",
  "protocol": {
    "schema": 1,
    "version": "20261124-120000.1",
    "filename": "protocol.json",
    "url": "releases/20261124-120000.1/protocol.json",
    "sha256": "REPLACE_WITH_PROTOCOL_SHA256"
  },
  "firmware": [
    {
      "target": "OLD_CC",
      "part": "xc878",
      "version": "20261124-120000.1",
      "date": "2026-11-24",
      "filename": "20261124-120000.1.hex",
      "url": "releases/20261124-120000.1/20261124-120000.1.hex",
      "sha256": "REPLACE_WITH_HEX_SHA256"
    }
  ],
  "versions": []
}
```

### Lithium Manifest Example

Save this separately as `buddy/old-cc-firmware/lithium/manifest.json`. Its hashes
must come from the lithium build and lithium protocol, not the NiMH files:

```json
{
  "schema": 2,
  "profile": "CC2_LITHIUM",
  "hardware": "XC878",
  "chemistry": "LITHIUM",
  "latest": "20261124-120000.1",
  "protocol": {
    "schema": 1,
    "version": "20261124-120000.1",
    "filename": "protocol.json",
    "url": "releases/20261124-120000.1/protocol.json",
    "sha256": "REPLACE_WITH_PROTOCOL_SHA256"
  },
  "firmware": [
    {
      "target": "OLD_CC",
      "part": "xc878",
      "version": "20261124-120000.1",
      "date": "2026-11-24",
      "filename": "20261124-120000.1.hex",
      "url": "releases/20261124-120000.1/20261124-120000.1.hex",
      "sha256": "REPLACE_WITH_HEX_SHA256"
    }
  ],
  "versions": []
}
```

Relative URLs resolve from **that profile's manifest**, so identical relative
paths in these examples still address different firmware/protocol objects.
`target: OLD_CC` identifies the existing XC878 flashing family; it does not
identify battery chemistry. The manifest's proposed `profile`, `hardware`, and
`chemistry` fields, the device identity, and `protocol.json`'s `cc2_profile`
must agree before offering an automatic update. Those extra manifest fields
are enforced by the dedicated XC878 manifest loader, not by generic downloads.

For subsequent releases, retain a newest-first `versions` history inside each
profile's manifest. Each history entry carries its own `version`, `date`,
`firmware` array, and `protocol` object, with the same relative URLs and hashes
as when published. Replace the top-level latest entries without deleting
earlier releases. Never combine NiMH and lithium into one version history.

### Publishing And Download Rules

1. Build the selected production profile, prepare its profile-specific protocol,
   and embed that protocol's digest and the build ID in firmware identity.
2. Validate the HEX, protocol JSON, profile agreement, and generated manifest.
   Generate hashes from finalized files, never from source text or a ZIP wrapper.
3. Upload the versioned HEX and `protocol.json` first. Verify the uploaded
   objects before publishing the profile's latest `manifest.json` last. Serve
   JSON as UTF-8 `application/json`, HEX as a downloadable file, and keep latest
   manifests revalidated/not long-cached. Versioned objects are immutable.
4. Restrict each publisher to its own complete prefix above. Never run a delete
   sync against `buddy/`, `buddy/old-cc-firmware/`, or the other chemistry's
   folder. Do not overwrite the existing root old-CC manifest or software-update
   manifest. Publishing a corrected build requires a new build ID.
5. The desktop must select a channel from positively detected profile, verify
   identity/manifest/protocol agreement, and compare the manifest's build ID
   against the device's reported build ID. Do not compare a whole HEX filename
   or a stock `CC2_YYMMDD` version against a replacement build ID as though they
   were the same release series. Unknown chemistry must not select a channel.
6. Downloads must stay separated locally too, for example
   `<app-data>/old-cc-firmware/nimh/OLD_CC/<build-id>/<build-id>.hex` and
   `<app-data>/old-cc-firmware/lithium/OLD_CC/<build-id>/<build-id>.hex`, with each
   release's matching protocol. Do not fall back to the newest HEX found across
   both directories or a different profile's cached protocol.

The desktop defaults to **Original Buddy (bundled)** until it identifies a
replacement, or the user selects another target. Original firmware always uses
the installer-bundled `CC2_190431.hex`; it is never downloaded. Builds fail if
that required asset is missing. The old root download manifest is not used.

Users may switch among Original, NiMH, and lithium. A type change requires the
explicit **Allow firmware type change** checkbox, battery-chemistry/configuration
warning, and typed flash confirmation. Type-change permission is consumed by
one accepted flash attempt and is not persisted. Version comparison applies
within a firmware family only, not between a stock `CC2_YYMMDD` identifier and
a dated replacement build. Unidentified/development builds need the separate
version service override when their versions cannot be compared reliably.

The target selector is independent of the installed profile: selecting lithium
while connected to NiMH must not change the decoder until that firmware is
actually installed and identified. Published versions select only their own
verified cached package. An uncached selected version remains unselected for
flashing until downloaded; it must not fall back to another cached build.
Choose **Local package** when browsing an offline package instead.

Downloads are cached separately under `<app-data>/old-cc-firmware/<kind>/`,
with the existing downloader's `OLD_CC/<build-id>/` subdirectory. That directory
contains the HEX, matching `protocol.json`, and a desktop-generated
`firmware.json` binding their hashes and profile metadata. The latter serializes
the firmware entry plus its nested `protocol` object; it is not an upload of
the whole channel manifest. Preserve these three files together for local or
offline flashing. A manually copied HEX without its validated package metadata
is not treated as a trusted replacement release.

After programming/readback verification, FTDI is returned to idle/reset and the
application identity is read again. Replacement profile, build ID and protocol
digest must match the selected package before success is reported. No settings
are automatically restored across different firmware/EEPROM layouts.

## EEPROM, Journals, And Measurement Validity

- Firmware reports should expose logical settings and logs, not force the PC to
  infer live configuration from raw EEPROM bytes or stock `$DMP#` output.
- VMS EEPROM and display EEPROM are independent devices with separate layouts,
  integrity, availability, and migration rules. Similar bytes do not prove they
  share settings. M9/Classic display choice is independent of NiMH/lithium.
- Distinguish stored value, effective default, erased/uninitialized storage,
  invalid/CRC-failed storage, and unavailable hardware. `00`/`FF` is not a
  universal default rule across all fields or either new firmware.
- Keep firmware build ID, report schema, config schema, journal schema, and
  display layout separate. Migrate deliberately; reading a report must not
  silently initialize, erase, or normalize settings.
- Report each measurement's validity and age where needed. Invalid feedback is
  unknown, not Off; invalid voltage/temperature is not zero. Use the existing
  field-level `presentation.validity_masks` relationships when applicable.
- Journal records need an explicit wrapping sequence and reference newest
  sequence; uptime is not calendar time and EEPROM slot order is not chronology.
  Include reset/session context where uptime can restart, document sequence
  wrap/reset, and label uncertain ordering rather than guessing.
- Apply the desktop's `presentation.ordering` metadata to retained records.
  Full-charge companion snapshots must join on both slot and sequence, with
  per-channel validity; never join solely by line order.
- Do not reset a fault's continuous-duration start timestamp when periodically
  saving statistics. Total time, current duration, and longest duration must
  remain consistent across persistence checkpoints and documented reboot rules.

## XC878 Implementation Constraints

The existing UART command buffer is 48 bytes. Set and document exact accepted
command length, overflow recovery, and bounded RX/TX storage; do not assume the
DA64 79-character command limit. Stream reports from a small state machine
instead of building the whole report in RAM.

At 19200 8N1 the theoretical wire budget is approximately 1920 bytes/second
before processing overhead. Large histories can take several seconds. Transmit
bounded chunks, preserving control deadlines, ADC scheduling, display service,
and the watchdog. Do not disable interrupts or block relay/charging protection
while printing. Capture small coherent snapshots without holding long locks;
freeze a journal boundary so pagination cannot mix overwritten generations.

Keep machine records on complete lines. Debug/I2C tracing must not inject text
inside a record; any human debug lines need a separate documented channel or
prefix and must not resemble unframed LOG. No automatic persistent writes on
connect, `IDENTIFY`, `VERSION`, `STATUS`, `REPORT`, or export.

Start with read-only features. Keep hardware-output tests, raw EEPROM writes,
calibration, and charging/drive overrides behind explicit service controls and
their existing interlocks. The lithium carcomputer is not a substitute for
independent cell-level pack protection.

## Desktop And Flashing Integration

The same Qt report presentation, live graph machinery, CSV logging, and report
ZIP export/import should serve all profiles. Transport and device policy are
separate: sharing a parser must not select DA64 wiring, AVR programming, or
legacy automatic NiMH repair.

Desktop integration requirements (implemented for the published contract):

1. Add read-only replacement identification before the legacy auto-service path.
2. Validate/select profile-bound JSON without leaking the active enum/conversion
   registry between simultaneous devices or imported report archives.
3. Route supported report buttons and Read All through the selected profile;
   gate BMSREPORT/bridge features without adding arbitrary JSON action execution.
4. Preserve identity, build, profile, protocol digest, captured report schemas,
   timestamps, decoded data, and raw lines in human-readable/importable exports.
   Imported reports must never trigger live service hooks or configuration writes.
5. Use receive timestamps and validity for graphs, keep serial work in device
   workers, and bound queues/history independently of rendering.
6. Match firmware download/update offers to hardware **and chemistry/profile**;
   do not choose by filename/version alone or offer stock 2019 as a replacement
   firmware update. Reject a cross-chemistry artifact unless an explicitly
   authorized conversion workflow is implemented.

Flashing stays on the existing original-controller **XC878 BSL through FTDI**
path, using the supported helper A, never avrdude/UPDI or JTAG. Read identity
before entering BSL, release the serial handle for FTDI control, verify the
programmed image, then restore FTDI idle/reset and identify the application
again. Restore settings only through a compatible, validated migration path;
do not erase or restore EEPROM/D-flash indiscriminately to copy DA64 behavior.

## Implementation Order And Acceptance Tests

1. Freeze identity/profile names, allocate shared schema/event IDs, generate
   release build identifiers and protocol digests, and add transcript fixtures.
2. Implement bounded line parsing and `IDENTIFY`; add the desktop detection guard
   before enabling replacement firmware on a customer's original adapter.
3. Implement Version/Status/Config/Temperatures/Electrical/Faults and a bounded
   `REPORT`; validate the exact emitted bytes against each profile's JSON.
4. Add valid live samples, histories, chronology/validity metadata, CSV and ZIP
   roundtrips, and only then explicit setters/calibration workflows.
5. Package versioned HEX/protocol pairs and test BSL recovery/update verification
   on the bench, separately for NiMH and lithium.

Minimum automated/bench coverage for **each profile**:

- Stock, NiMH, lithium, unknown, partial identity, wrong digest, and reboot during
  detection; assert zero legacy setters and zero output changes for replacements.
- Correct record counts, empty histories, unsupported/malformed schema or field,
  partial frames, timeout/disconnect, overlapping different report IDs, and events
  between records. Corrupt/incomplete frames must not appear as valid snapshots.
- Exact units, current sign, SOC/capacity accounting, ADC conversions, stale and
  invalid values, front/rear versus box temperature, and grouped-voltage sums.
- Both M9 and Classic display layouts; VMS available/display absent and the
  reverse; blank storage, CRC failures, and migration/version incompatibility.
- Ring wrap, duplicate sequence, uptime reset, missing/mismatched companion
  snapshots, and faults active longer than several statistics-save intervals.
- Large reports and repeated 500 ms live requests while measuring worst-case
  control-loop latency/watchdog margin. No unsafe output changes or dropped
  protection deadlines because the terminal is busy.
- Read All with/without a real BMS bridge, live graphs and CSV, human-readable
  ZIP export and offline import preserving original profile and interpretation.
- Correct-variant update, cross-chemistry rejection, interrupted BSL recovery,
  post-write verification and identity readback, with EEPROM preserved unless
  explicitly covered by a tested migration.

Do not claim feature parity based only on accepting the envelope. Validated
identity, truthful capabilities, correct physical units, non-destructive reads,
and profile-specific safety behavior are part of the report implementation.
