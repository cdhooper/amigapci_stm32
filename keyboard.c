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

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "printf.h"
#include "keyboard.h"
#include "utils.h"
#include "uart.h"
#include "usb.h"

#undef DEBUG_KEYBOARD
#ifdef DEBUG_KEYBOARD
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...) do { } while (0)
#endif

/* These definitiions are for USB HID keyboards */
#define KEYBOARD_MODIFIER_LEFTCTRL   BIT(0)  // Left Control
#define KEYBOARD_MODIFIER_LEFTSHIFT  BIT(1)  // Left Shift
#define KEYBOARD_MODIFIER_LEFTALT    BIT(2)  // Left Alt
#define KEYBOARD_MODIFIER_LEFTMETA   BIT(3)  // Left Meta (Windows)
#define KEYBOARD_MODIFIER_RIGHTCTRL  BIT(4)  // Right Control
#define KEYBOARD_MODIFIER_RIGHTSHIFT BIT(5)  // Right Shift
#define KEYBOARD_MODIFIER_RIGHTALT   BIT(6)  // Right Alt
#define KEYBOARD_MODIFIER_RIGHTMETA  BIT(7)  // Right Meta

/* HID Key to ASCII pseudo-conversion modifiers */

/*
 * HID Key to ASCII pseudo-conversion
 *
 * Most of the conversions below are to unshifted (lower byte) and shifted
 * (upper byte) ASCII values. This makes ASCII conversion very simple.
 *
 * Keys with 0x00 in the upper byte are ASCII, but unshiftable (keypad)
 * Keys with 0x01 in the upper byte are non-ASCII
 */
#define HK_NONE        (0x0000)
#define HK_ERR_OVF     (0x0101)
#define HK_POST_FAIL   (0x0102)
#define HK_KBD_ERROR   (0x0103)
#define HK_A           (0x4161)  // 'A' and 'a'
#define HK_B           (0x4262)  // 'B' and 'b'
#define HK_C           (0x4363)  // 'C' and 'c'
#define HK_D           (0x4464)  // 'D' and 'd'
#define HK_E           (0x4565)  // 'E' and 'e'
#define HK_F           (0x4666)  // 'F' and 'f'
#define HK_G           (0x4767)  // 'G' and 'g'
#define HK_H           (0x4868)  // 'H' and 'h'
#define HK_I           (0x4969)  // 'I' and 'i'
#define HK_J           (0x4a6a)  // 'J' and 'j'
#define HK_K           (0x4b6b)  // 'K' and 'k'
#define HK_L           (0x4c6c)  // 'L' and 'l'
#define HK_M           (0x4d6d)  // 'M' and 'm'
#define HK_N           (0x4e6e)  // 'N' and 'n'
#define HK_O           (0x4f6f)  // 'O' and 'o'
#define HK_P           (0x5070)  // 'P' and 'p'
#define HK_Q           (0x5171)  // 'Q' and 'q'
#define HK_R           (0x5272)  // 'R' and 'r'
#define HK_S           (0x5373)  // 'S' and 's'
#define HK_T           (0x5474)  // 'T' and 't'
#define HK_U           (0x5575)  // 'U' and 'u'
#define HK_V           (0x5676)  // 'V' and 'v'
#define HK_W           (0x5777)  // 'W' and 'w'
#define HK_X           (0x5878)  // 'X' and 'x'
#define HK_Y           (0x5979)  // 'Y' and 'y'
#define HK_Z           (0x5a7a)  // 'Z' and 'z'
#define HK_1           (0x2131)  // '!' and '1'
#define HK_2           (0x4032)  // '@' and '2'
#define HK_3           (0x2333)  // '#' and '3'
#define HK_4           (0x2434)  // '$' and '4'
#define HK_5           (0x2535)  // '%' and '5'
#define HK_6           (0x5e36)  // '^' and '6'
#define HK_7           (0x2637)  // '&' and '7'
#define HK_8           (0x2a38)  // '*' and '8'
#define HK_9           (0x2839)  // '(' and '9'
#define HK_0           (0x2930)  // ')' and '0'

