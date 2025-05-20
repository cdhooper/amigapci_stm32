/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * HIDEN (HID Enable) signal handling for control of Keyboard and Mouse
 */

#ifndef _HIDEN_H
#define _HIDEN_H

extern uint8_t hiden_is_set;

void hiden_set(unsigned int enable);

#endif /* _HIDEN_H */
