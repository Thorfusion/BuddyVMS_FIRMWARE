#include "cc2_config.h"
#include "cc2_hw.h"
#include "commands.h"
#include "controller.h"
#include "display.h"
#include "display_eeprom.h"
#include "drive.h"
#include "hardware.h"
#include "lithium.h"
#include "legacy.h"
#include "protocol.h"
#include "safety.h"
#include "settings.h"
#include "speed.h"
#include "timebase.h"
#include "uart.h"
#include "vehicle_eeprom.h"

static char buffer[CC2_COMMAND_BUFFER_SIZE];
static uint8_t length;
static uint8_t active;
static uint32_t frame_started;

static uint8_t equal(const char *a, const char *b)
{
    while (*a && *b) if (*a++ != *b++) return 0u;
    return !*a && !*b;
}

static void signed_number(int16_t value)
{
    if (value < 0) { uart_putc('-'); uart_put_u16((uint16_t)(-(int32_t)value)); }
    else uart_put_u16((uint16_t)value);
}

static void field(uint32_t value) { uart_putc(','); uart_put_u32(value); }
static void signed_field(int16_t value) { uart_putc(','); signed_number(value); }

static void config_report(void)
{
    uart_puts("$CONFIG,MODES");
    field(li_config.charge_mode); field(li_config.drive_mode);
    uart_puts(",FAN"); signed_field(li_config.fan_on_dc); signed_field(li_config.fan_off_dc);
    uart_puts(",SOURCE"); field(settings_source()); field(settings_sequence());
    uart_puts("#\r\n$CONFIG,LIMITS");
    field(li_config.cells); field(li_config.cell_min_mv); field(li_config.cell_max_mv);
    signed_field(li_config.charge_min_dc); signed_field(li_config.charge_max_dc);
    signed_field(li_config.drive_min_dc); signed_field(li_config.drive_max_dc);
    field(li_config.charge_max_da); field(li_config.discharge_max_da);
    field(li_config.charge_max_seconds); field(li_limits_valid());
    uart_puts("#\r\n");
}

static void bms_report(void)
{
    uint32_t now = timebase_uptime_ms();
    uart_puts("$BMS,STATUS");
    field(li_link_fresh(now)); field(li_link_age(now));
    if (li_link_age(now) != 0xFFFFFFFFUL) {
        field(li_packet.sequence); field(li_packet.flags); field(li_packet.cells);
        field(li_packet.pack_cv); field(li_packet.cell_min_mv); field(li_packet.cell_max_mv);
        signed_field(li_packet.temp_min_dc); signed_field(li_packet.temp_max_dc);
        signed_field(li_packet.current_da); field(li_packet.soc10);
    }
    uart_puts("#\r\n");
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
    if (!mode_value(text, &mode)) { uart_puts("$ERR,MODE#\r\n"); return; }
    if (mode != LI_MODE_OFF && !controller_idle()) {
        uart_puts("$ERR,KEY-OFF-UNPLUG-STOP-FIRST#\r\n"); return;
    }
    if (mode == LI_MODE_SUPERVISED && !li_limits_valid()) {
        uart_puts("$ERR,CONFIGURE-LIMITS-FIRST#\r\n"); return;
    }
    if (charging) { li_config.charge_mode = mode; li_charge_request = 0u; }
    else { li_config.drive_mode = mode; li_drive_permit = 0u;
        (void)drive_set_control_enabled(mode != LI_MODE_OFF); }
    controller_apply_outputs();
    uart_puts("$OK,MODE,RAM-ONLY#\r\n");
}

static void limits_command(char **fields)
{
    static int32_t values[10];
    static li_config_t previous;
    uint8_t i;
    if (!controller_idle()) { uart_puts("$ERR,KEY-OFF-UNPLUG-STOP-FIRST#\r\n"); return; }
    for (i = 0u; i < 10u; i++) {
        if (!protocol_number(fields[i + 2u], &values[i]) ||
            ((i < 3u || i > 6u) && values[i] < 0) ||
            (i >= 3u && i <= 6u && values[i] > 32767)) {
            uart_puts("$ERR,LIMITS,NUMBER#\r\n"); return;
        }
    }
    if (values[0] > 96) { uart_puts("$ERR,LIMITS,CELLS#\r\n"); return; }
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
        uart_puts("$ERR,LIMITS,RANGE#\r\n");
    } else uart_puts("$OK,LIMITS,RAM-ONLY#\r\n");
}