#define HK_ENTER       (0x0d0d)  // Carriage Return
#define HK_ESC         (0x1b1b)  // ESC
#define HK_BACKSPACE   (0x0808)  // Backspace '\b'
#define HK_TAB         (0x0909)  // Tab '\t'
#define HK_SPACE       (0x2020)  // Space ' '
#define HK_MINUS       (0x5f2d)  // '_' and '-'
#define HK_EQUAL       (0x2b3d)  // '+' and '='
#define HK_LEFTBRACE   (0x7b5b)  // '{' and '['

#define HK_RIGHTBRACE  (0x7d5d)  // '}' and ']'
#define HK_BACKSLASH   (0x7c5c)  // '|' and '\'
#define HK_HASHTILDE   (0x0023)  // '~' and '#' (International)
#define HK_SEMICOLON   (0x3a3b)  // ':' and ';'
#define HK_APOSTROPHE  (0x2227)  // '"' and '''
#define HK_GRAVE       (0x7e60)  // '~' and '`'
#define HK_COMMA       (0x3c2c)  // '<' and ','
#define HK_DOT         (0x3e2e)  // '>' and '.'
#define HK_SLASH       (0x3f2f)  // '?' and '/'
#define HK_CAPSLOCK    (0x0104)  // Caps Lock
#define HK_F1          (0x0121)  // F1
#define HK_F2          (0x0122)  // F2
#define HK_F3          (0x0123)  // F3
#define HK_F4          (0x0124)  // F4
#define HK_F5          (0x0125)  // F5
#define HK_F6          (0x0126)  // F6
#define HK_F7          (0x0127)  // F7
#define HK_F8          (0x0128)  // F8
#define HK_F9          (0x0129)  // F9
#define HK_F10         (0x012a)  // F10
#define HK_F11         (0x012b)  // F11
#define HK_F12         (0x012c)  // F12
#define HK_SYSRQ       (0x0105)  // Sys Request
#define HK_SCROLLLOCK  (0x0106)  // Scroll Lock
#define HK_PAUSE       (0x0107)  // Pause
#define HK_INSERT      (0x0108)  // Insert
#define HK_HOME        (0x0109)  // Home
#define HK_PAGEUP      (0x010a)  // Page Up
#define HK_DELETE      (0x010b)  // Delete
#define HK_END         (0x010c)  // End
#define HK_PAGEDOWN    (0x010d)  // Page Down
#define HK_RIGHT       (0x010e)  // Cursor Right
#define HK_LEFT        (0x010f)  // Cursor Left
#define HK_DOWN        (0x0110)  // Cursor Down
#define HK_UP          (0x0111)  // Cursor Up
#define HK_NUMLOCK     (0x0112)  // Num Lock

#define HK_KPSLASH     (0x002f)  // Keypad '/'
#define HK_KPASTERISK  (0x002a)  // Keypad '*'
#define HK_KPMINUS     (0x002d)  // Keypad '-'
#define HK_KPPLUS      (0x002b)  // Keypad '+'
#define HK_KPENTER     (0x000d)  // Keypad Enter
#define HK_KP1         (0x0031)  // Keypad '1'
#define HK_KP2         (0x0032)  // Keypad '2'
#define HK_KP3         (0x0033)  // Keypad '3'
#define HK_KP4         (0x0034)  // Keypad '4'
#define HK_KP5         (0x0035)  // Keypad '5'
#define HK_KP6         (0x0036)  // Keypad '6'
#define HK_KP7         (0x0037)  // Keypad '7'
#define HK_KP8         (0x0038)  // Keypad '8'
#define HK_KP9         (0x0039)  // Keypad '9'
#define HK_KP0         (0x0030)  // Keypad '0'
#define HK_KPDOT       (0x002e)  // Keypad '.'
#define HK_BACKSLASH2  (0x005c)  // '|' and '\' (International)
#define HK_COMPOSE     (0x0112)  // Compose key
#define HK_POWER       (0x0113)  // Power key
#define HK_KPEQUAL     (0x003d)  // Keypad '='

