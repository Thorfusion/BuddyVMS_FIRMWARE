#include "cc2_config.h"
#include "cc2_hw.h"
#include "speed.h"

static volatile uint16_t pulse_counter;
static uint16_t calibration;
static uint16_t last_window;
static uint16_t distance_remainder;
static uint16_t trip_tenths;
static uint32_t odometer_tenths;
static uint32_t total_pulses;
static uint8_t current_kmh;
static uint8_t trip_latched;

void speed_external_isr(void) __interrupt(CC2_EXTERNAL_3_6_INTERRUPT_VECTOR)
{
    uint8_t saved_page = MOD_PAGE;
    uint8_t flags;

    MOD_PAGE = CC2_MOD_PAGE_SCU_INTERRUPT;
    flags = SCU_IRCON0;
    if ((flags & CC2_SCU_IRCON0_EXINT4_MASK) != 0u) {
        SCU_IRCON0 = flags & (uint8_t)~CC2_SCU_IRCON0_EXINT4_MASK;
        pulse_counter++;
    }
    MOD_PAGE = saved_page;
}

static uint16_t take_pulses(void)
{
    uint8_t saved_ea = EA;
    uint16_t value;

    EA = 0;
    value = pulse_counter;
    pulse_counter = 0u;
    EA = saved_ea;
    return value;
}

void speed_init(void)
{
    pulse_counter = 0u;
    calibration = CC2_SPEED_DEFAULT_PULSES_PER_100M;
    last_window = 0u;
    distance_remainder = 0u;
    trip_tenths = 0u;
    odometer_tenths = 0UL;
    total_pulses = 0UL;
    current_kmh = 0u;
    trip_latched = 0u;

    MOD_PAGE = 0u;
    SCU_SFR_BA = 0x3Cu;
    MOD_PAGE = 3u;
    SCU_SFR_EA = 0x04u;
    MOD_PAGE = 0u;
    IEN1 |= 0x08u;
}

void speed_capture_window(void)
{
    uint32_t accumulated;
    uint32_t calculated;
    uint16_t steps;

    last_window = take_pulses();
    total_pulses += last_window;
    accumulated = (uint32_t)distance_remainder + last_window;
    steps = (uint16_t)(accumulated / calibration);
    distance_remainder = (uint16_t)(accumulated % calibration);
    if (steps != 0u) {
        if (trip_tenths <= (uint16_t)(0xFFFFu - steps)) {
            trip_tenths += steps;
        }
        odometer_tenths += steps;
    }

    calculated = ((uint32_t)last_window * 360000UL) /
                 ((uint32_t)calibration * CC2_SPEED_WINDOW_MS);
    if (calculated > 255UL) {
        calculated = 255UL;
    }
    current_kmh = (uint8_t)calculated;
}

void speed_trip_button_service(uint8_t pressed)
{
    if (pressed && !trip_latched) {
        speed_reset_trip();
    }
    trip_latched = pressed;
}

void speed_reset_trip(void)
{
    trip_tenths = 0u;
    distance_remainder = 0u;
}

uint8_t speed_kmh(void) { return current_kmh; }
uint16_t speed_trip_tenths_km(void) { return trip_tenths; }
uint32_t speed_odometer_tenths_km(void) { return odometer_tenths; }
uint32_t speed_total_pulses(void) { return total_pulses; }
uint16_t speed_last_window_pulses(void) { return last_window; }
uint16_t speed_pulses_per_100m(void) { return calibration; }

uint16_t speed_pending_pulses(void)
{
    uint8_t saved_ea = EA;
    uint16_t value;

    EA = 0;
    value = pulse_counter;
    EA = saved_ea;
    return value;
}

uint8_t speed_set_pulses_per_100m(uint16_t value)
{
    if (value == 0u) {
        return 0u;
    }
    calibration = value;
    distance_remainder = 0u;
    return 1u;
}
