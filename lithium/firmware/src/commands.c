#include "cc2_config.h"
#include "cc2_hw.h"
#include "commands.h"
#include "controller.h"
#include "display_eeprom.h"
#include "drive.h"
#include "lithium.h"
#include "legacy.h"
#include "protocol.h"
#include "reports.h"
#include "safety.h"
#include "settings.h"
#include "speed.h"
#include "timebase.h"
#include "vehicle_eeprom.h"
#include "uart.h"

static char buffer[CC2_COMMAND_BUFFER_SIZE];
static uint8_t length;
static uint8_t framing;
static uint32_t frame_started;

static uint8_t equal(const char *a, const char *b)
{
    while (*a && *b) if (*a++ != *b++) return 0u;
    return !*a && !*b;
}

static void read_report(uint8_t id, const char *command)
{
    if (!reports_request(id)) reports_reply(0u, command, REPORT_ERROR_BUSY);
}

static uint8_t mode_value(const char *text, uint8_t *mode)
{
    if (equal(text, "OFF")) *mode = LI_MODE_OFF;
    else if (equal(text, "EXTERNAL") || equal(text, "FORCE")) *mode = LI_MODE_EXTERNAL;
    else if (equal(text, "SUPERVISED")) *mode = LI_MODE_SUPERVISED;
    else return 0u;
    return 1u;
}

static void mode_command(uint8_t charging, const char *text)
{
    uint8_t mode;
    const char *name = charging ? "CHARGE" : "DRIVE";
    if (!mode_value(text, &mode)) { reports_reply(0u, name, REPORT_ERROR_SYNTAX); return; }
    if (mode != LI_MODE_OFF && !controller_idle()) {
        reports_reply(0u, name, REPORT_ERROR_INTERLOCK); return;
    }
    if (mode == LI_MODE_SUPERVISED && !li_limits_valid()) {
        reports_reply(0u, name, REPORT_ERROR_LIMITS); return;
    }
    if (charging) { li_config.charge_mode = mode; li_charge_request = 0u; }
    else {
        li_config.drive_mode = mode; li_drive_permit = 0u;
        (void)drive_set_control_enabled(mode != LI_MODE_OFF);
    }
    controller_apply_outputs();
    reports_reply(1u, name, 0u);
}

static void limits_command(char **fields)
{
    static int32_t values[10];
    static li_config_t previous;
    uint8_t i;
    if (!controller_idle()) { reports_reply(0u, "CONFIG", REPORT_ERROR_INTERLOCK); return; }
    for (i = 0u; i < 10u; i++) {
        if (!protocol_number(fields[i + 2u], &values[i]) ||
            ((i < 3u || i > 6u) && values[i] < 0) ||
            (i >= 3u && i <= 6u && values[i] > 32767)) {
            reports_reply(0u, "CONFIG", REPORT_ERROR_SYNTAX); return;
        }
    }
    if (values[0] > 96) { reports_reply(0u, "CONFIG", REPORT_ERROR_LIMITS); return; }
    previous = li_config;
    li_config.cells = (uint8_t)values[0];
    li_config.cell_min_mv = (uint16_t)values[1];
    li_config.cell_max_mv = (uint16_t)values[2];
    li_config.charge_min_dc = (int16_t)values[3];
    li_config.charge_max_dc = (int16_t)values[4];
    li_config.drive_min_dc = (int16_t)values[5];
    li_config.drive_max_dc = (int16_t)values[6];
    li_config.charge_max_da = (uint16_t)values[7];
    li_config.discharge_max_da = (uint16_t)values[8];
    li_config.charge_max_seconds = (uint16_t)values[9];
    if (!li_limits_valid()) {
        li_config = previous;
        reports_reply(0u, "CONFIG", REPORT_ERROR_LIMITS);
    } else reports_reply(1u, "CONFIG", 0u);
}

