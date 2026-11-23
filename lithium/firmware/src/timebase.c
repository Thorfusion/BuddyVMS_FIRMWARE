#include "cc2_hw.h"
#include "timebase.h"
#include "safety.h"

static volatile uint8_t pending_ms;
static volatile uint32_t uptime_ms;

void timebase_timer0_isr(void) __interrupt(CC2_TIMER0_INTERRUPT_VECTOR)
{
    TH0 = CC2_TIMER0_RELOAD_HIGH;
    TL0 = CC2_TIMER0_RELOAD_LOW;
    if (pending_ms != 0xFFu) {
        pending_ms++;
    }
    uptime_ms++;
    if (safety_lease_ms) {
        safety_lease_ms--;
        if (!safety_lease_ms) {
            uint8_t saved_page = PORT_PAGE;
            PORT_PAGE = CC2_PORT_PAGE_DATA;
            CHARGE_ENABLE_RELAY_N = 1;
            FULL_CHARGE_RELAY = 0;
            MOTOR_RELAY_ENABLE = 0;
            FOOTSWITCH_RELAY = 0;
            /* Preserve direction while the vehicle may still be moving. */
            PORT_PAGE = saved_page;
            safety_expired = 1u;
        }
    }
}

void timebase_init(void)
{
    pending_ms = 0u;
    uptime_ms = 0UL;
    TMOD = (TMOD & 0xF0u) | 0x01u;
    TH0 = CC2_TIMER0_RELOAD_HIGH;
    TL0 = CC2_TIMER0_RELOAD_LOW;
    IE |= 0x02u;
    TR0 = 1;
}

uint8_t timebase_take_1ms(void)
{
    uint8_t saved_ea = EA;

    EA = 0;
    if (pending_ms == 0u) {
        EA = saved_ea;
        return 0u;
    }
    pending_ms--;
    EA = saved_ea;
    return 1u;
}

uint32_t timebase_uptime_ms(void)
{
    uint8_t saved_ea = EA;
    uint32_t value;

    EA = 0;
    value = uptime_ms;
    EA = saved_ea;
    return value;
}
