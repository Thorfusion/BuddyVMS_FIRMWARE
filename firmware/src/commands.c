#include <stdint.h>

#include "cc2_config.h"
#include "cc2_hw.h"
#include "commands.h"
#include "display.h"
#include "display_eeprom.h"
#include "drive.h"
#include "hardware.h"
#include "speed.h"
#include "timebase.h"
#include "uart.h"
#include "vehicle_eeprom.h"

#define COMMAND_FIELD_COUNT 5u
#define EEPROM_COMMAND_READ_MAX 16u

static char command_buffer[CC2_COMMAND_BUFFER_SIZE];
static uint8_t command_length;
static uint8_t command_active;
static uint8_t eeprom_write_unlocked;

static uint8_t string_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) return 0u;
    }
    return (*left == '\0' && *right == '\0') ? 1u : 0u;
}

static uint8_t split_fields(char **fields)
{
    uint8_t count = 1u;
    uint8_t index;

    fields[0] = command_buffer;
    for (index = 0u; command_buffer[index] != '\0'; index++) {
        if (command_buffer[index] == ',' && count < COMMAND_FIELD_COUNT) {
            command_buffer[index] = '\0';
            fields[count++] = &command_buffer[index + 1u];
        }
    }
    return count;
}

static uint8_t parse_u16(const char *text, uint16_t *value)
{
    uint32_t result = 0UL;

    if (*text == '\0') return 0u;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return 0u;
        result = result * 10UL + (uint8_t)(*text - '0');
        if (result > 65535UL) return 0u;
        text++;
    }
    *value = (uint16_t)result;
    return 1u;
}

static void put_bit(uint8_t value)
{
    uart_putc(value ? '1' : '0');
}

static void command_din(void)
{
    uart_puts("$DIN,RAW-CHG=");
    put_bit(input_charger());
    uart_puts(",RAW-IGN=");
    put_bit(input_ignition());
    uart_puts(",RAW-DIR=");
    put_bit(input_direction());
    uart_puts(",RAW-TRIP=");
    put_bit(input_trip());
    uart_puts(",RAW-SPD=");
    put_bit(input_speed());
    uart_puts(",IGN-ON=");
    put_bit(drive_ignition_on());
    uart_puts(",CHG-PRESENT=");
    put_bit(drive_charger_present());
    uart_puts("#\r\n");
}

static void command_status(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    uart_puts("$STATUS,STATE=");
    uart_put_u8((uint8_t)drive_state());
    uart_puts(",AUTO=");
    put_bit(drive_control_enabled());
    uart_puts(",IGN=");
    put_bit(drive_ignition_on());
    uart_puts(",CHG=");
    put_bit(drive_charger_present());
    uart_puts(",REQ=");
    uart_putc(drive_forward_requested() ? 'F' : 'R');
    uart_puts(",APPLIED=");
    uart_putc(drive_forward_applied() ? 'F' : 'R');
    uart_puts(",KMH=");
    uart_put_u8(speed_kmh());
    uart_puts(",DELAY=");
    uart_put_u16(drive_delay_remaining_ms());
    uart_puts(",MOTOR=");
    put_bit(MOTOR_RELAY_ENABLE);
    uart_puts(",FOOT=");
    put_bit(FOOTSWITCH_RELAY);
    uart_puts(",DIR-RELAY=");
    put_bit(DIRECTION_RELAY);
    uart_puts("#\r\n");
}

static void command_speed(void)
{
    uart_puts("$SPEED,KMH=");
    uart_put_u8(speed_kmh());
    uart_puts(",WINDOW-PULSES=");
    uart_put_u16(speed_last_window_pulses());
    uart_puts(",PENDING=");
    uart_put_u16(speed_pending_pulses());
    uart_puts(",TOTAL=");
    uart_put_u32(speed_total_pulses());
    uart_puts(",TRIP10=");
    uart_put_u16(speed_trip_tenths_km());
    uart_puts(",ODO10=");
    uart_put_u32(speed_odometer_tenths_km());
    uart_puts(",CAL=");
    uart_put_u16(speed_pulses_per_100m());
    uart_puts("#\r\n");
}

