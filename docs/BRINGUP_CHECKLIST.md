# Bring-Up Checklist

Use a current-limited bench supply and a disconnected/test harness. Keep the
charger, traction controller, and lithium pack contactors physically unable to
energize during the first phases.

## 1. Preserve Recovery Material

- Save the original HEX and verify its SHA-256.
- Confirm the bootstrap-loader entry/reset procedure before replacing code.
- Label adapter voltage levels and RX/TX orientation.
- Keep a known-good recovery image available on a second machine or drive.

## 2. Power-Only Inspection

- Inspect for shorts, corrosion, and connector damage.
- Confirm logic rails and the REF198 output near 4.096 V.
- Confirm MCU reset behavior and supply current.
- Verify every relay remains inactive with MCU held in reset.

## 3. Safe Basic Firmware

1. Build with `-SafeBringup`.
2. Flash `firmware/build_safe/CC2_BASIC_DRIVE_SAFE.hex`.
3. Open the logic-level UART at 19200 8N1 with no flow control.
4. Reset and expect `$BOOT,CC2-CUSTOM-SDCC,0.3#`.
5. Send `$PING#`, `$ID#`, `$DIN#`, `$SPEED#`, and `$DISPLAY#`.
6. Confirm P3.1 flashes only during transmission.
7. Confirm all drive relays remain inactive while speed and display operate.

## 4. Input Characterization

- Toggle one field input at a time and record both connector voltage and raw
  `$DIN#` state.
- Measure active and inactive polarity for charger, ignition, direction, trip,
  and speed.
- Locate the unresolved optocoupler input by continuity and controlled stimulus.
- Add debounce/qualification only after recording raw behavior.

## 5. Output Characterization

Build with `-EnableOutputTests` only on a test unit. Use `$OUT,<name>,1#`, then
immediately `$OUT,<name>,0#`, one output at a time. Record MCU pin level,
connector voltage, relay contact behavior, and whether the load is active.
`$OUT,SAFE#` must always return every controlled output to the inactive state.
The resulting image is `build_output_test/CC2_BASIC_DRIVE_OUTPUT_TEST.hex`.
Send
`$DRIVE,0#` before individual `$OUT` commands.

## 6. Automatic Drive Logic

After confirming raw input polarities on the test unit, flash
`CC2_BASIC_DRIVE.hex`. Verify ignition-on enters states 1, 2, then 3; ignition
off and charger-present each return immediately to state 0. Request a direction
change while speed is nonzero and verify motor/footswitch drop before the
direction relay changes. Use `$STATUS#` to observe state and delay.

## 7. Analog Characterization

- Read the internal raw ADC code before applying engineering-unit conversions.
- Compare AN0 mux channels S1 through S8 against a calibrated meter.
- Characterize both shunt channels at zero and known bidirectional currents.
- Characterize all installed temperature sensors across the required range.
- Add open/short/plausibility faults before any analog value can enable charge.

## 8. Display And EEPROM

- Scan only the expected I2C addresses with relays inactive.
- Verify display bank lines at idle and during address `0x3A` writes.
- Read and save both EEPROMs before the first write.
- Use `$DRIVE,0#`, then bounded `$EEPROM,READ,...#` commands for the backup.
- Confirm `$EEPROM,PROBE#` reports the M24C02 byte and AT25128A status.
- Test new-format EEPROM writes in an unused range, with readback and CRC.
- Interrupt power during a test update to prove the two-slot commit scheme.

## 9. Lithium Control

Do not enable an automatic charger state until independent pack protection,
charger interlocks, current calibration, temperature validation, timeout
handling, watchdog/reset behavior, and contactor-fault behavior have each been
tested separately. Every invalid sensor or invalid state transition should move
to a latched charge-off state.
