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
#include "config.h"
#include "printf.h"
#include "keyboard.h"
#include "kbrst.h"
#include "power.h"
#include "timer.h"
#include "utils.h"
#include "uart.h"
#include "usb.h"
#include "gpio.h"
#include "mouse.h"
#include <libopencm3/stm32/gpio.h>

#undef DEBUG_KEYBOARD
#ifdef DEBUG_KEYBOARD
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...) do { } while (0)
#endif

/* Keyboard-to-Amiga ring buffer */
static uint    ak_rb_producer;
static uint    ak_rb_consumer;
static uint8_t ak_rb[16];
static volatile uint8_t ak_ctrl_amiga_amiga;

uint8_t amiga_keyboard_sent_wake;
uint8_t amiga_keyboard_has_sync;
uint8_t amiga_keyboard_lost_sync;

/* Amiga scancodes */
                               // Shifted Unshifted
#define AS_BACKTICK    (0x00)  // '~' and '`'
#define AS_1           (0x01)  // '!' and '1'
#define AS_2           (0x02)  // '@' and '2'
#define AS_3           (0x03)  // '#' and '3'
#define AS_4           (0x04)  // '$' and '4'
#define AS_5           (0x05)  // '%' and '5'
#define AS_6           (0x06)  // '^' and '6'
#define AS_7           (0x07)  // '&' and '7'
#define AS_8           (0x08)  // '*' and '8'
#define AS_9           (0x09)  // '(' and '9'
#define AS_0           (0x0a)  // ')' and '0'
#define AS_MINUS       (0x0b)  // '_' and '-'
#define AS_EQUAL       (0x0c)  // '+' and '='
#define AS_BACKSLASH   (0x0d)  // '|' and '\'
#define AS_KP0         (0x0f)  // Keypad '0'
#define AS_Q           (0x10)  // 'Q' and 'q'
#define AS_W           (0x11)  // 'W' and 'w'
#define AS_E           (0x12)  // 'E' and 'e'
#define AS_R           (0x13)  // 'R' and 'r'
#define AS_T           (0x14)  // 'T' and 't'
#define AS_Y           (0x15)  // 'Y' and 'y'
#define AS_U           (0x16)  // 'U' and 'u'
#define AS_I           (0x17)  // 'I' and 'i'
#define AS_O           (0x18)  // 'O' and 'o'
#define AS_P           (0x19)  // 'P' and 'p'
#define AS_LBRACKET    (0x1a)  // '{' and '['
#define AS_RBRACKET    (0x1b)  // '}' and ']'
#define AS_KP1         (0x1d)  // Keypad '1'
#define AS_KP2         (0x1e)  // Keypad '2'
#define AS_KP3         (0x1f)  // Keypad '3'
#define AS_A           (0x20)  // 'A' and 'a'
#define AS_S           (0x21)  // 'S' and 's'
#define AS_D           (0x22)  // 'D' and 'd'
#define AS_F           (0x23)  // 'F' and 'f'
#define AS_G           (0x24)  // 'G' and 'g'
#define AS_H           (0x25)  // 'H' and 'h'
#define AS_J           (0x26)  // 'J' and 'j'
#define AS_K           (0x27)  // 'K' and 'k'
#define AS_L           (0x28)  // 'L' and 'l'
#define AS_SEMICOLON   (0x29)  // ';' and ':'
#define AS_APOSTROPHE  (0x2a)  // ''' and '"'
#define AS_UNLABELED1  (0x2b)  // Key next to Return
#define AS_KP4         (0x2d)  // Keypad '4'
#define AS_KP5         (0x2e)  // Keypad '5'
#define AS_KP6         (0x2f)  // Keypad '6'
#define AS_UNLABELED2  (0x30)  // Key next to Left Shift
#define AS_Z           (0x31)  // 'Z' and 'z'
#define AS_X           (0x32)  // 'Z' and 'z'
#define AS_C           (0x33)  // 'Z' and 'z'
#define AS_V           (0x34)  // 'Z' and 'z'
#define AS_B           (0x35)  // 'Z' and 'z'
#define AS_N           (0x36)  // 'Z' and 'z'
#define AS_M           (0x37)  // 'Z' and 'z'
#define AS_COMMA       (0x38)  // '<' and ','
#define AS_DOT         (0x39)  // '>' and '.'
#define AS_SLASH       (0x3a)  // '>' and '/'
#define AS_KPDOT       (0x3c)  // Keypad '.'
#define AS_KP7         (0x3d)  // Keypad '7'
#define AS_KP8         (0x3e)  // Keypad '8'
#define AS_KP9         (0x3f)  // Keypad '9'
#define AS_SPACE       (0x40)  // Space
#define AS_BACKSPACE   (0x41)  // Backspace
#define AS_TAB         (0x42)  // Tab
#define AS_KPENTER     (0x43)  // Keypad Enter
#define AS_RETURN      (0x44)  // Carriage Return
#define AS_ESC         (0x45)  // ESC
#define AS_DELETE      (0x46)  // Delete
#define AS_KPMINUS     (0x4a)  // Keypad '-'
#define AS_UP          (0x4c)  // Cursor Up
#define AS_DOWN        (0x4d)  // Cursor Down
#define AS_RIGHT       (0x4e)  // Cursor Right
#define AS_LEFT        (0x4f)  // Cursor Left
#define AS_F1          (0x50)  // F1
#define AS_F2          (0x51)  // F2
#define AS_F3          (0x52)  // F3
#define AS_F4          (0x53)  // F4
#define AS_F5          (0x54)  // F5
#define AS_F6          (0x55)  // F6
#define AS_F7          (0x56)  // F7
#define AS_F8          (0x57)  // F8
#define AS_F9          (0x58)  // F9
#define AS_F10         (0x59)  // F10
#define AS_KP_LPAREN   (0x5a)  // Keypad '('
#define AS_KP_RPAREN   (0x5b)  // Keypad ')'
#define AS_KPSLASH     (0x5c)  // Keypad '/'
#define AS_KPASTERISK  (0x5d)  // Keypad '*'
#define AS_KPPLUS      (0x63)  // Keypad '+'
#define AS_HELP        (0x5f)  // Help
#define AS_LEFTSHIFT   (0x60)  // Left Shift
#define AS_RIGHTSHIFT  (0x61)  // Right Shift
#define AS_CAPSLOCK    (0x62)  // Caps Lock
#define AS_CTRL        (0x63)  // Ctrl
#define AS_LEFTALT     (0x64)  // Left Alt
#define AS_RIGHTALT    (0x65)  // Right Alt
#define AS_LEFTAMIGA   (0x66)  // Left Amiga
#define AS_RIGHTAMIGA  (0x67)  // Right Amiga
#define AS_RESET_WARN  (0x78)  // Reset warning
#define AS_LOST_SYNC   (0xf9)  // Keyboard lost sync with Amiga
#define AS_BUFOVERFLOW (0xfa)  // Keyboard output buffer overflow
#define AS_POST_FAIL   (0xfc)  // Keyboard selftest failed
#define AS_POWER_INIT  (0xfd)  // Keyboard powerup start key stream
#define AS_POWER_DONE  (0xfe)  // Keyboard powerup done key stream
#define AS_NONE        (0xff)  // Not a valid keycode