static void command_display(void)
{
    uart_puts("$DISPLAY,ACK=");
    uart_put_u8(display_ack_mask());
    uart_puts(",FAIL=");
    uart_put_u16(display_failure_count());
    uart_puts(",UPDATES=");
    uart_put_u16(display_update_count());
    uart_puts(",LAST-OK=");
    put_bit(display_last_update_ok());
    uart_puts("#\r\n");
}

static uint8_t parse_output(const char *name, cc2_output_t *output)
{
    if (string_equal(name, "CHARGE")) *output = CC2_OUTPUT_CHARGE;
    else if (string_equal(name, "FULL")) *output = CC2_OUTPUT_FULL;
    else if (string_equal(name, "FAN")) *output = CC2_OUTPUT_FAN;
    else if (string_equal(name, "MOTOR")) *output = CC2_OUTPUT_MOTOR;
    else if (string_equal(name, "DIRECTION")) *output = CC2_OUTPUT_DIRECTION;
    else if (string_equal(name, "FOOT")) *output = CC2_OUTPUT_FOOT;
    else if (string_equal(name, "DCDC")) *output = CC2_OUTPUT_DCDC;
    else if (string_equal(name, "BUZZER")) *output = CC2_OUTPUT_BUZZER;
    else return 0u;
    return 1u;
}

static uint8_t eeprom_is_display(const char *name, uint8_t *is_display)
{
    if (string_equal(name, "DISPLAY")) {
        *is_display = 1u;
    } else if (string_equal(name, "VMS")) {
        *is_display = 0u;
    } else {
        return 0u;
    }
    return 1u;
}

static uint8_t eeprom_read_selected(uint8_t is_display,
                                    uint16_t address,
                                    uint8_t *data,
                                    uint8_t count)
{
    if (is_display) return display_eeprom_read(address, data, count);
    return vehicle_eeprom_read(address, data, count);
}

static uint8_t eeprom_write_selected(uint8_t is_display,
                                     uint16_t address,
                                     const uint8_t *data)
{
    if (is_display) {
        return display_eeprom_write_verified(address, data, 1u);
    }
    return vehicle_eeprom_write_verified(address, data, 1u);
}

static void command_eeprom_status(void)
{
    uart_puts("$EEPROM,UNLOCKED=");
    put_bit(eeprom_write_unlocked);
    uart_puts(",DISPLAY-FAIL=");
    uart_put_u16(display_eeprom_failure_count());
    uart_puts(",VMS-FAIL=");
    uart_put_u16(vehicle_eeprom_failure_count());
    uart_puts("#\r\n");
}

static void command_eeprom_probe(void)
{
    uint8_t display_value;
    uint8_t status;
    uint8_t display_ok;
    uint8_t vms_ok;

    if (drive_control_enabled()) {
        uart_puts("$ERR,DISABLE-DRIVE-FIRST#\r\n");
        return;
    }
    display_ok = display_eeprom_read(0u, &display_value, 1u);
    vms_ok = vehicle_eeprom_read_status(&status);
    uart_puts("$EEPROM,PROBE,DISPLAY=");
    if (display_ok) uart_put_hex8(display_value);
    else uart_puts("ERR");
    uart_puts(",VMS-STATUS=");
    if (vms_ok) uart_put_hex8(status);
    else uart_puts("ERR");
    uart_puts("#\r\n");
}

