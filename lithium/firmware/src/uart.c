#include "cc2_config.h"
#include "cc2_hw.h"
#include "uart.h"

static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile uint8_t rx_overflow;
static volatile uint8_t tx_done;
static volatile char rx_buffer[CC2_UART_RX_SIZE];

void uart_isr(void) __interrupt(CC2_UART_INTERRUPT_VECTOR)
{
    uint8_t next;
    char value;
    if (RI) {
        value = (char)SBUF;
        RI = 0;
        next = (rx_head + 1u) & (CC2_UART_RX_SIZE - 1u);
        if (next == rx_tail) rx_overflow = 1u;
        else { rx_buffer[rx_head] = value; rx_head = next; }
    }
    if (TI) { TI = 0; tx_done = 1u; }
}

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
    rx_head = rx_tail = rx_overflow = 0u;
    tx_done = 0u;
    IE |= 0x10u;
}

void uart_putc(char value)
{
    uint16_t timeout = CC2_UART_WAIT_LIMIT;

    tx_done = 0u;
    TI = 0;
    SBUF = (uint8_t)value;
    while (!tx_done && timeout != 0u) {
        timeout--;
    }
    TI = 0;
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
    if (rx_head == rx_tail) return 0u;
    *value = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1u) & (CC2_UART_RX_SIZE - 1u);
    return 1u;
}

uint8_t uart_rx_overflowed(void)
{
    uint8_t saved = EA;
    uint8_t value;
    EA = 0;
    value = rx_overflow;
    if (value) rx_tail = rx_head;
    rx_overflow = 0u;
    EA = saved;
    return value;
}
