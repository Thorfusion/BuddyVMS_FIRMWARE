#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "cc2_hw.h"

void uart_isr(void) __interrupt(CC2_UART_INTERRUPT_VECTOR);

uint8_t uart_rx_overflowed(void);

void uart_init(void);
void uart_putc(char value);
void uart_puts(const char *value);
void uart_put_u8(uint8_t value);
void uart_put_u16(uint16_t value);
void uart_put_u32(uint32_t value);
void uart_put_hex8(uint8_t value);
uint8_t uart_try_getc(char *value);

#endif
