#include <string.h>
#include "cc2_hw.h"
#include "cc2_config.h"
#include "cc2_build.h"
#include "controller.h"
#include "display.h"
#include "drive.h"
#include "legacy.h"
#include "lithium.h"
#include "reports.h"
#include "settings.h"
#include "speed.h"
#include "timebase.h"
#include "uart.h"
#include "vehicle_eeprom.h"
#include "display_eeprom.h"
#include "hardware.h"

#define LINE_SIZE 160u
#define REPLY_SIZE 40u

typedef struct {
    li_config_t config;
    li_packet_t packet;
    li_inputs_t inputs;
    li_event_t events[8];
    uint32_t now;
    uint32_t age;
    uint32_t odometer;
    uint32_t committed;
    uint32_t settings_seq;
    uint16_t faults;
    uint16_t event_sequence;
    uint16_t trip;
    uint16_t calibration;
    uint16_t vms_failures;
    uint16_t display_failures;
    uint16_t legacy_failures;
    uint16_t display_updates;
    uint16_t display_errors;
    uint8_t fresh;
    uint8_t soc_valid;
    uint8_t charge_state;
    uint8_t charge_reason;
    uint8_t drive_reason;
    uint8_t drive_state;
    uint8_t drive_permit;
    uint8_t speed;
    uint8_t requested;
    uint8_t applied;
    uint8_t raw_inputs;
    uint8_t settings_source;
    uint8_t settings_dirty;
    uint8_t slot_a_state;
    uint8_t slot_b_state;
    uint8_t boot_read_mask;
    uint8_t limits_valid;
    uint8_t odo_source;
    uint8_t cal_source;
    uint8_t legacy_phase;
    uint8_t display_ack;
    uint8_t display_ok;
    uint8_t event_count;
} report_snapshot_t;

static report_snapshot_t snapshot;
static char line[LINE_SIZE];
static uint8_t length;
static uint8_t position;
static uint8_t line_failed;
static char replies[2][REPLY_SIZE];
static uint8_t reply_head;
static uint8_t reply_count;
static uint8_t request;
static uint8_t report_id;
static uint8_t aggregate_index;
static uint8_t row;
static uint8_t records;
static uint32_t last_progress;
static uint8_t resync;

static void put(char c)
{
    if (length < LINE_SIZE - 2u) line[length++] = c;
    else line_failed = 1u;
}

static void text(const char *value) { while (*value) put(*value++); }

static void number(uint32_t value)
{
    uint32_t divisor = 1000000000UL;
    uint8_t started = 0u;
    while (divisor) {
        uint8_t digit = (uint8_t)(value / divisor);
        if (digit || started || divisor == 1UL) { put((char)('0' + digit)); started = 1u; }
        value %= divisor;
        divisor /= 10UL;
    }
}

static void field(uint32_t value) { put(','); number(value); }
static void signed_field(int16_t value)
{
    put(',');
    if (value < 0) { put('-'); number((uint32_t)(-(int32_t)value)); }
    else number((uint16_t)value);
}

static void hex_field(uint16_t value)
{
    uint8_t shift = 12u;
    put(',');
    while (1) {
        uint8_t digit = (value >> shift) & 15u;
        put((char)(digit < 10u ? '0' + digit : 'A' + digit - 10u));
        if (!shift) break;
        shift -= 4u;
    }
}

static void meter_field(uint32_t tenths)
{
    put(','); number(tenths);
    if (tenths) text("00");
}

static void begin_data(uint8_t field_id)
{
    text("@D"); field(report_id); field(field_id); records++;
}

static uint8_t bit_count(uint16_t value)
{
    uint8_t count = 0u;
    while (value) { count += value & 1u; value >>= 1; }
    return count;
}