void commands_handle_line(char *line)
{
    static char *fields[13];
    static uint8_t data[16];
    uint8_t count;
    uint8_t device;
    int32_t number;
    int32_t other;
    li_inputs_t inputs;
    if (line[0] == 'B' && line[1] == 'M' && line[2] == 'S' && line[3] == ',' &&
        line[4] == '1' && line[5] == ',') {
        if (protocol_receive_bms(line, timebase_uptime_ms())) {
            controller_inputs(&inputs);
            li_tick(timebase_uptime_ms(), &inputs);
            controller_apply_outputs();
            /* A busy aggregate read must not reject fresh safety telemetry. */
            if (!reports_busy()) (void)reports_request(1u);
        } else reports_reply(0u, "BMS", REPORT_ERROR_SYNTAX);
        return;
    }
    count = protocol_split(line, fields, 13u);
    if (!count) { reports_reply(0u, "COMMAND", REPORT_ERROR_SYNTAX); return; }
    if (count == 1u && equal(fields[0], "IDENTIFY")) read_report(REPORT_IDENTIFY, "IDENTIFY");
    else if (count == 1u && equal(fields[0], "ID")) read_report(REPORT_IDENTIFY, "ID");
    else if (count == 1u && equal(fields[0], "VERSION")) read_report(2u, "VERSION");
    else if (count == 1u && equal(fields[0], "STATUS")) read_report(1u, "STATUS");
    else if (count == 1u && equal(fields[0], "REPORT")) read_report(REPORT_ALL, "REPORT");
    else if (count == 1u && equal(fields[0], "TEMPS")) read_report(3u, "TEMPS");
    else if (count == 1u && (equal(fields[0], "ELECTRICAL") || equal(fields[0], "BMS"))) read_report(4u, "ELECTRICAL");
    else if (count == 1u && equal(fields[0], "CHARGEREPORT")) read_report(6u, "CHARGEREPORT");
    else if (count == 1u && equal(fields[0], "CONFIG")) read_report(7u, "CONFIG");
    else if (count == 1u && (equal(fields[0], "RELAYS") || equal(fields[0], "DIN"))) read_report(8u, "RELAYS");
    else if (count == 1u && (equal(fields[0], "FAULTS") || equal(fields[0], "EVENTS"))) read_report(9u, "FAULTS");
    else if (count == 1u && (equal(fields[0], "STORAGE") || equal(fields[0], "EEPROM") ||
        equal(fields[0], "LEGACY") || equal(fields[0], "DISPLAY"))) read_report(13u, "STORAGE");
    else if (count == 1u && (equal(fields[0], "LIFETIME") || equal(fields[0], "SPEED"))) read_report(15u, "LIFETIME");
    else if (count == 1u && (equal(fields[0], "PING") || equal(fields[0], "HELP"))) reports_reply(1u, fields[0], 0u);
    else if (count == 2u && equal(fields[0], "BMS") && equal(fields[1], "RESET")) {
        if (!controller_idle()) { reports_reply(0u, "BMS", REPORT_ERROR_INTERLOCK); return; }
        li_reset_link(); reports_reply(1u, "BMS", 0u);
    } else if (count == 2u && equal(fields[0], "CONFIG") && equal(fields[1], "SAVE")) {
        if (!controller_idle()) { reports_reply(0u, "CONFIG", REPORT_ERROR_INTERLOCK); return; }
        if (settings_save()) reports_reply(1u, "CONFIG", 0u);
        else reports_reply(0u, "CONFIG", REPORT_ERROR_STORAGE);
    } else if (count == 2u && equal(fields[0], "CONFIG") && equal(fields[1], "DEFAULTS")) {
        if (!controller_idle()) { reports_reply(0u, "CONFIG", REPORT_ERROR_INTERLOCK); return; }
        li_defaults(); (void)drive_set_control_enabled(1u); reports_reply(1u, "CONFIG", 0u);
    } else if (count == 12u && equal(fields[0], "CONFIG") && equal(fields[1], "LIMITS")) limits_command(fields);
    else if (count == 4u && equal(fields[0], "CONFIG") && equal(fields[1], "FAN")) {
        if (!controller_idle()) { reports_reply(0u, "CONFIG", REPORT_ERROR_INTERLOCK); return; }
        if (!protocol_number(fields[2], &number) || !protocol_number(fields[3], &other)) {
            reports_reply(0u, "CONFIG", REPORT_ERROR_SYNTAX); return;
        }
        if (other < -500 || number > 1250 || other >= number) {
            reports_reply(0u, "CONFIG", REPORT_ERROR_LIMITS); return;
        }
        li_config.fan_on_dc = (int16_t)number; li_config.fan_off_dc = (int16_t)other;
        reports_reply(1u, "CONFIG", 0u);
    } else if (count == 2u && equal(fields[0], "CHARGE")) mode_command(1u, fields[1]);
    else if (count == 2u && equal(fields[0], "DRIVE")) mode_command(0u, fields[1]);
    else if (count == 1u && equal(fields[0], "STOP")) {
        controller_stop(); reports_reply(1u, "STOP", 0u);
    } else if (count == 2u && equal(fields[0], "FAULT") && equal(fields[1], "CLEAR")) {
        controller_inputs(&inputs);
        if (li_clear_faults(&inputs, timebase_uptime_ms())) { safety_reset(); reports_reply(1u, "FAULT", 0u); }
        else reports_reply(0u, "FAULT", REPORT_ERROR_FAULT_ACTIVE);
    } else if (count == 2u && equal(fields[0], "SPEEDCAL")) {
        if (!controller_idle()) { reports_reply(0u, "SPEEDCAL", REPORT_ERROR_INTERLOCK); return; }
        if (!protocol_number(fields[1], &number) || number < 100) {
            reports_reply(0u, "SPEEDCAL", REPORT_ERROR_SYNTAX); return;
        }
        if (legacy_save_speed_calibration((uint16_t)number)) reports_reply(1u, "SPEEDCAL", 0u);
        else reports_reply(0u, "SPEEDCAL", REPORT_ERROR_STORAGE);
    } else if (count == 2u && equal(fields[0], "TRIP") && equal(fields[1], "RESET")) {
        speed_reset_trip(); reports_reply(1u, "TRIP", 0u);
    } else if (count == 5u && equal(fields[0], "EEPROM") && equal(fields[1], "READ")) {
        if (!controller_idle()) { reports_reply(0u, "EEPROM", REPORT_ERROR_INTERLOCK); return; }
        if (!protocol_number(fields[3], &number) || number < 0 ||
            !protocol_number(fields[4], &other) || other <= 0 || other > 16) {
            reports_reply(0u, "EEPROM", REPORT_ERROR_SYNTAX); return;
        }
        if (reports_busy()) { reports_reply(0u, "EEPROM", REPORT_ERROR_BUSY); return; }
        device = equal(fields[2], "VMS") ? 1u : equal(fields[2], "DISPLAY") ? 2u : 0u;
        if (!device || !(device == 1u ? vehicle_eeprom_read((uint16_t)number, data, (uint8_t)other) :
            display_eeprom_read((uint16_t)number, data, (uint8_t)other))) {
            reports_reply(0u, "EEPROM", REPORT_ERROR_STORAGE); return;
        }
        reports_memory(device, (uint16_t)number, data, (uint8_t)other);
    } else reports_reply(0u, fields[0], REPORT_ERROR_UNSUPPORTED);
}

