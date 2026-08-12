#include "cc2_hw.h"
#include "display.h"
#include "i2c_bus.h"

#define DISPLAY_CONTROL_REGISTER 0x00u
#define DISPLAY_DIGIT_REGISTER 0x01u
#define DISPLAY_CONTROL_FULL_BRIGHT 0x17u
#define DISPLAY_BANK_DEFAULT 0u
#define DISPLAY_BANK_ODOMETER 1u
#define DISPLAY_BANK_AUXILIARY 2u

static const uint8_t __code digits[16] = {
    0x3Fu, 0x06u, 0x5Bu, 0x4Fu,
    0x66u, 0x6Du, 0x7Du, 0x07u,
    0x7Fu, 0x6Fu, 0x77u, 0x7Cu,
    0x39u, 0x5Eu, 0x79u, 0x71u
};

static const uint8_t __code bar_masks[9] = {
    0x00u, 0x01u, 0x03u, 0x07u, 0x0Fu,
    0x1Fu, 0x3Fu, 0x7Fu, 0xFFu
};

static uint8_t ack_bits;
static uint16_t failures;
static uint16_t updates;
static uint8_t last_ok;

static void set_bank(uint8_t bank)
{
    uint8_t saved_ea = EA;
    uint8_t saved_page;

    EA = 0;
    saved_page = PORT_PAGE;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    if (bank == DISPLAY_BANK_ODOMETER) {
        DASH_PIN_16 = 1;
        DASH_PIN_15 = 0;
    } else if (bank == DISPLAY_BANK_AUXILIARY) {
        DASH_PIN_16 = 0;
        DASH_PIN_15 = 1;
    } else {
        DASH_PIN_16 = 1;
        DASH_PIN_15 = 1;
    }
    PORT_PAGE = saved_page;
    EA = saved_ea;
}

static uint8_t address_valid(uint8_t address)
{
    return address == CC2_DISPLAY_SPEED_A ||
           address == CC2_DISPLAY_SPEED_B ||
           address == CC2_DISPLAY_TRIP ||
           address == CC2_DISPLAY_BANKED;
}

static uint8_t write_bytes(uint8_t address,
                           uint8_t reg,
                           const uint8_t *data,
                           uint8_t count)
{
    uint8_t index;

    if (!address_valid(address) || data == 0 || count == 0u) {
        return 0u;
    }
    i2c_start();
    i2c_send_byte(address);
    if (!i2c_receive_ack()) goto failed;
    i2c_send_byte(reg);
    if (!i2c_receive_ack()) goto failed;
    for (index = 0u; index < count; index++) {
        i2c_send_byte(data[index]);
        if (!i2c_receive_ack()) goto failed;
    }
    i2c_stop();
    return 1u;

failed:
    i2c_stop();
    failures++;
    return 0u;
}

static uint8_t write_four(uint8_t address,
                          uint8_t a,
                          uint8_t b,
                          uint8_t c,
                          uint8_t d)
{
    uint8_t data[4];

    data[0] = a;
    data[1] = b;
    data[2] = c;
    data[3] = d;
    return write_bytes(address, DISPLAY_DIGIT_REGISTER, data, 4u);
}

static uint8_t write_number(uint8_t address, uint16_t value)
{
    if (value > 9999u) value = 9999u;
    return write_four(address,
                      digits[value % 10u],
                      digits[(value / 10u) % 10u],
                      digits[(value / 100u) % 10u],
                      digits[(value / 1000u) % 10u]);
}

static uint8_t bar_chunk(uint8_t scaled, uint8_t start)
{
    if (scaled <= start) return 0u;
    scaled -= start;
    if (scaled > 8u) scaled = 8u;
    return bar_masks[scaled];
}

static uint8_t write_speed(uint8_t kmh)
{
    uint8_t scaled;
    uint8_t ok;

    if (kmh == 0u) scaled = 0u;
    else if (kmh <= 2u) scaled = 1u;
    else scaled = kmh / 2u;

    set_bank(DISPLAY_BANK_DEFAULT);
    ok = write_four(CC2_DISPLAY_SPEED_A,
                    bar_chunk(scaled, 0u),
                    bar_chunk(scaled, 16u),
                    bar_chunk(scaled, 8u),
                    bar_chunk(scaled, 24u));
    ok &= write_four(CC2_DISPLAY_SPEED_B,
                     bar_chunk(scaled, 32u),
                     bar_chunk(scaled, 48u),
                     bar_chunk(scaled, 40u),
                     0u);
    return ok;
}

