#ifndef SPEED_H
#define SPEED_H

#include <stdint.h>

#include "cc2_hw.h"

void speed_external_isr(void) __interrupt(CC2_EXTERNAL_3_6_INTERRUPT_VECTOR);
void speed_init(void);
void speed_capture_window(uint16_t elapsed_ms);
void speed_trip_button_service(uint8_t pressed);
void speed_reset_trip(void);
uint8_t speed_kmh(void);
uint16_t speed_trip_tenths_km(void);
uint32_t speed_odometer_tenths_km(void);
void speed_restore_odometer(uint32_t tenths);
uint32_t speed_total_pulses(void);
uint16_t speed_pending_pulses(void);
uint16_t speed_last_window_pulses(void);
uint16_t speed_pulses_per_100m(void);
uint8_t speed_set_pulses_per_100m(uint16_t value);

#endif
