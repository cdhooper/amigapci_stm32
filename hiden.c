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
#include "hiden.h"

uint8_t hiden_is_set;

void
hiden_set(unsigned int enable)
{
    enable = !!enable;
    if (hiden_is_set != enable) {
        hiden_is_set = enable;
        gpio_setv(HIDEN_PORT, HIDEN_PIN, !enable);
    }
}
