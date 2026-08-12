# CC2 Custom Firmware Kit

This folder is a standalone starting point for writing replacement firmware for
the original XC878-based carcomputer. It contains only original CC2 hardware
information, original-firmware observations, component references, and a small
SDCC firmware scaffold. It has no source or build dependency on any other
vehicle-controller project in this repository.

The basic firmware now provides:

1. Runs the recovered XC878 oscillator/PLL startup sequence.
2. Configures the original board ports and immediately places controlled loads
   in their known inactive states.
3. Starts the service UART on `P1.0`/`P1.1` at 19200 baud, 8 data bits, no
   parity, one stop bit.
4. Counts falling-edge speed pulses on `P4.1`, calculates speed, trip, and a
   RAM-only odometer using the firmware-proven default calibration.
5. Initializes and refreshes the original four-driver dashboard with speed,
   trip, and odometer data using the corrected display-bank mapping.
6. Debounces ignition, charger-present, direction, and trip inputs.
7. Controls direction, motor-enable, and footswitch relays through a basic
   drive state machine with start delays and a stopped-speed direction
   interlock.
8. Provides a `$COMMAND#` terminal for status, speed, display, drive enable,
   and explicitly enabled manual output tests.

It does **not** implement lithium charging. The original board measures six
grouped battery-voltage taps, not individual lithium cells, and the shunt gain
and several input polarities still require bench calibration. Treat this board
as supervisory vehicle control only; pack-level protection must remain
independent and able to open charging/discharging without firmware cooperation.
The firmware also contains no ADC, shunt, battery-voltage, temperature, or
automatic legacy-EEPROM interpretation. Raw, page-aware drivers are provided
for both physical EEPROMs.

## Start Here

- [Hardware overview](docs/HARDWARE_OVERVIEW.md)
- [MCU pin map](docs/MCU_PINOUT.md)
- [Machine-readable pin map](docs/CC2_PINOUT.csv)
- [Connector transcription](docs/CONNECTORS.md)
- [Analog front end](docs/ANALOG_FRONT_END.md)
- [Display and storage](docs/DISPLAY_AND_STORAGE.md)
- [EEPROM driver and command guide](docs/EEPROM_GUIDE.md)
- [Legacy EEPROM address map](docs/LEGACY_EEPROM_MAP.md)
- [SDCC guide](docs/SDCC_GUIDE.md)
- [Flashing and recovery](docs/FLASHING_AND_RECOVERY.md)
- [Bring-up checklist](docs/BRINGUP_CHECKLIST.md)
- [Basic drive firmware behavior](docs/BASIC_DRIVE_FIRMWARE.md)
- [Lithium firmware architecture](docs/LITHIUM_ARCHITECTURE.md)
- [Evidence and unknowns](docs/EVIDENCE_AND_UNKNOWNS.md)

## Build

Install SDCC with MCS-51 support and make `sdcc`, `sdas8051`, and `packihx`
available on `PATH`, then run:

```powershell
cd firmware
.\build.ps1 -Clean
```

The normal image leaves all load-output commands locked. For a disconnected
bench unit where arbitrary manual relay operation is intended:

```powershell
.\build.ps1 -Clean -EnableOutputTests
```

The normal image automatically controls the direction, motor, and footswitch
relays from ignition/direction/charger inputs. To build speed, display, and
terminal support with that automatic relay path disabled:

```powershell
.\build.ps1 -Clean -SafeBringup
```

Artifacts are written to `firmware/build/`, `firmware/build_safe/`, and
`firmware/build_output_test/`. The build reports programmed and remaining
P-Flash bytes. The normal image is `CC2_BASIC_DRIVE.hex`, the non-driving image
is `CC2_BASIC_DRIVE_SAFE.hex`, and the arbitrary relay-test image is
`CC2_BASIC_DRIVE_OUTPUT_TEST.hex`. See
[the SDCC guide](docs/SDCC_GUIDE.md) before flashing.

## Bench Protocol

Commands use the original framing style and are case-sensitive:

```text
$PING#
$ID#
$HELP#
$DIN#
$STATUS#
$SPEED#
$SPEEDCAL,1620#
$TRIP,RESET#
$DISPLAY#
$DISPLAY,REFRESH#
$DISPLAY,TEST,50#
$DRIVE,0#
$DRIVE,1#
$EEPROM#
$EEPROM,PROBE#
$EEPROM,READ,VMS,0,16#
$EEPROM,READ,DISPLAY,0,16#
$EEPROM,UNLOCK,WRITE#
$EEPROM,WRITE,VMS,4096,42#
$EEPROM,LOCK#
$OUT,SAFE#
$OUT,FAN,1#
$OUT,FAN,0#
```

`$OUT,<name>,<0|1>#` is accepted only by a build made with
`-EnableOutputTests`. Every reset calls `outputs_all_safe()` before entering the
command loop. Disable automatic drive with `$DRIVE,0#` before using a manual
drive-relay command. `SPEEDCAL` is RAM-only and resets at the next MCU reset.
EEPROM addresses and write values are decimal; returned data bytes are
hexadecimal. EEPROM access requires `$DRIVE,0#`, and each verified byte write
requires a fresh unlock.

## Local Tools

Build all three variants without overwriting one another:

```powershell
.\tools\build_all.ps1
```

Send one command to a unit and listen for the response:

```powershell
.\tools\serial_command.ps1 -Port COM8 -Command '$STATUS#'
```

Monitor boot/status output continuously:

```powershell
.\tools\serial_command.ps1 -Port COM8 -Command '' -Monitor
```

## Reference Material

The `reference/` directory contains the original CC2 connector image, original
firmware image, and local component data sheets used by these notes. Its
`SHA256SUMS.txt` records the exact files used.
