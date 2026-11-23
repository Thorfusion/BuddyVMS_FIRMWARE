#include "cc2_hw.h"
#include "hardware.h"
#include "safety.h"
#include "lithium.h"

static void ports_init_original_layout(void)
{
    PORT_PAGE = 3u;
    PORT_SFR_86 = 0xE7u;
    PORT_PAGE = 1u;
    PORT_SFR_86 = 0xE4u;
    PORT_PAGE = 0u;
    PORT_SFR_86 = 0xD8u;
    P0 = 0xC0u;

    PORT_PAGE = 3u;
    P1_DIR = 0x3Fu;
    PORT_PAGE = 0u;
    P1_DIR = 0xE0u;
    P1 = 0x20u;

    PORT_PAGE = 3u;
    P3 = 0x03u;
    P3_DIR = 0x0Fu;
    PORT_PAGE = 0u;
    P3 = 0x13u;
    P3_DIR = 0xF3u;

    PORT_PAGE = 3u;
    P4_DIR = 0x13u;
    PORT_PAGE = 0u;
    P4_DIR = 0xECu;

    PORT_PAGE = 3u;
    P5_DATA = 0xA4u;
    P5_DIR = 0xFFu;
    PORT_PAGE = 1u;
    P5_DIR = 0x5Fu;
    PORT_PAGE = 0u;
    P5_DIR = 0xA4u;
    P5_DATA = 0xA0u;
}

void outputs_all_safe(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    CHARGE_ENABLE_RELAY_N = 1;
    FULL_CHARGE_RELAY = 0;
    FAN_RELAY = 0;
    MOTOR_RELAY_ENABLE = 0;
    DIRECTION_RELAY = 0;
    FOOTSWITCH_RELAY = 0;
    DCDC_FORCE_OUTPUT = 0;
    BUZZER_OUTPUT = 0;
}

void hardware_init(void)
{
    EA = 0;
    IE = 0u;
    IEN1 = 0u;
    ports_init_original_layout();
    outputs_all_safe();
    ADC_ACTIVITY_LED_N = 1;
    UART_TX_LED_N = 1;
}

void output_set(cc2_output_t output, uint8_t active)
{
    uint8_t saved_ea = EA;
    EA = 0;
    active = active ? 1u : 0u;
    if ((safety_expired || li_faults) &&
        (output == CC2_OUTPUT_CHARGE || output == CC2_OUTPUT_MOTOR || output == CC2_OUTPUT_FOOT)) {
        active = 0u;
    }
    PORT_PAGE = CC2_PORT_PAGE_DATA;

    switch (output) {
    case CC2_OUTPUT_CHARGE:
        CHARGE_ENABLE_RELAY_N = active ? 0 : 1;
        break;
    case CC2_OUTPUT_FULL:
        FULL_CHARGE_RELAY = active;
        break;
    case CC2_OUTPUT_FAN:
        FAN_RELAY = active;
        break;
    case CC2_OUTPUT_MOTOR:
        MOTOR_RELAY_ENABLE = active;
        break;
    case CC2_OUTPUT_DIRECTION:
        DIRECTION_RELAY = active;
        break;
    case CC2_OUTPUT_FOOT:
        FOOTSWITCH_RELAY = active;
        break;
    case CC2_OUTPUT_DCDC:
        DCDC_FORCE_OUTPUT = active;
        break;
    case CC2_OUTPUT_BUZZER:
        BUZZER_OUTPUT = active;
        break;
    }
    EA = saved_ea;
}

uint8_t input_charger(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    return CHARGER_INPUT_DETECT ? 1u : 0u;
}

uint8_t input_ignition(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    return IGNITION_INPUT_DETECT ? 1u : 0u;
}

uint8_t input_direction(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    return (P5_DATA & CC2_P5_DIRECTION_INPUT_MASK) ? 1u : 0u;
}

uint8_t input_trip(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    return TRIP_BUTTON ? 1u : 0u;
}

uint8_t input_speed(void)
{
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    return SPEED_SIGNAL_PULSE ? 1u : 0u;
}
