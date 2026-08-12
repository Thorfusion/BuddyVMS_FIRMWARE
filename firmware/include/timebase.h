#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

#include "cc2_hw.h"

void timebase_timer0_isr(void) __interrupt(CC2_TIMER0_INTERRUPT_VECTOR);
void timebase_init(void);
uint8_t timebase_take_1ms(void);
uint32_t timebase_uptime_ms(void);

#endif

