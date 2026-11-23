#include "cc2_config.h"
#include "cc2_hw.h"
#include "safety.h"

volatile uint16_t safety_lease_ms;
volatile uint8_t safety_expired;

void safety_refresh(void)
{
    uint8_t saved = EA;
    EA = 0;
    if (!safety_expired) safety_lease_ms = CC2_FOREGROUND_LEASE_MS;
    EA = saved;
}

void safety_reset(void)
{
    uint8_t saved = EA;
    EA = 0;
    safety_expired = 0u;
    safety_lease_ms = CC2_FOREGROUND_LEASE_MS;
    EA = saved;
}

/* Original XC878 password/RMAP sequence; feed only after foreground progress. */
void watchdog_start(void) __naked
{
    __asm
        push 0xa8
        clr 0xaf
        orl 0x8f, #0x01
        mov 0xbc, #0x00
        orl 0xbb, #0x01
        anl 0x8f, #0xfe
        mov 0xbf, #0x01
        mov 0xbb, #0x9b
        mov 0xbf, #0x00
        orl 0x8f, #0x01
        orl 0xbb, #0x04
        anl 0x8f, #0xfe
        mov 0xbf, #0x01
        mov 0xbb, #0xab
        mov 0xbf, #0x00
        pop 0xa8
        ret
    __endasm;
}

void watchdog_service(void) __naked
{
    __asm
        push 0xa8
        clr 0xaf
        orl 0x8f, #0x01
        orl 0xbb, #0x02
        anl 0x8f, #0xfe
        pop 0xa8
        ret
    __endasm;
}