void commands_poll(void)
{
    char value;
    uint8_t budget = 24u;
    uint32_t now = timebase_uptime_ms();
    if (uart_rx_overflowed()) {
        /* The unreceived tail still belongs to the damaged frame. */
        framing = 4u; length = 0u;
        li_trip(LI_FAULT_UART); controller_apply_outputs(); return;
    }
    if (framing && framing != 4u && (uint32_t)(now - frame_started) >= 250UL) {
        framing = 4u; length = 0u;
        reports_reply(0u, "COMMAND", REPORT_ERROR_SYNTAX);
    }
    while (budget-- && uart_try_getc(&value)) {
        if (value == '$') { framing = 1u; length = 0u; frame_started = now; }
        else if (framing == 4u) {
            if (value == '\n' || value == '#') framing = length = 0u;
        }
        else if ((framing == 1u && value == '#') || (framing == 2u && value == '\n')) {
            buffer[length] = '\0';
            if (framing == 1u && equal(buffer, "ID")) read_report(REPORT_LEGACY_ID, "ID");
            else if (length) commands_handle_line(buffer);
            framing = 0u;
            return;
        } else if (value == '\r' && framing == 2u) {
            /* CR is accepted only as the optional character immediately before LF. */
            framing = 3u;
        } else if (framing == 3u && value == '\n') {
            buffer[length] = '\0'; framing = 0u;
            if (length) commands_handle_line(buffer);
            return;
        } else if (!framing && (value == '\r' || value == '\n')) { }
        else if (value >= 32 && value <= 126 && framing != 3u && length < sizeof(buffer) - 1u) {
            if (!framing) { framing = 2u; length = 0u; frame_started = now; }
            buffer[length++] = value;
        } else {
            framing = length = 0u;
            reports_reply(0u, "COMMAND", REPORT_ERROR_SYNTAX);
            /* Discard until a fresh delimiter, never execute an overflow suffix. */
            framing = 4u;
        }
    }
}
