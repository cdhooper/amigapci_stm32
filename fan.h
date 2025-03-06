/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Fan management
 */

#ifndef _FAN_H
#define _FAN_H

void fan_init(void);
void fan_set(uint speed);
uint fan_get_rpm(void);
uint fan_get_percent(void);
void fan_poll(void);
void fan_get_limits(int *limit_min, int *limit_max);

#endif /* _FAN_H */
