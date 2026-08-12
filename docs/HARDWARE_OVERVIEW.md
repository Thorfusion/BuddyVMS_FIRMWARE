# Hardware Overview

## Controller

The original carcomputer is built around an Infineon
`SAF-XC878-16FFI 5V AC`, an XC800/8051-family MCU. Important properties for
replacement firmware are:

- 8051-compatible CPU, built with SDCC's `mcs51` target.
- SFR paging through `PORT_PAGE`, `MOD_PAGE`, and `ADC_PAGE`.
- 5 V digital I/O environment on this board.
- 10-bit ADC using an external REF198 4.096 V reference.
- A dedicated UART baud generator and hardware SSC peripheral.
- A 3 KiB XRAM window mapped at XDATA `0xF000..0xFBFF` by startup code.

Code and XDATA are separate 8051 address spaces. The firmware build reserves
code `0x0000..0xEFFF` for P-Flash and uses XDATA `0xF600..0xFBFF` for SDCC
objects. Do not mistake XDATA at `0xF000` for code at `0xF000`.

## Major Board Functions

| Function | Hardware path |
|---|---|
| Service UART / bootstrap loader | `P1.0` RX, `P1.1` TX, 19200 8N1 in the original application |
| Relay outputs | `P0.3`, `P0.4`, `P3.4..P3.7` |
| Auxiliary outputs | `P1.7` DC/DC force, `P4.2` buzzer, `P5.2` standby auxiliary |
| Digital inputs | `P4.0`, `P4.1`, `P4.4`, `P5.0`, `P0.5`, plus one unresolved optocoupler input |
| Analog inputs | AN0 voltage mux, AN1/AN2 shunt paths, AN3 light, AN4..AN7 temperature inputs |
| Voltage selection | ADG408, address lines `P4.7/P4.6/P4.5`, common output to AN0 |
| Display bus | Bit-banged I2C on `P5.7` SDA and `P5.5` SCL; bank lines `P0.7/P0.6` |
| Persistent vehicle storage | AT25128A 16 KiB SPI EEPROM on `P1.2..P1.5` |
| Display storage | M24C02/24C02 256-byte I2C EEPROM at 7-bit address `0x51` |
| Status LEDs | `P3.0` ADC/activity and `P3.1` UART-transmit activity, both active low |

The root connector drawing is copied to
`reference/Schematic_Buddy_2026-07-05.png`. It does not show the dashboard
connector.

## Safe Reset State

The starter firmware uses the following inactive values immediately after port
setup:

| Load | Inactive MCU level |
|---|---:|
| Charge enable relay, active low | `P3.4 = 1` |
| Full-charge relay | `P3.6 = 0` |
| Fan relay | `P3.7 = 0` |
| Motor/Q6 relay enable | `P3.5 = 0` |
| Direction relay | `P0.3 = 0` |
| Footswitch relay | `P0.4 = 0` |
| DC/DC force | `P1.7 = 0` |
| Buzzer | `P4.2 = 0` |

Only the charge-enable polarity is directly proven active-low by original
firmware behavior. The other values match the established original inactive
state and successful relay bench tests, but connector-side electrical polarity
should still be measured before connecting a vehicle.

## Electrical Boundaries

- MCU pins are not pack-voltage tolerant. High-voltage connector signals pass
  through board conditioning, dividers, amplifiers, or optocouplers.
- Never inject pack voltage at an MCU pin or logic-level serial header.
- The voltage tap network was designed around the original battery topology.
  Its six values are grouped/tap measurements, not lithium cell monitoring.
- The SP3222E/SP3232E data sheet is included because the board family contains
  an RS-232 transceiver. Confirm whether a connector is logic-level UART or
  translated RS-232 before attaching an adapter.
- Firmware cannot be the only overcharge, overdischarge, overcurrent, or
  overtemperature protection for a lithium pack.

