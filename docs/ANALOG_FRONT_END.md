# Analog Front End

## ADC Reference And Conversion

The board uses a REF198 4.096 V reference and the XC878 ADC is 10-bit:

```text
ADC pin millivolts = raw_count * 4
raw_count range    = 0..1023
```

This conversion applies only at the MCU pin. Every external value still needs
the board divider, amplifier, offset, and sensor transfer function.

The result format observed in the original firmware is:

- result high byte: ADC bits 9 through 2;
- result low byte bits 7:6: ADC bits 1 through 0;
- result low byte bit 4: valid flag.

## ADC Selector Wrapper

The original firmware's logical selector wrapper maps as follows:

| Logical selector | Physical ADC input | Function |
|---:|---:|---|
| 3 | AN3 | Dashboard light level |
| 4 | AN1 | Shunt path A |
| 5 | AN2 | Shunt path B |
| 6 | AN4 | Box NTC |
| 7 | AN0 | ADG408 voltage mux |
| 10 | AN7 | Spare NTC |
| 11 | AN6 | Rear temperature |
| 12 | AN5 | Front temperature |

## ADG408 Voltage Multiplexer

Connections:

| ADG408 signal | MCU/board connection |
|---|---|
| A0 | `P4.7` |
| A1 | `P4.6` |
| A2 | `P4.5` |
| EN | Tied enabled to VDDP |
| D | Divider/conditioning path to AN0 |

The selector helper writes the following patterns. Note that the MCU pin order
is reversed relative to the conventional A2:A1:A0 display:

| Logical selector | P4.7:5 bits | A2:A1:A0 | Channel | Working board meaning |
|---:|---:|---:|---:|---|
| default | `0x00` | `000` | S1 | Offset/reference feedback candidate |
| 10 | `0x80` | `001` | S2 | 12 V system voltage |
| 5 | `0x40` | `010` | S3 | BT11/12, nominal 12 V block |
| 4 | `0xC0` | `011` | S4 | BT9/10, nominal 24 V tap |
| 3 | `0x20` | `100` | S5 | BT7/8, nominal 36 V tap |
| 2 | `0xA0` | `101` | S6 | BT5/6, nominal 48 V tap |
| 1 | `0x60` | `110` | S7 | BT3/4, nominal 60 V tap |
| 0 | `0xE0` | `111` | S8 | BT1/2, nominal 72 V tap |

S1 is most likely a measurement of the offset/reference applied to the other
tap conditioning channels. This explains how the firmware can represent an
individual nominal 12 V block over an approximately 8.28 V to 18.29 V range,
but the exact circuit still requires probing.

The original conversion path used a raw baseline near `0x017C`, a signed
per-row correction, and an empirical scale equivalent to:

```text
centivolts = corrected_count * 46 / 47 + 1200
```

This is evidence about the original calibration, not a lithium-ready
calibration. Record raw S1..S8 ADC values against a calibrated meter and fit a
fresh transfer function for each populated tap.

## Shunt Paths

AN1 and AN2 are two conditioned readings of the same shunt, likely providing
different gain/range. The original code uses a zero baseline near `0x02B0`, but
the external gains, polarity, shunt resistance, and amplifier offset are not
yet hardware-confirmed.

Before enabling current-based control:

1. Log both raw channels at zero current over temperature.
2. Apply known positive and negative currents with an independent meter.
3. Determine sign, gain, linear range, saturation, and channel crossover.
4. Store calibration with a version and checksum.
5. Fail charge control off whenever either channel is implausible.

## Temperature Inputs

AN4 is the box NTC, AN5 front temperature, AN6 rear temperature, and AN7 a spare
NTC input. Do not inherit original battery-chemistry temperature limits for a
lithium pack. Characterize the actual installed sensors and use raw-range,
short-circuit, open-circuit, plausibility, and rate-of-change checks before
using converted temperatures.

