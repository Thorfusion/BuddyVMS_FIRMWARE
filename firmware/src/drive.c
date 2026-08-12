#include "cc2_config.h"
#include "drive.h"
#include "hardware.h"
#include "speed.h"

typedef struct {
    uint8_t stable;
    uint8_t candidate;
    uint8_t count;
} debounce_t;

static debounce_t ignition_input;
static debounce_t charger_input;
static debounce_t direction_input;
static debounce_t trip_input;
static cc2_drive_state_t state;
static uint16_t delay_ms;
static uint8_t control_enabled;
static uint8_t applied_forward;

static void debounce_init(debounce_t *input, uint8_t value)
{
    input->stable = value;
    input->candidate = value;
    input->count = CC2_INPUT_DEBOUNCE_SAMPLES;
}

static void debounce_update(debounce_t *input, uint8_t value)
{
    if (value != input->candidate) {
        input->candidate = value;
        input->count = 1u;
        return;
    }
    if (input->count < CC2_INPUT_DEBOUNCE_SAMPLES) {
        input->count++;
        if (input->count >= CC2_INPUT_DEBOUNCE_SAMPLES) {
            input->stable = input->candidate;
        }
    }
}

static uint8_t active_level(uint8_t raw, uint8_t active)
{
    return raw == active ? 1u : 0u;
}

static void drive_outputs_off(void)
{
    output_set(CC2_OUTPUT_FOOT, 0u);
    output_set(CC2_OUTPUT_MOTOR, 0u);
}

static void apply_requested_direction(void)
{
    applied_forward = drive_forward_requested();
    output_set(CC2_OUTPUT_DIRECTION, applied_forward ? 0u : 1u);
}

static void set_delay(uint16_t value)
{
    delay_ms = value;
}

static uint8_t delay_elapsed(void)
{
    if (delay_ms <= CC2_CONTROL_PERIOD_MS) {
        delay_ms = 0u;
        return 1u;
    }
    delay_ms -= CC2_CONTROL_PERIOD_MS;
    return 0u;
}

void drive_init(void)
{
    debounce_init(&ignition_input, input_ignition());
    debounce_init(&charger_input, input_charger());
    debounce_init(&direction_input, input_direction());
    debounce_init(&trip_input, input_trip());
    state = CC2_DRIVE_OFF;
    delay_ms = 0u;
#if CC2_ENABLE_DRIVE_CONTROL
    control_enabled = 1u;
#else
    control_enabled = 0u;
#endif
    applied_forward = 1u;
    drive_outputs_off();
    output_set(CC2_OUTPUT_DIRECTION, 0u);
    if (control_enabled && speed_kmh() <= CC2_DIRECTION_CHANGE_MAX_KMH) {
        apply_requested_direction();
    }
}

void drive_tick_10ms(void)
{
    uint8_t allowed;
    uint8_t requested_forward;

    debounce_update(&ignition_input, input_ignition());
    debounce_update(&charger_input, input_charger());
    debounce_update(&direction_input, input_direction());
    debounce_update(&trip_input, input_trip());
    speed_trip_button_service(drive_trip_pressed());

    allowed = drive_ignition_on() && !drive_charger_present();
    requested_forward = drive_forward_requested();

    if (!control_enabled || !allowed) {
        drive_outputs_off();
        output_set(CC2_OUTPUT_DIRECTION, 0u);
        applied_forward = 1u;
        state = CC2_DRIVE_OFF;
        delay_ms = 0u;
        return;
    }

    switch (state) {
    case CC2_DRIVE_OFF:
        drive_outputs_off();
        if (speed_kmh() > CC2_DIRECTION_CHANGE_MAX_KMH &&
            requested_forward != applied_forward) {
            return;
        }
        apply_requested_direction();
        set_delay(CC2_DRIVE_START_DELAY_MS);
        state = CC2_DRIVE_START_DELAY;
        break;

    case CC2_DRIVE_START_DELAY:
        if (requested_forward != applied_forward) {
            if (speed_kmh() > CC2_DIRECTION_CHANGE_MAX_KMH) {
                state = CC2_DRIVE_WAIT_STOP;
                drive_outputs_off();
                break;
            }
            apply_requested_direction();
            set_delay(CC2_DRIVE_START_DELAY_MS);
        }
        if (delay_elapsed()) {
            output_set(CC2_OUTPUT_MOTOR, 1u);
            set_delay(CC2_MOTOR_TO_FOOT_DELAY_MS);
            state = CC2_DRIVE_MOTOR_WAIT;
        }
        break;

    case CC2_DRIVE_MOTOR_WAIT:
        if (requested_forward != applied_forward) {
            drive_outputs_off();
            state = CC2_DRIVE_WAIT_STOP;
            break;
        }
        if (delay_elapsed()) {
            output_set(CC2_OUTPUT_FOOT, 1u);
            state = CC2_DRIVE_RUNNING;
        }
        break;

    case CC2_DRIVE_RUNNING:
        if (requested_forward != applied_forward) {
            drive_outputs_off();
            state = CC2_DRIVE_WAIT_STOP;
        }
        break;

    case CC2_DRIVE_WAIT_STOP:
        drive_outputs_off();
        if (requested_forward == applied_forward) {
            set_delay(CC2_DRIVE_START_DELAY_MS);
            state = CC2_DRIVE_START_DELAY;
        } else if (speed_kmh() <= CC2_DIRECTION_CHANGE_MAX_KMH) {
            set_delay(CC2_DIRECTION_DEADTIME_MS);
            state = CC2_DRIVE_DIRECTION_DEADTIME;
        }
        break;

    case CC2_DRIVE_DIRECTION_DEADTIME:
        drive_outputs_off();
        if (speed_kmh() > CC2_DIRECTION_CHANGE_MAX_KMH) {
            state = CC2_DRIVE_WAIT_STOP;
        } else if (delay_elapsed()) {
            apply_requested_direction();
            set_delay(CC2_DRIVE_START_DELAY_MS);
            state = CC2_DRIVE_START_DELAY;
        }
        break;
    }
}

uint8_t drive_set_control_enabled(uint8_t enabled)
{
    if (!enabled) {
        control_enabled = 0u;
        state = CC2_DRIVE_OFF;
        delay_ms = 0u;
        drive_outputs_off();
        output_set(CC2_OUTPUT_DIRECTION, 0u);
        applied_forward = 1u;
        return 1u;
    }
#if CC2_ENABLE_DRIVE_CONTROL
    control_enabled = 1u;
    return 1u;
#else
    return 0u;
#endif
}

uint8_t drive_control_enabled(void) { return control_enabled; }
cc2_drive_state_t drive_state(void) { return state; }

uint8_t drive_ignition_on(void)
{
    return active_level(ignition_input.stable, CC2_IGNITION_ACTIVE_LEVEL);
}

uint8_t drive_charger_present(void)
{
    return active_level(charger_input.stable, CC2_CHARGER_PRESENT_LEVEL);
}

uint8_t drive_forward_requested(void)
{
    return active_level(direction_input.stable, CC2_DIRECTION_FORWARD_LEVEL);
}

uint8_t drive_forward_applied(void) { return applied_forward; }

uint8_t drive_trip_pressed(void)
{
    return active_level(trip_input.stable, CC2_TRIP_BUTTON_ACTIVE_LEVEL);
}

uint16_t drive_delay_remaining_ms(void) { return delay_ms; }