static void command_eeprom_read(char **fields)
{
    uint8_t data[EEPROM_COMMAND_READ_MAX];
    uint8_t is_display;
    uint8_t count;
    uint8_t index;
    uint16_t address;
    uint16_t parsed_count;

    if (drive_control_enabled()) {
        uart_puts("$ERR,DISABLE-DRIVE-FIRST#\r\n");
        return;
    }
    if (!eeprom_is_display(fields[2], &is_display) ||
        !parse_u16(fields[3], &address) ||
        !parse_u16(fields[4], &parsed_count) ||
        parsed_count == 0u || parsed_count > EEPROM_COMMAND_READ_MAX) {
        uart_puts("$ERR,EEPROM,RANGE#\r\n");
        return;
    }
    count = (uint8_t)parsed_count;
    if (!eeprom_read_selected(is_display, address, data, count)) {
        uart_puts("$ERR,EEPROM,READ#\r\n");
        return;
    }
    uart_puts("$EEPROM,DATA,");
    uart_puts(is_display ? "DISPLAY," : "VMS,");
    uart_put_u16(address);
    uart_putc(',');
    for (index = 0u; index < count; index++) {
        if (index != 0u) uart_putc('-');
        uart_put_hex8(data[index]);
    }
    uart_puts("#\r\n");
}

static void command_eeprom_write(char **fields)
{
    uint8_t is_display;
    uint8_t data;
    uint16_t address;
    uint16_t parsed_value;
    uint8_t ok;

    if (drive_control_enabled()) {
        uart_puts("$ERR,DISABLE-DRIVE-FIRST#\r\n");
        return;
    }
    if (!eeprom_write_unlocked) {
        uart_puts("$ERR,EEPROM,LOCKED#\r\n");
        return;
    }
    if (!eeprom_is_display(fields[2], &is_display) ||
        !parse_u16(fields[3], &address) ||
        !parse_u16(fields[4], &parsed_value) || parsed_value > 255u) {
        eeprom_write_unlocked = 0u;
        uart_puts("$ERR,EEPROM,RANGE#\r\n");
        return;
    }
    data = (uint8_t)parsed_value;
    ok = eeprom_write_selected(is_display, address, &data);
    eeprom_write_unlocked = 0u;
    uart_puts(ok ? "$OK,EEPROM,WRITE-VERIFIED,LOCKED#\r\n" :
                   "$ERR,EEPROM,WRITE,LOCKED#\r\n");
}

