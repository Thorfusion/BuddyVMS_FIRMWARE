# SDCC Guide For The XC878

## Toolchain

Use a recent Small Device C Compiler build with the MCS-51 backend. The required
programs are:

- `sdcc`: compiler and linker driver;
- `sdas8051`: 8051 assembler;
- `packihx`: converts SDCC Intel HEX output to a conventional packed HEX file.

The supplied scaffold was compiled and linked with SDCC `4.6.0 #16555` and
sdas8051 `5.50.4` in this workspace.

Official project and manual locations:

- `https://sdcc.sourceforge.net/`
- `https://sdcc.sourceforge.net/doc/sdccman.pdf`

Confirm the installed tools:

```powershell
sdcc --version
sdas8051 -v
packihx --help
```

Then build from `firmware/`:

```powershell
.\build.ps1 -Clean
```

The build script accepts explicit executable paths when SDCC is not on `PATH`:

```powershell
.\build.ps1 -Clean `
  -Sdcc C:\SDCC\bin\sdcc.exe `
  -Packihx C:\SDCC\bin\packihx.exe
```

## Build Choices

The firmware uses:

```text
-mmcs51
--model-large
--stack-auto
--std-sdcc99
--opt-code-size
--code-loc 0x0000
--xram-loc 0xF600
--xram-size 0x0600
--iram-size 0x100
--idata-loc 0x21
```

`--model-large` places ordinary non-local objects in XDATA and is forgiving as
the project grows. The fixed XDATA range is inside the board's mapped 3 KiB
window. Revisit the memory model only after checking the `.map` and `.mem`
files; 8051 pointer representation changes with the memory space.

## SDCC XC878 Declarations

SDCC does not need a vendor header if the required SFRs are declared locally:

```c
__sfr  __at(0x90) P1;
__sbit __at(0x97) P1_7;

__xdata uint8_t large_buffer[128];
__code const uint8_t lookup_table[] = { 1, 2, 3 };
```

Important qualifiers:

| Qualifier | Meaning |
|---|---|
| `__sfr __at(address)` | Direct byte SFR |
| `__sbit __at(address)` | Bit-address-space object |
| `__data` | Direct internal RAM |
| `__idata` | Indirect internal RAM |
| `__xdata` | External/XRAM data space |
| `__code` | Program-memory constant |
| `__at(address)` | Absolute placement; use sparingly |
| `__interrupt(n)` | Interrupt vector number |
| `__critical` | Disable interrupts around a function/block |

The same numeric address can mean different things in byte and bit address
spaces. In this target, byte SFR `0x92` is P5 data while bit address `0x92` is
P1.2.

## Startup Hook

SDCC calls `__sdcc_external_startup` before C data initialization. The firmware
implements that symbol in `startup_clock.asm`, configures the oscillator/PLL,
and maps XRAM. It returns zero in DPL so SDCC performs normal RAM/XRAM
initialization.

Do not move the protected clock sequence into ordinary C: the compiler may use
stack or XDATA before those resources are ready, and read-modify-write timing is
part of the recovered sequence.

## Interrupts

The UART remains polled, while global interrupts are enabled for Timer 0 and
the P4.1 EXINT4 speed input. Declare each handler in a translation unit linked
into the image and verify its vector in the `.map`/`.rst` output:

```c
void timer0_isr(void) __interrupt(1)
{
    /* Clear the hardware request and do bounded work only. */
}
```

Keep ISRs short. Copy multi-byte shared values inside a critical section or use
a sequence-counter snapshot. Do not call display, EEPROM, formatting, or charge
state-machine code from an ISR.

The supplied link should contain long jumps at code `0x000B` to Timer 0 and
`0x004B` to the shared EXINT3..6 handler.

## Flash Layout

The kit reserves code `0x0000..0xEFFF` for the application. The build rejects
an image that reaches `0xF000` and reports both total programmed bytes and the
contiguous free region. Inspect at least:

```text
build/cc2_custom.map
build/cc2_custom.mem
build/cc2_custom.ihx
build/CC2_BASIC_DRIVE.hex
```

Always retain the known original HEX and its checksum before replacing a unit.

## Common Failure Modes

- UART TX on P1.1 requires the **page-2 alternate-select pair**, not only
  `P1_DIR`.
- P3.1 is an LED, not UART TX.
- P5 cannot be controlled through `__sbit` aliases.
- Port and ADC SFR meanings change with their page register.
- SDCC generic pointers are larger and slower than memory-specific pointers.
- C startup needs the mapped XRAM before initializing large-model globals.
- A HEX that links successfully can still be unsafe if output polarity or
  connector orientation is wrong.