#define HK_F13         (0x012d)  // F13
#define HK_F14         (0x012e)  // F14
#define HK_F15         (0x012f)  // F15
#define HK_F16         (0x0130)  // F16
#define HK_F17         (0x0131)  // F17
#define HK_F18         (0x0132)  // F18
#define HK_F19         (0x0133)  // F19
#define HK_F20         (0x0134)  // F20
#define HK_F21         (0x0135)  // F21
#define HK_F22         (0x0136)  // F22
#define HK_F23         (0x0137)  // F23
#define HK_F24         (0x0138)  // F24

#define HK_OPEN        (0x0114)  // Open / Execute
#define HK_HELP        (0x0115)  // Help
#define HK_PROPS       (0x0116)  // Props / menu
#define HK_FRONT       (0x0117)  // Front
#define HK_STOP        (0x0118)  // Stop
#define HK_AGAIN       (0x0119)  // Again
#define HK_UNDO        (0x011a)  // Undo
#define HK_CUT         (0x011b)  // Cut
#define HK_COPY        (0x011c)  // Copy
#define HK_PASTE       (0x011d)  // Paste
#define HK_FIND        (0x011e)  // Find
#define HK_MUTE        (0x011f)  // Mute
#define HK_VOLUME_UP   (0x0139)  // Volume Up
#define HK_VOLUME_DOWN (0x013a)  // Volume Down
#define HK_KPCOMMA     (0x002c)  // Keypad ','
#define HK_SYSREQ      (0x013b)  // SysReq/Attention
#define HK_KP_LPAREN   (0x0028)  // Keypad '('
#define HK_KP_RPAREN   (0x0029)  // Keypad ')'
#define HK_KP_LCBRACE  (0x007b)  // Keypad '{'
#define HK_KP_RCBRACE  (0x007c)  // Keypad '}'
#define HK_KPTAB       (0x0009)  // Keypad Tab
#define HK_KPBACKSPACE (0x0008)  // Keypad Backspace
#define HK_CARAT       (0x0008)  // Keypad Backspace
#define HK_LEFTCTRL    (0x013c)  // Left Control
#define HK_LEFTSHIFT   (0x013d)  // Left Shift
#define HK_LEFTALT     (0x013e)  // Left Alt
#define HK_LEFTMETA    (0x013f)  // Left Meta (Windows)
#define HK_RIGHTCTRL   (0x0140)  // Left Control
#define HK_RIGHTSHIFT  (0x0141)  // Left Shift
#define HK_RIGHTALT    (0x0142)  // Left Alt
#define HK_RIGHTMETA   (0x0143)  // Left Meta (Windows)

