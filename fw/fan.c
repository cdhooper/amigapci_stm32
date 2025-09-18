#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "main.h"
#include "clock.h"
#include "fan.h"
#include "cmdline.h"
#include "irq.h"
#include "printf.h"
#include "utils.h"
#include "sensor.h"
#include "config.h"
#include "power.h"
#include "timer.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

static uint32_t timer_clocks_per_rpm;

#define SECONDS_PER_MINUTE         60
#define MILLI_UNITS                1000

/* Number of times tach is pulsed per fan revolution */
#define FAN_PULSES_PER_REVOLUTION  2  // Typical PC fan: 2 pulses per revolution

#define NUM_TACH_BUCKETS 8
#define TACH_DIV         128

#define FAN_HYSTERESIS_PERCENT     5  // Minimum percent for auto fan change

uint fan_percent;
uint fan_percent_last;
uint fan_auto;
uint fan_percent_min;   // Minimum expected fan percent

uint64_t timer_fan_limit_change;

/*
 * Fan management
 * --------------
 * The fan speed may be measured by counting pulses per minute on
 * the FANTACH input to the CPU. The fan speed may be set by pulse
 * width modulating the FANPWM output from the CPU.
 *
 * PB8 is FANPWM  : TIM10_CH1 (AF3) or TIM4_CH3 (AF2)
 * PB9 is FANTACH : TIM4_CH4 (AF2)  or TIM11_CH1 (AF3)
 */


void
fan_set(uint speed)
{
    fan_auto = (speed & BIT(7)) ? 1 : 0;
    fan_percent = speed & ~BIT(7);

    if ((fan_percent != 0) && (fan_percent < config.fan_speed_min))
        fan_percent = config.fan_speed_min;

    if (fan_auto) {
        printf("set fan to auto\n");
    } else {
        printf("set fan to %u%%\n", fan_percent);
    }
}

volatile uint64_t last_update;  // Last time fan rotor generated a pulse
static volatile uint16_t tach_buckets[NUM_TACH_BUCKETS];

static void
fan_update_bucket(uint cycles)
{
    static uint8_t cur_bucket = 0;

    tach_buckets[(cur_bucket++) % NUM_TACH_BUCKETS] = cycles;
}

/*
 * fan_get_rpm() computes the rotational speed of the fan, returning
 * a value in revolutions per minute.
 */
uint
fan_get_rpm(void)
{
    uint rpm;
    uint buckets = 0;
    uint bucket;
    uint total;
    uint64_t last;
    uint     usec;

    disable_irq();
    last  = last_update;
    enable_irq();
    usec = timer_tick_to_usec(timer_tick_get() - last);
    if (usec > 1000000) {
        /* Fan is not spinning */
        memset((void *)tach_buckets, 0, sizeof (tach_buckets));
        return (0);
    }

    for (total = 0, bucket = 0; bucket < NUM_TACH_BUCKETS; bucket++)
        if (tach_buckets[bucket] > 0) {
            total += tach_buckets[bucket];
            buckets++;
        }

    if (total == 0)
        rpm = 0;
    else
        rpm = timer_clocks_per_rpm * buckets / total;
    return (rpm);
}

/*
 * fan_get_percent() returns the current desired fan speed
 */
uint
fan_get_percent(void)
{
    return (fan_percent);
}


/*
 * fan_get_limits() returns the current limits for the fan speed
 */
void
fan_get_limits(int *limit_min, int *limit_max)
{
#define FAN_MARGIN_MIN  10  // Allowed percent below minimum speed
#define FAN_MARGIN_MAX  10  // Allowed percent above maximum speed
    if ((config.flags & CF_HAVE_FAN) &&
        (power_state == POWER_STATE_ON) && (config.flags & CF_HAVE_FAN)) {
        *limit_min = fan_percent_min * config.fan_rpm_max * 10;
        if (*limit_min >= config.fan_rpm_max * FAN_MARGIN_MIN * 10) {
            *limit_min -= config.fan_rpm_max * FAN_MARGIN_MIN * 10;
        } else {
            *limit_min = 0;
        }
    } else {
        *limit_min = 0;
    }
    *limit_max = (config.fan_rpm_max ? config.fan_rpm_max : 4000) *
                 (1000 + FAN_MARGIN_MAX * 10);
}