#define SAF_ADD_SHIFT 0x01
static const struct {
    uint8_t sa_amiga;    // Amiga scancode
    uint8_t sa_shifted;  // Only if different from native key
    uint8_t sa_flags;    // special flags
} scancode_to_amiga[] = {
                            //       Shifted Unshifted
    { AS_NONE,       0, 0x00 },  // 0x00 No key pressed
    { AS_NONE,       0, 0x00 },  // 0x01 Keyboard Error Roll Over
    { AS_POST_FAIL,  0, 0x00 },  // 0x02 Keyboard POST Fail
    { AS_NONE,       0, 0x00 },  // 0x03 Keyboard Error
    { AS_A,          0, 0x00 },  // 0x04  'A' and 'a'
    { AS_B,          0, 0x00 },  // 0x05  'B' and 'b'
    { AS_C,          0, 0x00 },  // 0x06  'C' and 'c'
    { AS_D,          0, 0x00 },  // 0x07  'D' and 'd'
    { AS_E,          0, 0x00 },  // 0x08  'E' and 'e'
    { AS_F,          0, 0x00 },  // 0x09  'F' and 'f'
    { AS_G,          0, 0x00 },  // 0x0a  'G' and 'g'
    { AS_H,          0, 0x00 },  // 0x0b  'H' and 'h'
    { AS_I,          0, 0x00 },  // 0x0c  'I' and 'i'
    { AS_J,          0, 0x00 },  // 0x0d  'J' and 'j'
    { AS_K,          0, 0x00 },  // 0x0e  'K' and 'k'
    { AS_L,          0, 0x00 },  // 0x0f  'L' and 'l'
    { AS_M,          0, 0x00 },  // 0x10  'M' and 'm'
    { AS_N,          0, 0x00 },  // 0x11  'N' and 'n'
    { AS_O,          0, 0x00 },  // 0x12  'O' and 'o'
    { AS_P,          0, 0x00 },  // 0x13  'P' and 'p'
    { AS_Q,          0, 0x00 },  // 0x14  'Q' and 'q'
    { AS_R,          0, 0x00 },  // 0x15  'R' and 'r'
    { AS_S,          0, 0x00 },  // 0x16  'S' and 's'
    { AS_T,          0, 0x00 },  // 0x17  'T' and 't'
    { AS_U,          0, 0x00 },  // 0x18  'U' and 'u'
    { AS_Y,          0, 0x00 },  // 0x19  'Y' and 'y'
    { AS_W,          0, 0x00 },  // 0x1a  'W' and 'w'
    { AS_X,          0, 0x00 },  // 0x1b  'X' and 'x'
    { AS_Y,          0, 0x00 },  // 0x1c  'Y' and 'y'
    { AS_Z,          0, 0x00 },  // 0x1d  'Z' and 'z'
    { AS_1,          0, 0x00 },  // 0x1e  '!' and '1'
    { AS_2,          0, 0x00 },  // 0x1f  '@' and '2'
    { AS_3,          0, 0x00 },  // 0x20  '#' and '3'
    { AS_4,          0, 0x00 },  // 0x21  '$' and '4'
    { AS_5,          0, 0x00 },  // 0x22  '%' and '5'
    { AS_6,          0, 0x00 },  // 0x23  '^' and '6'
    { AS_7,          0, 0x00 },  // 0x24  '&' and '7'
    { AS_8,          0, 0x00 },  // 0x25  '*' and '8'
    { AS_9,          0, 0x00 },  // 0x26  '(' and '9'
    { AS_0,          0, 0x00 },  // 0x27  ')' and '0'
    { AS_RETURN,     0, 0x00 },  // 0x28  Enter / Return
    { AS_ESC,        0, 0x00 },  // 0x29  ESC
    { AS_BACKSPACE,  0, 0x00 },  // 0x2a  Backspace
    { AS_TAB,        0, 0x00 },  // 0x2b  Tab
    { AS_SPACE,      0, 0x00 },  // 0x2c  Space
    { AS_MINUS,      0, 0x00 },  // 0x2d  '_' and '-'
    { AS_EQUAL,      0, 0x00 },  // 0x2e  '+' and '='
    { AS_LBRACKET,   0, 0x00 },  // 0x2f  '{' and '['
    { AS_RBRACKET,   0, 0x00 },  // 0x30  '}' and ']'
    { AS_BACKSLASH,  0, 0x00 },  // 0x31  '|' and '\'
    { AS_3, AS_BACKTICK, 0x01 }, // 0x32  '~' and '#' (Non-US)
    { AS_SEMICOLON,  0, 0x00 },  // 0x33  ':' and ';'
    { AS_APOSTROPHE, 0, 0x00 },  // 0x34  '"' and '''
    { AS_BACKTICK,   0, 0x00 },  // 0x35  '~' and '`'
    { AS_COMMA,      0, 0x00 },  // 0x36  '<' and ','
    { AS_DOT,        0, 0x00 },  // 0x37  '>' and '.'
    { AS_SLASH,      0, 0x00 },  // 0x38  '?' and '/'
    { AS_CAPSLOCK,   0, 0x00 },  // 0x39  Capslock
    { AS_F1,         0, 0x00 },  // 0x3a  F1
    { AS_F2,         0, 0x00 },  // 0x3b  F2
    { AS_F3,         0, 0x00 },  // 0x3c  F3
    { AS_F4,         0, 0x00 },  // 0x3d  F4
    { AS_F5,         0, 0x00 },  // 0x3e  F5
    { AS_F6,         0, 0x00 },  // 0x3f  F6
    { AS_F7,         0, 0x00 },  // 0x40  F7
    { AS_F8,         0, 0x00 },  // 0x41  F8
    { AS_F9,         0, 0x00 },  // 0x42  F9
    { AS_F10,        0, 0x00 },  // 0x43  F10
    { AS_NONE,       0, 0x00 },  // 0x44  F11
    { AS_NONE,       0, 0x00 },  // 0x45  F12
    { AS_NONE,       0, 0x00 },  // 0x46  SysRq
    { AS_NONE,       0, 0x00 },  // 0x47  Scroll Lock
    { AS_NONE,       0, 0x00 },  // 0x48  Pause
    { AS_BACKSLASH,  0, 0x00 },  // 0x49  Insert
    { AS_KP_LPAREN,  0, 0x00 },  // 0x4a  Home      (FS-UAE mapping)
    { AS_KP_RPAREN,  0, 0x00 },  // 0x4b  Page Up   (FS-UAE mapping)
    { AS_DELETE,     0, 0x00 },  // 0x4c  Delete
    { AS_HELP,       0, 0x00 },  // 0x4d  End       (FS-UAE mapping)
    { AS_RIGHTAMIGA, 0, 0x00 },  // 0x4e  Page Down (FS-UAE mapping)
    { AS_RIGHT,      0, 0x00 },  // 0x4f  Cursor Right
    { AS_LEFT,       0, 0x00 },  // 0x50  Cursor Left
    { AS_DOWN,       0, 0x00 },  // 0x51  Cursor Down
    { AS_UP,         0, 0x00 },  // 0x52  Cursor Up
    { AS_NONE,       0, 0x00 },  // 0x53  Numlock
    { AS_KPSLASH,    0, 0x00 },  // 0x54  Keypad '/'
    { AS_KPASTERISK, 0, 0x00 },  // 0x55  Keypad '*'
    { AS_KPMINUS,    0, 0x00 },  // 0x56  Keypad '-'
    { AS_KPPLUS,     0, 0x00 },  // 0x57  Keypad '+'
    { AS_KPENTER,    0, 0x00 },  // 0x58  Keypad Enter
    { AS_KP1,        0, 0x00 },  // 0x59  Keypad '1'
    { AS_KP2,        0, 0x00 },  // 0x5a  Keypad '2'
    { AS_KP3,        0, 0x00 },  // 0x5b  Keypad '3'
    { AS_KP4,        0, 0x00 },  // 0x5c  Keypad '4'
    { AS_KP5,        0, 0x00 },  // 0x5d  Keypad '5'
    { AS_KP6,        0, 0x00 },  // 0x5e  Keypad '6'
    { AS_KP7,        0, 0x00 },  // 0x5f  Keypad '7'
    { AS_KP8,        0, 0x00 },  // 0x60  Keypad '8'
    { AS_KP9,        0, 0x00 },  // 0x61  Keypad '9'
    { AS_KP0,        0, 0x00 },  // 0x62  Keypad '0'
    { AS_KPDOT,      0, 0x00 },  // 0x63  Keypad '.'
    { AS_BACKSLASH,  0, 0x00 },  // 0x64  102ND '\' and '|' (non-US)
    { AS_NONE,       0, 0x00 },  // 0x65  Compose
    { AS_NONE,       0, 0x00 },  // 0x66  Power Key
    { AS_EQUAL,      0, 0x00 },  // 0x67  Keypad '='
    { AS_NONE,       0, 0x00 },  // 0x68  F13
    { AS_NONE,       0, 0x00 },  // 0x69  F14
    { AS_NONE,       0, 0x00 },  // 0x6a  F15
    { AS_NONE,       0, 0x00 },  // 0x6b  F16
    { AS_NONE,       0, 0x00 },  // 0x6c  F17
    { AS_NONE,       0, 0x00 },  // 0x6d  F18
    { AS_NONE,       0, 0x00 },  // 0x6e  F19
    { AS_NONE,       0, 0x00 },  // 0x6f  F20
    { AS_NONE,       0, 0x00 },  // 0x70  F21
    { AS_NONE,       0, 0x00 },  // 0x71  F22
    { AS_NONE,       0, 0x00 },  // 0x72  F23
    { AS_NONE,       0, 0x00 },  // 0x73  F24
    { AS_NONE,       0, 0x00 },  // 0x74  Open (Execute)
    { AS_HELP,       0, 0x00 },  // 0x75  Help
    { AS_NONE,       0, 0x00 },  // 0x76  Props
    { AS_NONE,       0, 0x00 },  // 0x77  Front
    { AS_NONE,       0, 0x00 },  // 0x78  Stop
    { AS_NONE,       0, 0x00 },  // 0x79  Again
    { AS_NONE,       0, 0x00 },  // 0x7a  Undo
    { AS_NONE,       0, 0x00 },  // 0x7b  Cut
    { AS_NONE,       0, 0x00 },  // 0x7c  Copy
    { AS_NONE,       0, 0x00 },  // 0x7d  Paste
    { AS_NONE,       0, 0x00 },  // 0x7e  Find
    { AS_NONE,       0, 0x00 },  // 0x7f  Mute
    { AS_NONE,       0, 0x00 },  // 0x80  Volume Up
    { AS_NONE,       0, 0x00 },  // 0x81  Volume Down
    { AS_CAPSLOCK,   0, 0x00 },  // 0x82  Locking Caps Lock
    { AS_NONE,       0, 0x00 },  // 0x83  Locking Num Lock
    { AS_NONE,       0, 0x00 },  // 0x84  Locking Scroll Lock
    { AS_COMMA,      0, 0x00 },  // 0x85  Keypad ','
    { AS_EQUAL,      0, 0x00 },  // 0x86  Keypad '='
    { AS_NONE,       0, 0x00 },  // 0x87  International1 RO
    { AS_NONE,       0, 0x00 },  // 0x88  International2 Katakana Hiragana
    { AS_NONE,       0, 0x00 },  // 0x89  International3 Yen
    { AS_NONE,       0, 0x00 },  // 0x8a  International4 Henkan
    { AS_NONE,       0, 0x00 },  // 0x8b  International5 Muhenkan
    { AS_NONE,       0, 0x00 },  // 0x8c  International6 Keypad JP Comma
    { AS_NONE,       0, 0x00 },  // 0x8d  International7
    { AS_NONE,       0, 0x00 },  // 0x8e  International8
    { AS_NONE,       0, 0x00 },  // 0x8f  International9
    { AS_NONE,       0, 0x00 },  // 0x90  LANG1 Hangeul
    { AS_NONE,       0, 0x00 },  // 0x91  LANG2 Hanja
    { AS_NONE,       0, 0x00 },  // 0x92  LANG3 Katakana
    { AS_NONE,       0, 0x00 },  // 0x93  LANG4 Hiragana
    { AS_NONE,       0, 0x00 },  // 0x94  LANG5 Zenkakuhankaku
    { AS_NONE,       0, 0x00 },  // 0x95  LANG6
    { AS_NONE,       0, 0x00 },  // 0x96  LANG7
    { AS_NONE,       0, 0x00 },  // 0x97  LANG8
    { AS_NONE,       0, 0x00 },  // 0x98  LANG9
    { AS_NONE,       0, 0x00 },  // 0x99  Alternate Erase
    { AS_NONE,       0, 0x00 },  // 0x9a  SysReq / Attention
    { AS_NONE,       0, 0x00 },  // 0x9b  Cancel
    { AS_NONE,       0, 0x00 },  // 0x9c  Clear
    { AS_NONE,       0, 0x00 },  // 0x9d  Prior
    { AS_NONE,       0, 0x00 },  // 0x9e  Return
    { AS_NONE,       0, 0x00 },  // 0x9f  Separator
    { AS_NONE,       0, 0x00 },  // 0xa0  Out
    { AS_NONE,       0, 0x00 },  // 0xa1  Oper
    { AS_NONE,       0, 0x00 },  // 0xa2  Clear / Again
    { AS_NONE,       0, 0x00 },  // 0xa3  CrSel / Props
    { AS_NONE,       0, 0x00 },  // 0xa4  ExSel
    { AS_NONE,       0, 0x00 },  // 0xa5
    { AS_NONE,       0, 0x00 },  // 0xa6
    { AS_NONE,       0, 0x00 },  // 0xa7
    { AS_NONE,       0, 0x00 },  // 0xa8
    { AS_NONE,       0, 0x00 },  // 0xa9
    { AS_NONE,       0, 0x00 },  // 0xaa
    { AS_NONE,       0, 0x00 },  // 0xab
    { AS_NONE,       0, 0x00 },  // 0xac
    { AS_NONE,       0, 0x00 },  // 0xad
    { AS_NONE,       0, 0x00 },  // 0xae
    { AS_NONE,       0, 0x00 },  // 0xaf
    { AS_NONE,       0, 0x00 },  // 0xb0  Keypad 00
    { AS_NONE,       0, 0x00 },  // 0xb1  Keypad 000
    { AS_NONE,       0, 0x00 },  // 0xb2  Thousands Separator
    { AS_NONE,       0, 0x00 },  // 0xb3  Decimal Separator
    { AS_NONE,       0, 0x00 },  // 0xb4  Currency Unit
    { AS_NONE,       0, 0x00 },  // 0xb5  Currency Sub-unit
    { AS_KP_LPAREN,  0, 0x00 },  // 0xb6  Keypad '('
    { AS_KP_RPAREN,  0, 0x00 },  // 0xb7  Keypad ')'
    { AS_NONE,       0, 0x00 },  // 0xb8  Keypad '{'
    { AS_NONE,       0, 0x00 },  // 0xb9  Keypad '}'
    { AS_TAB,        0, 0x00 },  // 0xba  Keypad Tab
    { AS_BACKSPACE,  0, 0x00 },  // 0xbb  Keypad Backspace
    { AS_A,          0, 0x00 },  // 0xbc  Keypad 'A'
    { AS_B,          0, 0x00 },  // 0xbd  Keypad 'B'
    { AS_C,          0, 0x00 },  // 0xbe  Keypad 'C'
    { AS_D,          0, 0x00 },  // 0xbf  Keypad 'D'
    { AS_E,          0, 0x00 },  // 0xc0  Keypad 'E'
    { AS_F,          0, 0x00 },  // 0xc1  Keypad 'F'
    { AS_NONE,       0, 0x00 },  // 0xc2  Keypad XOR
    { AS_6,          0, 0x01 },  // 0xc3  Keypad '^'
    { AS_5,          0, 0x01 },  // 0xc4  Keypad '%'
    { AS_COMMA,      0, 0x01 },  // 0xc5  Keypad '<'
    { AS_DOT,        0, 0x01 },  // 0xc6  Keypad '>'
    { AS_7,          0, 0x01 },  // 0xc7  Keypad '&'
    { AS_NONE,       0, 0x00 },  // 0xc8  Keypad &&
    { AS_BACKSLASH,  0, 0x01 },  // 0xc9  Keypad '|'
    { AS_NONE,       0, 0x00 },  // 0xca  Keypad ||
    { AS_SEMICOLON,  0, 0x01 },  // 0xcb  Keypad ':'
    { AS_3,          0, 0x01 },  // 0xcc  Keypad '#'
    { AS_SPACE,      0, 0x00 },  // 0xcd  Keypad Space
    { AS_2,          0, 0x01 },  // 0xce  Keypad '@'
    { AS_1,          0, 0x01 },  // 0xcf  Keypad '!'
    { AS_NONE,       0, 0x00 },  // 0xd0  Keypad Memory Store
    { AS_NONE,       0, 0x00 },  // 0xd1  Keypad Memory Recall
    { AS_NONE,       0, 0x00 },  // 0xd2  Keypad Memory Clear
    { AS_NONE,       0, 0x00 },  // 0xd3  Keypad Memory Add
    { AS_NONE,       0, 0x00 },  // 0xd4  Keypad Memory Subtract
    { AS_NONE,       0, 0x00 },  // 0xd5  Keypad Memory Multiply
    { AS_NONE,       0, 0x00 },  // 0xd6  Keypad Memory Divide
    { AS_NONE,       0, 0x00 },  // 0xd7  Keypad +/-
    { AS_NONE,       0, 0x00 },  // 0xd8  Keypad Clear
    { AS_NONE,       0, 0x00 },  // 0xd9  Keypad Clear Entry
    { AS_NONE,       0, 0x00 },  // 0xda  Keypad Binary
    { AS_NONE,       0, 0x00 },  // 0xdb  Keypad Octal
    { AS_NONE,       0, 0x00 },  // 0xdc  Keypad Decimal
    { AS_NONE,       0, 0x00 },  // 0xdd  Keypad Hexadecimal
    { AS_NONE,       0, 0x00 },  // 0xde
    { AS_NONE,       0, 0x00 },  // 0xdf
    { AS_CTRL,       0, 0x00 },  // 0xe0  Left Control
    { AS_LEFTSHIFT,  0, 0x00 },  // 0xe1  Left Shift
    { AS_LEFTALT,    0, 0x00 },  // 0xe2  Left Alt
    { AS_LEFTAMIGA,  0, 0x00 },  // 0xe3  Left Meta
    { AS_CTRL,       0, 0x00 },  // 0xe4  Right Control
    { AS_RIGHTSHIFT, 0, 0x00 },  // 0xe5  Right Shift
    { AS_RIGHTALT,   0, 0x00 },  // 0xe6  Right Alt
    { AS_RIGHTAMIGA, 0, 0x00 },  // 0xe7  Right Meta
    { AS_NONE,       0, 0x00 },  // 0xe8  Media Play / Pause
    { AS_NONE,       0, 0x00 },  // 0xe9  Media Stop CD
    { AS_NONE,       0, 0x00 },  // 0xea  Media Previous Song
    { AS_NONE,       0, 0x00 },  // 0xeb  Media Next Song
    { AS_NONE,       0, 0x00 },  // 0xec  Media Eject CD
    { AS_NONE,       0, 0x00 },  // 0xed  Media Volume Up
    { AS_NONE,       0, 0x00 },  // 0xee  Media Volume Down
    { AS_NONE,       0, 0x00 },  // 0xef  Media Mute
    { AS_NONE,       0, 0x00 },  // 0xf0  Media WWW
    { AS_NONE,       0, 0x00 },  // 0xf1  Media Back
    { AS_NONE,       0, 0x00 },  // 0xf2  Media Forward
    { AS_NONE,       0, 0x00 },  // 0xf3  Media Stop
    { AS_NONE,       0, 0x00 },  // 0xf4  Media Find
    { AS_NONE,       0, 0x00 },  // 0xf5  Media Scroll Up
    { AS_NONE,       0, 0x00 },  // 0xf6  Media Scroll Down
    { AS_NONE,       0, 0x00 },  // 0xf7  Media Edit
    { AS_NONE,       0, 0x00 },  // 0xf8  Media Sleep
    { AS_NONE,       0, 0x00 },  // 0xf9  Media Coffee
    { AS_NONE,       0, 0x00 },  // 0xfa  Media Refresh
    { AS_NONE,       0, 0x00 },  // 0xfb  Media Calc
    { AS_NONE,       0, 0x00 },  // 0xfc
    { AS_NONE,       0, 0x00 },  // 0xfd
    { AS_NONE,       0, 0x00 },  // 0xfe
    { AS_NONE,       0, 0x00 },  // 0xff
};
// 0x01 = Assert shift on non-shifted version (SAF_ADD_SHIFT)

