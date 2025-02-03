/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * STM32 LED control
 */

#include "main.h"
#include <stdint.h>
#include "led.h"
#include "gpio.h"
#include "timer.h"
#include "printf.h"

#include <libopencm3/stm32/gpio.h>

static uint8_t  led_alert_state = 0;
static uint64_t led_power_timer;

void
led_power(int turn_on)
{
    if (turn_on)
        gpio_clear(POWER_LED_PORT, POWER_LED_PIN);
    else
        gpio_set(POWER_LED_PORT, POWER_LED_PIN);
}

void
led_alert(int turn_on)
{
    led_alert_state = turn_on;
    led_poll();
//  gpio_setv(LED_ALERT_PORT, LED_ALERT_PIN, turn_on);
}

void
led_busy(int turn_on)
{
//  gpio_setv(LED_BUSY_PORT, LED_BUSY_PIN, turn_on);
}

/*
 * Manage LED state such as error blink blinking
 */
void
led_poll(void)
{
    if (led_alert_state && (timer_tick_has_elapsed(led_power_timer))) {
        /* Blink Power LED */
        led_alert_state ^= 2;
        led_power(led_alert_state & 2);
        led_power_timer = timer_tick_plus_msec(250);
    }
}

void
led_init(void)
{
    gpio_mode_setup(POWER_LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                    POWER_LED_PIN);
    led_power(1);
}
