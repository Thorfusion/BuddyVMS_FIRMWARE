#include "display_eeprom.h"
#include "legacy.h"
#include "speed.h"
#include "vehicle_eeprom.h"

static uint8_t odo_source;
static uint8_t calibration_source;
static uint16_t failures;
static uint32_t committed;
static uint32_t snapshot;
static uint32_t last_save;
static uint32_t retry_time;
static uint8_t retry_pending;
static uint8_t phase;
static uint8_t bytes[5];
static uint8_t boot_read_mask;

static uint8_t read_checked(uint8_t display, uint16_t address, uint8_t length, uint32_t *value)
{
    uint8_t i;
    uint8_t sum = 0u;
    uint32_t result = 0UL;
    if (!(display ? display_eeprom_read(address, bytes, length + 1u) :
          vehicle_eeprom_read(address, bytes, length + 1u))) return 0u;
    boot_read_mask |= display ? 2u : 1u;
    for (i = 0u; i <= length; i++) {
        sum += bytes[i];
        if (i < length) result = (result << 8) | bytes[i];
    }
    if (sum) return 0u;
    *value = result;
    return 1u;
}

static uint8_t write_checked(uint8_t display, uint16_t address, uint8_t length, uint32_t value)
{
    uint8_t i;
    uint8_t sum = 0u;
    for (i = length; i; i--) {
        bytes[i - 1u] = (uint8_t)value;
        value >>= 8;
        sum += bytes[i - 1u];
    }
    bytes[length] = (uint8_t)(0u - sum);
    return display ? display_eeprom_write_verified(address, bytes, length + 1u) :
        vehicle_eeprom_write_verified(address, bytes, length + 1u);
}

void legacy_init(void)
{
    uint32_t value;
    uint8_t i;
    odo_source = calibration_source = phase = retry_pending = 0u;
    boot_read_mask = 0u;
    failures = 0u;
    committed = last_save = 0UL;
    /* Largest valid copy avoids rollback after an interrupted three-copy save. */
    for (i = 1u; i <= 3u; i++) {
        if (read_checked(i == 3u, i == 2u ? 0x0100u : 0u, 4u, &value) &&
            (!odo_source || value > committed)) {
            odo_source = i;
            committed = value;
        }
    }
    speed_restore_odometer(committed);
    for (i = 1u; i <= 3u; i++) {
        if (read_checked(i == 1u, i == 3u ? 0x01CFu : 0x00CFu, 2u, &value) && value) {
            if (speed_set_pulses_per_100m((uint16_t)value)) {
                calibration_source = i;
                break;
            }
        }
    }
}

void legacy_service(uint32_t now, uint8_t stopped)
{
    uint8_t ok;
    uint32_t current = speed_odometer_tenths_km();
    if (!odo_source || current < committed) return;
    if (retry_pending) {
        if ((uint32_t)(now - retry_time) < 5000UL) return;
        retry_pending = 0u;
    }
    if (!phase) {
        if (current == committed || (!stopped && current - committed < 10UL &&
            (uint32_t)(now - last_save) < 60000UL)) return;
        snapshot = current;
        phase = 1u;
    }
    /* One verified record per service visit, never a multi-device blocking loop. */
    ok = write_checked(phase == 1u, phase == 2u ? 0x0100u : 0u, 4u, snapshot);
    if (!ok) {
        failures++;
        phase = 0u;
        retry_pending = 1u;
        retry_time = now;
    } else if (++phase > 3u) {
        phase = 0u;
        committed = snapshot;
        last_save = now;
    }
}

uint8_t legacy_save_speed_calibration(uint16_t value)
{
    uint8_t ok = 1u;
    if (!value || phase) return 0u;
    if (!write_checked(1u, 0x00CFu, 2u, value)) ok = 0u;
    if (!write_checked(0u, 0x00CFu, 2u, value)) ok = 0u;
    if (!write_checked(0u, 0x01CFu, 2u, value)) ok = 0u;
    if (!ok) { failures++; return 0u; }
    return speed_set_pulses_per_100m(value);
}

uint8_t legacy_odometer_source(void) { return odo_source; }
uint8_t legacy_speed_source(void) { return calibration_source; }
uint16_t legacy_write_failures(void) { return failures; }
uint8_t legacy_write_phase(void) { return phase; }
uint32_t legacy_committed_odometer(void) { return committed; }
uint8_t legacy_boot_read_mask(void) { return boot_read_mask; }