static void capture(void)
{
    uint8_t i;
    snapshot.now = timebase_uptime_ms();
    snapshot.config = li_config;
    snapshot.packet = li_packet;
    snapshot.age = li_link_age(snapshot.now);
    snapshot.fresh = li_link_fresh(snapshot.now);
    snapshot.soc_valid = snapshot.fresh && li_packet.soc10 <= 1000u;
    snapshot.faults = li_faults;
    snapshot.charge_state = li_charge_state;
    snapshot.charge_reason = li_charge_reason;
    snapshot.drive_reason = li_drive_reason;
    snapshot.drive_permit = li_drive_permit;
    snapshot.drive_state = (uint8_t)drive_state();
    controller_inputs(&snapshot.inputs);
    snapshot.speed = speed_kmh();
    snapshot.odometer = speed_odometer_tenths_km();
    snapshot.trip = speed_trip_tenths_km();
    snapshot.calibration = speed_pulses_per_100m();
    snapshot.applied = (!CHARGE_ENABLE_RELAY_N ? 1u : 0u) | (MOTOR_RELAY_ENABLE ? 2u : 0u) |
        (FOOTSWITCH_RELAY ? 4u : 0u) | (DIRECTION_RELAY ? 8u : 0u) | (FAN_RELAY ? 16u : 0u);
    snapshot.requested = (li_charge_request ? 1u : 0u) |
        ((snapshot.drive_state == CC2_DRIVE_MOTOR_WAIT || snapshot.drive_state == CC2_DRIVE_RUNNING) ? 2u : 0u) |
        (snapshot.drive_state == CC2_DRIVE_RUNNING ? 4u : 0u) |
        (!drive_forward_applied() ? 8u : 0u) | (li_fan_request ? 16u : 0u);
    snapshot.raw_inputs = input_ignition() | (input_charger() << 1) | (input_direction() << 2) |
        (input_trip() << 3) | (input_speed() << 4);
    snapshot.settings_source = settings_source();
    snapshot.settings_seq = settings_sequence();
    snapshot.settings_dirty = settings_dirty();
    snapshot.slot_a_state = settings_slot_state(0u);
    snapshot.slot_b_state = settings_slot_state(1u);
    snapshot.boot_read_mask = legacy_boot_read_mask();
    snapshot.limits_valid = li_limits_valid();
    snapshot.vms_failures = vehicle_eeprom_failure_count();
    snapshot.display_failures = display_eeprom_failure_count();
    snapshot.odo_source = legacy_odometer_source();
    snapshot.cal_source = legacy_speed_source();
    snapshot.legacy_phase = legacy_write_phase();
    snapshot.legacy_failures = legacy_write_failures();
    snapshot.committed = legacy_committed_odometer();
    snapshot.display_ack = display_ack_mask();
    snapshot.display_ok = display_last_update_ok();
    snapshot.display_updates = display_update_count();
    snapshot.display_errors = display_failure_count();
    snapshot.event_count = li_event_count();
    snapshot.event_sequence = li_event_total();
    for (i = 0u; i < snapshot.event_count; i++) (void)li_event_get(i, &snapshot.events[i]);
}

void reports_init(void)
{
    request = length = position = line_failed = reply_head = reply_count = resync = 0u;
}

uint8_t reports_busy(void) { return request || position < length || reply_count; }

uint8_t reports_request(uint8_t id)
{
    uint8_t i;
    if (reports_busy()) return 0u;
    if (id != REPORT_ALL && id != REPORT_IDENTIFY && id != REPORT_LEGACY_ID) {
        for (i = 0u; i < CC2_AGGREGATE_COUNT; i++) if (cc2_aggregate_reports[i] == id) break;
        if (i == CC2_AGGREGATE_COUNT) return 0u;
    }
    request = id;
    aggregate_index = row = records = 0u;
    report_id = id == REPORT_ALL ? cc2_aggregate_reports[0] : id;
    capture();
    last_progress = timebase_uptime_ms();
    return 1u;
}

void reports_reply(uint8_t ok, const char *command, uint8_t code)
{
    uint8_t i = 0u;
    uint8_t slot;
    char *reply;
    if (reply_count == 2u) return;
    slot = (reply_head + reply_count) & 1u;
    reply = replies[slot];
    reply[i++] = '@';
    if (ok) { reply[i++] = 'O'; reply[i++] = 'K'; }
    else { reply[i++] = 'E'; reply[i++] = 'R'; reply[i++] = 'R'; }
    reply[i++] = ',';
    while (*command && i < REPLY_SIZE - 7u) {
        char c = *command++;
        reply[i++] = (c >= 'A' && c <= 'Z') ? c : '_';
    }
    reply[i++] = ',';
    if (code >= 10u) reply[i++] = (char)('0' + code / 10u);
    reply[i++] = (char)('0' + code % 10u);
    reply[i] = '\0';
    reply_count++;
}

