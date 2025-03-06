/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Power management functions
 */

#include "printf.h"
#include "main.h"
#include "power.h"
#include "gpio.h"
#include "config.h"
#include "sensor.h"
#include "timer.h"
#include "utils.h"

#define POWER_ADC_STABLE         10 // msec for ADCs to initialize
#define POWER_BUTTON_DEGLITCH   100 // msec min to deglitch user power button
#define POWER_CYCLE_OFF_PERIOD 1000 // msec to hold power off during cycle
#define POWER_ON_STABLE        2000 // msec max until power supply rails stable
#define POWER_OFF_STABLE       2000 // msec max time until rails drain to "off"

uint8_t power_state_desired;
uint8_t power_state;

static uint64_t power_timer;

/*
 * power_button_poll
 * -----------------
 * Manage user power button presses
 */
static void
power_button_poll(void)
{
    static uint8_t  power_button_last = 0;
    static uint8_t  power_button_deglitch = 0;
    static uint8_t  power_button_armed = 0;
    static uint64_t power_button_timer = 0;
    uint8_t         power_button = -1;

    /* Check and debounce power button press */
    power_button = !gpio_get(PWRSW_PORT, PWRSW_PIN);

    if (power_button_last != power_button) {
        power_button_last = power_button;
#if 0
        /* Deglitch debug */
        if ((power_button_timer != 0) &&
            !timer_tick_has_elapsed(power_button_timer)) {
            uint64_t now = timer_tick_get();
            uint64_t diff = timer_tick_to_usec(power_button_timer - now);
            printf("[%u]", (uint) (POWER_BUTTON_DEGLITCH - diff));
        }
#endif
        power_button_timer = timer_tick_plus_msec(POWER_BUTTON_DEGLITCH);
        power_button_deglitch = 1;
        return;  // state must be stable for deglitch period
    }
    if (power_button_deglitch) {
        if (!timer_tick_has_elapsed(power_button_timer))
            return;  // State not stable

        power_button_deglitch = 0;
        if (power_button == 1)
            printf("Power button pressed\n");
    }

    if (power_button == 0) {
        power_button_armed = 1;
        return;  // Not being pressed
    }

    if (power_button_armed == 0)
        return;

    switch (power_state) {
        case POWER_STATE_INITIAL:
        case POWER_STATE_POWERING_ON:
        case POWER_STATE_POWERING_OFF:
        case POWER_STATE_CYCLE:
            return;  // In a transitional power state -- take no action
        case POWER_STATE_ON:
            if (power_state_desired == POWER_STATE_ON) {
                uint64_t now = timer_tick_get();
                uint64_t diff = timer_tick_to_usec(now - power_button_timer);
                if (diff >= 500000) {
                    /* Power off can happen now */
                    power_state_desired = POWER_STATE_OFF;
                    power_button_armed = 0;
                    power_button_timer = 0;
                }
            }
            break;
        case POWER_STATE_OFF:
            if (power_state_desired == POWER_STATE_OFF) {
                /* Power on can happen immediately */
                power_state_desired = POWER_STATE_ON;
                power_button_armed = 0;
                power_button_timer = 0;
            }
            return;
        case POWER_STATE_FAULT:
        case POWER_STATE_FAULT_ON:
            /* Can only transition to power off */
            power_state_desired = POWER_STATE_OFF;
            power_button_armed = 0;
            power_button_timer = 0;
            return;
        case POWER_STATE_FAULT_OFF:
            /* Can only try to cycle power */
            power_state_desired = POWER_STATE_CYCLE;
            power_button_armed = 0;
            power_button_timer = 0;
            return;
    }
}

/*
 * power_poll
 * ----------
 * Manage power supply state machine and user power button
 */
