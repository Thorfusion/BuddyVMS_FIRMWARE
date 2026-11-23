#include "lithium.h"
#include "protocol.h"
#include "settings.h"
#include "vehicle_eeprom.h"

static uint8_t source;
static uint32_t sequence;
static uint8_t record[SETTINGS_RECORD_SIZE];
static uint8_t unrecognized_storage;

static void put16(uint8_t offset, uint16_t value)
{
    record[offset] = (uint8_t)value;
    record[offset + 1u] = (uint8_t)(value >> 8);
}

static uint16_t get16(uint8_t offset)
{
    return (uint16_t)record[offset] | ((uint16_t)record[offset + 1u] << 8);
}

static uint32_t record_sequence(void)
{
    return (uint32_t)get16(6u) | ((uint32_t)get16(8u) << 16);
}

static uint8_t read_record(uint16_t address)
{
    uint8_t i;
    if (!vehicle_eeprom_read(address, record, SETTINGS_RECORD_SIZE)) {
        unrecognized_storage = 1u;
        return 0u;
    }
    for (i = 0u; i < SETTINGS_RECORD_SIZE; i++) {
        if (record[i] != 0xFFu) unrecognized_storage = 1u;
    }
    return record[0] == 'C' && record[1] == 'C' && record[2] == 'L' && record[3] == 'I' &&
        record[4] == 1u && record[5] == SETTINGS_RECORD_SIZE && record[47] == 0xA5u &&
        get16(44u) == protocol_crc(record, 44u);
}

static void unpack(void)
{
    li_config.charge_mode = record[10];
    li_config.drive_mode = record[11];
    li_config.fan_on_dc = (int16_t)get16(12u);
    li_config.fan_off_dc = (int16_t)get16(14u);
    li_config.cells = record[16];
    li_config.cell_min_mv = get16(18u);
    li_config.cell_max_mv = get16(20u);
    li_config.charge_min_dc = (int16_t)get16(22u);
    li_config.charge_max_dc = (int16_t)get16(24u);
    li_config.drive_min_dc = (int16_t)get16(26u);
    li_config.drive_max_dc = (int16_t)get16(28u);
    li_config.charge_max_da = get16(30u);
    li_config.discharge_max_da = get16(32u);
    li_config.charge_max_seconds = get16(34u);
}

static uint8_t valid_settings_at(uint16_t address)
{
    if (!read_record(address)) return 0u;
    unpack();
    return li_settings_valid();
}

uint8_t settings_load(void)
{
    uint32_t first_sequence = 0UL;
    uint8_t first_valid;
    uint8_t second_valid;
    unrecognized_storage = 0u;
    first_valid = valid_settings_at(SETTINGS_SLOT_A);
    if (first_valid) first_sequence = record_sequence();
    second_valid = valid_settings_at(SETTINGS_SLOT_B);
    source = 0u;
    sequence = 0UL;
    if (second_valid && (!first_valid ||
        ((uint32_t)(record_sequence() - first_sequence) != 0UL &&
         (uint32_t)(record_sequence() - first_sequence) < 0x80000000UL))) {
        source = 2u;
        sequence = record_sequence();
    } else if (first_valid && valid_settings_at(SETTINGS_SLOT_A)) {
        source = 1u;
        sequence = first_sequence;
    }
    if (!source) {
        li_defaults();
        if (unrecognized_storage) {
            li_config.charge_mode = li_config.drive_mode = LI_MODE_OFF;
            source = 3u;
        }
    }
    return source;
}

uint8_t settings_save(void)
{
    uint8_t i;
    uint8_t commit = 0u;
    uint16_t address = source == 1u ? SETTINGS_SLOT_B : SETTINGS_SLOT_A;
    uint32_t next_sequence = sequence + 1UL;
    if (!li_settings_valid()) return 0u;
    for (i = 0u; i < SETTINGS_RECORD_SIZE; i++) record[i] = 0u;
    record[0] = 'C'; record[1] = 'C'; record[2] = 'L'; record[3] = 'I';
    record[4] = 1u; record[5] = SETTINGS_RECORD_SIZE;
    put16(6u, (uint16_t)next_sequence);
    put16(8u, (uint16_t)(next_sequence >> 16));
    record[10] = li_config.charge_mode;
    record[11] = li_config.drive_mode;
    put16(12u, (uint16_t)li_config.fan_on_dc);
    put16(14u, (uint16_t)li_config.fan_off_dc);
    record[16] = li_config.cells;
    put16(18u, li_config.cell_min_mv);
    put16(20u, li_config.cell_max_mv);
    put16(22u, (uint16_t)li_config.charge_min_dc);
    put16(24u, (uint16_t)li_config.charge_max_dc);
    put16(26u, (uint16_t)li_config.drive_min_dc);
    put16(28u, (uint16_t)li_config.drive_max_dc);
    put16(30u, li_config.charge_max_da);
    put16(32u, li_config.discharge_max_da);
    put16(34u, li_config.charge_max_seconds);
    put16(44u, protocol_crc(record, 44u));
    if (!vehicle_eeprom_write_verified(address + 47u, &commit, 1u) ||
        !vehicle_eeprom_write_verified(address, record, 47u)) return 0u;
    commit = 0xA5u;
    if (!vehicle_eeprom_write_verified(address + 47u, &commit, 1u)) return 0u;
    sequence = next_sequence;
    source = address == SETTINGS_SLOT_A ? 1u : 2u;
    return 1u;
}

uint8_t settings_source(void) { return source; }
uint32_t settings_sequence(void) { return sequence; }
