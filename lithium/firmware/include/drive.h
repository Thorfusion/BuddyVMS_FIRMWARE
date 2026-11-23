#ifndef DRIVE_H
#define DRIVE_H

#include <stdint.h>

typedef enum {
    CC2_DRIVE_OFF = 0,
    CC2_DRIVE_START_DELAY,
    CC2_DRIVE_MOTOR_WAIT,
    CC2_DRIVE_RUNNING,
    CC2_DRIVE_WAIT_STOP,
    CC2_DRIVE_DIRECTION_DEADTIME
} cc2_drive_state_t;

void drive_init(void);
void drive_tick_10ms(void);
uint8_t drive_set_control_enabled(uint8_t enabled);
uint8_t drive_control_enabled(void);
cc2_drive_state_t drive_state(void);
uint8_t drive_ignition_on(void);
uint8_t drive_charger_present(void);
uint8_t drive_forward_requested(void);
uint8_t drive_forward_applied(void);
uint8_t drive_trip_pressed(void);
uint16_t drive_delay_remaining_ms(void);

#endif
