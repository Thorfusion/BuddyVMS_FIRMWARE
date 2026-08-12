#include "cc2_config.h"
#include "commands.h"
#include "display.h"
#include "drive.h"
#include "hardware.h"
#include "speed.h"
#include "timebase.h"
#include "uart.h"
#include "vehicle_eeprom.h"

void main(void)
{
    uint16_t control_elapsed = 0u;
    uint16_t speed_elapsed = 0u;
    uint16_t display_elapsed = 0u;

    hardware_init();
    uart_init();
    display_bus_init();
    speed_init();
    drive_init();
    vehicle_eeprom_init();
    (void)display_init();
    timebase_init();
    EA = 1;
    uart_puts("$BOOT," CC2_FIRMWARE_NAME "," CC2_FIRMWARE_VERSION "#\r\n");

    while (1) {
        commands_poll();
        while (timebase_take_1ms()) {
            control_elapsed++;
            speed_elapsed++;
            display_elapsed++;

            if (control_elapsed >= CC2_CONTROL_PERIOD_MS) {
                control_elapsed = 0u;
                drive_tick_10ms();
            }
            if (speed_elapsed >= CC2_SPEED_WINDOW_MS) {
                speed_elapsed = 0u;
                speed_capture_window();
            }
            if (display_elapsed >= CC2_DISPLAY_PERIOD_MS) {
                display_elapsed = 0u;
                (void)display_refresh(speed_kmh(),
                                      speed_trip_tenths_km(),
                                      speed_odometer_tenths_km());
            }
        }
    }
}