void
power_poll(void)
{
    uint new_state;

    power_button_poll();

    if (power_state == power_state_desired)
        return;

    switch (power_state) {
        case POWER_STATE_INITIAL:
            break;  // Waiting for power supply to initialize
        case POWER_STATE_POWERING_ON:
            new_state = sensor_get_power_state();
            if (new_state == POWER_STATE_ON) {
                power_state = POWER_STATE_ON;
                printf("Power: on\n");
            } else if (timer_tick_has_elapsed(power_timer)) {
                /* Took longer than 2 seconds */
                printf("Power: Failed to power on\n");
                power_state = POWER_STATE_FAULT_ON;
                power_state_desired = POWER_STATE_FAULT_ON;
            }
            break;
        case POWER_STATE_POWERING_OFF:
            new_state = sensor_get_power_state();
            if (new_state == POWER_STATE_OFF) {
                power_state = POWER_STATE_OFF;
                printf("Power: off\n");
            } else if (timer_tick_has_elapsed(power_timer)) {
                /* Took longer than 2 seconds */
                printf("Power: Failed to power off\n");
                power_state = POWER_STATE_FAULT_OFF;
                power_state_desired = POWER_STATE_FAULT_OFF;
            }
            break;
        case POWER_STATE_CYCLE:
            /* Waiting for power off to initiate power on */
            if (timer_tick_has_elapsed(power_timer)) {
                gpio_setv(PSON_PORT, PSON_PIN, 0);
                power_state = POWER_STATE_POWERING_ON;
                power_state_desired = POWER_STATE_ON;
                power_timer = timer_tick_plus_msec(POWER_ON_STABLE);
            }
            break;
        case POWER_STATE_ON:
            if (power_state_desired == POWER_STATE_OFF) {
power_off:
                gpio_setv(PSON_PORT, PSON_PIN, 1);
                power_state = POWER_STATE_POWERING_OFF;
                power_timer = timer_tick_plus_msec(POWER_OFF_STABLE);
                printf("Power: powering off\n");
            } else if (power_state_desired == POWER_STATE_CYCLE) {
power_cycle:
                gpio_setv(PSON_PORT, PSON_PIN, 1);
                power_state = POWER_STATE_CYCLE;
                power_state_desired = POWER_STATE_ON;
                power_timer = timer_tick_plus_msec(POWER_CYCLE_OFF_PERIOD);
                printf("Power: cycling\n");
            }
            break;
        case POWER_STATE_OFF:
            if ((power_state_desired == POWER_STATE_ON) ||
                (power_state_desired == POWER_STATE_CYCLE)) {
                gpio_setv(PSON_PORT, PSON_PIN, 0);
                power_state = POWER_STATE_POWERING_ON;
                power_timer = timer_tick_plus_msec(POWER_OFF_STABLE);
                printf("Power: powering on\n");
            }
            break;
        case POWER_STATE_FAULT:
            if ((power_state_desired == POWER_STATE_CYCLE) ||
                (power_state_desired == POWER_STATE_ON))
                goto power_cycle;
            if (power_state_desired == POWER_STATE_OFF)
                goto power_off;
            break;
        case POWER_STATE_FAULT_ON:
            if ((power_state_desired == POWER_STATE_CYCLE) ||
                (power_state_desired == POWER_STATE_ON))
                goto power_cycle;
            if (power_state_desired == POWER_STATE_OFF)
                goto power_off;
            break;
        case POWER_STATE_FAULT_OFF:
            if ((power_state_desired == POWER_STATE_CYCLE) ||
                (power_state_desired == POWER_STATE_ON))
                goto power_cycle;
            break;
    }
}

/*
 * power_set
 * ---------
 * Set the desired state for the power supply
 */
void
power_set(uint state)
{
    power_state_desired = state;
}

/*
 * power_show
 * ----------
 * Display current power status
 */
void
power_show(void)
{
    const char *state;
    switch (power_state) {
        case POWER_STATE_INITIAL:
            state = "Initializing";
            break;
        case POWER_STATE_POWERING_ON:
            state = "Powering On";
            break;
        case POWER_STATE_POWERING_OFF:
            state = "Powering Off";
            break;
        case POWER_STATE_CYCLE:
            state = "Cycling Power";
            break;
        case POWER_STATE_ON:
            state = "On";
            break;
        case POWER_STATE_OFF:
            state = "Off";
            break;
        case POWER_STATE_FAULT:
            state = "Fault";
            break;
        case POWER_STATE_FAULT_ON:
            state = "Failed to power on";
            break;
        case POWER_STATE_FAULT_OFF:
            state = "Failed to power off";
            break;
        default:
            state = "Unknown";
            break;
    }
    printf("Power state: %s\n", state);
}

/*
 * power_init
 * ----------
 * Initialize power management functions
 */
void
power_init(void)
{
    power_state = POWER_STATE_OFF;  // Just check limits
    sensor_check_readings();
    power_state = POWER_STATE_INITIAL;
    power_state = sensor_get_power_state();
    power_timer = timer_tick_plus_msec(POWER_ON_STABLE);

    if (power_state == POWER_STATE_INITIAL) {
        printf("power_init() failed\n");
        return;  // Should not happen, as this function is gated by ADC startup
    }
    if (cold_poweron) {
        const char *state;
        const char *action;
        if (config.ps_on_mode & 1) {
            power_state_desired = POWER_STATE_ON;
            state = "on";
        } else {
            power_state_desired = POWER_STATE_OFF;
            state = "off";
        }
        if (power_state == power_state_desired)
            action = "Leave";
        else
            action = "Turn";
        printf("Cold poweron: %s power supply %s\n", action, state);
    } else {
        /* Warm restart */
#if 0
        const char *state;
        if (power_state == POWER_STATE_OFF) {
            state = "off";
        } else {
            state = "on";
        }
        printf("Warm restart: Power supply %s\n", state);
#endif
        power_state_desired = power_state;
    }
}
