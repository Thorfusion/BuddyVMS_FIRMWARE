#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>

typedef enum {
    CC2_OUTPUT_CHARGE = 0,
    CC2_OUTPUT_FULL,
    CC2_OUTPUT_FAN,
    CC2_OUTPUT_MOTOR,
    CC2_OUTPUT_DIRECTION,
    CC2_OUTPUT_FOOT,
    CC2_OUTPUT_DCDC,
    CC2_OUTPUT_BUZZER
} cc2_output_t;

void hardware_init(void);
void outputs_all_safe(void);
void output_set(cc2_output_t output, uint8_t active);
uint8_t input_charger(void);
uint8_t input_ignition(void);
uint8_t input_direction(void);
uint8_t input_trip(void);
uint8_t input_speed(void);

#endif

