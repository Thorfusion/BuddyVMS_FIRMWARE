#include "display_eeprom.h"
#include "i2c_bus.h"

#define DISPLAY_EEPROM_ADDRESS_RETRIES 3u
#define DISPLAY_EEPROM_READY_POLLS 255u

static uint16_t failure_count;

static uint8_t select_write_address(void)
{
    uint8_t retry;

    for (retry = 0u; retry < DISPLAY_EEPROM_ADDRESS_RETRIES; retry++) {
        i2c_start();
        i2c_send_byte(DISPLAY_EEPROM_WRITE_ADDRESS);
        if (i2c_receive_ack()) return 1u;
        i2c_stop();
    }
    return 0u;
}

static uint8_t wait_ready(void)
{
    uint8_t polls = DISPLAY_EEPROM_READY_POLLS;

    do {
        i2c_start();
        i2c_send_byte(DISPLAY_EEPROM_WRITE_ADDRESS);
        if (i2c_receive_ack()) {
            i2c_stop();
            return 1u;
        }
        i2c_stop();
        polls--;
    } while (polls != 0u);
    failure_count++;
    return 0u;
}

uint8_t display_eeprom_read(uint16_t address,
                            uint8_t *data,
                            uint8_t count)
{
    uint8_t index;

    if (count == 0u) return 1u;
    if (data == 0 || address >= DISPLAY_EEPROM_SIZE ||
        count > (uint16_t)(DISPLAY_EEPROM_SIZE - address)) return 0u;

    if (!select_write_address()) goto failed;
    i2c_send_byte((uint8_t)address);
    if (!i2c_receive_ack()) goto failed;
    i2c_restart();
    i2c_send_byte(DISPLAY_EEPROM_READ_ADDRESS);
    if (!i2c_receive_ack()) goto failed;

    for (index = 0u; index < count; index++) {
        data[index] = i2c_read_byte();
        i2c_send_ack(index + 1u < count);
    }
    i2c_stop();
    return 1u;

failed:
    i2c_stop();
    failure_count++;
    return 0u;
}

uint8_t display_eeprom_write(uint16_t address,
                             const uint8_t *data,
                             uint8_t count)
{
    uint8_t chunk;
    uint8_t index;

    if (count == 0u) return 1u;
    if (data == 0 || address >= DISPLAY_EEPROM_SIZE ||
        count > (uint16_t)(DISPLAY_EEPROM_SIZE - address)) return 0u;

    while (count != 0u) {
        chunk = (uint8_t)(DISPLAY_EEPROM_PAGE_SIZE -
                          (address & (DISPLAY_EEPROM_PAGE_SIZE - 1u)));
        if (chunk > count) chunk = count;
        if (!wait_ready() || !select_write_address()) goto failed;
        i2c_send_byte((uint8_t)address);
        if (!i2c_receive_ack()) goto failed;
        for (index = 0u; index < chunk; index++) {
            i2c_send_byte(data[index]);
            if (!i2c_receive_ack()) goto failed;
        }
        i2c_stop();
        address += chunk;
        data += chunk;
        count -= chunk;
    }
    return wait_ready();

failed:
    i2c_stop();
    failure_count++;
    return 0u;
}

uint8_t display_eeprom_write_verified(uint16_t address,
                                      const uint8_t *data,
                                      uint8_t count)
{
    uint8_t verify[16];
    uint8_t offset = 0u;
    uint8_t chunk;
    uint8_t index;

    if (!display_eeprom_write(address, data, count)) return 0u;
    while (offset < count) {
        chunk = (uint8_t)(count - offset);
        if (chunk > sizeof(verify)) chunk = sizeof(verify);
        if (!display_eeprom_read(address + offset, verify, chunk)) return 0u;
        for (index = 0u; index < chunk; index++) {
            if (verify[index] != data[offset + index]) {
                failure_count++;
                return 0u;
            }
        }
        offset += chunk;
    }
    return 1u;
}

uint16_t display_eeprom_failure_count(void)
{
    return failure_count;
}

