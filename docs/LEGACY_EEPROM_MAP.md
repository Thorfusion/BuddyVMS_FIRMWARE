# Legacy EEPROM Map

This map documents high-confidence original CC2 locations for backup and
reverse-engineering. The custom firmware does not load these records
automatically. Preserve VMS and display data as separate address spaces even
where their offsets look similar.

## Record Encodings

Common original records include:

- `u16be + checksum`: high byte, low byte, then a byte making the sum zero
  modulo 256;
- `u32be + checksum`: four big-endian bytes, then a zero-sum checksum;
- `value + complement`: two bytes whose sum is `0xFF`;
- raw fixed-length ASCII fields, often terminated or padded by `00`/`FF`.

Validation is field-specific. Do not infer an erased `FF` group as a valid
maximum value.

## Display M24C02

Normal-layout high-confidence locations:

| Field | Address | Encoding/notes |
|---|---:|---|
| Odometer | `0x0001..0x0004` | Big-endian km x10 plus checksum |
| Available-capacity copy | `0x0008..0x000C` | Legacy accumulator/checksum |
| Discharged-capacity copy | `0x0010..0x0014` | Legacy accumulator/checksum |
| Misuse count | `0x0016..0x0017` | Value/complement |
| Charge-start count | `0x001D..0x001F` | u16be/checksum |
| Charge-end count | `0x0020..0x0022` | u16be/checksum |
| Display capacity | `0x0024..0x0025` | Value/complement |
| Legacy display setup bytes | `0x0026..0x002D` | Eight individually written bytes |
| Shunt-offset copy | `0x0060..0x0062` | Two bytes/checksum |
| Battery-type copy | `0x0081..0x0082` | Value/complement |
| Balance-request copy | `0x0085..0x0086` | Value/complement |
| Battery date | `0x0088..0x0091` | ASCII `DD.MM.YYYY` |
| VIN | `0x0094..0x00A4` | ASCII |
| Battery ID | `0x00A6...` | ASCII, terminated/padded |
| Cycle count | `0x00C3..0x00C5` | u16be/checksum |
| Speed pulses per 100 m | `0x00CF..0x00D1` | u16be/checksum |
| Model/layout byte | `0x00E9` | Variant marker in known backups |

Classic-format display data can contain a second copy of VMS low-half bytes:
source `0x0000..0x007F` appears at both display `0x0000..0x007F` and
`0x0080..0x00FF`. Therefore normal-layout meanings at `0x0080+` are invalid
for a Classic copy.

## Vehicle AT25128A

The original application commonly mirrors low-page records at `+0x0100`.
Known locations include:

| Field | Primary | Mirror/related |
|---|---:|---:|
| Odometer/counter group | `0x0000` | `0x0100` |
| Capacity-left group | `0x0008` | `0x0108` |
| Capacity-absolute group | `0x0010` | `0x0110` |
| Scaled-current accumulator | `0x0018` | `0x0118` |
| Nominal Ah | `0x0023` | `0x0123` |
| Capacity factor | `0x0030` | `0x0130` |
| Battery type | `0x0080` | `0x0180` |
| Balance request | `0x0084` | `0x0184` |
| Battery date | `0x0088` | copied text conventions vary |
| VIN | `0x0094` | copied text conventions vary |
| Battery ID | `0x00A6` | copied text conventions vary |
| Speed pulses per 100 m | `0x00CF` | `0x01CF` |
| VMS serial | `0x0203` | `0x0283` |
| Four shunt-gain records | `0x0208..0x0213` | corresponding records at `0x0288+` |
| DC/DC shutdown count | `0x0217` | `0x0297` |
| Event-ring metadata | `0x0214` | `0x0294` |
| Event-ring cursor | `0x021A` | `0x029A` |
| Temperature warning groups | `0x0708`, `0x0747`, `0x0786`, `0x07C5` | repeated records |
| Temperature-delta groups | `0x0804`, `0x0843`, `0x0882` | repeated records |
| Legacy low-voltage threshold | `0x08C1` | u16be/checksum |
| Legacy event ring | `0x1800..0x3FFF` | page/ring records |

The apparent coverage through `0x3FFF` means there is no globally safe
"unused" AT25128A region that can be assumed without first backing up the
specific unit. New firmware may deliberately replace the complete legacy
layout, but that should be an explicit migration decision.

