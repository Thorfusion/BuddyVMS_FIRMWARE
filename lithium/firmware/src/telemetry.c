#include "cc2_hw.h"
#include "drive.h"
#include "lithium.h"
#include "protocol.h"
#include "speed.h"
#include "timebase.h"
#include "uart.h"

static uint16_t tx_crc;

static void frame_char(char c)
{
    tx_crc = protocol_crc_byte(tx_crc, (uint8_t)c);
    uart_putc(c);
}

static void frame_text(const char *text)
{
    while (*text) frame_char(*text++);
}

static void frame_number(uint32_t value)
{
    uint32_t divisor = 1000000000UL;
    uint8_t started = 0u;
    while (divisor) {
        uint8_t digit = (uint8_t)(value / divisor);
        if (digit || started || divisor == 1UL) {
            frame_char((char)('0' + digit));
            started = 1u;
        }
        value %= divisor;
        divisor /= 10UL;
    }
}

static void field(uint32_t value) { frame_char(','); frame_number(value); }

void protocol_send_status(void)
{
    uint32_t now = timebase_uptime_ms();
    uint8_t outputs;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    outputs = (!CHARGE_ENABLE_RELAY_N ? 1u : 0u) |
        (MOTOR_RELAY_ENABLE ? 2u : 0u) | (FOOTSWITCH_RELAY ? 4u : 0u) |
        (DIRECTION_RELAY ? 8u : 0u) | (FAN_RELAY ? 16u : 0u);
    uart_putc('$');
    tx_crc = 0xFFFFu;
    frame_text("CC,1");
    field(li_link_age(now) == 0xFFFFFFFFUL ? 0UL : li_packet.sequence);
    field(now);
    field(li_config.charge_mode);
    field(li_config.drive_mode);
    field(li_charge_state);
    field(li_charge_reason);
    field(li_faults);
    field((uint8_t)drive_state());
    field(outputs);
    field(speed_kmh());
    field(li_link_fresh(now) ? li_packet.soc10 : 65535UL);
    field(li_link_age(now));
    field(li_drive_reason);
    uart_putc('*');
    uart_put_hex8((uint8_t)(tx_crc >> 8));
    uart_put_hex8((uint8_t)tx_crc);
    uart_puts("#\r\n");
}
