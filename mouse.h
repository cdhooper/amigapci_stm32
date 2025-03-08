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

void mouse_action(int off_x, int off_y, uint button0, uint button1,
                  uint button2);
void mouse_poll(void);

#endif /* _MOUSE_H */
