#include "lithium.h"
#include "protocol.h"

uint16_t protocol_crc_byte(uint16_t crc, uint8_t value)
{
    uint8_t bit;
    crc ^= (uint16_t)value << 8;
    for (bit = 0u; bit < 8u; bit++) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t protocol_crc(const uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFFu;
    while (length--) crc = protocol_crc_byte(crc, *data++);
    return crc;
}

uint8_t protocol_split(char *line, char **fields, uint8_t maximum)
{
    uint8_t count = 1u;
    fields[0] = line;
    while (*line) {
        if (*line == ',') {
            if (count == maximum) return 0u;
            *line = '\0';
            fields[count++] = line + 1;
        }
        line++;
    }
    return count;
}

uint8_t protocol_number(const char *text, int32_t *value)
{
    uint32_t result = 0UL;
    uint8_t negative = 0u;
    uint8_t digit;
    if (*text == '-') { negative = 1u; text++; }
    if (!*text) return 0u;
    while (*text) {
        if (*text < '0' || *text > '9') return 0u;
        digit = (uint8_t)(*text++ - '0');
        if (result > (65535UL - digit) / 10UL) return 0u;
        result = result * 10UL + digit;
    }
    if (negative && result > 32768UL) return 0u;
    *value = negative ? -(int32_t)result : (int32_t)result;
    return 1u;
}

static uint8_t hex_digit(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 255u;
}

uint8_t protocol_receive_bms(char *line, uint32_t now)
{
    /* Foreground-only parser workspace; leave IRAM stack room for interrupts. */
    static char *fields[13];
    static int32_t values[12];
    static li_packet_t packet;
    uint8_t length = 0u;
    uint8_t i;
    uint8_t digit;
    uint16_t crc = 0u;
    while (length < 138u && line[length] && line[length] != '*') length++;
    if (length >= 138u) return 0u;
    if (line[length] != '*') return 0u;
    for (i = 1u; i <= 4u; i++) {
        if (!line[length + i]) return 0u;
        digit = hex_digit(line[length + i]);
        if (digit == 255u) return 0u;
        crc = (uint16_t)((crc << 4) | digit);
    }
    if (line[length + 5u] || crc != protocol_crc((const uint8_t *)line, length)) return 0u;
    line[length] = '\0';
    if (protocol_split(line, fields, 13u) != 13u ||
        fields[0][0] != 'B' || fields[0][1] != 'M' || fields[0][2] != 'S' || fields[0][3]) return 0u;
    for (i = 0u; i < 12u; i++) {
        if (!protocol_number(fields[i + 1u], &values[i])) return 0u;
        if (i != 8u && i != 9u && i != 10u && values[i] < 0) return 0u;
    }
    if (values[0] != 1 || values[2] > LI_BMS_FLAGS || values[3] > 96 ||
        values[8] > 32767 || values[9] > 32767 || values[10] > 32767) return 0u;
    packet.sequence = (uint16_t)values[1];
    packet.flags = (uint8_t)values[2];
    packet.cells = (uint8_t)values[3];
    packet.pack_cv = (uint16_t)values[4];
    packet.cell_min_mv = (uint16_t)values[5];
    packet.cell_max_mv = (uint16_t)values[6];
    /* values[7] is reserved for adapter fault detail, zero in protocol v1. */
    if (values[7] != 0) return 0u;
    packet.temp_min_dc = (int16_t)values[8];
    packet.temp_max_dc = (int16_t)values[9];
    packet.current_da = (int16_t)values[10];
    packet.soc10 = (uint16_t)values[11];
    return li_accept_packet(&packet, now);
}