#define ATA_ADD_SHIFT 0x100
#define ATA_ADD_CTRL  0x200
static const uint16_t
ascii_to_amiga[] = {
    AS_NONE,                       // 0x00 ^@
    AS_A | ATA_ADD_CTRL,           // 0x01 ^A
    AS_B | ATA_ADD_CTRL,           // 0x02 ^B
    AS_C | ATA_ADD_CTRL,           // 0x03 ^C
    AS_D | ATA_ADD_CTRL,           // 0x04 ^D
    AS_E | ATA_ADD_CTRL,           // 0x05 ^E
    AS_F | ATA_ADD_CTRL,           // 0x06 ^F
    AS_G | ATA_ADD_CTRL,           // 0x07 ^G
    AS_BACKSPACE,                  // 0x08 ^H
    AS_TAB,                        // 0x09 ^I
    AS_J | ATA_ADD_CTRL,           // 0x0a ^J
    AS_K | ATA_ADD_CTRL,           // 0x0b ^K
    AS_L | ATA_ADD_CTRL,           // 0x0c ^L
    AS_RETURN,                     // 0x0d ^M
    AS_N | ATA_ADD_CTRL,           // 0x0e ^N
    AS_O | ATA_ADD_CTRL,           // 0x0f ^O
    AS_P | ATA_ADD_CTRL,           // 0x10 ^P
    AS_Q | ATA_ADD_CTRL,           // 0x11 ^Q
    AS_R | ATA_ADD_CTRL,           // 0x12 ^R
    AS_S | ATA_ADD_CTRL,           // 0x13 ^S
    AS_T | ATA_ADD_CTRL,           // 0x14 ^T
    AS_U | ATA_ADD_CTRL,           // 0x15 ^U
    AS_V | ATA_ADD_CTRL,           // 0x16 ^V
    AS_W | ATA_ADD_CTRL,           // 0x17 ^W
    AS_X | ATA_ADD_CTRL,           // 0x18 ^X
    AS_Y | ATA_ADD_CTRL,           // 0x19 ^Y
    AS_Z | ATA_ADD_CTRL,           // 0x1a ^Z
    AS_ESC,                        // 0x1b ESC
    AS_NONE,                       // 0x1c
    AS_NONE,                       // 0x1d
    AS_NONE,                       // 0x1e
    AS_NONE,                       // 0x1f
    AS_SPACE,                      // 0x20 ' '
    AS_1 | ATA_ADD_SHIFT,          // 0x21 '!'
    AS_APOSTROPHE | ATA_ADD_SHIFT, // 0x22 '"'
    AS_3 | ATA_ADD_SHIFT,          // 0x23 '#'
    AS_4 | ATA_ADD_SHIFT,          // 0x24 '$'
    AS_5 | ATA_ADD_SHIFT,          // 0x25 '%'
    AS_7 | ATA_ADD_SHIFT,          // 0x26 '&'
    AS_APOSTROPHE,                 // 0x27 '''
    AS_9 | ATA_ADD_SHIFT,          // 0x28 '('
    AS_0 | ATA_ADD_SHIFT,          // 0x29 ')'
    AS_8 | ATA_ADD_SHIFT,          // 0x2a '*'
    AS_EQUAL | ATA_ADD_SHIFT,      // 0x2b '+'
    AS_COMMA,                      // 0x2c ','
    AS_MINUS,                      // 0x2d '-'
    AS_DOT,                        // 0x2e '.'
    AS_SLASH,                      // 0x2f '/'
    AS_0,                          // 0x30 '0'
    AS_1,                          // 0x31 '1'
    AS_2,                          // 0x32 '2'
    AS_3,                          // 0x33 '3'
    AS_4,                          // 0x34 '4'
    AS_5,                          // 0x35 '5'
    AS_6,                          // 0x36 '6'
    AS_7,                          // 0x37 '7'
    AS_8,                          // 0x38 '8'
    AS_9,                          // 0x39 '9'
    AS_SEMICOLON | ATA_ADD_SHIFT,  // 0x3a ':'
    AS_SEMICOLON,                  // 0x3b ';'
    AS_COMMA | ATA_ADD_SHIFT,      // 0x3c '<'
    AS_EQUAL,                      // 0x3d '='
    AS_DOT | ATA_ADD_SHIFT,        // 0x3e '>'
    AS_SLASH | ATA_ADD_SHIFT,      // 0x3f '?'
    AS_2 | ATA_ADD_SHIFT,          // 0x40 '@'
    AS_A | ATA_ADD_SHIFT,          // 0x41 'A'
    AS_B | ATA_ADD_SHIFT,          // 0x42 'B'
    AS_C | ATA_ADD_SHIFT,          // 0x43 'C'
    AS_D | ATA_ADD_SHIFT,          // 0x44 'D'
    AS_E | ATA_ADD_SHIFT,          // 0x45 'E'
    AS_F | ATA_ADD_SHIFT,          // 0x46 'F'
    AS_G | ATA_ADD_SHIFT,          // 0x47 'G'
    AS_H | ATA_ADD_SHIFT,          // 0x48 'H'
    AS_I | ATA_ADD_SHIFT,          // 0x49 'I'
    AS_J | ATA_ADD_SHIFT,          // 0x4a 'J'
    AS_K | ATA_ADD_SHIFT,          // 0x4b 'K'
    AS_L | ATA_ADD_SHIFT,          // 0x4c 'L'
    AS_M | ATA_ADD_SHIFT,          // 0x4d 'M'
    AS_N | ATA_ADD_SHIFT,          // 0x4e 'N'
    AS_O | ATA_ADD_SHIFT,          // 0x4f 'O'
    AS_P | ATA_ADD_SHIFT,          // 0x50 'P'
    AS_Q | ATA_ADD_SHIFT,          // 0x51 'Q'
    AS_R | ATA_ADD_SHIFT,          // 0x52 'R'
    AS_S | ATA_ADD_SHIFT,          // 0x53 'S'
    AS_T | ATA_ADD_SHIFT,          // 0x54 'T'
    AS_U | ATA_ADD_SHIFT,          // 0x55 'U'
    AS_V | ATA_ADD_SHIFT,          // 0x56 'V'
    AS_W | ATA_ADD_SHIFT,          // 0x57 'R'
    AS_X | ATA_ADD_SHIFT,          // 0x58 'X'
    AS_Y | ATA_ADD_SHIFT,          // 0x59 'Y'
    AS_Z | ATA_ADD_SHIFT,          // 0x5a 'Z'
    AS_LBRACKET,                   // 0x5b '['
    AS_BACKSLASH,                  // 0x5c '\'
    AS_RBRACKET,                   // 0x5d ']'
    AS_6 | ATA_ADD_SHIFT,          // 0x5e '^'
    AS_MINUS | ATA_ADD_SHIFT,      // 0x5f '_'
    AS_BACKTICK,                   // 0x60 '`'
    AS_A,                          // 0x61 'a'
    AS_B,                          // 0x62 'b'
    AS_C,                          // 0x63 'c'
    AS_D,                          // 0x64 'd'
    AS_E,                          // 0x65 'e'
    AS_F,                          // 0x66 'f'
    AS_G,                          // 0x67 'g'
    AS_H,                          // 0x68 'h'
    AS_I,                          // 0x69 'i'
    AS_J,                          // 0x6a 'j'
    AS_K,                          // 0x6b 'k'
    AS_L,                          // 0x6c 'l'
    AS_M,                          // 0x6d 'm'
    AS_N,                          // 0x6e 'n'
    AS_O,                          // 0x6f 'o'
    AS_P,                          // 0x70 'p'
    AS_Q,                          // 0x71 'q'
    AS_R,                          // 0x72 'r'
    AS_S,                          // 0x73 's'
    AS_T,                          // 0x74 't'
    AS_U,                          // 0x75 'u'
    AS_V,                          // 0x76 'v'
    AS_W,                          // 0x77 'w'
    AS_X,                          // 0x78 'x'
    AS_Y,                          // 0x79 'y'
    AS_Z,                          // 0x7a 'z'
    AS_LBRACKET | ATA_ADD_SHIFT,   // 0x7b '{'
    AS_BACKSLASH | ATA_ADD_SHIFT,  // 0x7c '|'
    AS_RBRACKET | ATA_ADD_SHIFT,   // 0x7d '}'
    AS_BACKTICK | ATA_ADD_SHIFT,   // 0x7e '~'
    AS_BACKSPACE,                  // 0x7f Backspace
    AS_DELETE,                     // 0x80 Delete
    AS_UP,                         // 0x81 Cursor up
    AS_DOWN,                       // 0x82 Cursor down
    AS_LEFT,                       // 0x83 Cursor left
    AS_RIGHT,                      // 0x84 Cursor right
    AS_KPENTER,                    // 0x85 Keypad enter
};

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
static const uint16_t scancode_to_ascii_ext[] =
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

