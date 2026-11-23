#include "cc2_config.h"
#include "lithium.h"

#define EVENT_COUNT 8u

li_config_t li_config;
li_packet_t li_packet;
uint16_t li_faults;
uint8_t li_charge_state;
uint8_t li_charge_reason;
uint8_t li_charge_request;
uint8_t li_drive_permit;
uint8_t li_drive_reason;
uint8_t li_fan_request;

static uint8_t have_packet;
static uint32_t packet_time;
static uint32_t qualify_time;
static uint32_t charge_time;
static uint8_t complete_latched;
static li_event_t events[EVENT_COUNT];
static uint8_t event_next;
static uint8_t event_used;
static uint16_t event_total;
static uint16_t logged_faults;
static uint8_t logged_state;
static uint8_t logged_reason;

void li_defaults(void)
{
    li_config.charge_mode = LI_MODE_EXTERNAL;
    li_config.drive_mode = LI_MODE_EXTERNAL;
    li_config.fan_on_dc = 400;
    li_config.fan_off_dc = 350;
    li_config.cells = 0u;
    li_config.cell_min_mv = 0u;
    li_config.cell_max_mv = 0u;
    li_config.charge_min_dc = 0;
    li_config.charge_max_dc = 0;
    li_config.drive_min_dc = 0;
    li_config.drive_max_dc = 0;
    li_config.charge_max_da = 0u;
    li_config.discharge_max_da = 0u;
    li_config.charge_max_seconds = 0u;
}

void li_init(void)
{
    li_defaults();
    have_packet = 0u;
    li_faults = 0u;
    li_charge_state = LI_CHARGE_IDLE;
    li_charge_reason = LI_REASON_NONE;
    li_charge_request = 0u;
    li_drive_permit = 0u;
    li_drive_reason = LI_REASON_NONE;
    li_fan_request = 0u;
    complete_latched = 0u;
    event_next = event_used = 0u;
    event_total = 0u;
    logged_faults = 0u;
    logged_state = logged_reason = 255u;
}

uint8_t li_limits_valid(void)
{
    return li_config.cells > 0u && li_config.cells <= 96u &&
        li_config.cell_min_mv >= 500u &&
        li_config.cell_max_mv <= 5000u &&
        li_config.cell_min_mv < li_config.cell_max_mv &&
        li_config.charge_min_dc >= -500 &&
        li_config.charge_max_dc <= 1250 &&
        li_config.charge_min_dc < li_config.charge_max_dc &&
        li_config.drive_min_dc >= -500 &&
        li_config.drive_max_dc <= 1250 &&
        li_config.drive_min_dc < li_config.drive_max_dc &&
        li_config.charge_max_da > 0u && li_config.charge_max_da <= 30000u &&
        li_config.discharge_max_da > 0u && li_config.discharge_max_da <= 30000u &&
        li_config.charge_max_seconds > 0u;
}

uint8_t li_settings_valid(void)
{
    return li_config.charge_mode <= LI_MODE_SUPERVISED &&
        li_config.drive_mode <= LI_MODE_SUPERVISED &&
        li_config.fan_off_dc >= -500 && li_config.fan_on_dc <= 1250 &&
        li_config.fan_off_dc < li_config.fan_on_dc &&
        ((li_config.charge_mode != LI_MODE_SUPERVISED &&
          li_config.drive_mode != LI_MODE_SUPERVISED) || li_limits_valid());
}

