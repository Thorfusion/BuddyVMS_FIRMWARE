; Recovered from the original CC2 protected oscillator/PLL startup.
; This runs before SDCC initializes RAM or XRAM.

        .module startup_clock
        .globl ___sdcc_external_startup
        .area CSEG (CODE)

___sdcc_external_startup:
        mov  0xBF,#0x01
        mov  0xBB,#0xC0
        anl  0xB6,#0xFB
        orl  0xB6,#0x40
        orl  0xB6,#0x20
        anl  0xB6,#0xF7
        mov  r2,#0x1E
clock_delay_outer:
        mov  r3,#0x64
clock_delay_inner:
        djnz r3,clock_delay_inner
        djnz r2,clock_delay_outer
        orl  0xB6,#0x02
        mov  r0,#0x41
clock_delay_short:
        djnz r0,clock_delay_short
clock_wait_osc:
        mov  a,0xB6
        jnb  0xE0,clock_wait_osc
        orl  0xB6,#0x04
        mov  0xB7,#0x88
        mov  0xEA,#0x00
        mov  a,#0x00
        orl  0xEA,a
        mov  a,#0x00
        orl  0xBA,a
        anl  0xB6,#0xDF
        orl  0xB6,#0x80
clock_wait_pll_1:
        mov  a,0xB7
        jnb  0xE1,clock_wait_pll_1
clock_wait_pll_0:
        mov  a,0xB7
        jnb  0xE0,clock_wait_pll_0
        anl  0xB6,#0xBF
        mov  0xBB,#0xC3
        mov  0xBF,#0x00
        mov  0x96,#0x1F
        mov  dpl,#0x00
        ret