void reports_memory(uint8_t device, uint16_t address, const uint8_t *data, uint8_t count)
{
    uint8_t i;
    if (reports_busy()) { reports_reply(0u, "EEPROM", REPORT_ERROR_BUSY); return; }
    length = position = line_failed = 0u;
    text("@OK,EEPROM"); field(device); field(address); field(count);
    for (i = 0u; i < count; i++) field(data[i]);
    put('\n');
    last_progress = timebase_uptime_ms();
}

static uint8_t data_row(void)
{
    li_config_t *config = &snapshot.config;
    li_packet_t *packet = &snapshot.packet;
    li_inputs_t *inputs = &snapshot.inputs;
    uint8_t index;
    uint8_t value;
    if (report_id == 1u) {
        if (row == 1u) {
            begin_data(1u);
            field(inputs->charger ? 1u : inputs->ignition ? 2u : 0u);
            field(snapshot.charge_state);
            field(snapshot.speed); hex_field(snapshot.faults); hex_field(0u);
        } else if (row == 2u) {
            begin_data(2u); field(inputs->ignition); field(inputs->charger); field(snapshot.drive_permit);
            field(snapshot.drive_state); field(snapshot.charge_reason); field(snapshot.drive_reason);
            field(snapshot.soc_valid); field(snapshot.fresh); field(snapshot.age); field(snapshot.now);
        } else if (row == 3u) {
            if (!snapshot.soc_valid) return 2u;
            begin_data(3u); field(packet->soc10 / 10u);
        } else return 0u;
    } else if (report_id == 2u) {
        if (row != 1u) return 0u;
        begin_data(1u); field(3u); field(1u); field(0u);
    } else if (report_id == 3u) {
        if (row == 1u) { begin_data(1u); field(snapshot.fresh); field(snapshot.age); field(snapshot.fresh ? 1u : 0u); }
        else if (row == 2u) {
            if (!snapshot.fresh) return 2u;
            begin_data(2u); signed_field(packet->temp_min_dc); signed_field(packet->temp_max_dc);
        } else return 0u;
    } else if (report_id == 4u) {
        if (row == 1u) {
            begin_data(1u); field(snapshot.fresh); field(snapshot.age);
            field(snapshot.age == 0xFFFFFFFFUL ? 0u : packet->sequence); field(snapshot.soc_valid);
        } else if (row == 2u) {
            if (!snapshot.fresh) return 2u;
            begin_data(2u); field(packet->pack_cv); field(packet->cell_min_mv); field(packet->cell_max_mv);
            field(packet->cells); signed_field(packet->current_da); hex_field(packet->flags);
        } else if (row == 3u) {
            if (!snapshot.soc_valid) return 2u;
            begin_data(3u); field((uint32_t)packet->soc10 * 10UL);
        } else return 0u;
    } else if (report_id == 6u) {
        if (row != 1u) return 0u;
        begin_data(1u); field(config->charge_mode); field(snapshot.charge_state); field(snapshot.charge_reason);
        field((snapshot.requested & 1u) != 0u); field((snapshot.applied & 1u) != 0u);
        field(inputs->charger); field(inputs->ignition); field(inputs->moving); field(inputs->drive_busy);
        field(config->charge_mode == LI_MODE_SUPERVISED); hex_field(snapshot.faults);
    } else if (report_id == 7u) {
        if (row == 1u) {
            begin_data(1u); field(config->charge_mode); field(config->drive_mode); field(snapshot.settings_source);
            field(snapshot.settings_seq); field(snapshot.settings_dirty); field(snapshot.limits_valid);
        } else if (row == 2u) {
            begin_data(2u); signed_field(config->fan_on_dc); signed_field(config->fan_off_dc);
            field(snapshot.calibration); field(CC2_BMS_TIMEOUT_MS); field(CC2_CHARGE_QUALIFY_MS);
            field(CC2_FOREGROUND_LEASE_MS); field(CC2_SAFE_BRINGUP);
        } else if (row == 3u) {
            if (!snapshot.limits_valid) return 2u;
            begin_data(3u); field(config->cells); field(config->cell_min_mv); field(config->cell_max_mv);
            signed_field(config->charge_min_dc); signed_field(config->charge_max_dc);
            signed_field(config->drive_min_dc); signed_field(config->drive_max_dc);
            field(config->charge_max_da); field(config->discharge_max_da); field(config->charge_max_seconds);
        } else return 0u;
    } else if (report_id == 8u) {
        if (row == 1u) {
            begin_data(2u); hex_field(snapshot.requested); hex_field(snapshot.applied);
            hex_field(0u); hex_field(0u);
        } else if (row == 2u) { begin_data(3u); hex_field(snapshot.raw_inputs); }
        else return 0u;
    } else if (report_id == 9u) {
        if (row == 1u) {
            begin_data(1u); hex_field(snapshot.faults); hex_field(0u); field(bit_count(snapshot.faults));
            field(snapshot.event_sequence); field(snapshot.event_count);
        } else if (row <= 9u) {
            value = row - 2u;
            if (!(snapshot.faults & ((uint16_t)1u << value))) return 2u;
            begin_data(2u); field(value + 1u); field(1u);
        } else {
            index = row - 10u;
            if (index >= snapshot.event_count) return 0u;
            begin_data(3u); field(index);
            field((uint16_t)(snapshot.event_sequence - snapshot.event_count + index + 1u));
            field(snapshot.events[index].time_ms); field(snapshot.events[index].state);
            field(snapshot.events[index].reason); hex_field(snapshot.events[index].faults); hex_field(0u);
        }
    } else if (report_id == 13u) {
        if (row == 1u) {
            begin_data(1u); field(snapshot.settings_source); field(snapshot.settings_seq); field(snapshot.settings_dirty);
            field(snapshot.vms_failures); field(snapshot.display_failures);
            field(snapshot.slot_a_state); field(snapshot.slot_b_state);
        } else if (row == 2u) {
            begin_data(5u); field(snapshot.odo_source); field(snapshot.cal_source);
            field(snapshot.legacy_phase); field(snapshot.legacy_failures);
            field((snapshot.boot_read_mask & 1u) != 0u); field((snapshot.boot_read_mask & 2u) != 0u);
        } else if (row == 3u) {
            begin_data(6u); hex_field(snapshot.display_ack); field(snapshot.display_errors);
            field(snapshot.display_updates); field(snapshot.display_ok);
        } else return 0u;
    } else if (report_id == 15u) {
        if (row == 1u) { begin_data(1u); field(snapshot.odo_source != 0u); field(snapshot.odo_source); field(snapshot.now); }
        else if (row == 2u) {
            if (!snapshot.odo_source) return 2u;
            begin_data(2u); meter_field(snapshot.odometer); meter_field(snapshot.committed);
        } else if (row == 3u) { begin_data(3u); field(snapshot.trip); field(snapshot.calibration); }
        else return 0u;
    } else return 0u;
    return 1u;
}