/*
 * tim4_isr() is the interrupt service routing for the TIM4 timer.
 * This timer is used to measure the fan rotation rate.
 */
void
tim4_isr(void)
{
    uint32_t flags = TIM_SR(TIM4) & TIM_DIER(TIM4);
    uint value;
    bool            over_cap = FALSE;
    static uint16_t prev  = 0;

    timer_clear_flag(TIM4, flags);

    if (TIM_SR(TIM3) & TIM_OC4) {
        printf("o");
        over_cap = TRUE;
        timer_clear_flag(TIM4, TIM_OC4);
    }
    value = TIM_CCR4(TIM4);
    if (!over_cap) {
        uint64_t tick_now = timer_tick_get();
        uint16_t diff = value - prev;
        /* 62597 tach ticks is about 8012450 timer ticks */
        if ((tick_now - last_update) > 0xff00 * TACH_DIV) {
            printf("O");
            diff = 0;  // Pulse took too long
        }
        last_update = tick_now;
        fan_update_bucket(diff);
    }
    prev = value;
//  printf("[%u]", fan_get_rpm());
}

static void
fan_pwm_set(uint percent)
{
    timer_set_oc_value(TIM10, TIM_OC1, 100 - percent);
}

static void
fan_init_pwm(void)
{
    uint ppre_freq = clock_get_apb1();  // Is TIM10 in apb1?

    /* Enable and reset TIM10 */
    RCC_APB2ENR  |=  RCC_APB2ENR_TIM10EN;
    RCC_APB2RSTR |=  RCC_APB2RSTR_TIM10RST;
    RCC_APB2RSTR &= ~RCC_APB2RSTR_TIM10RST;

#if 0
    init_timer_settings(TIM10, rcc_ppre1_frequency, 100);
#endif
    timer_disable_oc_output(TIM10, TIM_OC1);

    timer_set_mode(TIM10,
                   TIM_CR1_CKD_CK_INT,
                   TIM_CR1_CMS_EDGE,
                   TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM10, ppre_freq * 2 / 1000000 - 1);
    timer_set_repetition_counter(TIM10, 0);
    timer_enable_preload(TIM10);
    timer_continuous_mode(TIM10);
    timer_set_period(TIM10, 100);

    timer_set_oc_mode(TIM10, TIM_OC1, TIM_OCM_PWM1);
    timer_set_oc_value(TIM10, TIM_OC1, 100);  // PWM off
    timer_enable_oc_output(TIM10, TIM_OC1);
    timer_generate_event(TIM10, TIM_EGR_UG);

    timer_enable_counter(TIM10);  // Start timer
}

