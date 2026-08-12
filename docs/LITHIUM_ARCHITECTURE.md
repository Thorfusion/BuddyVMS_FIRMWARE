# Lithium Firmware Architecture

This document defines software boundaries, not battery limits. Voltage,
current, temperature, timing, and balancing thresholds must come from the exact
cell chemistry, pack construction, charger, and independent pack-protection
specifications.

## Safety Model

The carcomputer may request charge, cooling, motor enable, direction, and
DC/DC operation. It must not be the sole safety device. Independent pack
protection must be able to remove charger and traction permission even if the
XC878 is crashed, reset, misconfigured, or driving a pin incorrectly.

Use normally inactive outputs, hardwired interlocks where available, and a
watchdog strategy that returns outputs safe on missed deadlines.

## Suggested Modules

| Module | Responsibility |
|---|---|
| `hal_gpio` | Page-safe pin access and output-safe operation |
| `hal_adc` | Raw conversion queue, validity, timeout, reference checks |
| `voltage` | ADG408 scan, per-channel calibration, tap plausibility |
| `current` | Dual-range shunt calibration, selection, filtering, sign |
| `temperature` | Sensor conversion, open/short detection, plausibility |
| `inputs` | Raw/qualified ignition, charger, direction, trip, speed |
| `outputs` | Desired-output model, interlocks, final pin application |
| `pack_link` | Independent pack-protection status and charge permission |
| `charger` | Explicit state machine and latched stop reasons |
| `drive` | Motor/footswitch/direction sequencing and feedback timeout |
| `dashboard` | Presentation only; no safety decisions |
| `storage` | Versioned CRC records and event ring |
| `terminal` | Read-only diagnostics by default; authenticated/test build writes |
| `scheduler` | Fixed-rate tasks, watchdog deadline, runtime monitoring |

## Charge State Machine

A conservative starting topology is:

```text
BOOT_SAFE
  -> IDLE
  -> PRECHECK
  -> CHARGE_REQUEST
  -> CHARGING
  -> COMPLETE

Any state -> FAULT_LATCHED -> IDLE only after cause clears and explicit reset
```

`PRECHECK` should require all of the following for a continuous qualification
period:

- ignition/drive state permits charging;
- charger input is valid;
- independent pack protection explicitly permits charge;
- every required voltage, current, and temperature input is fresh and valid;
- configured limits and calibration records pass version/CRC/range checks;
- relay feedback, if present, is in the expected inactive state;
- no latched timeout, sensor, storage, or watchdog fault exists.

`CHARGING` must continuously re-evaluate those conditions. A stop request should
de-energize the active-low charge relay first, then verify current decays and
relay feedback changes within bounded time. Failure to stop becomes a latched
fault reported independently of the display.

## Measurement Validity

Each measurement should carry:

```c
struct measurement {
    int32_t value;
    uint16_t age_ticks;
    uint8_t valid;
    uint8_t fault_bits;
};
```

Control code consumes only qualified measurements. Raw ADC drivers must detect
conversion timeouts; conversion modules must detect range and calibration
errors; system logic must detect stale data and disagreement between redundant
or related channels.

## Voltage Topology Limitation

The existing ADG408 path exposes nominal 12 V increments/taps. That can monitor
groups or cumulative tap voltages but cannot prove that each lithium cell is
within limits. Per-cell voltage and balancing therefore belong in an external
pack-level system. The carcomputer may display or log its permission/fault
status, but should not synthesize permission from six grouped voltages alone.

## Event Logging

Log state transitions and stop reasons using numeric, versioned event IDs. A
useful record includes sequence, reset cause, uptime, state, fault bits, pack
permission, calibrated voltage/current/temperature snapshots, and CRC. Logging
must never delay opening the charge relay.

## Development Builds

Keep load-driving terminal commands behind a compile-time test option. A
production image should preserve useful read-only commands while omitting or
permanently locking arbitrary EEPROM writes, relay commands, calibration
overrides, and fault clearing during active charge or drive.

