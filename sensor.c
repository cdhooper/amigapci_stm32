/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Functions to monitor sensors and report readings.
 */

#include <stdlib.h>
#include <string.h>
#include "cmdline.h"
#include "printf.h"
#include "main.h"
#include "adc.h"
#include "gpio.h"
#include "power.h"
#include "sensor.h"
#include "timer.h"

#define PA 1
#define PB 2
#define PC 3
#define PD 4
#define PE 5
#define PF 6
#define GPP(g, p) ((g << 4) | (p))

typedef struct {
    const char *s_name;
    uint8_t     s_adc_channel;  // ADC channel (0 - 18)
    uint8_t     s_gpio_packed;  // GPIO pin (0 = virtual)
    uint8_t     s_power_domain; // Power domain (0 = all)
    int16_t     s_mul;          // Scale reading multiplier (< 6500)
    int16_t     s_div;          // Scale reading divisor    (> 0)
    int         s_add;          // Add constant to reading after multiply
    int         s_limit_min;    // Minimum in milli-units
    int         s_limit_max;    // Maximum in milli-units
} sensor_t;

static const sensor_t sensors[] = {
    { "VrefInt", ADC_CHANNEL_VREF, 0x00, 0, 1, 1, 0, 1200, 1220 },
    { "TEMP",    ADC_CHANNEL_TEMP, 0x00, 0, 10000 / TEMP_AVGSLOPE, 1,
            -1 * TEMP_V25 * 10000 / TEMP_AVGSLOPE + 25 * 100000, 0, 60000 },
    { "VBAT",    ADC_CHANNEL_VBAT, 0x00, 0, 2, 1, 0, 2000, 5100 },
    { "V5SB",    1,  GPP(PA, 1), 0, 2, 1, 0,   4500,   5300 },
    { "V5",      0,  GPP(PA, 0), 1, 2, 1, 0,   4500,   5300 },
    { "V3P3",    2,  GPP(PA, 2), 1, 2, 1, 0,   3200,   3400 },
    { "V1P2",    3,  GPP(PA, 3), 1, 1, 1, 0,   1100,   1300 },
//  { "MONx",    6,  GPP(PA, 6), 0, 2, 1, 0,      0,   5000 },
//  { "MONy",    7,  GPP(PA, 7), 0, 1, 1, 0,      0,   5000 },
    { "V12",     14, GPP(PC, 4), 1, 61, 10, 0,  11400,  12600 },
    { "V-12",    15, GPP(PC, 5), 1, 61, 10, -1670000, -12600, -11400 },
};
#define SENSOR_COUNT ARRAY_SIZE(sensors)

#define SS_FLAG_UNDER_LIMIT        0x0001
#define SS_FLAG_WARNED_OVER_LIMIT  0x0002
#define SS_FLAG_OVER_LIMIT         0x0004
#define SS_FLAG_WARNED_UNDER_LIMIT 0x0008
#define SS_FLAG_IGNORED            0x0010
typedef struct {
    int  ss_reading;
    uint ss_flags;
} sensor_state_t;
sensor_state_t sensor_states[SENSOR_COUNT];

uint64_t adc_startup_time;

/*
 * print_limit
 * -----------
 * Display sensor limit
 */
static void
print_limit(int value, const char *suffix)
{
    int units;

    units = value / 1000;

    if (*suffix == 'C') {
        uint tenths = (abs(value) % 1000) / 100;
        printf("%3d.%u %-3s", units, tenths, suffix);
    } else if ((*suffix == 'R') || (*suffix == '%')) {
        units = value / 1000;
        printf("%5u %-3s", units, suffix);  // RPM or %
    } else {
        uint hundredths = (abs(value) % 1000) / 10;
        if ((units < -9) || (units > 9))
            printf("%3d.%u %-3s", units, hundredths / 10, suffix);
        else
            printf("%2d.%02u %-3s", units, hundredths, suffix);
    }
}

/*
 * print_reading
 * -------------
 * Display sensor reading, with appropriate rounding.
 */
