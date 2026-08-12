# Connector Transcription

This is a manual transcription of the supplied connector image. Colors and
wording are retained where useful, but spelling is normalized. Treat every row
as image-derived until continuity-tested. The image is the better reference for
orientation and pin numbering.

## X1: Power, Speed, Temperature

| Pin | Label/function from image |
|---:|---|
| 1 | Green, 12 V supply for carcomputer, F13/F15 |
| 2 | Blank / not connected |
| 3 | Black, chassis ground for carcomputer power |
| 4 | Beige, speed sensor red/1, 12 V positive |
| 5 | Black, speed-sensor pulse ground/line; correlates with `P4.1` pulse input |
| 6 | White, speed sensor blue, 12 V negative |
| 7 | Blue, 0-5 V front temperature; correlates with AN5 |
| 8 | Yellow, 0-5 V rear temperature; correlates with AN6 |
| 9 | Green, box NTC; correlates with AN4 |
| 10 | Black, box NTC ground |
| 11 | Red, 72 V LED/status, function not confirmed |
| 12 | Black, unused but NTC-reading line; correlates with AN7 |

## X2: Battery Voltage Taps

| Pin | Label/function from image |
|---:|---|
| 1 | Red, BT1 positive / 72 V |
| 2 | Orange, BT3 positive / 60 V |
| 3 | Blank / not connected |
| 4 | Orange-black, BT5 positive / 48 V |
| 5 | White, BT7 positive / 36 V |
| 6 | Blue-black, BT9 positive / 24 V |
| 7 | Red-black, BT11 positive / 12 V |
| 8 | Black, BT12 negative / 72 V ground |
| 9 | Blank / not connected |
| 10 | Blank / not connected |
| 11 | Blank / not connected |
| 12 | Red, +72 V; image shows a second top-pack connection |

Pins 1 and 12 both appear as 72 V-related labels in the supplied drawing.
Resolve their exact circuit destinations by continuity measurement before
making a harness.

## X3: Shunt And Motor-Control High Voltage

| Pin | Label/function from image |
|---:|---|
| 1 | Black-white, shunt positive |
| 2 | Black, shunt negative |
| 3 | 12 V signal detect, noted unused |
| 4 | Purple-grey, CANL footprint, resistor not populated |
| 5 | Black, charger isolated ground |
| 6 | Blank / not connected |
| 7 | Black, 72 V ground |
| 8 | White-yellow, CANH footprint, resistor not populated |
| 9 | Not connected, 12 V ground signal-detect note |
| 10 | Common 72 V ground for motor-control relay poles |
| 11 | Red-orange, reverse relay to motor controller |
| 12 | Red-blue, forward relay to motor controller |

## X4: Ignition, Direction, DC/DC, Optocouplers

| Pin | Label/function from image |
|---:|---|
| 1 | Brown, DC/DC 5 V |
| 2 | White-black, DC/DC enable, ground/5 V note |
| 3 | White-black, 12 V from ignition relay Q4 or motor relay Q6 |
| 4 | Red-white, motor relay Q5/common, positive control |
| 5 | Ignition bypass note associated with Q6 motor relay |
| 6 | Grey-blue, forward/reverse button/direction signal |
| 7 | Grey, common ignition; turns off charger relay |
| 8 | Blank / not connected |
| 9 | Grey-red, normal ignition |
| 10 | Black, 12 V ground for optocoupler inputs |
| 11 | Red, +12 V measurement input |
| 12 | Black, 12 V ground measurement input |

## X5: Charger, Fan, Sensor Supply

| Pin | Label/function from image |
|---:|---|
| 1 | Blank / not connected |
| 2 | Red, +12 V input/supply for fan relay |
| 3 | Blank / not connected |
| 4 | Charge-Ah line, ambiguous in image |
| 5 | White-black, charge-Ah relay line |
| 6 | Brown-white, charge-Ah relay line |
| 7 | Two black-red, fused 5 V temperature-sensor supply |
| 8 | Two brown-red, 5 V sensor ground |
| 9 | Grey, 12 V fan output |
| 10 | Blank / not connected |
| 11 | Light blue, charger relay CN14 line |
| 12 | Black/blue/white, charger ON14 start/stop line |

## Missing Dashboard Connector

The supplied image does not document the dashboard connector. The MCU-side
facts are known: I2C uses `P5.7`/`P5.5`, and dashboard pins 15/16 are controlled
by `P0.7`/`P0.6`. Trace connector numbering before fabricating a replacement
display harness.

