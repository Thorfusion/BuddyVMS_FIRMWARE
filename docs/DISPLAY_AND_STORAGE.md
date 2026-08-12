# Display And Storage

The dashboard I2C bus and the vehicle-data SPI EEPROM are separate interfaces.
Keeping that distinction in code prevents several historical address-label
confusions.

## Display I2C Bus

| Signal | MCU pin | Access method |
|---|---|---|
| SDA | `P5.7` | `P5_DATA`/`P5_DIR` mask `0x80` |
| SCL | `P5.5` | `P5_DATA`/`P5_DIR` mask `0x20` |
| Dashboard pin 15 | `P0.7` | Bank control |
| Dashboard pin 16 | `P0.6` | Bank control |

P5 is not bit-addressable. Implement open-drain behavior by driving a line low
with its direction/data registers and releasing it for high; never declare P5
signals as `__sbit` objects.

The live dashboard uses four SAA1064-compatible LED-driver addresses:

| Historical 8-bit write label | 7-bit I2C address | Use |
|---:|---:|---|
| `0x70` | `0x38` | Speed LED bar, first block |
| `0x72` | `0x39` | Four-digit trip display |
| `0x74` | `0x3A` | Banked battery/ECO and odometer block |
| `0x76` | `0x3B` | Speed LED bar, second block |

For address `0x3A`, the original CC2 bank behavior is:

| P0.6 / pin 16 | P0.7 / pin 15 | Selected bank |
|---:|---:|---|
| High | High | Default/unselected state |
| Low | High | Battery/ECO |
| High | Low | Odometer |

Return both lines high after a banked transaction. Do not change a bank line in
the middle of an I2C byte.

## Display EEPROM

The display storage device is an ST M24C02/24C02:

- 7-bit I2C address: `0x51`.
- Historical 8-bit write/read labels: `0xA2`/`0xA3`.
- Capacity: 256 bytes.
- One-byte memory address.
- 16-byte physical page writes.
- Poll for write completion and never cross a 16-byte page in one page-write
  operation.

Do not confuse the display EEPROM's `0xA2` bus address with the original
firmware's internal storage-dispatch labels.

The standalone driver is implemented in `firmware/src/display_eeprom.c`. It
supports bounded sequential reads, page-aware writes, write-cycle ACK polling,
and readback verification.

## Vehicle EEPROM

The board contains an Atmel-marked `25128AN`, matching AT25128A behavior:

- Interface: SPI through XC878 SSC.
- Capacity: 128 Kbit / 16 KiB.
- Page size: 64 bytes.
- Address width: 16 bits.
- `P1.2=SCK`, `P1.3=MOSI`, `P1.4=MISO`, `P1.5=/CS`.

| Opcode | Operation |
|---:|---|
| `0x01` | WRSR |
| `0x02` | WRITE |
| `0x03` | READ |
| `0x04` | WRDI |
| `0x05` | RDSR |
| `0x06` | WREN |

The standalone SSC driver is implemented in `firmware/src/vehicle_eeprom.c`.
It reproduces the original SSC setup and vector-7 completion path, polls WIP,
checks WEL, splits writes at 64-byte boundaries, and supports readback
verification.

For new firmware, define a new storage layout with a magic value, schema
version, payload length, monotonic sequence number, and CRC. Keep two slots and
commit the newest valid slot last so power loss cannot destroy the only valid
configuration. Do not interpret legacy bytes as lithium settings.

## Suggested Driver Boundaries

```text
i2c_bus.c           shared open-drain start/stop/read/write primitives
display.c           SAA1064 addresses, segments, banks, presentation
display_eeprom.c    M24C02 page-aware reads/writes
vehicle_eeprom.c    AT25128A SSC transactions and new record format
```

Initialize both buses released and chip selects inactive before enabling any
periodic scheduler or interrupts.

See [EEPROM_GUIDE.md](EEPROM_GUIDE.md) for APIs and terminal use and
[LEGACY_EEPROM_MAP.md](LEGACY_EEPROM_MAP.md) for known original addresses.

