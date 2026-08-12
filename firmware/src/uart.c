#include "cc2_config.h"
#include "cc2_hw.h"
#include "uart.h"

void uart_init(void)
{
    /* Original CC2 routing: RX=P1.0, TX=P1.1 using port page 2. */
    PORT_PAGE = 2u;
    P1 &= 0xFDu;
    P1_DIR |= CC2_UART_TX_MASK;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    P1_DIR = (P1_DIR & (uint8_t)~CC2_UART_RX_MASK) | CC2_UART_TX_MASK;
    MODPISEL &= 0xFEu;

    MOD_PAGE = 0u;
    UART_BCON = 0u;
    SCON = 0x70u;
    UART_BG = CC2_UART_BG_RELOAD;
    UART_BCON |= 0x01u;
    RI = 0;
    TI = 0;
}

void uart_putc(char value)
{
    uint16_t timeout = CC2_UART_WAIT_LIMIT;

    UART_TX_LED_N = 0;
    TI = 0;
    SBUF = (uint8_t)value;
    while (!TI && timeout != 0u) {
        timeout--;
    }
    TI = 0;
    UART_TX_LED_N = 1;
}

void uart_puts(const char *value)
{
    while (*value != '\0') {
        uart_putc(*value++);
    }
}

void uart_put_u32(uint32_t value)
{
    uint32_t divisor = 1000000000UL;
    uint8_t started = 0u;

    while (divisor != 0UL) {
        uint8_t digit = (uint8_t)(value / divisor);
        if (digit != 0u || started || divisor == 1UL) {
            uart_putc((char)('0' + digit));
            started = 1u;
        }
        value %= divisor;
        divisor /= 10UL;
    }
}

void uart_put_u16(uint16_t value)
{
    uart_put_u32((uint32_t)value);
}

void uart_put_u8(uint8_t value)
{
    uart_put_u32((uint32_t)value);
}

void uart_put_hex8(uint8_t value)
{
    uint8_t nibble = value >> 4;

    uart_putc((char)(nibble < 10u ? '0' + nibble : 'A' + nibble - 10u));
    nibble = value & 0x0Fu;
    uart_putc((char)(nibble < 10u ? '0' + nibble : 'A' + nibble - 10u));
}

uint8_t uart_try_getc(char *value)
{
    if (!RI) {
        return 0u;
    }
    RI = 0;
    *value = (char)SBUF;
    return 1u;
}
