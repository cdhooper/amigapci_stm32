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

#ifndef _POWER_H
#define _POWER_H

void power_init(void);
void power_poll(void);
void power_set(uint state);  // POWER_STATE_ ON, OFF, or CYCLE
void power_show(void);

/* Transitional states (will end in another state) */
#define POWER_STATE_INITIAL      0  // Power supply state is initiallizing
#define POWER_STATE_POWERING_ON  1  // Powering on
#define POWER_STATE_POWERING_OFF 2  // Powering off
#define POWER_STATE_CYCLE        3  // Cycling power

/* Resting power states */
#define POWER_STATE_ON           4  // Power supply is on
#define POWER_STATE_OFF          5  // Power supply is off
#define POWER_STATE_FAULT        6  // Power supply has faulted
#define POWER_STATE_FAULT_ON     7  // Power supply has failed to power on
#define POWER_STATE_FAULT_OFF    8  // Power supply has failed to power off

extern uint8_t power_state;
extern uint8_t power_state_desired;

#endif /* _POWER_H */
