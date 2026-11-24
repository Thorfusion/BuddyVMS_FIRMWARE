# MCU Pin Map

Confidence is based on original firmware side effects, MCU data-sheet mapping,
board tracing, and meter-confirmed tests. `Raw` means the logical level is
reported exactly as read; field polarity must be confirmed at the connector.

## Controlled Outputs

| MCU pin | Function | Active level | Confidence |
|---|---|---:|---|
| `P3.4` | Charge-enable relay | Low | High |
| `P3.6` | Full-charge/Ah relay | High, working assumption | High pin; medium electrical polarity |
| `P3.7` | Fan relay | High, working assumption | High pin; medium electrical polarity |
| `P3.5` | Q6 motor-relay enable | High, working assumption | High pin; medium electrical polarity |
| `P0.3` | Direction relay | High, working assumption | High pin; medium electrical polarity |
| `P0.4` | Footswitch relay | High, working assumption | High pin; medium electrical polarity |
| `P1.7` | DC/DC force output | High, working assumption | High pin; medium firmware semantics |
| `P4.2` | Buzzer | High | High |
| `P5.2` | Standby auxiliary output | Unresolved | High pin behavior; low physical role |

The native diagnostic described `P5.2` low as DC/DC forced on and high as
shutdown allowed. Its electrical relationship to `P1.7` remains unresolved, so
the starter does not drive it.

## Digital Inputs

| MCU pin | Function | Notes |
|---|---|---|
| `P4.0` | Charger-connected detect | Raw level; original firmware samples it |
| `P4.4` | Ignition detect | Raw level; original firmware samples it |
| `P5.0` | Direction input | `P5_DATA & 0x01`; P5 is byte-only, not bit-addressable |
| `P0.5` | Trip button | Raw level |
| `P4.1` | Speed pulse | EXINT4-capable; original config uses falling edge |
| Unknown | 72 V optocoupler / pedal / motor-relay feedback | Must be traced before use |
| `P5.3` | Tied to ground | Do not configure as an output |

## Communications And Display

| MCU pin | Function | Notes |
|---|---|---|
| `P1.0` | UART RX | `MODPISEL.URRIS` selects the UART input; host TX connects here |
| `P1.1` | UART TX | Page-2 alternate routing pair; verified service connection |
| `P3.0` | ADC/activity LED | Active low; independent of UART pins |
| `P3.1` | Controller-work LED in original CC2 | Active low; starter currently repurposes it for UART-transmit activity |
| `P5.7` | I2C SDA | Use byte masks on `P5_DATA`/`P5_DIR` |
| `P5.5` | I2C SCL | Use byte masks on `P5_DATA`/`P5_DIR` |
| `P0.7` | Dashboard pin 15 | Display bank control |
| `P0.6` | Dashboard pin 16 | Display bank control |
| `P1.2` | AT25128A SCK | SSC alternate function `SCK_0` |
| `P1.3` | AT25128A MOSI/SI | SSC alternate function `MTSR_0` |
| `P1.4` | AT25128A MISO/SO | SSC alternate function `MRST_0` |
| `P1.5` | AT25128A `/CS` | Software-controlled active-low chip select |


The original CC2 activates P3.1 around scheduled work, not around each
transmitted byte. The current starter's `uart_putc` drives the same independent
LED as `UART_TX_LED_N`. Distinguish firmware behavior from physical wiring
when using the LED to diagnose communication.

## Analog Channels

| ADC channel | Board function | Confidence |
|---:|---|---|
| AN0 | ADG408 common output through divider | High |
| AN1 | Shunt amplifier path, lower gain candidate | High pin; unknown gain/polarity |
| AN2 | Shunt amplifier path, higher gain candidate | High pin; unknown gain/polarity |
| AN3 | Dashboard light level | High |
| AN4 | Battery-box NTC | High |
| AN5 | Front temperature input | High |
| AN6 | Rear temperature input | High |
| AN7 | Spare NTC, unused on at least one vehicle variant | High |

## XC878 Addressing Traps

- Byte SFR `0x92` is `P5_DATA` when `PORT_PAGE=0`; bit address `0x92` is
  `P1.2`. Instruction form chooses the address space.
- Byte SFR `0xC8` is `P4_DATA`; bit address `0xCA` is `P4.2`, not a generic
  8052 timer bit.
- `P5_DATA` and `P5_DIR` are byte SFRs at `0x92` and `0x93`; do not create
  fake `__sbit` aliases for P5.
- Direct SFRs `0xCA..0xD3` are ADC page dependent. Always set and restore
  `ADC_PAGE` around accesses.
- `P1` data and direction SFRs change purpose on port pages 1 through 3. Set
  `PORT_PAGE=0` before normal GPIO reads/writes.

