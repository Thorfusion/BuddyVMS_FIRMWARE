#include "cc2_hw.h"
#include "i2c_bus.h"

#define I2C_SCL CC2_I2C_SCL_MASK
#define I2C_SDA CC2_I2C_SDA_MASK
#define I2C_ACK_TIMEOUT 100u

static void i2c_delay(void)
{
    volatile uint8_t count;

    for (count = 0u; count < 10u; count++) {
    }
}

static void i2c_drive_low(uint8_t mask)
{
    uint8_t saved_ea = EA;
    uint8_t saved_page;

    EA = 0;
    saved_page = PORT_PAGE;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    P5_DIR |= mask;
    P5_DATA &= (uint8_t)~mask;
    PORT_PAGE = saved_page;
    EA = saved_ea;
    i2c_delay();
}

static void i2c_release(uint8_t mask)
{
    uint8_t saved_ea = EA;
    uint8_t saved_page;

    EA = 0;
    saved_page = PORT_PAGE;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    P5_DIR &= (uint8_t)~mask;
    PORT_PAGE = saved_page;
    EA = saved_ea;
    i2c_delay();
}

static uint8_t i2c_sda_high(void)
{
    uint8_t saved_ea = EA;
    uint8_t saved_page;
    uint8_t value;

    EA = 0;
    saved_page = PORT_PAGE;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    value = (P5_DATA & I2C_SDA) ? 1u : 0u;
    PORT_PAGE = saved_page;
    EA = saved_ea;
    return value;
}

void i2c_bus_init(void)
{
    uint8_t saved_page = PORT_PAGE;

    PORT_PAGE = CC2_PORT_PAGE_DATA;
    P5_DATA &= (uint8_t)~(I2C_SCL | I2C_SDA);
    P5_DIR &= (uint8_t)~(I2C_SCL | I2C_SDA);
    PORT_PAGE = saved_page;
}

void i2c_start(void)
{
    i2c_release(I2C_SCL | I2C_SDA);
    i2c_drive_low(I2C_SDA);
    i2c_drive_low(I2C_SCL);
}

void i2c_restart(void)
{
    i2c_drive_low(I2C_SCL);
    i2c_release(I2C_SDA);
    i2c_release(I2C_SCL);
    i2c_drive_low(I2C_SDA);
    i2c_drive_low(I2C_SCL);
}

void i2c_stop(void)
{
    i2c_drive_low(I2C_SCL | I2C_SDA);
    i2c_release(I2C_SCL);
    i2c_release(I2C_SDA);
}

void i2c_send_byte(uint8_t value)
{
    uint8_t mask;

    for (mask = 0x80u; mask != 0u; mask >>= 1) {
        if (value & mask) i2c_release(I2C_SDA);
        else i2c_drive_low(I2C_SDA);
        i2c_release(I2C_SCL);
        i2c_drive_low(I2C_SCL);
    }
}

uint8_t i2c_receive_ack(void)
{
    uint8_t timeout;
    uint8_t received = 0u;

    i2c_release(I2C_SDA);
    i2c_release(I2C_SCL);
    for (timeout = 0u; timeout < I2C_ACK_TIMEOUT; timeout++) {
        if (!i2c_sda_high()) {
            received = 1u;
            break;
        }
    }
    i2c_drive_low(I2C_SCL);
    return received;
}

uint8_t i2c_read_byte(void)
{
    uint8_t count;
    uint8_t value = 0u;

    i2c_drive_low(I2C_SCL);
    i2c_release(I2C_SDA);
    for (count = 0u; count < 8u; count++) {
        i2c_release(I2C_SCL);
        value <<= 1;
        if (i2c_sda_high()) value |= 1u;
        i2c_drive_low(I2C_SCL);
    }
    return value;
}

void i2c_send_ack(uint8_t acknowledge)
{
    i2c_drive_low(I2C_SCL);
    if (acknowledge) i2c_drive_low(I2C_SDA);
    else i2c_release(I2C_SDA);
    i2c_release(I2C_SCL);
    i2c_drive_low(I2C_SCL);
    i2c_release(I2C_SDA);
}

