# EEPROM Guide

The CC2 has two physically separate EEPROMs. The live dashboard drivers share
the display EEPROM's I2C wires, but they are not EEPROM storage.

## Device Summary

| Property | Display EEPROM | Vehicle EEPROM |
|---|---|---|
| Part family | ST M24C02 / 24C02 | Atmel AT25128A |
| Interface | Software I2C on P5 | XC878 hardware SSC/SPI |
| Capacity | 256 bytes | 16,384 bytes |
| Address bytes | One | Two, high byte first |
| Page size | 16 bytes | 64 bytes |
| Pins | SDA P5.7, SCL P5.5 | SCK P1.2, MOSI P1.3, MISO P1.4, `/CS` P1.5 |
| Read label/opcode | `0xA3` | `READ 0x03` |
| Write label/opcode | `0xA2` | `WRITE 0x02` after `WREN 0x06` |

The display labels are historical 8-bit I2C address bytes. The physical 7-bit
address is `0x51`. The original firmware's internal `0xA0` VMS dispatch label
is not an I2C address; the VMS EEPROM is SPI.

## Driver Files

| File | Responsibility |
|---|---|
| `firmware/src/i2c_bus.c` | P5 open-drain start, repeated-start, stop, byte, ACK, and NACK operations |
| `firmware/src/display_eeprom.c` | M24C02 bounded reads, 16-byte page writes, ACK polling, verification |
| `firmware/src/vehicle_eeprom.c` | XC878 SSC setup/ISR, AT25 status, reads, WREN, 64-byte page writes, verification |

Public APIs are declared in `display_eeprom.h` and `vehicle_eeprom.h`. Both
drivers reject null buffers and out-of-range requests. A zero-byte operation is
a successful no-op.

## Display EEPROM Transactions

Random/sequential read:

```text
START -> A2 -> memory address -> RESTART -> A3 -> data... -> NACK -> STOP
```

Page write:

```text
ACK-poll ready -> START -> A2 -> memory address -> up to page boundary -> STOP
```

The driver never allows a physical page write to wrap from the end of one
16-byte page to the beginning of that page. Longer API writes are split.

## Vehicle EEPROM Transactions

The XC878 SSC configuration follows the original firmware:

```text
SSC_CONL = 0x37
SSC_CONH = 0x46, then enable bit 0x80
SSC_BRH:SSC_BRL = 0x0011
completion interrupt vector = 7, code address 0x003B
```

Read:

```text
/CS low -> 03 -> address high -> address low -> dummy clocks/data -> /CS high
```

Write:

```text
poll RDSR/WIP clear -> WREN -> verify WEL -> /CS low
-> 02 -> address high -> address low -> page bytes -> /CS high
-> poll WIP clear
```

Status bits used by the driver are WIP bit 0 and WEL bit 1. Board-level `/WP`
and `/HOLD` connections are not firmware-controlled and should be traced if a
write fails despite WEL handling.

## Terminal Commands

Commands use decimal addresses/counts/values. Returned bytes are two-digit hex.
No EEPROM command is accepted for data access while automatic drive control is
enabled.

```text
$DRIVE,0#
$EEPROM#
$EEPROM,PROBE#
$EEPROM,READ,VMS,0,16#
$EEPROM,READ,DISPLAY,0,16#
$EEPROM,UNLOCK,WRITE#
$EEPROM,WRITE,VMS,4096,42#
$EEPROM,LOCK#
```

Reads are limited to 16 bytes per command. A terminal write changes exactly one
byte, reads it back, and automatically relocks whether it succeeds or fails.
Re-enabling drive also locks writes. The C driver APIs support longer requests
and split them across page boundaries.

`PROBE` reads display address 0 and the AT25128A status register. A display byte
of `FF` can be valid erased data; it is not by itself a failed probe. AT25
status commonly reads `00` when idle and unprotected, but protection bits may
differ.

## New Configuration Records

Do not treat original chemistry records as lithium configuration. Before using
any region, dump both complete devices and decide whether legacy data must be
preserved. A new record should include at least:

```text
magic, schema version, payload length, sequence, payload, CRC
```

Use two independently valid slots. Write the inactive slot, verify it, and
commit its validity/sequence last. On boot choose the newest record with valid
bounds, schema, and CRC. Never erase the only valid configuration before its
replacement has been verified.