void
keyboard_set_defaults(void)
{
    uint code;
    for (code = 0; code < 256; code++)
        config.keymap[code] = scancode_to_amiga[code].sa_amiga;
    config.modkeymap[0] = AS_CTRL;
    config.modkeymap[1] = AS_LEFTSHIFT;
    config.modkeymap[2] = AS_LEFTALT;
    config.modkeymap[3] = AS_LEFTAMIGA;
    config.modkeymap[4] = AS_CTRL;
    config.modkeymap[5] = AS_RIGHTSHIFT;
    config.modkeymap[6] = AS_RIGHTALT;
    config.modkeymap[7] = AS_RIGHTAMIGA;
}

static uint32_t
convert_scancode_to_amiga(uint8_t keycode, uint8_t modifier,
                          uint8_t *amiga_modifier)
{
    uint32_t code = config.keymap[keycode];
    *amiga_modifier = 0;
    if (modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                    KEYBOARD_MODIFIER_RIGHTSHIFT)) {
        if (scancode_to_amiga[keycode].sa_shifted != 0)
            code = scancode_to_amiga[keycode].sa_shifted;  // Can't remap these
    } else {
        if (scancode_to_amiga[keycode].sa_flags & SAF_ADD_SHIFT) {
            // Must assert shift modifier to Amiga
            *amiga_modifier = 1;
        }
    }
    return (code);
}

