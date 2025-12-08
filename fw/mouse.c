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
#include <string.h>
#include "main.h"
#include "config.h"
#include "mouse.h"
#include "gpio.h"
#include "printf.h"
#include "timer.h"
#include "utils.h"
#include "keyboard.h"
#include "amiga_kbd_codes.h"
#include "hid_kbd_codes.h"
#include "hiden.h"

#define B0_GPIO ADDR32(BND_IO(FIRE_PORT + GPIO_ODR_OFFSET, low_bit(FIRE_PIN)))
#define B1_GPIO ADDR32(BND_IO(PotY_PORT + GPIO_ODR_OFFSET, low_bit(PotY_PIN)))
#define B2_GPIO ADDR32(BND_IO(PotX_PORT + GPIO_ODR_OFFSET, low_bit(PotX_PIN)))

#define BACK_GPIO ADDR32(BND_IO(BACK_PORT + GPIO_ODR_OFFSET, low_bit(BACK_PIN)))
#define RIGHT_GPIO ADDR32(BND_IO(RIGHT_PORT + GPIO_ODR_OFFSET, \
                          low_bit(RIGHT_PIN)))
#define FORWARD_GPIO ADDR32(BND_IO(FORWARD_PORT + GPIO_ODR_OFFSET, \
                                   low_bit(FORWARD_PIN)))
#define LEFT_GPIO ADDR32(BND_IO(LEFT_PORT + GPIO_ODR_OFFSET, low_bit(LEFT_PIN)))

/* X is Down/Right */
#define QX0_GPIO BACK_GPIO
#define QX1_GPIO RIGHT_GPIO

/* Y is Up/Left */
#define QY0_GPIO FORWARD_GPIO
#define QY1_GPIO LEFT_GPIO

static const uint8_t quad0[] = { 0, 0, 1, 1 };
static const uint8_t quad1[] = { 0, 1, 1, 0 };

static uint8_t      xquad;
static uint8_t      yquad;
static volatile int mouse_x;
static volatile int mouse_y;
uint32_t mouse_buttons_add;
uint8_t mouse_asserted;

/*
 * mouse_put_macro() sets a mouse button or joystick direction or sends
 *                   a keyboard macro sequence.
 *
 * Values
 *    0 Left Mouse button
 *    1 Right Mouse button
 *    2 Middle Mouse button
 *    4 Fourth Mouse button keystroke
 *    6 Joystick up
 *    7 Joystick down
 *    8 Joystick left
 *    9 Joystick right
 *  >15 Keyboard macro (up to 4 keys may be sent)
 */
void
mouse_put_macro(uint32_t tcode, uint is_pressed, uint was_pressed)
{
    for (; tcode != 0; tcode >>= 8) {
        uint8_t code = (uint8_t) tcode;
        if (code & 0x80) {
            switch (code) {
                case ASE_BUTTON_0:
                    *B0_GPIO = !is_pressed;
                    break;
                case ASE_BUTTON_1:
                    *B1_GPIO = !is_pressed;
                    break;
                case ASE_BUTTON_2:
                    *B2_GPIO = !is_pressed;
                    break;
                case ASE_BUTTON_3:
                    if (was_pressed != is_pressed)
                        keyboard_put_macro(NM_BUTTON_FOURTH, is_pressed);
                    break;
                case ASE_BUTTON_4:
                    if (was_pressed != is_pressed)
                        keyboard_put_macro(NM_BUTTON_FIFTH, is_pressed);
                    break;
                case ASE_JOYSTICK_UP:     // Joystick up
                    *BACK_GPIO = !is_pressed;
                    break;
                case ASE_JOYSTICK_DOWN:   // Joystick down
                    *FORWARD_GPIO = !is_pressed;
                    break;
                case ASE_JOYSTICK_LEFT:   // Joystick left
                    *LEFT_GPIO = !is_pressed;
                    break;
                case ASE_JOYSTICK_RIGHT:  // Joystick right
                    *RIGHT_GPIO = !is_pressed;
                    break;
            }
            if (config.debug_flag & (DF_USB_MOUSE | DF_AMIGA_MOUSE |
                                     DF_AMIGA_JOYSTICK)) {
                if (was_pressed)
                    putchar('-');
                if ((code >= ASE_JOYSTICK_UP) &&
                           (code <= ASE_JOYSTICK_RIGHT)) {
                    putchar("UDLR"[code - ASE_JOYSTICK_UP]);
                } else {
                    uint bnum = code - ASE_BUTTON_0;
                    putchar('B');
                    if (bnum >= 10) {
                        putchar('0' + bnum / 10);
                        bnum %= 10;
                    }
                    putchar('0' + bnum);
                }
            }
        } else {
            /* Pass macro to keyboard processing */
            if (was_pressed != is_pressed)
                keyboard_put_macro(code, is_pressed);
        }
    }
}

