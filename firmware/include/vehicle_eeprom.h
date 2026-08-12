#ifndef VEHICLE_EEPROM_H
#define VEHICLE_EEPROM_H

#include <stdint.h>

#include "cc2_hw.h"

void vehicle_eeprom_ssc_isr(void) __interrupt(CC2_SSC_INTERRUPT_VECTOR);
void vehicle_eeprom_init(void);
uint8_t vehicle_eeprom_read_status(uint8_t *status);
uint8_t vehicle_eeprom_read(uint16_t address,
                            uint8_t *data,
                            uint8_t count);
uint8_t vehicle_eeprom_write(uint16_t address,
                             const uint8_t *data,
                             uint8_t count);
uint8_t vehicle_eeprom_write_verified(uint16_t address,
                                      const uint8_t *data,
                                      uint8_t count);
uint16_t vehicle_eeprom_failure_count(void);

#endif

