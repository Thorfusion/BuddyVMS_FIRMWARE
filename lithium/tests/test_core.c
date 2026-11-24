#include <stdint.h>
#include <string.h>
#include "cc2_config.h"
#include "lithium.h"
#include "protocol.h"
#include "settings.h"
#include "legacy.h"

__xdata __at(0xF000) volatile uint16_t assertions;
__xdata __at(0xF002) volatile uint16_t failures;
__xdata __at(0xF004) volatile uint16_t first_failure;
static uint8_t eeprom[128];
static __xdata __at(0x0000) uint8_t vms_low[512];
static __xdata __at(0x0200) uint8_t display_low[256];
static uint32_t odo;
static uint16_t speed_cal;
static int16_t write_budget;
static uint16_t write_calls;
static uint8_t read_error;
static uint16_t case_id;
static li_inputs_t inputs;
static li_packet_t packet;
static char line[144];

static void check(uint8_t result)
{
    assertions++;
    if (!result) { failures++; if (!first_failure) first_failure = case_id; }
}

uint8_t vehicle_eeprom_read(uint16_t address, uint8_t *data, uint8_t count)
{
    if (read_error) return 0u;
    if (address + count <= 512u) { memcpy(data, &vms_low[address], count); return 1u; }
    if (address < SETTINGS_SLOT_A || address + count > SETTINGS_SLOT_A + 128u) return 0u;
    memcpy(data, &eeprom[address - SETTINGS_SLOT_A], count);
    return 1u;
}

uint8_t vehicle_eeprom_write_verified(uint16_t address, const uint8_t *data, uint8_t count)
{
    uint8_t *destination;
    write_calls++;
    if (address + count <= 512u) destination = &vms_low[address];
    else if (address >= SETTINGS_SLOT_A && address + count <= SETTINGS_SLOT_A + 128u) {
        destination = &eeprom[address - SETTINGS_SLOT_A];
    } else return 0u;
    while (count--) {
        if (write_budget == 0) return 0u;
        if (write_budget > 0) write_budget--;
        *destination++ = *data++;
    }
    return 1u;
}

uint8_t display_eeprom_read(uint16_t address, uint8_t *data, uint8_t count)
{
    if (address + count > 256u) return 0u;
    memcpy(data, &display_low[address], count); return 1u;
}

uint8_t display_eeprom_write_verified(uint16_t address, const uint8_t *data, uint8_t count)
{
    write_calls++;
    if (address + count > 256u) return 0u;
    while (count--) {
        if (!write_budget) return 0u;
        if (write_budget > 0) write_budget--;
        display_low[address++] = *data++;
    }
    return 1u;
}

void speed_restore_odometer(uint32_t value) { odo = value; }
uint32_t speed_odometer_tenths_km(void) { return odo; }
uint8_t speed_set_pulses_per_100m(uint16_t value) { if (!value) return 0u; speed_cal = value; return 1u; }

static void baseline(void)
{
    li_init();
    inputs.ignition = inputs.charger = inputs.moving = inputs.drive_busy = 0u;
    packet.sequence = 1u;
    packet.flags = LI_BMS_VALID | LI_BMS_CHARGE | LI_BMS_DRIVE;
    packet.cells = 24u;
    packet.pack_cv = 7920u;
    packet.cell_min_mv = 3290u;
    packet.cell_max_mv = 3310u;
    packet.temp_min_dc = 200;
    packet.temp_max_dc = 250;
    packet.current_da = 100;
    packet.soc10 = 500u;
}

static void limits(void)
{
    li_config.cells = 24u;
    li_config.cell_min_mv = 2500u;
    li_config.cell_max_mv = 3650u;
    li_config.charge_min_dc = 0;
    li_config.charge_max_dc = 450;
    li_config.drive_min_dc = -200;
    li_config.drive_max_dc = 600;
    li_config.charge_max_da = 200u;
    li_config.discharge_max_da = 1000u;
    li_config.charge_max_seconds = 3600u;
}

