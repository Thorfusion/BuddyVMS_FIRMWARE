# Evidence And Unknowns

## Evidence Order

When sources disagree, use this order:

1. Original CC2 machine-code side effects checked against disassembly.
2. MCU and component data sheets.
3. Continuity tracing and controlled meter/scope tests on the original board.
4. Supplied connector image.
5. Working hypotheses, clearly marked and never used as safety limits.

The copied original image has SHA-256
`dc42df2778291064a4d09770d0635c6953079c4db3148731faf6610c086dd02a`.
`reference/SHA256SUMS.txt` records the complete reference pack.

## Firmware-Proven Anchors

| Original code location | Observation |
|---:|---|
| `0xA713..0xA75D` | Protected oscillator/PLL setup used by starter assembly |
| `0xA769` | Maps the 3 KiB XRAM window |
| `0xBFA9` | Page-sensitive port initialization values |
| `0xDA77` | P1.0/P1.1 UART routing and 19200 baud setup |
| `0xCAC3` | P3.4 charge enable is active low |
| `0xDB8D` | P3.6 full-charge relay driver |
| `0x47F2` | P3.7 fan relay driver |
| `0xCB13` | P0.3/P0.4/P3.5/P4.2 output application |
| `0xCC99` | P4.0/P4.4/P5.0/P0.5 digital input sampling |
| `0xCBB1` | P4.5..P4.7 ADG408 selector patterns |
| `0xBB69..0xBBDE` | ADC selector and result path; P3.0 activity LED |
| `0xDFAA/0xDFAD`, called at `0xD4DE/0xD4E4` | P3.1 scheduled controller-work LED, active low; original UART byte launcher `0xAFFB` does not control it |
| `0xDF9E..0xDFA7` | P0.7/P0.6 display bank-control helpers |

The current starter uses this LED around UART byte transmission.
That implementation differs from the original evidence above. Do not infer
original UART wiring or a transmitted byte from a controller-work LED pulse.

## Open Hardware Questions

- Exact MCU pin for the 72 V optocoupler / pedal / motor-relay feedback input.
- Electrical purpose and relationship of `P1.7` and `P5.2` DC/DC-related
  outputs.
- Active polarity at every connector-side relay/load other than charge enable.
- Shunt resistance, AN1/AN2 amplifier gains, polarity, and crossover range.
- Exact S1 offset/reference circuit and per-channel voltage transfer functions.
- Temperature sensor part/curve and populated pull-up values.
- Dashboard connector numbering and the electrical role of pins 15/16 beyond
  the proven bank states.
- Duplicate-looking 72 V labels on X2 pins 1 and 12.
- Reset/bootstrap-loader button wiring and required entry timing.

## Rules For New Code

- Unknown values stay unknown; never fill them from an unrelated controller.
- Record raw readings alongside converted values during bring-up.
- Every calibration structure is versioned, range-checked, and checksummed.
- Any page-changing SFR helper restores the expected page before returning.
- Any path that can energize charge, drive, or DC/DC has a safe default and a
  bounded timeout.
- Production behavior is tested from reset and brownout, not only from a warm
  command session.