/* Scancodes to ASCII per USB spec 1.11 */
const uint16_t scancode_to_ascii_ext[] =
{
    HK_NONE, HK_ERR_OVF, HK_POST_FAIL, HK_KBD_ERROR, HK_A, HK_B, HK_C, HK_D,
    HK_E, HK_F, HK_G, HK_H, HK_I, HK_J, HK_K, HK_L,
    HK_M, HK_N, HK_O, HK_P, HK_Q, HK_R, HK_S, HK_T,
    HK_U, HK_V, HK_W, HK_X, HK_Y, HK_Z, HK_1, HK_2,
    HK_3, HK_4, HK_5, HK_6, HK_7, HK_8, HK_9, HK_0,
    HK_ENTER, HK_ESC, HK_BACKSPACE, HK_TAB,
                    HK_SPACE, HK_MINUS, HK_EQUAL, HK_LEFTBRACE,
    HK_RIGHTBRACE, HK_BACKSLASH, HK_HASHTILDE, HK_SEMICOLON,
                    HK_APOSTROPHE, HK_GRAVE, HK_COMMA, HK_DOT,
    HK_SLASH, HK_CAPSLOCK, HK_F1, HK_F2,
                         HK_F3, HK_F4, HK_F5, HK_F6,
    HK_F7, HK_F8, HK_F9, HK_F10,
                         HK_F11, HK_F12, HK_SYSRQ, HK_SCROLLLOCK,

    HK_PAUSE, HK_INSERT, HK_HOME, HK_PAGEUP,
                         HK_DELETE, HK_END, HK_PAGEDOWN, HK_RIGHT,
    HK_LEFT, HK_DOWN, HK_UP, HK_NUMLOCK,
                         HK_KPSLASH, HK_KPASTERISK, HK_KPMINUS, HK_KPPLUS,
    HK_KPENTER, HK_KP1, HK_KP2, HK_KP3,
                         HK_KP4, HK_KP5, HK_KP6, HK_KP7,
    HK_KP8, HK_KP9, HK_KP0, HK_KPDOT,
                         HK_BACKSLASH2, HK_COMPOSE, HK_POWER, HK_KPEQUAL,
    HK_F13, HK_F14, HK_F15, HK_F16,
                         HK_F17, HK_F18, HK_F19, HK_F20,
    HK_F21, HK_F22, HK_F23, HK_F24,
                         HK_OPEN, HK_HELP, HK_PROPS, HK_FRONT,
    HK_STOP, HK_AGAIN, HK_UNDO, HK_CUT,
                         HK_COPY, HK_PASTE, HK_FIND, HK_MUTE,
    HK_VOLUME_UP, HK_VOLUME_DOWN, 0, 0,  // Locking Caps, Locking Num Lock
                        0, HK_KPCOMMA, HK_KPEQUAL, 0, // Locking Scroll, RO
    0, 0, 0, 0,  // KATAKANAHIRAGANA, YEN, HENKAN, MUHENKAN
                        0, 0, 0, 0,  // KPJPCOMMA, INTL7, INTL8, INTL9
    0, 0, 0, 0,  // HANGEUL, HANJA, KATAKANA, HIRAGANA
                        0, 0, 0, 0,  // ZENKAKUHANKAKU, LANG6, LANG7, LANG8
    0, 0, HK_SYSREQ, 0,  // LANG9, Alt Erase, SysReq/Attn, Clear
                        0, 0, 0, 0,  // Prior, Return, Separator, Quit
    0, 0, 0, 0,  // Out, Oper, Clear/Again, CrSel/Props
                        0, 0, 0, 0,  // Exsel, -, -, -
    0, 0, 0, 0,  // -, -, -, -
                        0, 0, 0, 0,  // -, -, -, -
    0, 0, 0, 0,  // Keypad 00, Keypad 000, Thousands sep, Decimal separator
                        0, 0, HK_KP_LPAREN, HK_KP_RPAREN,  // Currency Unit, Sub
    HK_KP_LCBRACE, HK_KP_RCBRACE, HK_KPTAB, HK_KPBACKSPACE,
                        'A', 'B', 'C', 'D',  // Keypad A, B, C, D
    'E', 'F', 0, '^',  // Keypad E, F, XOR, Carat
                        '%', '<', '>', '&',  // Keypad %, <, >, &
    0, '|', 0, ':',  // Keypad &&, |, ||, :
                        '#', ' ', '@', '!',  // Keypad #, Space, @, !
    0, 0, 0, 0,  // Keypad Memory Store, Recall, Clear, +
                        0, 0, 0, 0,  // Keypad Memory -, *, /, +/-
    0, 0, 0, 0,  // Keypad Clear, Clear Entry, Binary, Octal
                        0, 0, 0, 0,  // Decimal, Hexadecimal, -, -
    HK_LEFTCTRL, HK_LEFTSHIFT, HK_LEFTALT, HK_LEFTMETA,
                        HK_RIGHTCTRL, HK_RIGHTSHIFT, HK_RIGHTALT, HK_RIGHTMETA,
    0, 0, 0, 0,  // Media Play/Pause, Stop CD, Prev Song, Next Song
                        0, 0, 0, 0,  // Media Eject CD, Vol Up, Vol Down, Mute
    0, 0, 0, 0,  // Media WWW, Back, Forward, Stop
                        0, 0, 0, 0,  // Media Find, Scroll Up, Scroll Down, Edit
    0, 0, 0, 0,  // Media Sleep, Coffee, Refresh, Calc
                        0, 0, 0, 0,  // Unused - - -
};
CC_ASSERT_ARRAY_SIZE(scancode_to_ascii_ext, 256);

