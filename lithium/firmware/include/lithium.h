#ifndef LITHIUM_H
#define LITHIUM_H

#include <stdint.h>

#define LI_MODE_OFF 0u
#define LI_MODE_EXTERNAL 1u
#define LI_MODE_SUPERVISED 2u

#define LI_BMS_CHARGE 1u
#define LI_BMS_DRIVE 2u
#define LI_BMS_COMPLETE 4u
#define LI_BMS_FAULT 8u
#define LI_BMS_VALID 16u
#define LI_BMS_FLAGS 31u

#define LI_FAULT_LINK 1u
#define LI_FAULT_BMS 2u
#define LI_FAULT_VOLTAGE 4u
#define LI_FAULT_TEMPERATURE 8u
#define LI_FAULT_CURRENT 16u
#define LI_FAULT_TIMEOUT 32u
#define LI_FAULT_CONTROL 64u
#define LI_FAULT_UART 128u

#define LI_CHARGE_IDLE 0u
#define LI_CHARGE_WAIT 1u
#define LI_CHARGE_QUALIFY 2u
#define LI_CHARGE_ON 3u
#define LI_CHARGE_COMPLETE 4u
#define LI_CHARGE_FAULT 5u

#define LI_REASON_NONE 0u
#define LI_REASON_DISABLED 1u
#define LI_REASON_IGNITION 2u
#define LI_REASON_MOVING 3u
#define LI_REASON_CONFIG 4u
#define LI_REASON_LINK 5u
#define LI_REASON_PERMISSION 6u
#define LI_REASON_COMPLETE 7u
#define LI_REASON_FAULT 8u

typedef struct {
    uint8_t charge_mode;
    uint8_t drive_mode;
    int16_t fan_on_dc;
    int16_t fan_off_dc;
    uint8_t cells;
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    int16_t charge_min_dc;
    int16_t charge_max_dc;
    int16_t drive_min_dc;
    int16_t drive_max_dc;
    uint16_t charge_max_da;
    uint16_t discharge_max_da;
    uint16_t charge_max_seconds;
} li_config_t;

typedef struct {
    uint16_t sequence;
    uint8_t flags;
    uint8_t cells;
    uint16_t pack_cv;
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    int16_t temp_min_dc;
    int16_t temp_max_dc;
    int16_t current_da;
    uint16_t soc10;
} li_packet_t;

typedef struct {
    uint8_t ignition;
    uint8_t charger;
    uint8_t moving;
    uint8_t drive_busy;
} li_inputs_t;

typedef struct {
    uint32_t time_ms;
    uint16_t faults;
    uint8_t state;
    uint8_t reason;
} li_event_t;

extern li_config_t li_config;
extern li_packet_t li_packet;
extern uint16_t li_faults;
extern uint8_t li_charge_state;
extern uint8_t li_charge_reason;
extern uint8_t li_charge_request;
extern uint8_t li_drive_permit;
extern uint8_t li_drive_reason;
extern uint8_t li_fan_request;

void li_init(void);
void li_defaults(void);
uint8_t li_limits_valid(void);
uint8_t li_settings_valid(void);
uint8_t li_accept_packet(const li_packet_t *packet, uint32_t now);
uint8_t li_link_fresh(uint32_t now);
uint32_t li_link_age(uint32_t now);
void li_tick(uint32_t now, const li_inputs_t *inputs);
void li_trip(uint16_t faults);
uint8_t li_clear_faults(const li_inputs_t *inputs, uint32_t now);
void li_reset_link(void);
uint8_t li_event_count(void);
uint16_t li_event_total(void);
uint8_t li_event_get(uint8_t oldest_index, li_event_t *event);

#endif
