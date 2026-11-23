#ifndef DISPLAY_EEPROM_H
#define DISPLAY_EEPROM_H

#include <stdint.h>

#define DISPLAY_EEPROM_SIZE 256u
#define DISPLAY_EEPROM_PAGE_SIZE 16u
#define DISPLAY_EEPROM_WRITE_ADDRESS 0xA2u
#define DISPLAY_EEPROM_READ_ADDRESS 0xA3u

uint8_t display_eeprom_read(uint16_t address,
                            uint8_t *data,
                            uint8_t count);
uint8_t display_eeprom_write(uint16_t address,
                             const uint8_t *data,
                             uint8_t count);
uint8_t display_eeprom_write_verified(uint16_t address,
                                      const uint8_t *data,
                                      uint8_t count);
uint16_t display_eeprom_failure_count(void);

#endif
