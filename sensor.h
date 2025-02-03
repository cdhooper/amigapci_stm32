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

#ifndef _SENSOR_H
#define _SENSOR_H

void sensor_init(void);
void sensor_poll(void);
void sensor_show(void);
uint sensor_get_power_state(void);
void sensor_check_readings(void);

#endif /* _SENSOR_H */