static inline void
set_kbclk_0(void)
{
   *ADDR32(BND_IO(KBCLK_PORT + GPIO_ODR_OFFSET, low_bit(KBCLK_PIN))) = 0;
}

static inline void
set_kbclk_1(void)
{
   *ADDR32(BND_IO(KBCLK_PORT + GPIO_ODR_OFFSET, low_bit(KBCLK_PIN))) = 1;
}

static inline void
set_kbdat_0(void)
{
   *ADDR32(BND_IO(KBDATA_PORT + GPIO_ODR_OFFSET, low_bit(KBDATA_PIN))) = 0;
}

static inline void
set_kbdat_1(void)
{
   *ADDR32(BND_IO(KBDATA_PORT + GPIO_ODR_OFFSET, low_bit(KBDATA_PIN))) = 1;
}

static inline uint
get_kbdat(void)
{
   return (*ADDR32(BND_IO(KBDATA_PORT + GPIO_IDR_OFFSET, low_bit(KBDATA_PIN))));
}

static inline uint
get_kbclk(void)
{
   return (*ADDR32(BND_IO(KBCLK_PORT + GPIO_IDR_OFFSET, low_bit(KBCLK_PIN))));
}

static inline uint
get_kbclk_output_value(void)
{
   return (*ADDR32(BND_IO(KBDATA_PORT + GPIO_ODR_OFFSET, low_bit(KBDATA_PIN))));
}


/*
 * amiga_keyboard_send() is responsible for clocking out keyboard data
 * to the Amiga.
 *
 * The KCLK line is active low, and is driven by the keyboard. During
 * the low time of KCLK, the Amiga will sample KDAT.
 * The KDAT line is active low, and can be driven by either the keyboard
 * or the Amiga. The Amiga will drive bit 9 after receiving 8 bits from
 * the keyboard.
 *
 * Data is rotated before being sent from the keyboard such that the
 * bit order is 6-5-4-3-2-1-0-7.
 *
 *     ___   ___   ___   ___   ___   ___   ___   ___   _______
 * KCLK   \_/   \_/   \_/   \_/   \_/   \_/   \_/   \_/
 *
 *     _______________________________________________________
 * KDAT   \_____x_____x_____x_____x_____x_____x_____x_____/
 *          (6)   (5)   (4)   (3)   (2)   (1)   (0)   (7)
 *
 *         First                                     Last
 *          sent                                     sent
 *
 * The keyboard sets KDAT, waits 20 usec, pulls KCLK low for 20 usec,
 * releases KCLK, waits 20 usec, then releases KDAT. This results in a
 * bit rate of 17 kbps.
 */
