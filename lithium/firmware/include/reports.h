#ifndef REPORTS_H
#define REPORTS_H
#include <stdint.h>

#define REPORT_IDENTIFY 254u
#define REPORT_LEGACY_ID 253u
#define REPORT_ALL 255u
#define REPORT_ERROR_BUSY 20u
#define REPORT_ERROR_UNSUPPORTED 2u
#define REPORT_ERROR_SYNTAX 3u
#define REPORT_ERROR_INTERLOCK 4u
#define REPORT_ERROR_LIMITS 5u
#define REPORT_ERROR_STORAGE 6u
#define REPORT_ERROR_FAULT_ACTIVE 7u

void reports_init(void);
uint8_t reports_busy(void);
uint8_t reports_request(uint8_t id);
void reports_poll(void);
void reports_reply(uint8_t ok, const char *command, uint8_t code);
void reports_memory(uint8_t device, uint16_t address, const uint8_t *data, uint8_t count);

#endif
