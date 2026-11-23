#include "cc2_config.h"
#include "commands.h"
#include "controller.h"
#include "display.h"
#include "drive.h"
#include "hardware.h"
#include "lithium.h"
#include "legacy.h"
#include "safety.h"
#include "settings.h"
#include "speed.h"
#include "timebase.h"
#include "uart.h"
#include "vehicle_eeprom.h"

void main(void)
{
    uint32_t now;
    uint32_t control_time = 0UL;
    uint32_t speed_time = 0UL;
    uint32_t display_time = 0UL;

    hardware_init();
    uart_init();
    display_bus_init();
    speed_init();
    li_init();
    drive_init();
    vehicle_eeprom_init();
    /* SSC completion needs interrupts even during the read-only boot restore. */
    EA = 1;
    (void)settings_load();
    legacy_init();
    (void)display_init();
    timebase_init();
    safety_reset();
    watchdog_start();
    uart_puts("$BOOT," CC2_FIRMWARE_NAME "," CC2_FIRMWARE_VERSION "#\r\n");

    while (1) {
        now = timebase_uptime_ms();
        if ((uint32_t)(now - speed_time) >= CC2_SPEED_WINDOW_MS) {
            speed_capture_window((uint16_t)((uint32_t)(now - speed_time) > 65535UL ?
                65535UL : (uint32_t)(now - speed_time)));
            speed_time = now;
        }
        if ((uint32_t)(now - control_time) >= CC2_CONTROL_PERIOD_MS) {
            control_time = now;
            controller_tick();
            legacy_service(now, !drive_ignition_on() && speed_kmh() == 0u);
        }
        commands_poll();
        now = timebase_uptime_ms();
        if ((uint32_t)(now - display_time) >= CC2_DISPLAY_PERIOD_MS) {
            display_time = now;
            (void)display_refresh(speed_kmh(), speed_trip_tenths_km(),
                                  speed_odometer_tenths_km());
        }
    }
}
