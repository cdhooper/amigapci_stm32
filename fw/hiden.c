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
#include "config.h"
#include "gpio.h"
#include "printf.h"
#include "hiden.h"
#include "timer.h"
#include "usb.h"
#include "gpio.h"

uint8_t hiden_is_set;
static uint64_t hiden_timeout;

void
hiden_set(unsigned int enable)
{
    if (hiden_is_set != enable) {
        hiden_is_set = enable;
        dprintf(DF_HIDEN, "HID %s\n", enable ? "enabled" : "disabled");
        gpio_setv(HIDEN_PORT, HIDEN_PIN, !enable);
    }
    hiden_timeout = 0;
}

void
hiden_poll(void)
{
    /* Automatic HID disable when no mouse is present */
    if (hiden_is_set) {
        if (hiden_timeout == 0) {
            if (usb_mouse_count == 0)
                hiden_timeout = timer_tick_plus_msec(500);
            else
                hiden_timeout = timer_tick_plus_msec(5000);
            return;
        }
        if (timer_tick_has_elapsed(hiden_timeout)) {
            dprintf(DF_HIDEN, "Auto ");
            hiden_set(0);
            hiden_timeout = 0;

            /* Reset mouse / pins */
            gpio_setv(FORWARD_PORT, FORWARD_PIN | BACK_PIN | LEFT_PIN |
                                    RIGHT_PIN | FIRE_PIN, 1);
            gpio_setv(PotX_PORT, PotX_PIN | PotY_PIN, 1);
        }
    }
}