uint8_t li_accept_packet(const li_packet_t *packet, uint32_t now)
{
    uint16_t delta;
    uint32_t pack_mv = (uint32_t)packet->pack_cv * 10UL;
    uint32_t minimum = (uint32_t)packet->cell_min_mv * packet->cells;
    uint32_t maximum = (uint32_t)packet->cell_max_mv * packet->cells;

    if (packet->flags & (uint8_t)~LI_BMS_FLAGS ||
        !(packet->flags & LI_BMS_VALID) ||
        packet->cells == 0u || packet->cells > 96u ||
        packet->cell_min_mv < 500u || packet->cell_max_mv > 5000u ||
        packet->cell_min_mv > packet->cell_max_mv ||
        packet->temp_min_dc < -500 || packet->temp_max_dc > 1250 ||
        packet->temp_min_dc > packet->temp_max_dc ||
        (packet->soc10 > 1000u && packet->soc10 != 65535u) ||
        pack_mv + 20UL < minimum || pack_mv > maximum + 20UL) return 0u;
    if (have_packet) {
        delta = (uint16_t)(packet->sequence - li_packet.sequence);
        if (delta == 0u || delta >= 32768u) return 0u;
    }
    li_packet = *packet;
    have_packet = 1u;
    packet_time = now;
    return 1u;
}

uint32_t li_link_age(uint32_t now)
{
    return have_packet ? (uint32_t)(now - packet_time) : 0xFFFFFFFFUL;
}

uint8_t li_link_fresh(uint32_t now)
{
    return have_packet && li_link_age(now) < CC2_BMS_TIMEOUT_MS;
}

void li_reset_link(void) { have_packet = 0u; }

void li_trip(uint16_t faults)
{
    li_faults |= faults;
    li_charge_request = 0u;
    li_drive_permit = 0u;
}

static uint16_t measurement_faults(uint8_t charging)
{
    uint16_t faults = 0u;
    if (li_packet.flags & LI_BMS_FAULT) faults |= LI_FAULT_BMS;
    if (li_packet.cells != li_config.cells ||
        li_packet.cell_min_mv < li_config.cell_min_mv ||
        li_packet.cell_max_mv > li_config.cell_max_mv) faults |= LI_FAULT_VOLTAGE;
    if (li_packet.temp_min_dc < (charging ? li_config.charge_min_dc : li_config.drive_min_dc) ||
        li_packet.temp_max_dc > (charging ? li_config.charge_max_dc : li_config.drive_max_dc)) {
        faults |= LI_FAULT_TEMPERATURE;
    }
    /* Protocol current is positive into the battery, negative during discharge. */
    if ((int32_t)li_packet.current_da > (int32_t)li_config.charge_max_da ||
        (int32_t)li_packet.current_da < -(int32_t)li_config.discharge_max_da) {
        faults |= LI_FAULT_CURRENT;
    }
    return faults;
}

uint8_t li_clear_faults(const li_inputs_t *inputs, uint32_t now)
{
    if (inputs->ignition || inputs->moving || inputs->drive_busy || li_charge_request) return 0u;
    if (li_config.charge_mode == LI_MODE_SUPERVISED || li_config.drive_mode == LI_MODE_SUPERVISED) {
        if (!li_limits_valid() || !li_link_fresh(now) || measurement_faults(0u) ||
            (inputs->charger && measurement_faults(1u))) return 0u;
    }
    li_faults = 0u;
    li_charge_state = LI_CHARGE_IDLE;
    return 1u;
}

static void log_change(uint32_t now)
{
    if (logged_faults == li_faults && logged_state == li_charge_state &&
        logged_reason == li_charge_reason) return;
    events[event_next].time_ms = now;
    events[event_next].faults = li_faults;
    events[event_next].state = li_charge_state;
    events[event_next].reason = li_charge_reason;
    event_next = (event_next + 1u) % EVENT_COUNT;
    if (event_used < EVENT_COUNT) event_used++;
    event_total++;
    logged_faults = li_faults;
    logged_state = li_charge_state;
    logged_reason = li_charge_reason;
}