static void
amiga_keyboard_send(void)
{
    uint code;
    int  mask;
    static uint64_t timer_kbdata_0;

    if (ak_rb_consumer == ak_rb_producer)
        return;  // Send buffer is empty

    if (get_kbclk() == 0) {
        amiga_keyboard_has_sync  = 0;
        amiga_keyboard_lost_sync = 1;
        return;
    }
    if (get_kbdat() == 0) {
        if (timer_kbdata_0 == 0) {
            timer_kbdata_0 = timer_tick_plus_msec(10);
            return;
        }
        if (timer_tick_has_elapsed(timer_kbdata_0)) {
            printf("K0");
            amiga_keyboard_has_sync  = 0;
            amiga_keyboard_lost_sync = 1;
        }
        return;
    }

    if (amiga_keyboard_lost_sync)
        code = AS_LOST_SYNC;
    else
        code = ak_rb[ak_rb_consumer];
    dprintf(DF_AMIGA_KEYBOARD, "[tx %x]", code);
    for (mask = 0x80; mask != 0; mask >>= 1) {
        if (code & mask)
            set_kbdat_1();
        else
            set_kbdat_0();

        timer_delay_usec(19);
        set_kbclk_0();
        if ((code & mask) && (get_kbdat() == 0)) {
            /*
             * KBDATA was set to 1, but it's stuck low. Assume we've lost
             * sync with the Amiga. Abort.
             */
            amiga_keyboard_has_sync  = 0;
            amiga_keyboard_lost_sync = 1;
            printf("Lsync1");
            timer_delay_usec(19);
            set_kbclk_1();
            timer_kbdata_0 = 0;
            return;
        }
        timer_delay_usec(20);
        set_kbclk_1();
        timer_delay_usec(20);
    }
    set_kbdat_1();
    timer_delay_usec(10);

    /* Wait for KBDAT to go low (ACK from Amiga) */
    timer_kbdata_0 = timer_tick_plus_msec(143);  // Spec is 143 msec
    while (get_kbdat() != 0) {
        if (timer_tick_has_elapsed(timer_kbdata_0)) {
            /* No ACK from Amiga */
            timer_kbdata_0 = 0;
            amiga_keyboard_has_sync  = 0;
            amiga_keyboard_lost_sync = 1;
            printf("Lsync2");
            return;
        }
    }
    timer_kbdata_0 = 0;
//  printf(",%02x,", code);

    if (amiga_keyboard_lost_sync)
        amiga_keyboard_lost_sync = 0;
    else
        ak_rb_consumer = ((ak_rb_consumer + 1) % sizeof (ak_rb));
}

/*
 * amiga_keyboard_put
 * ------------------
 * Push the specified keystroke to the Amiga keyboard buffer
 */
void
amiga_keyboard_put(uint8_t code)
{
    uint new_prod;

    switch (code) {
        case AS_CTRL:
            ak_ctrl_amiga_amiga |= BIT(0);
            break;
        case AS_CTRL | 0x80:
            ak_ctrl_amiga_amiga &= ~BIT(0);
            break;
        case AS_LEFTAMIGA:
            ak_ctrl_amiga_amiga |= BIT(1);
            break;
        case AS_LEFTAMIGA | 0x80:
            ak_ctrl_amiga_amiga &= ~BIT(1);
            break;
        case AS_RIGHTAMIGA:
            ak_ctrl_amiga_amiga |= BIT(2);
            break;
        case AS_RIGHTAMIGA | 0x80:
            ak_ctrl_amiga_amiga &= ~BIT(2);
            break;
    }

    dprintf(DF_USB_KEYBOARD, "[%02x]", code);
    new_prod = ((ak_rb_producer + 1) % sizeof (ak_rb));
    if (new_prod == ak_rb_consumer) {
        /* Ring buffer full! */
//      printf("!%02x!", code);
        return;
    }

    /* Rotate and invert for send */
    ak_rb[ak_rb_producer] = ~((code << 1) | (code >> 7));
    __sync_synchronize();  // Memory barrier
    ak_rb_producer = new_prod;
}

/*
 * amiga_keyboard_sync() attempts to achieve sync with the Amiga
 *                       keyboard interface. It does this by slowly
 *                       clocking out KBDAT=1 values until the Amiga
 *                       responds by driving KBDAT=0.
 */
static void
amiga_keyboard_sync(void)
{
    static uint8_t  sync_state;
    static uint64_t timer_kbdata_0;

#if 0
    if ((get_kbclk() == 0) && (get_kbclk_output_value() != 0)) {
        sync_state = 0;
        return;  // Nothing can be done
    }
#endif

    switch (sync_state) {
        case 0:  // Drive KBDATA low
            if ((get_kbdat() == 0) || (get_kbclk() == 0)) {
                set_kbclk_1();
                return;  // Wait for Amiga to stop driving
            }
            set_kbdat_0();
            sync_state++;
            break;
        case 1:  // Drive KBCLK low
            set_kbclk_0();
            sync_state++;
            break;
        case 2:  // Release KBCLK
            set_kbclk_1();
            sync_state++;
            break;
        case 3:  // Release KBDATA
            set_kbdat_1();
            timer_kbdata_0 = timer_tick_plus_msec(143);
            sync_state++;
            break;
        case 4:  // Wait for Amiga to respond by driving KBDATA low
            if (get_kbdat() == 0) {
                timer_kbdata_0 = timer_tick_plus_msec(143);
                sync_state++;    // Advance to next state
            } else {
                if (timer_tick_has_elapsed(timer_kbdata_0)) {
                    sync_state = 0;  // Got no response -- start over
                }
            }
            break;
        case 5:  // Wait for Amiga to release KBDATA
            if (get_kbdat() == 0) {
                /* Still low */
                if (timer_tick_has_elapsed(timer_kbdata_0)) {
                    /* KBDATA Stuck low */
                    printf("Stuck");
                    set_kbclk_1();
                    sync_state = 0;  // Start all over again
                    return;
                }
                return;  // Wait in the current statre
            }
            printf("Ksync\n");
            /* Amiga is no longer driving low: got sync */
            amiga_keyboard_has_sync = 1;
            sync_state = 0;   // Reset state for next loss of sync
            break;
    }
}

/*
 * keyboard_handle_magic
 * ---------------------
 * Handle magic keystroke sequences from USB Keyboard
 */
static void
keyboard_handle_magic(uint8_t keycode, uint modifier)
{
    static const uint8_t power_seq[] = { 'p', 'o', 'w', 'e', 'r' };
    static const uint8_t reset_seq[] = { 'r', 'e', 's', 'e', 't' };
    static const uint8_t stm32_seq[] = { 's', 't', 'm' };
    static uint8_t       power_pos;
    static uint8_t       reset_pos;
    static uint8_t       stm32_pos;
    uint8_t              ascii = scancode_to_ascii_ext[keycode];
    if ((modifier & (KEYBOARD_MODIFIER_LEFTCTRL |
                    KEYBOARD_MODIFIER_RIGHTCTRL)) == 0) {
        power_pos = 0;
        reset_pos = 0;
        stm32_pos = 0;
        return;
    }
    if (ascii == power_seq[power_pos]) {
        if (++power_pos == ARRAY_SIZE(power_seq)) {
            if (power_state == POWER_STATE_OFF)
                power_set(POWER_STATE_ON);
            else
                power_set(POWER_STATE_OFF);
            kbrst_amiga(0, 0);
            power_pos = 0;
        }
    } else {
        power_pos = 0;
    }
    if (ascii == reset_seq[reset_pos]) {
        if (++reset_pos == ARRAY_SIZE(reset_seq)) {
            kbrst_amiga(0, 0);
            reset_pos = 0;
        }
    } else {
        reset_pos = 0;
    }
    if (ascii == stm32_seq[stm32_pos]) {
        if (++stm32_pos == ARRAY_SIZE(stm32_seq)) {
            usb_keyboard_terminal = !usb_keyboard_terminal;
            printf("%s STM32 keyboard\n",
                   usb_keyboard_terminal ? "Become" : "Leave");
            reset_pos = 0;
        }
    } else {
        stm32_pos = 0;
    }
}

