#ifndef SAFETY_H
#define SAFETY_H
#include <stdint.h>
extern volatile uint16_t safety_lease_ms;
extern volatile uint8_t safety_expired;
void safety_refresh(void);
void safety_reset(void);
void watchdog_start(void);
void watchdog_service(void);
#endif
