#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>
#define SETTINGS_SLOT_A 0x1000u
#define SETTINGS_SLOT_B 0x1040u
#define SETTINGS_RECORD_SIZE 48u
uint8_t settings_load(void);
uint8_t settings_save(void);
uint8_t settings_source(void);
uint32_t settings_sequence(void);
uint8_t settings_dirty(void);
uint8_t settings_slot_state(uint8_t slot);
#endif
