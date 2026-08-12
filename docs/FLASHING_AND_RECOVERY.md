# Flashing And Recovery

The XC878 bootstrap-loader serial path and the application service UART share
`P1.0` RX and `P1.1` TX. P3.0/P3.1 are only indicator LEDs.

## Before The First Flash

1. Preserve and checksum the original `CC2_190430.hex`.
2. Prove that the existing bootstrap-loader tool can identify, erase, program,
   verify, and reset the test unit.
3. Record the exact reset/boot-entry sequence for the board revision.
4. Confirm adapter logic levels and cross TX to RX correctly.
5. Disconnect or inhibit charger, motor, contactors, and DC/DC loads.

The reset-button and boot-strap wiring have not been fully documented in the
available connector image. Do not assume that pressing reset alone selects the
bootstrap loader; use the procedure already proven on the specific board.

## Image Choice

| Image | Purpose |
|---|---|
| `build/CC2_BASIC_DRIVE.hex` | Normal image; automatically controls direction, motor, and footswitch relays |
| `build_safe/CC2_BASIC_DRIVE_SAFE.hex` | `-SafeBringup`; speed/display/terminal active, automatic drive disabled |
| `build_output_test/CC2_BASIC_DRIVE_OUTPUT_TEST.hex` | Bench-only arbitrary relay image built with `-EnableOutputTests` |
| `reference/CC2_190430.hex` | Preserved original recovery image |

The basic images occupy P-Flash below code `0xF000`. They do not place application
code in D-Flash. Configure a programming tool to verify the programmed bytes
against the selected HEX and do not erase/write unrelated nonvolatile regions
unless that behavior is understood and intended.

## First Boot

After programming the selected image, open the logic UART at 19200 baud, 8 data
bits, no parity, one stop bit, and no flow control. Reset the MCU and expect:

```text
$BOOT,CC2-CUSTOM-SDCC,0.3#
```

Then send `$PING#`. If there is no response:

- scope P1.1 directly during reset and `$PING#` traffic;
- confirm P1.1 page-2 alternate selection, not only GPIO direction;
- confirm host TX reaches P1.0 and is idle high;
- confirm the oscillator/PLL startup did not remain in a wait loop;
- confirm the adapter is logic UART rather than an RS-232-voltage interface;
- recover the original image if board behavior is uncertain.

Do not diagnose UART traffic from P3.1 alone. It is an active-low LED output
driven by firmware around transmitted bytes.
