# PoldenTEK CC2 Lithium Firmware

Independent SDCC firmware for the original XC878 carcomputer and original
dashboard. This is a new lithium vehicle supervisor, not a completed clone of
the original firmware and not firmware for the newer carcomputer hardware.

**Default charging mode is EXTERNAL / FORCE, as requested.** After one second
of stable prerequisites, charge enable stays requested while plugged in and
ignition is off. The external charger and independent BMS must regulate and
stop the actual charge. No adapter heartbeat, SOC, cell voltage, current or
temperature limit is enforced by the CC in this mode. There is no NiMH
maintenance, balance operation, float cycle or automatic SOC-to-100% reset.
The FULL/balance relay always stays inactive.

The charger itself must already have the correct lithium charging profile.
Changing these relay commands does not turn an old NiMH charger into a lithium
charger. Independent BMS protection must interrupt charging and traction
without relying on this MCU, serial connection or software.

## Included

- Ignition, charger and direction inputs; sequenced motor/footswitch relays.
- Speed, trip and original odometer, with legacy EEPROM record preservation.
- Original dashboard speed, odometer and BMS SOC bars; CHG/FULL/Err indication.
- Our versioned, CRC-protected universal BMS-adapter protocol over the existing
  P1.0 RX / P1.1 TX service UART, 19200 baud, 8N1.
- Independent OFF, EXTERNAL and SUPERVISED modes for charging and driving.
- Supervised permissions, measurement limits, timeout and latched faults.
- CRC-protected redundant lithium settings; no automatic EEPROM formatting.
- Read-only diagnostics, RAM event history, test tools and SDCC builds.

Grouped on-board voltage inputs, shunt ADC and NTC acquisition are deliberately
not used by this version. Battery readings and SOC come from the future BMS
adapter, not fabricated analog conversions. Trip is RAM-only. Odometer and
speed calibration retain their original EEPROM encodings. VIN, serial number
and other unowned original records are left untouched.

## Build And Test

From this folder, with SDCC and Python on PATH:

```powershell
.\firmware\build.ps1
.\firmware\build.ps1 -SafeBringup
.\tools\verify_all.ps1
```

Normal image: `firmware/build/CC2_LITHIUM.hex`.
Loads-disabled image: `firmware/build_safe/CC2_LITHIUM_SAFE.hex`.
The `build_test` image is simulator-only and must not be flashed.
Ready-built firmware and verification reports are placed in `releases/v0.1.0`.
See [build and verification](docs/BUILD_AND_TEST.md).

## Service Commands

Commands retain the familiar `$COMMAND#` framing, but this is not the complete
original command protocol. Mode/config changes are RAM-only until explicitly
saved. Enabling a mode, editing configuration and saving require key off,
charger unplugged, stopped speed and no active drive relays. OFF and STOP are
accepted immediately.

| Command | Purpose |
|---|---|
| `$ID#`, `$PING#`, `$HELP#` | Identity, communication and command list |
| `$STATUS#`, `$REPORT#` | Checksummed CC protocol status and stop reasons |
| `$BMS#`, `$CONFIG#`, `$LEGACY#` | Adapter, settings and legacy storage status |
| `$CHARGE,FORCE#` | Default: continuous external-BMS charge-enable request |
| `$CHARGE,SUPERVISED#` | Use adapter permission and configured limits |
| `$CHARGE,OFF#` | Immediately inhibit charging |
| `$DRIVE,EXTERNAL#`, `$DRIVE,SUPERVISED#`, `$DRIVE,OFF#` | Drive policy |
| `$STOP#` | Both policies OFF in RAM; does not change stored modes |
| `$CONFIG,DEFAULTS#` | Restore RAM defaults, including FORCE charging |
| `$CONFIG,SAVE#` | Explicitly save current lithium settings |
| `$FAULT,CLEAR#` | Explicit stopped reset; refuses active supervised causes |
| `$BMS,RESET#` | Reset sequence tracking while idle after adapter restart |
| `$LOG#` | Eight most recent RAM transitions, oldest first |
| `$DIN#`, `$SPEED#`, `$DISPLAY#` | Raw inputs, motion and display diagnostics |
| `$TRIP,RESET#` | Reset trip only, not odometer |
| `$SPEEDCAL,1620#` | Save pulses/100 m in the three original checked records |
| `$EEPROM,READ,VMS,0,16#` | Read up to 16 bytes; DISPLAY also supported |

If neither lithium settings copy is valid and the slots are not completely
erased, or storage cannot be read, both modes start OFF with settings source 3.
This avoids silently replacing a damaged saved OFF policy with FORCE. After
backup and inspection, `$CONFIG,DEFAULTS#` then `$CONFIG,SAVE#` explicitly
initializes lithium settings. A completely erased pair uses the normal FORCE
default. No boot-time writes are performed.

## Documents

- [Control behavior, interlocks and faults](docs/BEHAVIOR.md)
- [Universal adapter protocol](docs/PROTOCOL.md)
- [Build, storage preservation and verification](docs/BUILD_AND_TEST.md)

This is a bench-testable first version. Physical XC878 timing, watchdog reset,
electrical relay polarity, EEPROM interruption behavior and complete charging
sessions still need hardware validation. Do not infer vehicle readiness from
successful simulator tests.
