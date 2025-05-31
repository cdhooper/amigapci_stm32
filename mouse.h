/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Amiga and USB HID keyboard handling.
 */
#ifndef _MOUSE_H
#define _MOUSE_H

void mouse_action(int off_x, int off_y, int off_wheel, uint32_t buttons);
void mouse_poll(void);
extern uint32_t mouse_buttons_add;

#endif /* _MOUSE_H */
