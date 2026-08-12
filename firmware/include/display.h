#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define CC2_DISPLAY_SPEED_A 0x70u
#define CC2_DISPLAY_TRIP 0x72u
#define CC2_DISPLAY_BANKED 0x74u
#define CC2_DISPLAY_SPEED_B 0x76u

void display_bus_init(void);
uint8_t display_init(void);
uint8_t display_clear(void);
uint8_t display_refresh(uint8_t speed_kmh,
                        uint16_t trip_tenths,
                        uint32_t odometer_tenths);
uint8_t display_diagnostic_sweep(uint8_t value);
uint8_t display_ack_mask(void);
uint16_t display_failure_count(void);
uint16_t display_update_count(void);
uint8_t display_last_update_ok(void);

#endif

