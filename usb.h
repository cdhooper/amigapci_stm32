/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * USB handling.
 */

#ifndef _USB_H
#define _USB_H

#include <libopencm3/usb/usbd.h>

#define USB0_BASE USB_OTG_FS_BASE
#define USB1_BASE USB_OTG_HS_BASE

#define USB_SET_POWER_ON  1
#define USB_SET_POWER_OFF 0

void usb_init(void);
void usb_poll(void);
void usb_ls(uint verbose);
void usb_show_regs(void);
void usb_show_stats(void);
void usb_set_power(int state);
void usb_shutdown(void);

extern uint usb_debug_mask;
extern uint usb_keyboard_terminal;

#endif /* _USB_H */