void display_bus_init(void)
{
    uint8_t saved_page = PORT_PAGE;

    i2c_bus_init();
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    DASH_PIN_16 = 1;
    DASH_PIN_15 = 1;
    PORT_PAGE = saved_page;
}

uint8_t display_clear(void)
{
    uint8_t ok = 1u;

    set_bank(DISPLAY_BANK_DEFAULT);
    ok &= write_four(CC2_DISPLAY_SPEED_A, 0u, 0u, 0u, 0u);
    ok &= write_four(CC2_DISPLAY_SPEED_B, 0u, 0u, 0u, 0u);
    ok &= write_four(CC2_DISPLAY_TRIP, 0u, 0u, 0u, 0u);
    set_bank(DISPLAY_BANK_ODOMETER);
    ok &= write_four(CC2_DISPLAY_BANKED, 0u, 0u, 0u, 0u);
    set_bank(DISPLAY_BANK_AUXILIARY);
    ok &= write_four(CC2_DISPLAY_BANKED, 0u, 0u, 0u, 0u);
    set_bank(DISPLAY_BANK_DEFAULT);
    return ok;
}

uint8_t display_init(void)
{
    static const uint8_t __code addresses[4] = {
        CC2_DISPLAY_SPEED_A,
        CC2_DISPLAY_SPEED_B,
        CC2_DISPLAY_TRIP,
        CC2_DISPLAY_BANKED
    };
    uint8_t control = DISPLAY_CONTROL_FULL_BRIGHT;
    uint8_t index;

    ack_bits = 0u;
    failures = 0u;
    updates = 0u;
    last_ok = 0u;
    set_bank(DISPLAY_BANK_DEFAULT);
    for (index = 0u; index < 4u; index++) {
        if (write_bytes(addresses[index], DISPLAY_CONTROL_REGISTER,
                        &control, 1u)) {
            ack_bits |= (uint8_t)(1u << index);
        }
    }
    if (ack_bits != 0x0Fu) {
        return 0u;
    }
    return display_clear();
}

uint8_t display_refresh(uint8_t kmh,
                        uint16_t trip_tenths,
                        uint32_t odometer_tenths)
{
    uint8_t low_digit;
    uint8_t ok;
    uint32_t odometer_km = odometer_tenths / 10UL;

    if (ack_bits != 0x0Fu) {
        return 0u;
    }
    if (odometer_km > 99999UL) odometer_km = 99999UL;
    low_digit = digits[odometer_km % 10UL];

    ok = write_speed(kmh);
    set_bank(DISPLAY_BANK_DEFAULT);
    ok &= write_number(CC2_DISPLAY_TRIP, trip_tenths / 10u);
    set_bank(DISPLAY_BANK_ODOMETER);
    ok &= write_number(CC2_DISPLAY_BANKED,
                       (uint16_t)(odometer_km / 10UL));
    set_bank(DISPLAY_BANK_AUXILIARY);
    ok &= write_four(CC2_DISPLAY_BANKED, low_digit, 0u, 0u, 0u);
    set_bank(DISPLAY_BANK_DEFAULT);
    last_ok = ok;
    updates++;
    return ok;
}

uint8_t display_diagnostic_sweep(uint8_t value)
{
    uint8_t segment;
    uint8_t ok;

    if (value > 102u) value = 102u;
    ok = write_speed(value);
    segment = digits[value / 11u];
    set_bank(DISPLAY_BANK_DEFAULT);
    ok &= write_four(CC2_DISPLAY_TRIP, segment, segment, segment, segment);
    set_bank(DISPLAY_BANK_ODOMETER);
    ok &= write_four(CC2_DISPLAY_BANKED, segment, segment, segment, segment);
    set_bank(DISPLAY_BANK_AUXILIARY);
    ok &= write_four(CC2_DISPLAY_BANKED, segment, 0u, 0u, 0u);
    set_bank(DISPLAY_BANK_DEFAULT);
    return ok;
}

uint8_t display_ack_mask(void) { return ack_bits; }
uint16_t display_failure_count(void) { return failures; }
uint16_t display_update_count(void) { return updates; }
uint8_t display_last_update_ok(void) { return last_ok; }
