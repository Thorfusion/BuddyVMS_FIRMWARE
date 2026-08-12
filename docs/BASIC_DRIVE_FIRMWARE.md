# Basic Drive Firmware

This image is a small vehicle-motion and dashboard foundation for later lithium
work. It intentionally has no charging state machine, ADC conversion, shunt,
battery-voltage, temperature, capacity, or balancing logic. EEPROM drivers are
available to commands and future configuration code, but this runtime does not
automatically interpret legacy battery records.

## Input Interpretation

| Input | Raw active level | Interpreted meaning |
|---|---:|---|
| `P4.4` ignition | Low | Ignition on |
| `P4.0` charger detect | High | Charger present; driving inhibited |
| `P5.0` direction | High | Forward requested |
| `P5.0` direction | Low | Reverse requested |
| `P0.5` trip button | High | Pressed; resets trip on the pressed edge |
| `P4.1` speed pulse | Falling edge | One SigmaDrive speed pulse |

The ignition and direction interpretations follow original runtime behavior,
not generic logic-level naming. `$DIN#` reports both raw and interpreted values.
Ignition, charger, direction, and trip are stable after five matching 10 ms
samples.

## Relay Ownership

Automatic drive control owns only:

| Output | Use |
|---|---|
| `P0.3` | Direction relay; off for forward, on for reverse |
| `P3.5` | Q6 motor-relay enable |
| `P0.4` | Footswitch relay |

Charge enable remains inactive-high. Full-charge, fan, DC/DC force, and buzzer
remain inactive. Charger-present is only a drive inhibit; this firmware never
starts charging.

## Drive States

| Value | State | Behavior |
|---:|---|---|
| 0 | Off | Motor and footswitch off |
| 1 | Start delay | Direction applied; waits 500 ms |
| 2 | Motor wait | Motor relay on; waits 100 ms before footswitch |
| 3 | Running | Motor and footswitch on |
| 4 | Wait stop | Both drive relays off until speed is at most 1 km/h |
| 5 | Direction deadtime | Both drive relays off for 300 ms before switching direction |

Ignition off, charger present, or `$DRIVE,0#` immediately drops motor,
footswitch, and the direction relay. A direction request while running also drops both drive relays, waits for speed
to fall to at most 1 km/h, applies deadtime, switches the direction relay, and
then repeats the normal start sequence.

The interlock uses calculated speed. A disconnected or failed speed input can
read zero, so this remains basic control rather than a complete safety system.
Add independent direction/contactor feedback before vehicle deployment.

## Speed And Distance

P4.1 uses XC878 EXINT4 on falling edges. Every 250 ms the main loop atomically
takes the pulse count and computes:

```text
km/h = pulses * 360000 / (pulses_per_100m * 250)
```

The default is 1,620 pulses per 100 m. Every calibration-count pulses adds
0.1 km to trip and the RAM-only odometer. `$SPEEDCAL,<value>#` changes the
runtime calibration but does not write either EEPROM.

## Dashboard

The firmware initializes historical write addresses `0x70`, `0x72`, `0x74`,
and `0x76`, then refreshes every 250 ms:

- speed on the two LED-bar drivers;
- whole trip kilometres on the trip digits;
- the RAM-only odometer across the corrected `0x74` banks;
- battery and ECO bars blank, because no battery data is collected.

`$DISPLAY#` reports the four-bit initialization ACK mask, write failures,
refresh count, and last-refresh result. A healthy full display reports ACK
decimal `15`.

## Timer And Interrupts

The recovered PLL sequence supplies a 24 MHz system clock. Timer 0 runs at
`fSYS/2`, reloads `0xD120`, and produces the 1 ms scheduler tick at vector 1.
EXINT4 uses the shared external 3..6 vector 9. The linker output is checked for
jumps at code `0x000B` and `0x004B`.