static void command_execute(void)
{
    char *fields[COMMAND_FIELD_COUNT];
    uint8_t count = split_fields(fields);
    uint16_t number;
    cc2_output_t output;

    if (string_equal(fields[0], "PING")) {
        uart_puts("$PONG#\r\n");
    } else if (string_equal(fields[0], "ID")) {
        uart_puts("$ID," CC2_FIRMWARE_NAME "," CC2_FIRMWARE_VERSION "#\r\n");
    } else if (string_equal(fields[0], "HELP")) {
        uart_puts("$HELP,PING,ID,DIN,STATUS,SPEED,SPEEDCAL,TRIP,DISPLAY,DRIVE,OUT,EEPROM#\r\n");
    } else if (string_equal(fields[0], "DIN") && count == 1u) {
        command_din();
    } else if (string_equal(fields[0], "STATUS") && count == 1u) {
        command_status();
    } else if (string_equal(fields[0], "SPEED") && count == 1u) {
        command_speed();
    } else if (string_equal(fields[0], "SPEEDCAL") && count == 2u &&
               parse_u16(fields[1], &number) &&
               speed_set_pulses_per_100m(number)) {
        uart_puts("$OK,SPEEDCAL,RAM-ONLY#\r\n");
    } else if (string_equal(fields[0], "TRIP") && count == 2u &&
               string_equal(fields[1], "RESET")) {
        speed_reset_trip();
        uart_puts("$OK,TRIP,RESET#\r\n");
    } else if (string_equal(fields[0], "DISPLAY") && count == 1u) {
        command_display();
    } else if (string_equal(fields[0], "DISPLAY") && count == 2u &&
               string_equal(fields[1], "REFRESH")) {
        uart_puts(display_refresh(speed_kmh(), speed_trip_tenths_km(),
                                  speed_odometer_tenths_km()) ?
                  "$OK,DISPLAY,REFRESH#\r\n" :
                  "$ERR,DISPLAY,NACK#\r\n");
    } else if (string_equal(fields[0], "DISPLAY") && count == 3u &&
               string_equal(fields[1], "TEST") &&
               parse_u16(fields[2], &number) && number <= 102u) {
        uart_puts(display_diagnostic_sweep((uint8_t)number) ?
                  "$OK,DISPLAY,TEST#\r\n" :
                  "$ERR,DISPLAY,NACK#\r\n");
    } else if (string_equal(fields[0], "DRIVE") && count == 1u) {
        command_status();
    } else if (string_equal(fields[0], "DRIVE") && count == 2u &&
               (fields[1][0] == '0' || fields[1][0] == '1') &&
               fields[1][1] == '\0') {
        if (fields[1][0] == '1') eeprom_write_unlocked = 0u;
        uart_puts(drive_set_control_enabled(fields[1][0] == '1') ?
                  "$OK,DRIVE#\r\n" : "$ERR,DRIVE,LOCKED#\r\n");
    } else if (string_equal(fields[0], "OUT") && count == 2u &&
               string_equal(fields[1], "SAFE")) {
        (void)drive_set_control_enabled(0u);
        outputs_all_safe();
        uart_puts("$OK,OUT,SAFE,DRIVE-DISABLED#\r\n");
    } else if (string_equal(fields[0], "OUT") && count == 3u &&
               parse_output(fields[1], &output) &&
               (fields[2][0] == '0' || fields[2][0] == '1') &&
               fields[2][1] == '\0') {
#if CC2_ENABLE_OUTPUT_TESTS
        if (drive_control_enabled()) {
            uart_puts("$ERR,DISABLE-DRIVE-FIRST#\r\n");
        } else {
            output_set(output, fields[2][0] == '1');
            uart_puts("$OK,OUT#\r\n");
        }
#else
        (void)output;
        uart_puts("$ERR,OUTPUTS-LOCKED#\r\n");
#endif
    } else if (string_equal(fields[0], "UPTIME") && count == 1u) {
        uart_puts("$UPTIME,MS=");
        uart_put_u32(timebase_uptime_ms());
        uart_puts("#\r\n");
    } else if (string_equal(fields[0], "EEPROM") && count == 1u) {
        command_eeprom_status();
    } else if (string_equal(fields[0], "EEPROM") && count == 2u &&
               string_equal(fields[1], "PROBE")) {
        command_eeprom_probe();
    } else if (string_equal(fields[0], "EEPROM") && count == 2u &&
               string_equal(fields[1], "LOCK")) {
        eeprom_write_unlocked = 0u;
        uart_puts("$OK,EEPROM,LOCKED#\r\n");
    } else if (string_equal(fields[0], "EEPROM") && count == 3u &&
               string_equal(fields[1], "UNLOCK") &&
               string_equal(fields[2], "WRITE") &&
               !drive_control_enabled()) {
        eeprom_write_unlocked = 1u;
        uart_puts("$OK,EEPROM,UNLOCKED-ONE-WRITE#\r\n");
    } else if (string_equal(fields[0], "EEPROM") && count == 5u &&
               string_equal(fields[1], "READ")) {
        command_eeprom_read(fields);
    } else if (string_equal(fields[0], "EEPROM") && count == 5u &&
               string_equal(fields[1], "WRITE")) {
        command_eeprom_write(fields);
    } else {
        uart_puts("$ERR,COMMAND#\r\n");
    }
}

void commands_poll(void)
{
    char value;

    if (!uart_try_getc(&value)) return;
    if (value == '$') {
        command_active = 1u;
        command_length = 0u;
        return;
    }
    if (!command_active) return;
    if (value == '#') {
        command_buffer[command_length] = '\0';
        command_active = 0u;
        command_execute();
        return;
    }
    if (command_length < (CC2_COMMAND_BUFFER_SIZE - 1u)) {
        command_buffer[command_length++] = value;
    } else {
        command_active = 0u;
        command_length = 0u;
        uart_puts("$ERR,TOO-LONG#\r\n");
    }
}