static void log_report(void)
{
    li_event_t event;
    uint8_t i;
    uart_puts("$LOG,OLDEST-FIRST"); field(li_event_total()); field(li_event_count()); uart_puts("#\r\n");
    for (i = 0u; li_event_get(i, &event); i++) {
        uart_puts("$EVENT"); field(i); field(event.time_ms); field(event.state);
        field(event.reason); field(event.faults); uart_puts("#\r\n");
    }
}

void commands_handle_line(char *line)
{
    static char *fields[13];
    uint8_t count;
    uint8_t i;
    static uint8_t data[16];
    uint16_t mode;
    int32_t number;
    int32_t other;
    li_inputs_t inputs;
    if (line[0] == 'B' && line[1] == 'M' && line[2] == 'S' && line[3] == ',' &&
        line[4] == '1' && line[5] == ',') {
        if (protocol_receive_bms(line, timebase_uptime_ms())) {
            controller_inputs(&inputs);
            li_tick(timebase_uptime_ms(), &inputs);
            controller_apply_outputs();
            protocol_send_status();
        } else uart_puts("$ERR,BMS,FRAME#\r\n");
        return;
    }
    count = protocol_split(line, fields, 13u);
    if (!count) { uart_puts("$ERR,FIELDS#\r\n"); return; }
    if (count == 1u && equal(fields[0], "PING")) uart_puts("$PONG#\r\n");
    else if (count == 1u && equal(fields[0], "ID")) {
        uart_puts("$ID," CC2_FIRMWARE_NAME "," CC2_FIRMWARE_VERSION "#\r\n");
    } else if (count == 1u && equal(fields[0], "HELP")) {
        uart_puts("$HELP,PING,ID,STATUS,REPORT,BMS,CONFIG,CHARGE,DRIVE,STOP,FAULT,LOG,DIN,SPEED,TRIP,DISPLAY,EEPROM,LEGACY#\r\n");
    } else if (count == 1u && (equal(fields[0], "STATUS") || equal(fields[0], "REPORT"))) {
        protocol_send_status();
    } else if (count == 1u && equal(fields[0], "BMS")) bms_report();
    else if (count == 2u && equal(fields[0], "BMS") && equal(fields[1], "RESET") && controller_idle()) {
        li_reset_link(); uart_puts("$OK,BMS,RESET#\r\n");
    } else if (count == 1u && equal(fields[0], "CONFIG")) config_report();
    else if (count == 2u && equal(fields[0], "CONFIG") && equal(fields[1], "SAVE") && controller_idle()) {
        uart_puts(settings_save() ? "$OK,CONFIG,SAVED#\r\n" : "$ERR,CONFIG,SAVE#\r\n");
    } else if (count == 2u && equal(fields[0], "CONFIG") && equal(fields[1], "DEFAULTS") && controller_idle()) {
        li_defaults(); (void)drive_set_control_enabled(1u);
        uart_puts("$OK,CONFIG,DEFAULTS,RAM-ONLY#\r\n");
    } else if (count == 12u && equal(fields[0], "CONFIG") && equal(fields[1], "LIMITS")) limits_command(fields);
    else if (count == 4u && equal(fields[0], "CONFIG") && equal(fields[1], "FAN") && controller_idle() &&
        protocol_number(fields[2], &number) && protocol_number(fields[3], &other) &&
        other >= -500 && number <= 1250 && other < number) {
        li_config.fan_on_dc = (int16_t)number; li_config.fan_off_dc = (int16_t)other;
        uart_puts("$OK,FAN,RAM-ONLY#\r\n");
    } else if (count == 2u && equal(fields[0], "CHARGE")) mode_command(1u, fields[1]);
    else if (count == 2u && equal(fields[0], "DRIVE")) mode_command(0u, fields[1]);
    else if (count == 1u && equal(fields[0], "STOP")) {
        controller_stop(); uart_puts("$OK,STOP,RAM-ONLY#\r\n");
    } else if (count == 2u && equal(fields[0], "FAULT") && equal(fields[1], "CLEAR")) {
        controller_inputs(&inputs);
        if (li_clear_faults(&inputs, timebase_uptime_ms())) {
            safety_reset(); uart_puts("$OK,FAULT,CLEARED#\r\n");
        } else uart_puts("$ERR,FAULT,CAUSE-ACTIVE-OR-NOT-STOPPED#\r\n");
    } else if (count == 1u && equal(fields[0], "LOG")) log_report();
    else if (count == 1u && equal(fields[0], "DIN")) {
        uart_puts("$DIN,RAW-IGN,RAW-CHG,RAW-DIR,RAW-TRIP,RAW-SPD");
        field(input_ignition()); field(input_charger()); field(input_direction());
        field(input_trip()); field(input_speed()); uart_puts("#\r\n");
    } else if (count == 1u && equal(fields[0], "SPEED")) {
        uart_puts("$SPEED"); field(speed_kmh()); field(speed_trip_tenths_km());
        field(speed_odometer_tenths_km()); field(speed_last_window_pulses());
        field(speed_pulses_per_100m()); uart_puts("#\r\n");
    } else if (count == 2u && equal(fields[0], "SPEEDCAL") && controller_idle() &&
        protocol_number(fields[1], &number) && number >= 100 && legacy_save_speed_calibration((uint16_t)number)) {
        uart_puts("$OK,SPEEDCAL,SAVED-LEGACY#\r\n");
    } else if (count == 2u && equal(fields[0], "TRIP") && equal(fields[1], "RESET")) {
        speed_reset_trip(); uart_puts("$OK,TRIP#\r\n");
    } else if (count == 1u && equal(fields[0], "DISPLAY")) {
        uart_puts("$DISPLAY"); field(display_ack_mask()); field(display_failure_count());
        field(display_update_count()); field(display_last_update_ok()); uart_puts("#\r\n");
    } else if (count == 1u && equal(fields[0], "EEPROM")) {
        uart_puts("$EEPROM"); field(vehicle_eeprom_failure_count()); field(display_eeprom_failure_count()); uart_puts("#\r\n");
    } else if (count == 1u && equal(fields[0], "LEGACY")) {
        uart_puts("$LEGACY"); field(legacy_odometer_source()); field(legacy_speed_source());
        field(legacy_committed_odometer()); field(legacy_write_phase());
        field(legacy_write_failures()); uart_puts("#\r\n");
    } else if (count == 5u && equal(fields[0], "EEPROM") && equal(fields[1], "READ") && controller_idle() &&
        protocol_number(fields[3], &number) && number >= 0 &&
        protocol_number(fields[4], &other) && other > 0 && other <= 16) {
        mode = equal(fields[2], "VMS") ? 1u : equal(fields[2], "DISPLAY") ? 2u : 0u;
        if (!mode || !(mode == 1u ? vehicle_eeprom_read((uint16_t)number, data, (uint8_t)other) :
            display_eeprom_read((uint16_t)number, data, (uint8_t)other))) {
            uart_puts("$ERR,EEPROM,READ#\r\n"); return;
        }
        uart_puts("$EEPROM,DATA");
        for (i = 0u; i < (uint8_t)other; i++) { uart_putc(','); uart_put_hex8(data[i]); }
        uart_puts("#\r\n");
    } else uart_puts("$ERR,COMMAND-OR-INTERLOCK#\r\n");
}

void commands_poll(void)
{
    char value;
    uint8_t budget = 24u;
    uint32_t now = timebase_uptime_ms();
    if (uart_rx_overflowed()) {
        active = length = 0u;
        li_trip(LI_FAULT_UART);
        controller_apply_outputs();
        return;
    }
    if (active && (uint32_t)(now - frame_started) >= 250UL) active = length = 0u;
    while (budget-- && uart_try_getc(&value)) {
        if (value == '$') {
            active = 1u; length = 0u; frame_started = now;
        } else if (active && value == '#') {
            buffer[length] = '\0'; active = 0u;
            commands_handle_line(buffer);
            return;
        } else if (active && value >= 32 && value <= 126 && length < sizeof(buffer) - 1u) {
            buffer[length++] = value;
        } else if (active) {
            active = length = 0u;
            uart_puts("$ERR,FRAME#\r\n"); return;
        }
    }
}