static inline bool
find_key_in_report(usb_keyboard_report_t *report, uint8_t keycode)
{
    uint i;
    for (i = 0; i < 6; i++)
        if (report->keycode[i] == keycode) {
            report->keycode[i] = 0;  // Remove as it was seen again
            return (true);
        }

    return (false);
}

static void
keyboard_terminal_put(uint ascii, uint conv)
{
    switch (conv) {
        case HK_UP:
            usb_rb_put(0x1b); // ESC
            usb_rb_put('[');
            usb_rb_put('A');
            break;
        case HK_DOWN:
            usb_rb_put(0x1b); // ESC
            usb_rb_put('[');
            usb_rb_put('B');
            break;
        case HK_RIGHT:
            usb_rb_put(0x1b); // ESC
            usb_rb_put('[');
            usb_rb_put('C');
            break;
        case HK_LEFT:
            usb_rb_put(0x1b); // ESC
            usb_rb_put('[');
            usb_rb_put('D');
            break;
        default:
            if (ascii != 0)
                usb_rb_put(ascii);
            break;
    }
}

/* Handle input from USB keyboard */
void
usb_keyboard_input(usb_keyboard_report_t *report)
{
    static usb_keyboard_report_t prev_report;
    uint modifier = report->modifier;
    uint cur;
    uint ascii;
    uint16_t conv;

    for (cur = 0; cur < ARRAY_SIZE(report->keycode); cur++) {
        uint8_t keycode = report->keycode[cur];
        if (report->keycode[cur]) {
            if (find_key_in_report(&prev_report, report->keycode[cur])) {
                /* Current key is being held */
            } else {
                /* New keypress */
                conv = scancode_to_ascii_ext[keycode];
                if ((conv >> 8) == 0) {
                    /* Simple ASCII (shift and control are not applied) */
                    ascii = conv;
                    DPRINTF("KeyDown %c\n", ascii);
                } else if ((conv >> 8) <  0x07) {
                    /* Non-ASCII */
                    DPRINTF("KeyDown Non-ASCII %02x\n", conv & 0xff);
                    ascii = 0;
                } else {
                    if (modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                                         KEYBOARD_MODIFIER_RIGHTSHIFT)) {
                        ascii = conv >> 8;
                    } else {
                        ascii = conv & 0xff;
                    }
                    if (modifier & (KEYBOARD_MODIFIER_LEFTCTRL |
                                    KEYBOARD_MODIFIER_RIGHTCTRL)) {
                        if ((ascii >= '@') && (ascii <= 'Z'))
                            ascii -= '@';
                        else if ((ascii >= '`') && (ascii <= 'z'))
                            ascii -= '`';
                    }
                    if ((ascii >= ' ') && (ascii < 0x7f))
                        DPRINTF("KeyDown %c\n", ascii);
                    else
                        DPRINTF("KeyDown %02x\n", ascii);
                }
                if (usb_keyboard_terminal)
                    keyboard_terminal_put(ascii, conv);
            }
        }
    }

    /*
     * XXX: Any keys which were not found above are left marked in
     *      the prev_report array. For the Amiga, these should be
     *      treated as key up events.
     */
    for (cur = 0; cur < ARRAY_SIZE(prev_report.keycode); cur++) {
        uint8_t keycode = prev_report.keycode[cur];
        if (keycode != 0) {
            conv = scancode_to_ascii_ext[keycode];
            if ((conv >> 8) == 0) {
                /* Simple ASCII (shift is not applied) */
                ascii = conv;
            } else if ((conv >> 8) <  0x07) {
                /* Non-ASCII */
                DPRINTF("KeyUp special %02x\n", conv & 0xff);
            } else {
                if (modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                                KEYBOARD_MODIFIER_RIGHTSHIFT))
                    ascii = conv >> 8;
                else
                    ascii = conv & 0xff;
                if ((ascii >= ' ') && (ascii < 0x7f))
                    DPRINTF("KeyUp %c\n", ascii);
                else
                    DPRINTF("KeyUp %02x\n", ascii);
            }
        }
    }
    prev_report = *report;
}