static void supervised(void)
{
    baseline(); limits();
    li_config.charge_mode = li_config.drive_mode = LI_MODE_SUPERVISED;
    check(li_accept_packet(&packet, 0UL));
}

static void update(uint32_t now)
{
    packet.sequence++;
    check(li_accept_packet(&packet, now));
}

static void external_tests(void)
{
    case_id = 1u;
    baseline();
    check(li_config.charge_mode == LI_MODE_EXTERNAL && li_config.drive_mode == LI_MODE_EXTERNAL);
    check(li_settings_valid() && !li_limits_valid());
    li_tick(0UL, &inputs);
    check(!li_charge_request && li_drive_permit);
    inputs.charger = 1u;
    li_tick(10UL, &inputs); check(li_charge_state == LI_CHARGE_QUALIFY && !li_drive_permit);
    li_tick(1009UL, &inputs); check(!li_charge_request);
    li_tick(1010UL, &inputs); check(li_charge_request);
    packet.flags |= LI_BMS_FAULT | LI_BMS_COMPLETE;
    packet.temp_min_dc = -300;
    check(li_accept_packet(&packet, 1010UL));
    li_tick(1011UL, &inputs); check(li_charge_request && !li_faults);
    li_tick(2000000000UL, &inputs); check(li_charge_request && !li_faults);
    inputs.ignition = 1u; li_tick(2000000010UL, &inputs);
    check(!li_charge_request && li_charge_reason == LI_REASON_IGNITION);
    inputs.ignition = 0u; inputs.moving = 1u; li_tick(2000000020UL, &inputs);
    check(!li_charge_request && li_charge_reason == LI_REASON_MOVING);
    inputs.moving = 0u; inputs.drive_busy = 1u; li_tick(2000000030UL, &inputs); check(!li_charge_request);
    inputs.drive_busy = 0u; inputs.charger = 0u; li_tick(2000000040UL, &inputs);
    check(!li_charge_request && li_drive_permit);
    li_config.charge_mode = li_config.drive_mode = LI_MODE_OFF;
    inputs.charger = 1u; li_tick(2000000050UL, &inputs);
    check(!li_charge_request && !li_drive_permit && li_charge_reason == LI_REASON_DISABLED);
}

static void link_tests(void)
{
    case_id = 2u;
    supervised(); inputs.charger = 1u;
    li_tick(0UL, &inputs); li_tick(1000UL, &inputs); check(li_charge_request);
    li_tick(1499UL, &inputs); check(li_charge_request);
    check(!li_accept_packet(&packet, 1499UL));
    li_tick(1500UL, &inputs); check(!li_charge_request && li_faults == LI_FAULT_LINK);
    check(!li_clear_faults(&inputs, 1500UL));
    update(1500UL); check(li_clear_faults(&inputs, 1500UL));
    li_tick(1500UL, &inputs); check(li_charge_state == LI_CHARGE_QUALIFY);
    inputs.charger = 0u; inputs.ignition = 1u;
    li_tick(1501UL, &inputs); check(li_drive_permit);
    li_tick(3000UL, &inputs); check(!li_drive_permit && (li_faults & LI_FAULT_LINK));
    check(!li_clear_faults(&inputs, 3000UL));
    baseline();
    packet.sequence = 65535u; check(li_accept_packet(&packet, 0xFFFFFF00UL));
    packet.sequence = 0u; check(li_accept_packet(&packet, 0xFFFFFF01UL));
    check(li_link_fresh(0x000004DCUL));
    check(!li_link_fresh(0x000004DDUL));
    packet.sequence = 32768u; check(!li_accept_packet(&packet, 10UL));
    packet.sequence = 65535u; check(!li_accept_packet(&packet, 10UL));
    li_reset_link(); check(li_accept_packet(&packet, 10UL));
}

