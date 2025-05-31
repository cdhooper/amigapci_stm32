/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Amiga and USB HID mouse handling.
 */

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "config.h"
#include "mouse.h"
#include "mouse.h"
#include "gpio.h"
#include "printf.h"
#include "timer.h"
#include "utils.h"
#include "keyboard.h"
#include "hiden.h"

#define B0_GPIO ADDR32(BND_IO(FIRE_PORT + GPIO_ODR_OFFSET, low_bit(FIRE_PIN)))
#define B1_GPIO ADDR32(BND_IO(PotY_PORT + GPIO_ODR_OFFSET, low_bit(PotY_PIN)))
#define B2_GPIO ADDR32(BND_IO(PotX_PORT + GPIO_ODR_OFFSET, low_bit(PotX_PIN)))

/* X is Down/Right */
#define QX0_GPIO ADDR32(BND_IO(BACK_PORT + GPIO_ODR_OFFSET, low_bit(BACK_PIN)))
#define QX1_GPIO ADDR32(BND_IO(RIGHT_PORT + GPIO_ODR_OFFSET, \
                        low_bit(RIGHT_PIN)))

/* Y is Up/Left */
#define QY0_GPIO ADDR32(BND_IO(FORWARD_PORT + GPIO_ODR_OFFSET, \
                               low_bit(FORWARD_PIN)))
#define QY1_GPIO ADDR32(BND_IO(LEFT_PORT + GPIO_ODR_OFFSET, low_bit(LEFT_PIN)))

static const uint8_t quad0[] = { 0, 0, 1, 1 };
static const uint8_t quad1[] = { 0, 1, 1, 0 };

static uint8_t      xquad;
static uint8_t      yquad;
static volatile int mouse_x;
static volatile int mouse_y;
uint32_t mouse_buttons_add;

void
mouse_action(int off_x, int off_y, int off_wheel, uint32_t buttons)
{
    static uint8_t last[16];
    uint b;
    (void) off_wheel;
    mouse_x += off_x;
    mouse_y += off_y;

    if (config.debug_flag & DF_USB_MOUSE) {
        printf(" ");
        if (off_x != 0)
            printf("Mx ");
        if (off_y != 0)
            printf("My ");
        if (off_wheel != 0)
            printf("Mw ");
    }

    buttons |= mouse_buttons_add;
    hiden_set(1);

    for (b = 0; b < ARRAY_SIZE(config.buttonmap); b++) {
        uint bmapped = config.buttonmap[b];
        uint is_pressed = (buttons & BIT(b)) ? 1 : 0;
        if (bmapped == 0)
            bmapped = b;  // Not reassigned: default this button to itself
        else if (bmapped <= 8)
            bmapped--;
        switch (bmapped) {
            case 0:
                *B0_GPIO = !is_pressed;
                break;
            case 1:
                *B1_GPIO = !is_pressed;
                break;
            case 2:
                *B2_GPIO = !is_pressed;
                break;
            default:
                if ((bmapped >= 0x10) && (last[b] != is_pressed)) {
                    /* Button mapped to one to four Keystrokes */
                    uint32_t val;
                    bmapped -= 0x10;
                    for (val = bmapped; val != 0; val >>= 8) {
                        if (is_pressed)
                            amiga_keyboard_put(val);
                        else
                            amiga_keyboard_put(val | 0x80);
                    }
                }
        }
        if (is_pressed && (bmapped < 0x10))
            dprintf(DF_USB_MOUSE | DF_AMIGA_MOUSE, "B%x", bmapped);
        last[b] = is_pressed;
    }
}

static void
move_x(int dir)
{
    dprintf(DF_AMIGA_MOUSE, "%c", (dir > 0) ? 'x' : 'X');
    xquad = (xquad + dir) & 3;
    *QX0_GPIO = quad0[xquad];
    *QX1_GPIO = quad1[xquad];
}

static void
move_y(int dir)
{
    dprintf(DF_AMIGA_MOUSE, "%c", (dir > 0) ? 'y' : 'Y');
    yquad = (yquad + dir) & 3;
    *QY0_GPIO = quad0[yquad];
    *QY1_GPIO = quad1[yquad];
}

void
mouse_poll(void)
{
    static uint64_t mouse_timer;

    if (!timer_tick_has_elapsed(mouse_timer))
        return;
    mouse_timer = timer_tick_plus_usec(250);
    if (mouse_x > 0) {
        mouse_x--;
        move_x(-1);
    } else if (mouse_x < 0) {
        mouse_x++;
        move_x(1);
    }
    if (mouse_y > 0) {
        mouse_y--;
        move_y(-1);
    } else if (mouse_y < 0) {
        mouse_y++;
        move_y(1);
    }
}
