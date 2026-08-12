#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdint.h>

void i2c_bus_init(void);
void i2c_start(void);
void i2c_restart(void);
void i2c_stop(void);
void i2c_send_byte(uint8_t value);
uint8_t i2c_receive_ack(void);
uint8_t i2c_read_byte(void);
void i2c_send_ack(uint8_t acknowledge);

#endif