static void supervision_tests(void)
{
    uint8_t i;
    case_id = 3u;
    supervised(); inputs.charger = 1u;
    li_tick(0UL, &inputs); li_tick(1000UL, &inputs); check(li_charge_request);
    packet.flags &= (uint8_t)~LI_BMS_CHARGE; update(1001UL); li_tick(1001UL, &inputs);
    check(!li_charge_request && !li_faults && li_charge_reason == LI_REASON_PERMISSION);
    packet.flags |= LI_BMS_CHARGE; update(1002UL); li_tick(1002UL, &inputs);
    li_tick(2001UL, &inputs); check(!li_charge_request);
    li_tick(2002UL, &inputs); check(li_charge_request);
    packet.flags |= LI_BMS_COMPLETE; update(2003UL); li_tick(2003UL, &inputs);
    check(li_charge_state == LI_CHARGE_COMPLETE && !li_charge_request && li_packet.soc10 == 500u);
    packet.flags &= (uint8_t)~LI_BMS_COMPLETE; update(2004UL); li_tick(2004UL, &inputs);
    check(li_charge_state == LI_CHARGE_COMPLETE);
    inputs.charger = 0u; li_tick(2005UL, &inputs);
    inputs.charger = 1u; li_tick(2006UL, &inputs); check(li_charge_state == LI_CHARGE_QUALIFY);
    for (i = 0u; i < 7u; i++) {
        case_id = 10u + i;
        supervised(); inputs.charger = 1u;
        if (i == 0u) packet.flags |= LI_BMS_FAULT;
        if (i == 1u) { packet.cell_min_mv = 2499u; packet.pack_cv = 7200u; }
        if (i == 2u) packet.cell_max_mv = 3651u;
        if (i == 3u) packet.temp_min_dc = -1;
        if (i == 4u) packet.temp_max_dc = 451;
        if (i == 5u) packet.current_da = 201;
        if (i == 6u) packet.current_da = -32768;
        update(1UL); li_tick(1UL, &inputs);
        check(li_faults != 0u && !li_charge_request && !li_drive_permit);
    }
    case_id = 20u;
    supervised(); inputs.charger = 1u;
    packet.cell_min_mv = 2500u; packet.cell_max_mv = 3650u;
    packet.temp_min_dc = 0; packet.temp_max_dc = 450; packet.current_da = 200;
    update(0UL); li_tick(0UL, &inputs); li_tick(1000UL, &inputs);
    check(li_charge_request && !li_faults);
    update(3601000UL); li_tick(3601000UL, &inputs);
    check(!li_charge_request && li_faults == LI_FAULT_TIMEOUT);
    case_id = 21u;
    supervised(); inputs.ignition = 1u;
    packet.temp_min_dc = -100; update(0UL); li_tick(0UL, &inputs);
    check(li_drive_permit && !li_faults);
    case_id = 22u;
    baseline(); li_config.charge_mode = LI_MODE_SUPERVISED;
    inputs.charger = 1u; li_tick(0UL, &inputs);
    check(!li_charge_request && li_charge_reason == LI_REASON_CONFIG);
    case_id = 23u;
    baseline(); inputs.charger = 1u;
    li_tick(0xFFFFFF00UL, &inputs); li_tick(0x000002E8UL, &inputs);
    check(li_charge_request);
    li_trip(LI_FAULT_CONTROL); li_tick(1000UL, &inputs);
    check(!li_charge_request && !li_drive_permit);
    check(li_clear_faults(&inputs, 1000UL));
}

