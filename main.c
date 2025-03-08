/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * main routine.
 */

#include <stdint.h>
#include "main.h"
#include "printf.h"
#include "led.h"
#include "gpio.h"
#include "timer.h"
#include "uart.h"
#include "cmdline.h"
#include "clock.h"
#include "config.h"
#include "fan.h"
#include "kbrst.h"
#include "keyboard.h"
#include "mouse.h"
#include "power.h"
#include "readline.h"
#include "sensor.h"
#include "usb.h"
#include "utils.h"
#include "version.h"
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

void
main_poll(void)
{
    led_poll();
    sensor_poll();
    config_poll();
    usb_poll();
    power_poll();
    fan_poll();
    keyboard_poll();
    kbrst_poll();
    mouse_poll();
}

int
main(void)
{
//  reset_periphs();
//  reset_check();
    get_reset_reason();     // Must be done very early
    gpio_init_early();
    clock_init();
    timer_init();
//  timer_delay_msec(500);  // Just for development purposes
    uart_init();
    gpio_init();            // Needs UART for debug
    led_init();
    led_power(1);

    printf("\r\nAmigaPCI STM32 %s\n", version_str);

    identify_cpu();
    show_reset_reason();
    config_read();
    sensor_init();          // also starts adc_init()
    fan_init();
    keyboard_init();
    usb_init();

    rl_initialize();        // Enable command editing and history
    using_history();

    while (1) {
        main_poll();
        cmdline();
    }
}
