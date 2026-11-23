#include "cc2_config.h"
#include "cc2_hw.h"
#include "vehicle_eeprom.h"

#define VEHICLE_EEPROM_READY_POLLS 65535u

static volatile uint8_t transfer_complete;
static uint16_t failure_count;

void vehicle_eeprom_ssc_isr(void) __interrupt(CC2_SSC_INTERRUPT_VECTOR)
{
    uint8_t saved_page = MOD_PAGE;
    uint8_t flags;

    MOD_PAGE = CC2_MOD_PAGE_SSC_INTERRUPT;
    flags = SCU_IRCON1;
    SCU_IRCON1 = flags & (uint8_t)~(
        CC2_SCU_IRCON1_SSC_ERROR_MASK |
        CC2_SCU_IRCON1_SSC_RECEIVE_MASK |
        CC2_SCU_IRCON1_SSC_TRANSMIT_MASK);
    if (flags & CC2_SCU_IRCON1_SSC_TRANSMIT_MASK) {
        transfer_complete = 1u;
    }
    MOD_PAGE = saved_page;
}

void vehicle_eeprom_init(void)
{
    failure_count = 0u;
    transfer_complete = 0u;

    MOD_PAGE = 3u;
    SCU_SFR_BE = 0u;
    MOD_PAGE = 0u;

    PORT_PAGE = 2u;
    P1 |= 0x0Cu;
    P1_DIR &= 0xF3u;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    P1_DIR |= 0x0Cu;

    SSC_CONH &= (uint8_t)~CC2_SSC_CONH_ENABLE;
    SSC_BRH = 0u;
    SSC_BRL = CC2_SSC_BRL_INIT;
    SSC_CONH = CC2_SSC_CONH_INIT;
    SSC_CONL = CC2_SSC_CONL_INIT;
    SSC_CONH |= CC2_SSC_CONH_ENABLE;
    IEN1 |= 0x02u;

    MOD_PAGE = 3u;
    SCU_SFR_B6 |= 0x07u;
    MOD_PAGE = 0u;
    VMS_SPI_CS_N = 1;
}

static uint8_t transfer(uint8_t outgoing, uint8_t *incoming)
{
    uint16_t timeout = CC2_UART_WAIT_LIMIT;

    transfer_complete = 0u;
    SSC_TBL = outgoing;
    while (!transfer_complete && timeout != 0u) {
        timeout--;
    }
    if (timeout == 0u) {
        failure_count++;
        return 0u;
    }
    *incoming = SSC_RBL;
    return 1u;
}

uint8_t vehicle_eeprom_read_status(uint8_t *status)
{
    uint8_t ignored;

    if (status == 0) return 0u;
    VMS_SPI_CS_N = 0;
    if (!transfer(CC2_AT25_RDSR, &ignored) || !transfer(0u, status)) {
        VMS_SPI_CS_N = 1;
        return 0u;
    }
    VMS_SPI_CS_N = 1;
    return 1u;
}

static uint8_t wait_ready(void)
{
    uint16_t polls = VEHICLE_EEPROM_READY_POLLS;
    uint8_t status;

    do {
        if (!vehicle_eeprom_read_status(&status)) return 0u;
        if (!(status & CC2_AT25_STATUS_WIP)) return 1u;
        polls--;
    } while (polls != 0u);
    failure_count++;
    return 0u;
}

static uint8_t write_enable(void)
{
    uint8_t ignored;
    uint8_t status;

    VMS_SPI_CS_N = 0;
    if (!transfer(CC2_AT25_WREN, &ignored)) {
        VMS_SPI_CS_N = 1;
        return 0u;
    }
    VMS_SPI_CS_N = 1;
    if (!vehicle_eeprom_read_status(&status)) return 0u;
    if (!(status & CC2_AT25_STATUS_WEL)) {
        failure_count++;
        return 0u;
    }
    return 1u;
}

uint8_t vehicle_eeprom_read(uint16_t address,
                            uint8_t *data,
                            uint8_t count)
{
    uint8_t ignored;
    uint8_t index;

    if (count == 0u) return 1u;
    if (data == 0 || address >= CC2_AT25128_SIZE ||
        count > (uint16_t)(CC2_AT25128_SIZE - address)) return 0u;
    if (!wait_ready()) return 0u;

    VMS_SPI_CS_N = 0;
    if (!transfer(CC2_AT25_READ, &ignored) ||
        !transfer((uint8_t)(address >> 8), &ignored) ||
        !transfer((uint8_t)address, &ignored)) goto failed;
    for (index = 0u; index < count; index++) {
        if (!transfer(0u, &data[index])) goto failed;
    }
    VMS_SPI_CS_N = 1;
    return 1u;

failed:
    VMS_SPI_CS_N = 1;
    return 0u;
}

uint8_t vehicle_eeprom_write(uint16_t address,
                             const uint8_t *data,
                             uint8_t count)
{
    uint8_t ignored;
    uint8_t chunk;
    uint8_t index;

    if (count == 0u) return 1u;
    if (data == 0 || address >= CC2_AT25128_SIZE ||
        count > (uint16_t)(CC2_AT25128_SIZE - address)) return 0u;

    while (count != 0u) {
        chunk = (uint8_t)(CC2_AT25128_PAGE_SIZE -
                          (address & (CC2_AT25128_PAGE_SIZE - 1u)));
        if (chunk > count) chunk = count;
        if (!wait_ready() || !write_enable()) return 0u;

        VMS_SPI_CS_N = 0;
        if (!transfer(CC2_AT25_WRITE, &ignored) ||
            !transfer((uint8_t)(address >> 8), &ignored) ||
            !transfer((uint8_t)address, &ignored)) goto failed;
        for (index = 0u; index < chunk; index++) {
            if (!transfer(data[index], &ignored)) goto failed;
        }
        VMS_SPI_CS_N = 1;
        address += chunk;
        data += chunk;
        count -= chunk;
    }
    return wait_ready();

failed:
    VMS_SPI_CS_N = 1;
    return 0u;
}

uint8_t vehicle_eeprom_write_verified(uint16_t address,
                                      const uint8_t *data,
                                      uint8_t count)
{
    uint8_t verify[16];
    uint8_t offset = 0u;
    uint8_t chunk;
    uint8_t index;

    if (!vehicle_eeprom_write(address, data, count)) return 0u;
    while (offset < count) {
        chunk = (uint8_t)(count - offset);
        if (chunk > sizeof(verify)) chunk = sizeof(verify);
        if (!vehicle_eeprom_read(address + offset, verify, chunk)) return 0u;
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

uint16_t vehicle_eeprom_failure_count(void)
{
    return failure_count;
}