static void protocol_tests(void)
{
    uint8_t i;
    int32_t value;
    case_id = 30u;
    baseline();
    check(protocol_crc((const uint8_t *)"123456789", 9u) == 0x29B1u);
    strcpy(line, "BMS,1,1,19,24,7920,3290,3310,0,200,250,100,500*4EDB");
    check(protocol_receive_bms(line, 0UL));
    check(li_packet.sequence == 1u && li_packet.soc10 == 500u && li_packet.current_da == 100);
    for (i = 0u; i < 45u; i++) {
        baseline();
        strcpy(line, "BMS,1,1,19,24,7920,3290,3310,0,200,250,100,500*4EDB");
        line[i] ^= 1u;
        check(!protocol_receive_bms(line, 0UL));
    }
    strcpy(line, "BMS,1,1,19,24,7920,3290,3310,0,200,250,100,500*4EDB0");
    check(!protocol_receive_bms(line, 0UL));
    check(!protocol_number("", &value)); check(!protocol_number("-", &value));
    check(!protocol_number("65536", &value)); check(!protocol_number("-32769", &value));
    check(!protocol_number("999999999999999999999999", &value));
    check(!protocol_number("+1", &value)); check(!protocol_number(" 1", &value));
    check(protocol_number("-32768", &value) && value == -32768L);
    check(protocol_number("65535", &value) && value == 65535L);
    case_id = 31u;
    baseline(); packet.flags = 0u; check(!li_accept_packet(&packet, 0UL));
    baseline(); packet.cells = 0u; check(!li_accept_packet(&packet, 0UL));
    baseline(); packet.soc10 = 1001u; check(!li_accept_packet(&packet, 0UL));
    baseline(); packet.soc10 = 65535u; check(li_accept_packet(&packet, 0UL));
    baseline(); packet.pack_cv = 9000u; check(!li_accept_packet(&packet, 0UL));
    baseline(); packet.temp_min_dc = 251; check(!li_accept_packet(&packet, 0UL));
}

static void settings_tests(void)
{
    int16_t cut;
    uint8_t backup[64];
    uint8_t bit;
    uint8_t byte;
    case_id = 40u;
    memset(eeprom, 0xFF, sizeof(eeprom)); write_budget = -1; write_calls = 0u;
    baseline(); check(!settings_load());
    check(settings_dirty());
    check(settings_slot_state(0u) == 1u && settings_slot_state(1u) == 1u);
    check(!write_calls && li_config.charge_mode == LI_MODE_EXTERNAL);
    li_config.charge_mode = LI_MODE_OFF;
    check(settings_save() && settings_source() == 1u && settings_sequence() == 1UL);
    check(!settings_dirty());
    check(settings_slot_state(0u) == 2u);
    li_config.drive_mode = LI_MODE_OFF; check(settings_dirty());
    li_config.drive_mode = LI_MODE_EXTERNAL; check(!settings_dirty());
    memcpy(backup, eeprom, 64u);
    for (cut = 0; cut <= 49; cut++) {
        case_id = 100u + (uint16_t)cut;
        memcpy(eeprom, backup, 64u); memset(eeprom + 64u, 0xFF, 64u);
        li_init(); check(settings_load() == 1u);
        li_config.charge_mode = LI_MODE_EXTERNAL;
        write_budget = cut;
        check(settings_save() == (cut == 49));
        li_init();
        check(settings_load() == (cut == 49 ? 2u : 1u));
        check(li_config.charge_mode == (cut == 49 ? LI_MODE_EXTERNAL : LI_MODE_OFF));
    }
    case_id = 41u;
    write_budget = -1;
    memcpy(eeprom, backup, 64u); memset(eeprom + 64u, 0xFF, 64u);
    li_init(); (void)settings_load(); limits(); li_config.charge_mode = LI_MODE_SUPERVISED;
    check(settings_save());
    memcpy(backup, eeprom + 64u, 64u);
    for (byte = 0u; byte < 48u; byte++) {
        if (byte == 46u) continue;
        for (bit = 0u; bit < 8u; bit++) {
            memcpy(eeprom + 64u, backup, 64u);
            eeprom[64u + byte] ^= (uint8_t)(1u << bit);
            li_init();
            check(settings_load() == 1u && li_config.charge_mode == LI_MODE_OFF);
        }
    }
    memcpy(eeprom + 64u, backup, 64u); li_init();
    check(settings_load() == 2u && li_config.cells == 24u && li_config.charge_max_seconds == 3600u);
    memset(eeprom, 0u, sizeof(eeprom)); li_init();
    check(settings_load() == 3u && li_config.charge_mode == LI_MODE_OFF && li_config.drive_mode == LI_MODE_OFF);
    check(settings_slot_state(0u) == 3u && settings_slot_state(1u) == 3u);
    read_error = 1u; li_init();
    check(settings_load() == 3u && settings_slot_state(0u) == 4u && settings_slot_state(1u) == 4u);
    read_error = 0u;
}

