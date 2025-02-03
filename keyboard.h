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
#ifndef _KEYBOARD_H
#define _KEYBOARD_H

extern const uint8_t keycode2ascii[128][2];

typedef struct {
    uint8_t modifier;   // Keyboard modifier (KEYBOARD_MODIFIER_* masks)
    uint8_t reserved;   // Reserved for OEM use, always set to 0
    uint8_t keycode[6]; // Key codes of the currently pressed keys
} usb_keyboard_report_t;

/* Handle input from USB keyboard */
void usb_keyboard_input(usb_keyboard_report_t *report);

#endif /* _KEYBOARD_H */
