/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Amiga KBRST handling
 */

#include <stdint.h>
#include "board.h"
#include "main.h"
#include "config.h"
#include "gpio.h"
#include "printf.h"
#include "timer.h"
#include "kbrst.h"
#include "power.h"

uint8_t         amiga_in_reset         = 0xff;  // Not initialized
static uint64_t amiga_reset_timer      = 0;  // Timer to take Amiga out of reset
static uint64_t amiga_kclk_reset_timer = 0;

/*
 * set_amiga_reset() drives the KBRST pin to set the Amiga in reset or take
 *                   it out of reset.  If the state is to release reset (0),
 *                   then the pin will be briefly driven high, then released
 *                   to open drain. This is to allow other AmigaPCI hardware
 *                   to drive the pin low.
 */
static void
set_amiga_reset(uint put_in_reset)
{
    gpio_setv(KBRST_PORT, KBRST_PIN, !put_in_reset);
    if (put_in_reset == 0) {
        gpio_setmode(KBRST_PORT, KBRST_PIN, GPIO_SETMODE_OUTPUT_2);
        timer_delay_usec(2);
        gpio_setmode(KBRST_PORT, KBRST_PIN,
                     GPIO_SETMODE_OUTPUT_ODRAIN_25 | GPIO_SETMODE_PU);
    }
    amiga_in_reset = put_in_reset;
}

void
kbrst_poll(void)
{
    uint8_t in_reset;
    static uint8_t in_reset_last;

    if (power_state == POWER_STATE_INITIAL)
        return;  // Wait for power state to be determined

    if (amiga_in_reset == 0xff) {
        /* Get initial state */
        amiga_in_reset = !gpio_get(KBRST_PORT, KBRST_PIN);
    }

    if ((amiga_kclk_reset_timer != 0) &&
        timer_tick_has_elapsed(amiga_kclk_reset_timer)) {
        amiga_kclk_reset_timer = 0;
        gpio_setv(KBRST_PORT, KBCLK_PIN | KBDATA_PIN, 1);
    }

    /* Handle power state changes affecting KBRST */
    static uint8_t power_state_last = POWER_STATE_INITIAL;
    if ((config.board_type != 2) && (power_state != power_state_last)) {
        if (power_state == POWER_STATE_ON) {
            if (amiga_in_reset)
                amiga_reset_timer = timer_tick_plus_msec(400);
        } else {
            if (!amiga_in_reset)
                set_amiga_reset(1);  // Assert KBRST now (put Amiga in reset)
        }
        power_state_last = power_state;
    }

    /* Handle reset timer */
    if ((amiga_reset_timer != 0) && timer_tick_has_elapsed(amiga_reset_timer)) {
        amiga_reset_timer = 0;
        set_amiga_reset(0);  // Deassert KBRST (take Amiga out of reset)
    }

    /* Report reset state change */
    in_reset = !gpio_get(KBRST_PORT, KBRST_PIN);
    if (in_reset_last != in_reset) {
        in_reset_last = in_reset;
        /* Amiga reset state change has occurred */

        if (power_state == POWER_STATE_ON) {
            /* Only report reset state when power is on */
            if (in_reset == 0) {
                /* Out of reset */
                printf("Amiga out of reset\n");
            } else {
                /* In reset */
                printf("Amiga in reset\n");
            }
        }
    }
}

void
kbrst_amiga(uint hold, uint longreset)
{
    gpio_setv(KBRST_PORT, KBCLK_PIN | KBDATA_PIN, 0);
    gpio_setv(KBRST_PORT, KBRST_PIN, 0);

    if (hold) {
        amiga_reset_timer = 0;
        amiga_kclk_reset_timer = 0;
    } else {
        if (longreset) {
            amiga_reset_timer = timer_tick_plus_msec(2500);
            amiga_kclk_reset_timer = timer_tick_plus_msec(2500);
        } else {
            amiga_reset_timer = timer_tick_plus_msec(400);
            amiga_kclk_reset_timer = timer_tick_plus_msec(500);
        }
    }
}