static void
print_reading(int value, const char *suffix)
{
    uint units;
    char buf[16];

#undef DEBUG_READING_CONVERSION
#ifdef DEBUG_READING_CONVERSION
    printf("[%8d]", value);
#endif
    if (*suffix == 'C') {
        uint tenths;
        if (value >= 0)
            value += 5000;
        else
            value -= 5000;
        units = abs(value) / 100000;
        tenths = (abs(value) % 100000) / 10000;
        sprintf(buf, "%s%d.%u %-3s",
                (value < 0) ? "-" : "", units, tenths, suffix);
    } else if ((*suffix == 'R') || (*suffix == '%')) {
        value += 50000;
        units = value / 100000;
        sprintf(buf, "%u %-3s", units, suffix);  // RPM or %
    } else {
        uint hundredths;
        if (value >= 0)
            value += 500;
        else
            value -= 500;
        units = abs(value) / 100000;
        hundredths = (abs(value) % 100000) / 1000;
        sprintf(buf, "%s%d.%02u %-3s",
                (value < 0) ? "-" : "", units, hundredths, suffix);
    }
    printf("%10s", buf);
}

void
sensor_check_readings(void)
{
    uint cur;
    uint adc_which = 0;

    for (cur = 0; cur < ARRAY_SIZE(sensors); cur++) {
        int reading = adc_get_reading(adc_which++) * sensors[cur].s_mul /
                      sensors[cur].s_div + sensors[cur].s_add;
        sensor_states[cur].ss_reading = reading;

        /* Check limits */
        if (reading / 100 < sensors[cur].s_limit_min) {
            sensor_states[cur].ss_flags |= SS_FLAG_UNDER_LIMIT;
        } else if (reading / 100 > sensors[cur].s_limit_max) {
            sensor_states[cur].ss_flags |= SS_FLAG_OVER_LIMIT;
        } else {
            /* Sensor is in normal state */
            sensor_states[cur].ss_flags &= ~(SS_FLAG_OVER_LIMIT |
                                             SS_FLAG_UNDER_LIMIT |
                                             SS_FLAG_IGNORED);
        }

        /* Check if fault can be ignored */
        if ((sensors[cur].s_power_domain == 0) ||
            (power_state == POWER_STATE_ON) ||
            (power_state == POWER_STATE_FAULT) ||
            (power_state == POWER_STATE_FAULT_ON)) {
            /*
             * This sensor should not be ignored either because it's in
             * the always-on power domain (0), or the power supply is in
             * an "on" state.
             */
            sensor_states[cur].ss_flags &= ~SS_FLAG_IGNORED;
        } else if (sensor_states[cur].ss_flags & (SS_FLAG_UNDER_LIMIT |
                                                  SS_FLAG_OVER_LIMIT)) {
            /*
             * Sensor has a tripped limit, but we are ignoring it because
             * the power domain is not up.
             */
            sensor_states[cur].ss_flags |= SS_FLAG_IGNORED;
        }
    }
}

uint
sensor_get_power_state(void)
{
    uint cur;
    uint count_bad = 0;
    uint count_good = 0;

    if (adc_startup_time != 0)
        return (POWER_STATE_INITIAL);  // Waiting for ADCs to start up

    // XXX: Actually, need to check all secondary rails
    //      If any are down, then the power supply is in a fault state
    // (sensors[cur].s_power_domain == 1)

    /* Use the 12V and 5V rails to determine the power supply state */
    for (cur = 0; cur < ARRAY_SIZE(sensors); cur++) {
        if (sensors[cur].s_power_domain == 1) {
            if (sensor_states[cur].ss_flags &
                (SS_FLAG_UNDER_LIMIT | SS_FLAG_OVER_LIMIT))
                count_bad++;
            else
                count_good++;
        }
    }

#if 0
    // XXX: Simplify for now
    if (count_good != 0)
        return (POWER_STATE_ON);
    // XXX: Simplify for now
#endif

    if ((count_bad == 0) && (count_good > 0))
        return (POWER_STATE_ON);   // All on

    if ((count_good == 0) && (count_bad > 0))
        return (POWER_STATE_OFF);  // All off

    return (POWER_STATE_FAULT);     // Not all on
}

