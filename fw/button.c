/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Button functions
 */

#include "main.h"
#include "button.h"
#include "printf.h"
#include "config.h"
#include "gpio.h"
#include "timer.h"
#include "utils.h"

typedef enum {
    BUTTON_USER_SHORT,
    BUTTON_USER_LONG,
    BUTTON_DFU_SHORT,
    BUTTON_DFU_LONG,
    BUTTON_BOTH_SHORT,
    BUTTON_BOTH_LONG,
} button_op_t;

static void
button_op(button_op_t op)
{
    switch (op) {
        case BUTTON_USER_SHORT:
            break;
        case BUTTON_USER_LONG:
            break;
        case BUTTON_DFU_SHORT:
            break;
        case BUTTON_DFU_LONG:
            break;
        case BUTTON_BOTH_SHORT:
            break;
        case BUTTON_BOTH_LONG:
            break;
    }
}

/*
 * button_poll
 * ----------
 * Manage User and DFU buttons
 */
void
button_poll(void)
{
    static uint8_t  buttons_last = 0;
    static uint8_t  buttons_prev = 0;
    static uint64_t deglitch_timer = 0;
    static uint64_t long_timer = 0;
    uint8_t         buttons;

    switch (config.board_type) {
        case BOARD_TYPE_KEYJAM:
            buttons = gpio_get(GPIOC, GPIO13 | GPIO14) >> 13;
            break;
        case BOARD_TYPE_APCIDEV:
            buttons = !gpio_get(GPIOC, GPIO13);
            break;
        default:
            return;
    }

    /* Check and debounce button press */
    if (buttons_last != buttons) {
        buttons_last = buttons;
        deglitch_timer = timer_tick_plus_msec(100);
        long_timer     = timer_tick_plus_msec(750);
        return;  // state must be stable for deglitch period
    }
    if (deglitch_timer && timer_tick_has_elapsed(deglitch_timer)) {
        uint8_t diff = buttons ^ buttons_prev;
        deglitch_timer = 0;

        if ((diff & buttons_prev) && (long_timer != 0)) {
            /* This is a release before the long timer has elapsed */
            switch (diff & buttons_prev) {
                case BIT(0):
                    button_op(BUTTON_USER_SHORT);
                    break;
                case BIT(1):
                    button_op(BUTTON_DFU_SHORT);
                    break;
                case BIT(0) | BIT(1):
                    button_op(BUTTON_BOTH_SHORT);
                    break;
            }
            long_timer = 0;
        }
        buttons_prev = buttons;
    }

    if (long_timer && timer_tick_has_elapsed(long_timer)) {
        switch (buttons) {
            case BIT(0):
                button_op(BUTTON_USER_LONG);
                break;
            case BIT(1):
                button_op(BUTTON_DFU_LONG);
                break;
            case BIT(0) | BIT(1):
                button_op(BUTTON_BOTH_LONG);
                break;
        }
        buttons_prev = 0;
        long_timer = 0;
    }
}