void li_tick(uint32_t now, const li_inputs_t *inputs)
{
    uint8_t fresh = li_link_fresh(now);
    uint8_t charge_reason = LI_REASON_NONE;
    uint8_t drive_reason = LI_REASON_NONE;
    uint16_t faults;

    if (!inputs->charger) complete_latched = 0u;
    if (fresh) {
        if (li_packet.temp_max_dc >= li_config.fan_on_dc) li_fan_request = 1u;
        else if (li_packet.temp_max_dc <= li_config.fan_off_dc) li_fan_request = 0u;
    } else {
        li_fan_request = 0u;
    }

    if (li_config.drive_mode == LI_MODE_OFF) drive_reason = LI_REASON_DISABLED;
    else if (inputs->charger) drive_reason = LI_REASON_PERMISSION;
    else if (li_config.drive_mode == LI_MODE_SUPERVISED) {
        if (!li_limits_valid()) drive_reason = LI_REASON_CONFIG;
        else if (!fresh) {
            drive_reason = LI_REASON_LINK;
            if (li_drive_permit && inputs->ignition) li_trip(LI_FAULT_LINK);
        } else {
            faults = measurement_faults(0u);
            if (faults && inputs->ignition) li_trip(faults);
            if (faults) drive_reason = LI_REASON_PERMISSION;
            if (!(li_packet.flags & LI_BMS_DRIVE)) drive_reason = LI_REASON_PERMISSION;
        }
    }

    if (li_config.charge_mode == LI_MODE_OFF) charge_reason = LI_REASON_DISABLED;
    else if (inputs->ignition) charge_reason = LI_REASON_IGNITION;
    else if (inputs->moving || inputs->drive_busy) charge_reason = LI_REASON_MOVING;
    else if (li_config.charge_mode == LI_MODE_SUPERVISED) {
        if (!li_limits_valid()) charge_reason = LI_REASON_CONFIG;
        else if (!fresh) {
            charge_reason = LI_REASON_LINK;
            if (li_charge_request) li_trip(LI_FAULT_LINK);
        } else {
            faults = measurement_faults(1u);
            if (faults && inputs->charger) li_trip(faults);
            if (faults) charge_reason = LI_REASON_PERMISSION;
            if (inputs->charger && (li_packet.flags & LI_BMS_COMPLETE)) complete_latched = 1u;
            if (!(li_packet.flags & LI_BMS_CHARGE)) charge_reason = LI_REASON_PERMISSION;
        }
    }

    li_drive_reason = li_faults ? LI_REASON_FAULT : drive_reason;
    li_drive_permit = !li_faults && drive_reason == LI_REASON_NONE;
    li_charge_request = 0u;
    li_charge_reason = charge_reason;
    if (li_faults) {
        li_charge_state = LI_CHARGE_FAULT;
        li_charge_reason = LI_REASON_FAULT;
    } else if (!inputs->charger) {
        li_charge_state = LI_CHARGE_IDLE;
    } else if (complete_latched && li_config.charge_mode == LI_MODE_SUPERVISED) {
        li_charge_state = LI_CHARGE_COMPLETE;
        li_charge_reason = LI_REASON_COMPLETE;
    } else if (charge_reason != LI_REASON_NONE) {
        li_charge_state = LI_CHARGE_WAIT;
    } else if (li_charge_state == LI_CHARGE_ON) {
        if (li_config.charge_mode == LI_MODE_SUPERVISED &&
            (uint32_t)(now - charge_time) >= (uint32_t)li_config.charge_max_seconds * 1000UL) {
            li_trip(LI_FAULT_TIMEOUT);
            li_charge_state = LI_CHARGE_FAULT;
            li_charge_reason = LI_REASON_FAULT;
        } else li_charge_request = 1u;
    } else if (li_charge_state != LI_CHARGE_QUALIFY) {
        qualify_time = now;
        li_charge_state = LI_CHARGE_QUALIFY;
    } else if ((uint32_t)(now - qualify_time) >= CC2_CHARGE_QUALIFY_MS) {
        charge_time = now;
        li_charge_state = LI_CHARGE_ON;
        li_charge_request = 1u;
    }
    log_change(now);
}

uint8_t li_event_count(void) { return event_used; }
uint16_t li_event_total(void) { return event_total; }
uint8_t li_event_get(uint8_t oldest_index, li_event_t *event)
{
    if (oldest_index >= event_used) return 0u;
    *event = events[(event_next + EVENT_COUNT - event_used + oldest_index) % EVENT_COUNT];
    return 1u;
}