static void next_line(void)
{
    uint8_t result;
    length = position = line_failed = 0u;
    if (resync) { put('\n'); resync = 0u; return; }
    if (reply_count) {
        text(replies[reply_head]);
        reply_head ^= 1u; reply_count--;
    } else if (request == REPORT_IDENTIFY) {
        text("@OK,IDENTIFY,1,CC2_LITHIUM,XC878,LITHIUM," CC2_BUILD_ID "," CC2_PROTOCOL_SHA256);
        request = 0u;
    } else if (request == REPORT_LEGACY_ID) {
        text("$ID," CC2_FIRMWARE_NAME "," CC2_BUILD_ID "#"); request = 0u;
    } else if (request) {
        if (!row) { text("@B"); field(report_id); field(cc2_report_versions[report_id]); row = 1u; }
        else {
            do { result = data_row(); if (result) row++; } while (result == 2u);
            if (!result) {
                text("@E"); field(report_id); field(records);
                row = records = 0u;
                if (request == REPORT_ALL && ++aggregate_index < CC2_AGGREGATE_COUNT) report_id = cc2_aggregate_reports[aggregate_index];
                else request = 0u;
            }
        }
    }
    if (length) put('\n');
    if (line_failed) { request = length = 0u; resync = 1u; }
}

void reports_poll(void)
{
    uint8_t budget = 8u;
    uint32_t now = timebase_uptime_ms();
    if (position == length) next_line();
    while (position < length && budget--) {
        if (!uart_try_putc(line[position])) break;
        position++;
        last_progress = now;
    }
    if (position < length && (uint32_t)(now - last_progress) >= 1000UL) {
        /* No success terminator after a partial transmit; resynchronize by LF. */
        request = length = position = 0u;
        resync = 1u;
        last_progress = now;
    }
}