static void
fan_init_tach(void)
{
    uint ppre_freq = clock_get_apb1();  // Is TIM4 in apb1?

    /* Enable and reset TIM4 */
    RCC_APB1ENR  |=  RCC_APB1ENR_TIM4EN;
    RCC_APB1RSTR |=  RCC_APB1RSTR_TIM4RST;
    RCC_APB1RSTR &= ~RCC_APB1RSTR_TIM4RST;

    /* Configure TIM4 timer */
    timer_disable_counter(TIM4);
    timer_continuous_mode(TIM4);

    timer_disable_oc_output(TIM4, TIM_OC4);

    /* Set for medium measurement clock rate */
    timer_set_prescaler(TIM4, TACH_DIV - 1);
    timer_clocks_per_rpm = ppre_freq  * 2 / TACH_DIV /
                           FAN_PULSES_PER_REVOLUTION * SECONDS_PER_MINUTE;

    /* Fan tach: Set the capture prescaler */
    TIM4_CCMR2 &= TIM_CCMR2_IC4PSC_MASK;
    TIM4_CCMR2 |= TIM_CCMR2_IC4PSC_OFF;

    /* Fan tach: Select the Input and set the filter */
    TIM4_CCMR2 &= ~(TIM_CCMR2_CC4S_MASK | TIM_CCMR2_IC4F_MASK);
    TIM4_CCMR2 |= TIM_CCMR2_CC4S_IN_TI4 | TIM_CCMR2_IC4F_OFF;

    /* Fan tach: Set Active low polarity */
    TIM4_CCER &= ~(TIM_CCER_CC4NP | TIM_CCER_CC4NP);
    TIM4_CCER |= TIM_CCER_CC4P | TIM_CCER_CC4P;

    /* Fan tach: Enable */
    timer_enable_oc_output(TIM4, TIM_OC4);
    timer_enable_counter(TIM4);  // Start timer

    /* Enable timer interrupt on fan tach pulse */
    nvic_set_priority(NVIC_TIM4_IRQ, 0x50);
    nvic_enable_irq(NVIC_TIM4_IRQ);
    timer_enable_irq(TIM4, TIM_DIER_CC4IE);

}

void
fan_init(void)
{
    fan_init_tach();   // Set up FANTACH input
    fan_init_pwm();    // Set up FANPWM output

    fan_auto = (config.fan_speed & BIT(7)) ? 1 : 0;
    fan_percent = config.fan_speed & ~BIT(7);
    if (fan_percent < config.fan_speed_min)
        fan_percent = config.fan_speed_min;
    timer_fan_limit_change = timer_tick_plus_msec(1000);
}

void
fan_poll(void)
{
    uint percent;
    if (fan_auto) {
        /* Adjust fan_percent based on temperature input */
        uint value;
        const char *type;
        if (sensor_get("TEMP", &value, &type) == 0) {
            uint temp_min = config.fan_temp_min * 100;
            uint temp_max = config.fan_temp_max * 100;
            int diff;
            /* Convert to integer temperature */
            value = (value + 500) / 1000;
            if (value <= temp_min) {
                percent = config.fan_speed_min;
            } else if (value >= temp_max) {
                percent = 100;
            } else if (temp_max <= temp_min) {
                percent = 100;  // invalid config
            } else {
                uint rpercent = (value - temp_min) * 100 /
                                (temp_max - temp_min);
                percent = config.fan_speed_min +
                          rpercent * (100 - config.fan_speed_min) / 100;
            }
// if (percent > 41)
//      printf("[%u]", percent);
            diff = fan_percent - percent;
            if (diff < 0)
                diff = -diff;
            if (diff >= FAN_HYSTERESIS_PERCENT)
                fan_percent = percent;
        }
    }
    if (fan_percent_last != fan_percent) {
        /* Need to adjust the PWM */
// printf("[%u] change %u %u", percent, fan_percent_last, fan_percent);
        fan_pwm_set(fan_percent);
        fan_percent_last = fan_percent;
        if (fan_percent_min > fan_percent)
            fan_percent_min = fan_percent;
        else
            timer_fan_limit_change = timer_tick_plus_msec(1000);
    }
    if (power_state == POWER_STATE_ON) {
        if (fan_percent_min != fan_percent) {
            if (timer_tick_has_elapsed(timer_fan_limit_change)) {
                int diff = fan_percent - fan_percent_min;
                if (diff > 20)  // Maximum 20% change per second
                    diff = 20;
                dprintf(DF_FAN, "Fan [%u->%u]", fan_percent_min, fan_percent);
                fan_percent_min += diff;
                if (fan_percent_min != fan_percent)
                    timer_fan_limit_change = timer_tick_plus_msec(1000);
            }
        }
    } else {
        fan_percent_min = 0;
    }
}
