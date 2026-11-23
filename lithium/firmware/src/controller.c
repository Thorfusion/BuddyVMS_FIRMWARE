#include "cc2_config.h"
#include "cc2_hw.h"
#include "controller.h"
#include "drive.h"
#include "hardware.h"
#include "safety.h"
#include "speed.h"
#include "timebase.h"

void controller_inputs(li_inputs_t *inputs)
{
    inputs->ignition = drive_ignition_on() || input_ignition() == CC2_IGNITION_ACTIVE_LEVEL;
    inputs->charger = drive_charger_present() || input_charger() == CC2_CHARGER_PRESENT_LEVEL;
    inputs->moving = speed_kmh() != 0u || speed_pending_pulses() != 0u;
    inputs->drive_busy = MOTOR_RELAY_ENABLE || FOOTSWITCH_RELAY;
}

void controller_apply_outputs(void)
{
    uint8_t saved_ea = EA;
    EA = 0;
    PORT_PAGE = CC2_PORT_PAGE_DATA;
    FULL_CHARGE_RELAY = 0;
    /* A timer-ISR expiry must not race a foreground relay-enable write. */
#if CC2_SAFE_BRINGUP
    CHARGE_ENABLE_RELAY_N = 1;
    MOTOR_RELAY_ENABLE = FOOTSWITCH_RELAY = 0;
    FAN_RELAY = BUZZER_OUTPUT = 0;
#else
    if (safety_expired || li_faults) {
        CHARGE_ENABLE_RELAY_N = 1;
        MOTOR_RELAY_ENABLE = FOOTSWITCH_RELAY = 0;
    } else {
        if (!li_drive_permit || input_charger()) MOTOR_RELAY_ENABLE = FOOTSWITCH_RELAY = 0;
        CHARGE_ENABLE_RELAY_N = !(li_charge_request && input_charger() &&
            input_ignition() != CC2_IGNITION_ACTIVE_LEVEL &&
            !MOTOR_RELAY_ENABLE && !FOOTSWITCH_RELAY);
    }
    FAN_RELAY = li_fan_request;
    BUZZER_OUTPUT = li_faults != 0u;
#endif
    EA = saved_ea;
}

void controller_tick(void)
{
    li_inputs_t inputs;
    CONTROLLER_WORK_LED_N = 0;
    if (safety_expired) li_trip(LI_FAULT_CONTROL);
    controller_inputs(&inputs);
    li_tick(timebase_uptime_ms(), &inputs);
    drive_tick_10ms();
    controller_apply_outputs();
    safety_refresh();
    watchdog_service();
    CONTROLLER_WORK_LED_N = 1;
}

uint8_t controller_idle(void)
{
    li_inputs_t inputs;
    controller_inputs(&inputs);
    return !inputs.ignition && !inputs.charger && !inputs.moving && !inputs.drive_busy &&
        !li_charge_request;
}

void controller_stop(void)
{
    li_config.charge_mode = LI_MODE_OFF;
    li_config.drive_mode = LI_MODE_OFF;
    li_charge_request = li_drive_permit = 0u;
    (void)drive_set_control_enabled(0u);
    controller_apply_outputs();
}