/*
 * mouse_action() converts USB Mouse input to Amiga mouse or keyboard input.
 *
 * @param [in]  off_x     - X movement of the mouse (< 0 is left)
 * @param [in]  off_y     - Y movement of the mouse (< 0 is up)
 * @param [in]  off_wheel - Wheel movement of the mouse (< 0 is up)
 * @param [in]  off_pan   - Left-right movement of the mouse (< 0 is left)
 */
void
mouse_action(int off_x, int off_y, int off_wheel, int off_pan)
{
    static int last_wheel;
    static int last_pan;
    uint32_t macro;
    uint     change = 0;

    if (config.debug_flag & DF_USB_MOUSE) {
        if (off_x != 0)
            printf(" Mx");
        if (off_y != 0)
            printf(" My");
        if (off_wheel != 0)
            printf(" Mw");
        if (off_pan != 0)
            printf(" Mp");
    }

    if (config.flags & CF_MOUSE_INVERT_X)
        off_x = -off_x;
    if (config.flags & CF_MOUSE_INVERT_Y)
        off_y = -off_y;
    if (config.flags & CF_MOUSE_INVERT_W)
        off_wheel = -off_wheel;
    if (config.flags & CF_MOUSE_INVERT_P)
        off_pan = -off_pan;
    if (config.flags & CF_MOUSE_SWAP_XY) {
        int temp = off_x;
        off_x = off_y;
        off_y = temp;
    }
    if (config.flags & CF_MOUSE_SWAP_WP) {
        int temp = off_wheel;
        off_wheel = off_pan;
        off_pan = temp;
    }

    mouse_x += off_x;
    mouse_y += off_y;

    /* Limit how far behind the mouse can get */
    if (mouse_x > 20)
        mouse_x = 20;
    if (mouse_x < -20)
        mouse_x = -20;
    if (mouse_y > 20)
        mouse_y = 20;
    if (mouse_y < -20)
        mouse_y = -20;

    if ((mouse_x != 0) || (mouse_y != 0))
        change = 1;  // Mouse moved

    /* Up/down wheel */
    if (off_wheel != last_wheel) {
        macro = config.keymap[HS_MEDIA_S_UP];
        if (last_wheel < 0)
            mouse_put_macro(macro, 0, 1);
        if (off_wheel < 0)
            mouse_put_macro(macro, 1, 0);

        macro = config.keymap[HS_MEDIA_S_DOWN];
        if (last_wheel > 0)
            mouse_put_macro(macro, 0, 1);
        if (off_wheel > 0)
            mouse_put_macro(macro, 1, 0);

        /*
         * Update last_wheel if we want keystroke-like behavior
         *
         * XXX: More code is needed to implement this for wheel because the
         *      wheel won't give a state release like pan does. Need to
         *      set a timeout for the poll code to send a "key up"
         */
        if (config.flags & CF_MOUSE_KEYUP_WP)
            last_wheel = off_wheel;
        change = 1;
    }

    /* Left/right pan */
    if (off_pan != last_pan) {
        macro = config.keymap[HS_MEDIA_BACK];
        if (last_pan < 0)
            mouse_put_macro(macro, 0, 1);
        if (off_pan < 0)
            mouse_put_macro(macro, 1, 0);

        macro = config.keymap[HS_MEDIA_FWD];
        if (last_pan > 0)
            mouse_put_macro(macro, 0, 1);
        if (off_pan > 0)
            mouse_put_macro(macro, 1, 0);

        /* Update last_pan if we want keystroke-like behavior */
        if (config.flags & CF_MOUSE_KEYUP_WP)
            last_pan = off_pan;
        change = 1;
    }

    if (change)
        hiden_set(1);
}

/*
 * mouse_action() converts USB Mouse button input to Amiga actions
 *
 * @param [in]  buttons   - Bits representing buttno state (1 = pressed)
 */
void
mouse_action_button(uint32_t buttons)
{
    static uint32_t last_buttons;
    uint32_t        macro;
    uint            change = 0;

    buttons |= mouse_buttons_add;

    if (buttons != last_buttons) {
        uint bit;
        for (bit = 0; bit < ARRAY_SIZE(config.buttonmap); bit++) {
            uint     is_pressed  = (buttons & BIT(bit)) ? 1 : 0;
            uint     was_pressed = (last_buttons & BIT(bit)) ? 1 : 0;
            macro = config.buttonmap[bit];
            if (macro == 0)
                macro = 0x80 + bit;  // Not reassigned: default to self
            else if (macro <= 4)
                macro--;
            if (is_pressed != was_pressed)
                mouse_put_macro(macro, is_pressed, was_pressed);
        }
        last_buttons = buttons;
        mouse_asserted = !!buttons;
        change = 1;
    }

    if (change)
        hiden_set(1);
}

void
mouse_set_defaults(void)
{
    config.mouse_mul_x = 0;
    config.mouse_mul_y = 0;
    config.mouse_div_x = 0;
    config.mouse_div_y = 0;
    memset(config.buttonmap, 0, sizeof (config.buttonmap));    // Mouse
    memset(config.jbuttonmap, 0, sizeof (config.jbuttonmap));  // Joystick
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