static void seed_record(uint8_t *data, uint32_t value, uint8_t count)
{
    uint8_t sum = 0u;
    uint8_t i;
    for (i = count; i; i--) { data[i - 1u] = (uint8_t)value; value >>= 8; sum += data[i - 1u]; }
    data[count] = (uint8_t)(0u - sum);
}

static void legacy_tests(void)
{
    uint8_t mask;
    uint8_t i;
    uint16_t before;
    uint8_t expected_source;
    for (mask = 0u; mask < 8u; mask++) {
        case_id = 200u + mask;
        memset(vms_low, 0xFF, sizeof(vms_low)); memset(display_low, 0xFF, sizeof(display_low));
        if (mask & 1u) seed_record(vms_low, 1000UL, 4u);
        if (mask & 2u) seed_record(vms_low + 0x100u, 2000UL, 4u);
        if (mask & 4u) seed_record(display_low, 3000UL, 4u);
        write_calls = 0u; write_budget = -1;
        legacy_init();
        expected_source = mask & 4u ? 3u : mask & 2u ? 2u : mask & 1u ? 1u : 0u;
        check(legacy_odometer_source() == expected_source);
        check(legacy_boot_read_mask() == 3u);
        check(odo == (uint32_t)expected_source * 1000UL && !write_calls);
        odo++;
        legacy_service(1UL, 0u); check(!write_calls);
        legacy_service(2UL, 1u);
        if (mask) {
            check(write_calls == 1u && legacy_write_phase() == 2u);
            legacy_service(3UL, 1u); check(write_calls == 2u && legacy_write_phase() == 3u);
            legacy_service(4UL, 1u); check(write_calls == 3u && !legacy_write_phase());
            check(legacy_committed_odometer() == odo);
            legacy_init(); check(speed_odometer_tenths_km() == odo);
        } else check(!write_calls);
    }
    case_id = 210u;
    seed_record(vms_low + 0xCFu, 1600UL, 2u);
    seed_record(vms_low + 0x1CFu, 1700UL, 2u);
    seed_record(display_low + 0xCFu, 1800UL, 2u);
    legacy_init(); check(legacy_speed_source() == 1u && speed_cal == 1800u);
    display_low[0xD1u]++;
    legacy_init(); check(legacy_speed_source() == 2u && speed_cal == 1600u);
    vms_low[0xD1u]++;
    legacy_init(); check(legacy_speed_source() == 3u && speed_cal == 1700u);
    vms_low[0x94u] = 'V'; display_low[0x94u] = 'V';
    check(legacy_save_speed_calibration(1620u));
    check(speed_cal == 1620u && vms_low[0xCFu] == 6u && vms_low[0xD0u] == 84u && vms_low[0xD1u] == 166u);
    check(!memcmp(vms_low + 0xCFu, display_low + 0xCFu, 3u));
    check(!memcmp(vms_low + 0xCFu, vms_low + 0x1CFu, 3u));
    check(vms_low[0x94u] == 'V' && display_low[0x94u] == 'V');
    case_id = 211u;
    odo += 10UL; write_budget = 2;
    legacy_service(100UL, 0u);
    check(legacy_write_failures() == 1u && !legacy_write_phase());
    before = write_calls;
    legacy_service(5099UL, 1u); check(write_calls == before);
    write_budget = -1;
    for (i = 0u; i < 3u; i++) legacy_service(5100UL + i, 1u);
    check(legacy_committed_odometer() == odo && !legacy_write_phase());
}

void test_done(void) { __asm nop __endasm; }

void main(void)
{
    assertions = failures = first_failure = 0u;
    external_tests(); link_tests(); supervision_tests(); protocol_tests(); settings_tests(); legacy_tests();
    test_done();
    while (1) { }
}
