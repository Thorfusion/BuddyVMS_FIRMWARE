#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>
uint16_t protocol_crc(const uint8_t *data, uint8_t length);
uint16_t protocol_crc_byte(uint16_t crc, uint8_t value);
uint8_t protocol_split(char *line, char **fields, uint8_t maximum);
uint8_t protocol_number(const char *text, int32_t *value);
uint8_t protocol_receive_bms(char *line, uint32_t now);
#endif
