#ifndef LEGACY_H
#define LEGACY_H
#include <stdint.h>
void legacy_init(void);
void legacy_service(uint32_t now, uint8_t stopped);
uint8_t legacy_save_speed_calibration(uint16_t value);
uint8_t legacy_odometer_source(void);
uint8_t legacy_speed_source(void);
uint16_t legacy_write_failures(void);
uint8_t legacy_write_phase(void);
uint32_t legacy_committed_odometer(void);
uint8_t legacy_boot_read_mask(void);
#endif
