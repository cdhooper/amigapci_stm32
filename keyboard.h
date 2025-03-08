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

/* These definitiions are for USB HID keyboards */
#define KEYBOARD_MODIFIER_LEFTCTRL   BIT(0)  // Left Control
#define KEYBOARD_MODIFIER_LEFTSHIFT  BIT(1)  // Left Shift
#define KEYBOARD_MODIFIER_LEFTALT    BIT(2)  // Left Alt
#define KEYBOARD_MODIFIER_LEFTMETA   BIT(3)  // Left Meta (Windows)
#define KEYBOARD_MODIFIER_RIGHTCTRL  BIT(4)  // Right Control
#define KEYBOARD_MODIFIER_RIGHTSHIFT BIT(5)  // Right Shift
#define KEYBOARD_MODIFIER_RIGHTALT   BIT(6)  // Right Alt
#define KEYBOARD_MODIFIER_RIGHTMETA  BIT(7)  // Right Meta

extern const uint8_t keycode2ascii[128][2];

typedef struct {
    uint8_t modifier;   // Keyboard modifier (KEYBOARD_MODIFIER_* masks)
    uint8_t reserved;   // Reserved for OEM use, always set to 0
    uint8_t keycode[6]; // Key codes of the currently pressed keys
} usb_keyboard_report_t;

void keyboard_usb_input(usb_keyboard_report_t *report);  // USB keyboard input
void keyboard_term(void);  // ASCII terminal input to Amiga
void keyboard_set_defaults(void);
uint keyboard_reset_warning(void);
void keyboard_poll(void);
void keyboard_init(void);

#endif /* _KEYBOARD_H */