/* Handle input from USB keyboard */
void
keyboard_usb_input(usb_keyboard_report_t *report)
{
    static usb_keyboard_report_t prev_report;
    static uint8_t capslock;
    uint modifier = report->modifier;
    uint pmodifier = prev_report.modifier;
    uint mod_diff = modifier ^ pmodifier;
    uint cur;
    uint ascii;
    uint32_t mouse_buttons_old = mouse_buttons_add;

    if ((mod_diff != 0) & !usb_keyboard_terminal) {
        uint32_t code;
        uint bit;
        for (bit = 0; bit < 8; bit++) {
            if (mod_diff & BIT(bit)) {
                code = config.modkeymap[bit];
                while (code != 0) {
                    if (modifier & BIT(bit)) {
                        if (code & 0x80) {
                            /* Button press or macro expansion */
                            mouse_buttons_add |= BIT(code & 31);
                        } else {
                            amiga_keyboard_put(code);         // pressed
                        }
                    } else {
                        if (code & 0x80) {
                            /* Button press or macro expansion */
                            mouse_buttons_add &= ~BIT(code & 31);
                        } else {
                            amiga_keyboard_put(code | 0x80);  // released
                        }
                    }
                    code >>= 8;
                }
            }
        }
    }
    for (cur = 0; cur < ARRAY_SIZE(report->keycode); cur++) {
        uint8_t keycode = report->keycode[cur];
        if (report->keycode[cur]) {
            if (find_key_in_report(&prev_report, report->keycode[cur])) {
                /* Current key is being held */
            } else {
                /* New keypress */
                keyboard_handle_magic(keycode, modifier);
                if (usb_keyboard_terminal) {
                    uint16_t conv = scancode_to_ascii_ext[keycode];
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
                    keyboard_terminal_put(ascii, conv);
                } else {
                    /* Convert to Amiga keypress */
                    uint8_t amiga_modifier;
                    uint32_t tcode;

                    tcode = convert_scancode_to_amiga(keycode, modifier,
                                                      &amiga_modifier);
                    while (tcode != 0) {
                        uint8_t code = tcode & 0xff;
                        if (tcode == AS_CAPSLOCK) {
                            /* Capslock is pressed a second time to release */
                            if (capslock)
                                code = AS_NONE;
                        }
                        if (code != AS_NONE) {
                            if (code & 0x80) // Button press or macro expansion
                                /* Button press or macro expansion */
                                mouse_buttons_add |= BIT(code & 31);
                            else
                                amiga_keyboard_put(code);
                        }
                        tcode >>= 8;
                    }
                }
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
            /* Key released */
            if (usb_keyboard_terminal) {
                uint16_t conv = scancode_to_ascii_ext[keycode];
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
            } else {
                /* Convert to Amiga keypress */
                uint8_t amiga_modifier;
                uint32_t tcode;
                tcode = convert_scancode_to_amiga(keycode, modifier,
                                                  &amiga_modifier);
                while (tcode != 0) {
                    uint8_t code = tcode & 0xff;
                    if (code == AS_CAPSLOCK) {
                        if (capslock) {
                            capslock = 0;
                        } else {
                            capslock = 1;
                            code = AS_NONE;
                        }
                    }
                    if (code != AS_NONE) {
                        if (code & 0x80)  // Button press or macro expansion
                            mouse_buttons_add &= ~BIT(code & 31);
                        else
                            amiga_keyboard_put(code | 0x80);
                    }
                    tcode >>= 8;
                }
            }
        }
    }
    if (mouse_buttons_old != mouse_buttons_add)
        mouse_action(0, 0, 0, 0, 0);  // Inject button / macro expansion change

    prev_report = *report;
}

/* Input ESC key modes */
typedef enum {
    INPUT_MODE_NORMAL,  /* Normal user input */
    INPUT_MODE_ESC,     /* ESC key pressed */
    INPUT_MODE_BRACKET, /* ESC [ pressed */
    INPUT_MODE_O,       /* ESC [ O pressed (cursor key sequence) */
    INPUT_MODE_1,       /* ESC [ 1 pressed (HOME key sequence) */
    INPUT_MODE_2,       /* ESC [ 2 pressed (INSERT key sequence) */
    INPUT_MODE_3,       /* ESC [ 3 pressed (DEL key sequence) */
    INPUT_MODE_1SEMI,   /* ESC [ 1 ; pressed (ctrl-cursor key) */
    INPUT_MODE_1SEMI2,  /* ESC [ 1 ; 2 pressed (shift-cursor key) */
    INPUT_MODE_1SEMI3,  /* ESC [ 1 ; 3 pressed (alt-cursor key) */
    INPUT_MODE_1SEMI5,  /* ESC [ 1 ; 5 pressed (ctrl-cursor key) */
    INPUT_MODE_LITERAL, /* Control-V pressed (next input is literal) */
} input_mode_t;

#define KEY_CTRL_Q           0x11  // ^Q Exit term
#define KEY_ESC              0x1b  // ESC key
#define KEY_DELETE           0x80  // Delete key
#define KEY_UP               0x81  // Cursor up
#define KEY_DOWN             0x82  // Cursor down
#define KEY_LEFT             0x83  // Cursor left
#define KEY_RIGHT            0x84  // Cursor right
#define KEY_KPENTER          0x85  // Keypad enter

void
keyboard_term(void)
{
    int          ch;
    uint16_t     code;
    input_mode_t input_mode   = INPUT_MODE_NORMAL;
    uint         adding_ctrl  = 0;
    uint         adding_shift = 0;
    uint64_t     esc_timeout  = 0;

    printf("Press ^Q to exit\n");
    while (1) {
        main_poll();
        ch = getchar();
        if (ch <= 0) {
            if (input_mode != INPUT_MODE_NORMAL) {
                if (timer_tick_has_elapsed(esc_timeout)) {
                    input_mode = INPUT_MODE_NORMAL;
                    ch = KEY_ESC;
                    goto handle_literal;
                }
            }
            continue;
        }
        if (ch == KEY_CTRL_Q)
            break;  // End session

        switch (input_mode) {
            default:
            case INPUT_MODE_NORMAL:
                if (ch == KEY_ESC) {
                    input_mode = INPUT_MODE_ESC;
                    esc_timeout = timer_tick_plus_msec(10);
                    continue;
                }
                break;

            case INPUT_MODE_ESC:
                if ((ch == '[') || (ch == 'O')) {
                    input_mode = INPUT_MODE_BRACKET;
                } else {
                    /* Unrecognized ESC sequence -- swallow both */
                    input_mode = INPUT_MODE_NORMAL;
                }
                continue;

            case INPUT_MODE_BRACKET:
                input_mode = INPUT_MODE_NORMAL;
                switch (ch) {
                    case 'A':
                        ch = KEY_UP;
                        break;
                    case 'B':
                        ch = KEY_DOWN;
                        break;
                    case 'C':
                        ch = KEY_RIGHT;
                        break;
                    case 'D':
                        ch = KEY_LEFT;
                        break;
                    case 'F':
                        continue;  // Line End
                    case 'H':
                        continue;  // Line Begin
                    case 'M':
                        ch = KEY_KPENTER;  // Enter on numeric keypad
                        break;
                    case 'O':
                        input_mode = INPUT_MODE_O;
                        break;
                    case '1':
                        input_mode = INPUT_MODE_1;
                        continue;
                    case '2':
                        input_mode = INPUT_MODE_2;
                        continue;
                    case '3':
                        input_mode = INPUT_MODE_3;
                        continue;
                    case '5':
                        continue;  // Page Up
                    case '6':
                        continue;  // Page Down
                    default:
                        printf("\nUnknown 'ESC [ %c'\n", ch);
                        input_mode = INPUT_MODE_NORMAL;
                        continue;
                }
                break;

            case INPUT_MODE_O:
                input_mode = INPUT_MODE_NORMAL;
                switch (ch) {
                    case 'P':
                        code = AS_F1;  // F1
                        goto handle_code;
                    case 'Q':
                        code = AS_F2;  // F2
                        goto handle_code;
                    case 'R':
                        code = AS_F3;  // F3
                        goto handle_code;
                    case 'S':
                        code = AS_F4;  // F4
                        goto handle_code;
                    default:
                        printf("\nUnknown 'ESC [ O %c'\n", ch);
                        continue;
                }
                continue;

            case INPUT_MODE_1:
                input_mode = INPUT_MODE_NORMAL;
                switch (ch) {
                    case ';':
                        input_mode = INPUT_MODE_1SEMI;
                        continue;
                    case '~':
                        continue;  // Line Begin
                    case '4':
                        continue;  // F12
                    case '5':
                        code = AS_F5;  // F5
                        goto handle_code;
                    case '7':
                        code = AS_F6;  // F6
                        goto handle_code;
                    case '8':
                        code = AS_F7;  // F7
                        goto handle_code;
                    case '9':
                        code = AS_F8;  // F8
                        goto handle_code;
                    default:
                        printf("\nUnknown 'ESC [ 1 %c'\n", ch);
                        continue;
                }
                break;

            case INPUT_MODE_1SEMI:
                switch (ch) {
                    case '2':
                        input_mode = INPUT_MODE_1SEMI2;
                        break;
                    case '3':
                        input_mode = INPUT_MODE_1SEMI3;
                        break;
                    case '5':
                        input_mode = INPUT_MODE_1SEMI5;
                        break;
                    default:
                        input_mode = INPUT_MODE_NORMAL;
                        printf("\nUnknown 'ESC [ 1 ; %c'\n", ch);
                        continue;
                }
                continue;

            case INPUT_MODE_1SEMI2:
            case INPUT_MODE_1SEMI3:
            case INPUT_MODE_1SEMI5:
                input_mode = INPUT_MODE_NORMAL;
                switch (ch) {
                    case 'C':
                        continue;  // Line End
                    case 'D':
                        continue;  // Line Begin
                    default:
                        printf("\nUnknown 'ESC [ 1 ; %c %c'\n",
                               (input_mode == INPUT_MODE_1SEMI2) ? '2' :
                               (input_mode == INPUT_MODE_1SEMI3) ? '3' : '5',
                               ch);
                        continue;
                }
                break;

            case INPUT_MODE_2:
                switch (ch) {
                    case '~':
                        break;
                    case '0':
                        code = AS_F9;  // F9
                        goto handle_code;
                    case '1':
                        code = AS_F10;  // F10
                        goto handle_code;
                    case '2':
                        continue;  // F11
                    case '3':
                        continue;  // F12
                    default:
                        printf("\nUnknown 'ESC [ 2 %c'\n", ch);
                        continue;
                }
                /* Insert key */
                input_mode = INPUT_MODE_NORMAL;
                continue;

            case INPUT_MODE_3:
                input_mode = INPUT_MODE_NORMAL;
                if (ch != '~') {
                    printf("\nUnknown 'ESC [ 3 %c'\n", ch);
                    continue;
                }
                ch = KEY_DELETE;
                break;
            case INPUT_MODE_LITERAL:
                input_mode = INPUT_MODE_NORMAL;
                break;
        }
handle_literal:
        if (ch <= ARRAY_SIZE(ascii_to_amiga)) {
            code = ascii_to_amiga[ch];
handle_code:
            if (ch != AS_NONE) {
                if (code & ATA_ADD_CTRL) {
                    if (adding_ctrl == 0) {
                        amiga_keyboard_put(AS_CTRL);
                        adding_ctrl = 1;
                    }
                } else if (adding_ctrl) {
                    adding_ctrl = 0;
                    amiga_keyboard_put(AS_CTRL | 0x80);
                }
                if (code & ATA_ADD_SHIFT) {
                    if (adding_shift == 0) {
                        adding_shift = 1;
                        amiga_keyboard_put(AS_LEFTSHIFT);
                    }
                } else if (adding_shift) {
                    adding_shift = 0;
                    amiga_keyboard_put(AS_LEFTSHIFT | 0x80);
                }
                amiga_keyboard_put(code & 0x7f);
                amiga_keyboard_put(code | 0x80);
            }
        }
    }
    if (adding_ctrl)
        amiga_keyboard_put(AS_CTRL | 0x80);
    if (adding_shift)
        amiga_keyboard_put(AS_LEFTSHIFT | 0x80);
}

void
keyboard_poll(void)
{
    static uint8_t last_ctrl_amiga_amiga;
    static uint8_t recursive;

    if (usb_keyboard_count == 0) {
        amiga_keyboard_sent_wake = 0;  // No USB keyboard
        return;
    }

    if (recursive == 0) {
        uint8_t cur_ctrl_amiga_amiga = ak_ctrl_amiga_amiga;
        recursive = 1;
        if (last_ctrl_amiga_amiga != cur_ctrl_amiga_amiga) {
            uint8_t last = last_ctrl_amiga_amiga;
            last_ctrl_amiga_amiga = cur_ctrl_amiga_amiga;
            if (cur_ctrl_amiga_amiga == (BIT(0) | BIT(1) | BIT(2))) {
                /* Ctrl-Amiga-Amiga is being pressed */
                keyboard_reset_warning();
                kbrst_amiga(1, 0);  // Assert and hold KBRST
            } else if (last == (BIT(0) | BIT(1) | BIT(2))) {
                /* Ctrl-Amiga-Amiga has been released */
                kbrst_amiga(0, 0);  // Release KBRST
            }
        }
        recursive = 0;
    }

    if (amiga_keyboard_has_sync == 0) {
        amiga_keyboard_sync();
        return;
    }

    if (amiga_keyboard_sent_wake == 0) {
        amiga_keyboard_put(AS_POWER_INIT);
        amiga_keyboard_put(AS_POWER_DONE);
        amiga_keyboard_sent_wake = 1;
    }

    amiga_keyboard_send();
}

/*
 * keyboard_reset_warning() issues a reset warning (reset is pending) to
 * the Amiga. The warnings will be sent. If the Amiga doesn't immediately
 * ack the first warning, this function will return a non-zero value.
 * Otherwise, the second warning is sent. The Amiga must ack that message
 * within 250 milliseconds, but it may hold KBDAT low for up to 10 seconds.
 * Once KBDAT goes high, the Amiga may be reset (via KBRST).
 */
uint
keyboard_reset_warning(void)
{
    uint64_t timeout;
    uint64_t start = timer_tick_get();
    ak_rb_consumer = ak_rb_producer;  // Flush the ring buffer
    amiga_keyboard_put(AS_RESET_WARN);
    timeout = timer_tick_plus_msec(200);
    while (ak_rb_consumer != ak_rb_producer) {
        keyboard_poll();
        main_poll();
        if (timer_tick_has_elapsed(timeout)) {
//          ak_rb_consumer = ak_rb_producer;  // Flush the ring buffer
            return (1);
        }
    }
    printf("WARN1: %llu usec\n", timer_tick_to_usec(timer_tick_get() - start));
    start = timer_tick_get();

    /* First warning was received; send second warning. */
    amiga_keyboard_put(AS_RESET_WARN);
    timeout = timer_tick_plus_msec(250);
    while (ak_rb_consumer != ak_rb_producer) {
        keyboard_poll();
        main_poll();
        if (timer_tick_has_elapsed(timeout)) {
//          ak_rb_consumer = ak_rb_producer;  // Flush the ring buffer
            return (1);
        }
    }
    printf("WARN2: %llu usec\n", timer_tick_to_usec(timer_tick_get() - start));
    start = timer_tick_get();

    /* Second warning was received. Wait up to 10 seconds */
    timeout = timer_tick_plus_msec(10000);
    while (get_kbdat() == 0) {
        main_poll();
        if (timer_tick_has_elapsed(timeout)) {
            return (1);
        }
    }
printf("3: %llu usec\n", timer_tick_to_usec(timer_tick_get() - start));
    return (0);
}

void
keyboard_init(void)
{
    ak_rb_producer = ak_rb_consumer = 0;
#if 0
    printf("BND KBCLK_IN=%x KBCLK_OUT=%x KBDAT_IN=%x KBDAT_OUT=%x\n",
           BND_IO(KBCLK_PORT + GPIO_IDR_OFFSET, low_bit(KBCLK_PIN)),
           BND_IO(KBCLK_PORT + GPIO_ODR_OFFSET, low_bit(KBCLK_PIN)),
           BND_IO(KBDATA_PORT + GPIO_IDR_OFFSET, low_bit(KBDATA_PIN)),
           BND_IO(KBDATA_PORT + GPIO_ODR_OFFSET, low_bit(KBDATA_PIN)));
#endif
}