static void
sensor_show_state(uint pos, uint with_limits)
{
    uint is_temp = sensors[pos].s_adc_channel == ADC_CHANNEL_TEMP;
    int reading  = sensor_states[pos].ss_reading;
    const char *status;
    const char *suffix;

    if (sensor_states[pos].ss_flags & SS_FLAG_IGNORED)
        status = "Ignored";
    else if (sensor_states[pos].ss_flags & SS_FLAG_UNDER_LIMIT)
        status = "Under limit";
    else if (sensor_states[pos].ss_flags & SS_FLAG_OVER_LIMIT)
        status = "Over limit";
    else
        status = "Normal";

    suffix = is_temp ? "C" : "V";

    printf("%-15s", sensors[pos].s_name);
    print_reading(reading, suffix);
    printf(" %-12s", status);
    if (with_limits) {
        printf(" [ ");
        print_limit(sensors[pos].s_limit_min, suffix);
        printf(" - ");
        print_limit(sensors[pos].s_limit_max, suffix);
        printf(" ]");
    }
    printf("\n");
}

void
sensor_show(void)
{
    uint     cur;
    sensor_check_readings();
    printf("   Sensor       Reading      Status             Limits\n"
           "-------------- ---------- ------------ "
           "-------------------------\n");
    for (cur = 0; cur < ARRAY_SIZE(sensors); cur++) {
        sensor_show_state(cur, 1);
    }
}

void
sensor_poll(void)
{
    uint cur;

    /* Wait for ADCs to stabilize */
    if (adc_startup_time != 0) {
        if (!timer_tick_has_elapsed(adc_startup_time))
            return;
        adc_startup_time = 0;
        power_init();
    }

    sensor_check_readings();

    for (cur = 0; cur < ARRAY_SIZE(sensors); cur++) {
        uint report_state = 0;
        if (sensor_states[cur].ss_flags & SS_FLAG_IGNORED)
            continue;

        /* Check for over limit */
        if (sensor_states[cur].ss_flags & SS_FLAG_WARNED_UNDER_LIMIT) {
            if ((sensor_states[cur].ss_flags & SS_FLAG_UNDER_LIMIT) == 0) {
                if ((sensor_states[cur].ss_flags & SS_FLAG_OVER_LIMIT) == 0) {
                    report_state = 1;  // normal
                }
                sensor_states[cur].ss_flags &= ~SS_FLAG_WARNED_UNDER_LIMIT;
            }
        } else if (sensor_states[cur].ss_flags & SS_FLAG_UNDER_LIMIT) {
            sensor_states[cur].ss_flags |= SS_FLAG_WARNED_UNDER_LIMIT;
            report_state = 1;
        }

        /* Check for under limit */
        if (sensor_states[cur].ss_flags & SS_FLAG_WARNED_OVER_LIMIT) {
            if ((sensor_states[cur].ss_flags & SS_FLAG_OVER_LIMIT) == 0) {
                if ((sensor_states[cur].ss_flags & SS_FLAG_UNDER_LIMIT) == 0) {
                    report_state = 1;  // normal
                }
                sensor_states[cur].ss_flags &= ~SS_FLAG_WARNED_OVER_LIMIT;
            }
        } else if (sensor_states[cur].ss_flags & SS_FLAG_OVER_LIMIT) {
            sensor_states[cur].ss_flags |= SS_FLAG_WARNED_OVER_LIMIT;
            report_state = 1;
        }

        if (report_state) {
            sensor_show_state(cur, 0);
        }
    }
}

void
sensor_init(void)
{
    uint cur;
    uint adc_which = 0;

    /* Initialize all sensors */
    for (cur = 0; cur < ARRAY_SIZE(sensors); cur++) {
        // Eventually handle other sensors, such as Fan RPM & PWM
        adc_setup_sensor(adc_which++, sensors[cur].s_gpio_packed,
                         sensors[cur].s_adc_channel);
    }
    adc_init();
    adc_startup_time = timer_tick_plus_msec(1);
}
