/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Amiga and USB HID joystick / gamepad handling.
 */

#include <stdint.h>
#include "main.h"
#include "config.h"
#include "joystick.h"
#include "mouse.h"
#include "gpio.h"
#include "hiden.h"
#include "printf.h"
#include "utils.h"

#define B0_GPIO ADDR32(BND_IO(FIRE_PORT + GPIO_ODR_OFFSET, low_bit(FIRE_PIN)))
#define B1_GPIO ADDR32(BND_IO(PotY_PORT + GPIO_ODR_OFFSET, low_bit(PotY_PIN)))
#define B2_GPIO ADDR32(BND_IO(PotX_PORT + GPIO_ODR_OFFSET, low_bit(PotX_PIN)))

#define BACK_GPIO ADDR32(BND_IO(BACK_PORT + GPIO_ODR_OFFSET, low_bit(BACK_PIN)))
#define RIGHT_GPIO ADDR32(BND_IO(RIGHT_PORT + GPIO_ODR_OFFSET, \
                          low_bit(RIGHT_PIN)))
#define FORWARD_GPIO ADDR32(BND_IO(FORWARD_PORT + GPIO_ODR_OFFSET, \
                                   low_bit(FORWARD_PIN)))
#define LEFT_GPIO ADDR32(BND_IO(LEFT_PORT + GPIO_ODR_OFFSET, low_bit(LEFT_PIN)))

uint8_t joystick_asserted;

void
joystick_action(uint up, uint down, uint left, uint right, uint32_t buttons)
{
    static uint8_t  last_up;
    static uint8_t  last_down;
    static uint8_t  last_left;
    static uint8_t  last_right;
    static uint32_t last_buttons;
    uint32_t        macro;
    uint            change = 0;

    joystick_asserted = up | down | left | right || !!buttons;

    if (last_up != up) {
        macro = config.jdirectmap[0] ? config.jdirectmap[0] : 6;
        mouse_put_macro(macro, up, last_up);
        last_up = up;
        change = 1;
    }
    if (last_down != down) {
        macro = config.jdirectmap[1] ? config.jdirectmap[1] : 7;
        mouse_put_macro(macro, down, last_down);
        last_down = down;
        change = 1;
    }
    if (last_left != left) {
        macro = config.jdirectmap[2] ? config.jdirectmap[2] : 8;
        mouse_put_macro(macro, left, last_left);
        last_left = left;
        change = 1;
    }
    if (last_right != right) {
        macro = config.jdirectmap[3] ? config.jdirectmap[3] : 9;
        mouse_put_macro(macro, right, last_right);
        last_right = right;
        change = 1;
    }

    buttons |= mouse_buttons_add;

    if (buttons != last_buttons) {
        uint bit;
        for (bit = 0; bit < ARRAY_SIZE(config.jbuttonmap); bit++) {
            uint     is_pressed  = (buttons & BIT(bit)) ? 1 : 0;
            uint     was_pressed = (last_buttons & BIT(bit)) ? 1 : 0;
            macro = config.jbuttonmap[bit];
            if (macro == 0)
                macro = bit;  // Not reassigned: default this button to itself
            else if (macro <= 4)
                macro--;
            if (is_pressed != was_pressed) {
                mouse_put_macro(macro, is_pressed, was_pressed);
            }
        }
        last_buttons = buttons;
        change = 1;
    }
    if (change)
        hiden_set(1);
}
