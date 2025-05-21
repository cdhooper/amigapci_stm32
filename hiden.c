/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * HIDEN (HID Enable) signal handling for control of Keyboard and Mouse
 */

#include <stdint.h>
#include "main.h"
#include "gpio.h"
#include "printf.h"
#include "hiden.h"
#include "timer.h"
#include "usb.h"

uint8_t hiden_is_set;
static uint64_t hiden_timeout;

void
hiden_set(unsigned int enable)
{
    if (hiden_is_set != enable) {
        hiden_is_set = enable;
//      printf("HID %s\n", enable ? "enabled" : "disabled");
        gpio_setv(HIDEN_PORT, HIDEN_PIN, !enable);
    }
    if (enable)
        hiden_timeout = 0;
}

void
hiden_poll(void)
{
    /* Automatic HID disable when devices are missing */
    if (hiden_is_set &&
        ((usb_keyboard_count + usb_mouse_count) == 0)) {
        if (hiden_timeout == 0) {
            hiden_timeout = timer_tick_plus_msec(1000);
            return;
        }
        if (timer_tick_has_elapsed(hiden_timeout)) {
//          printf("Auto ");
            hiden_set(0);
            hiden_timeout = 0;
        }
    }
}
