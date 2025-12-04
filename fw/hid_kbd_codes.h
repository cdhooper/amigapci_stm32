/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * USB HID keyboard scancode definitions.
 */
#ifndef _HID_KBD_CODES_H
#define _HID_KBD_CODES_H

#define HS_NONE         (0x00)  // No key pressed
#define HS_KBD_ROLLOVER (0x01)  // Keyboard Error Roll Over
#define HS_KBD_POSTFAIL (0x02)  // Keyboard POST Fail
#define HS_KBD_ERROR    (0x03)  // Keyboard error
#define HS_A            (0x04)  // 'A' and 'a'
#define HS_B            (0x05)  // 'B' and 'b'
#define HS_C            (0x06)  // 'C' and 'c'
#define HS_D            (0x07)  // 'D' and 'd'
#define HS_E            (0x08)  // 'E' and 'e'
#define HS_F            (0x09)  // 'F' and 'f'
#define HS_G            (0x0a)  // 'G' and 'g'
#define HS_H            (0x0b)  // 'H' and 'h'
#define HS_I            (0x0c)  // 'I' and 'i'
#define HS_J            (0x0d)  // 'J' and 'j'
#define HS_K            (0x0e)  // 'K' and 'k'
#define HS_L            (0x0f)  // 'L' and 'l'
#define HS_M            (0x10)  // 'M' and 'm'
#define HS_N            (0x11)  // 'N' and 'n'
#define HS_O            (0x12)  // 'O' and 'o'
#define HS_P            (0x13)  // 'P' and 'p'
#define HS_Q            (0x14)  // 'Q' and 'q'
#define HS_R            (0x15)  // 'R' and 'r'
#define HS_S            (0x16)  // 'S' and 's'
#define HS_T            (0x17)  // 'T' and 't'
#define HS_U            (0x18)  // 'U' and 'u'
#define HS_V            (0x19)  // 'V' and 'v'
#define HS_W            (0x1a)  // 'W' and 'w'
#define HS_X            (0x1b)  // 'X' and 'x'
#define HS_Y            (0x1c)  // 'Y' and 'y'
#define HS_Z            (0x1d)  // 'Z' and 'z'
#define HS_1            (0x1e)  // '!' and '1'
#define HS_2            (0x1f)  // '@' and '2'
#define HS_3            (0x20)  // '#' and '3'
#define HS_4            (0x21)  // '$' and '4'
#define HS_5            (0x22)  // '%' and '5'
#define HS_6            (0x23)  // '^' and '6'
#define HS_7            (0x24)  // '&' and '7'
#define HS_8            (0x25)  // '*' and '8'
#define HS_9            (0x26)  // '(' and '9'
#define HS_0            (0x27)  // ')' and '0'
#define HS_ENTER        (0x28)  // Enter
#define HS_ESC          (0x29)  // ESC
#define HS_BACKSPACE    (0x2a)  // Backspace
#define HS_TAB          (0x2b)  // Tab
#define HS_SPACE        (0x2c)  // Space
#define HS_MINUS        (0x2d)  // '_' and '-'
#define HS_EQUAL        (0x2e)  // '+' and '='
#define HS_LBRACKET     (0x2f)  // '{' and '['
#define HS_RBRACKET     (0x30)  // '}' and ']'
#define HS_BACKSLASH    (0x31)  // '|' and '\'
#define HS_NUMBERTILDE  (0x32)  // '~' and '#' (Non-US)
#define HS_SEMICOLON    (0x33)  // ';' and ':'
#define HS_APOSTROPHE   (0x34)  // ''' and '"'
#define HS_BACKTICK     (0x35)  // '~' and '`'
#define HS_COMMA        (0x36)  // '<' and ','
#define HS_DOT          (0x37)  // '>' and '.'
#define HS_SLASH        (0x38)  // '>' and '/'
#define HS_CAPSLOCK     (0x39)  // Caps Lock
#define HS_F1           (0x3a)  // F1
#define HS_F2           (0x3b)  // F2
#define HS_F3           (0x3c)  // F3
#define HS_F4           (0x3d)  // F4
#define HS_F5           (0x3e)  // F5
#define HS_F6           (0x3f)  // F6
#define HS_F7           (0x40)  // F7
#define HS_F8           (0x41)  // F8
#define HS_F9           (0x42)  // F9
#define HS_F10          (0x43)  // F10
#define HS_F11          (0x44)  // F11
#define HS_F12          (0x45)  // F12
#define HS_PRTSCN       (0x46)  // Print Screen
#define HS_SCROLL_LOCK  (0x47)  // Sroll Lock
#define HS_PAUSE        (0x48)  // Pause
#define HS_INSERT       (0x49)  // Insert
#define HS_HOME         (0x4a)  // Home
#define HS_PAGEUP       (0x4b)  // Page Up
#define HS_DELETE       (0x4c)  // Delete
#define HS_END          (0x4d)  // End
#define HS_PAGEDOWN     (0x4e)  // Page Down
#define HS_RIGHT        (0x4f)  // Cursor Right
#define HS_LEFT         (0x50)  // Cursor Left
#define HS_DOWN         (0x51)  // Cursor Down
#define HS_UP           (0x52)  // Cursor Up
#define HS_NUMLOCK      (0x53)  // Numlock
#define HS_KP_DIV       (0x54)  // Keypad '/'
#define HS_KP_MUL       (0x55)  // Keypad '*'
#define HS_KP_MINUS     (0x56)  // Keypad '-'
#define HS_KP_PLUS      (0x57)  // Keypad '+'
#define HS_KP_ENTER     (0x58)  // Keypad Enter
#define HS_KP_1         (0x59)  // Keypad '1'
#define HS_KP_2         (0x5a)  // Keypad '2'
#define HS_KP_3         (0x5b)  // Keypad '3'
#define HS_KP_4         (0x5c)  // Keypad '4'
#define HS_KP_5         (0x5d)  // Keypad '5'
#define HS_KP_6         (0x5e)  // Keypad '6'
#define HS_KP_7         (0x5f)  // Keypad '7'
#define HS_KP_8         (0x60)  // Keypad '8'
#define HS_KP_9         (0x61)  // Keypad '9'
#define HS_KP_0         (0x62)  // Keypad '0'
#define HS_KP_DOT       (0x63)  // Keypad '.'
#define HS_BACKSLASH2   (0x64)  // 102ND '\' and '|' (non-US)
#define HS_MENU         (0x65)  // Menu (aka Application / Compose)
#define HS_POWER        (0x66)  // Power Key
#define HS_KP_EQUAL     (0x67)  // Keypad '='
#define HS_F13          (0x68)  // F13
#define HS_F14          (0x69)  // F14
#define HS_F15          (0x6a)  // F15
#define HS_F16          (0x6b)  // F16
#define HS_F17          (0x6c)  // F17
#define HS_F18          (0x6d)  // F18
#define HS_F19          (0x6e)  // F19
#define HS_F20          (0x6f)  // F20
#define HS_F21          (0x70)  // F21
#define HS_F22          (0x71)  // F22
#define HS_F23          (0x72)  // F23
#define HS_F24          (0x73)  // F24
#define HS_OPEN         (0x74)  // Open
#define HS_HELP         (0x75)  // Help
#define HS_PROPS        (0x76)  // Props
#define HS_FRONT        (0x77)  // Front
#define HS_STOP         (0x78)  // Stop
#define HS_AGAIN        (0x79)  // Again
#define HS_UNDO         (0x7a)  // Undo
#define HS_CUT          (0x7b)  // Cut
#define HS_COPY         (0x7c)  // Copy
#define HS_PASTE        (0x7d)  // Paste
#define HS_FIND         (0x7e)  // Find
#define HS_MUTE         (0x7f)  // Mute
#define HS_VOLUME_UP    (0x80)  // Volume Up
#define HS_VOLUME_DOWN  (0x81)  // Volume Down
#define HS_CAPS_LOCK    (0x82)  // Locking Caps Lock
#define HS_NUM_LOCK     (0x83)  // Locking Num Lock
#define HS_LSCROLL_LOCK (0x84)  // Locking Scroll Lock
#define HS_KP_COMMA     (0x85)  // Keypad Comma
#define HS_KP_EQUAL2    (0x86)  // Keypad Equal
#define HS_INTL1        (0x87)  // International1 RO
#define HS_INTL2        (0x88)  // International2 Katakana Hiragana
#define HS_INTL3        (0x89)  // International3 Yen
#define HS_INTL4        (0x8a)  // International4 Henkan
#define HS_INTL5        (0x8b)  // International5 Muhenkan
#define HS_INTL6        (0x8c)  // International6 Keypad JP Comma
#define HS_INTL7        (0x8d)  // International7
#define HS_INTL8        (0x8e)  // International8
#define HS_INTL9        (0x8f)  // International9
#define HS_LANG1        (0x90)  // LANG1 Hangeul
#define HS_LANG2        (0x91)  // LANG2 Hanja
#define HS_LANG3        (0x92)  // LANG3 Katakana
#define HS_LANG4        (0x93)  // LANG4 Hiragana
#define HS_LANG5        (0x94)  // LANG5 Zenkakuhankaku
#define HS_LANG6        (0x95)  // LANG6
#define HS_LANG7        (0x96)  // LANG7
#define HS_LANG8        (0x97)  // LANG8
#define HS_LANG9        (0x98)  // LANG9
#define HS_ALT_ERASE    (0x99)  // Alternate Erase
#define HS_SYSRQ_ATTN   (0x9a)  // SysReq / Attention
#define HS_CANCEL       (0x9b)  // Cancel
#define HS_CLEAR        (0x9c)  // Clear
#define HS_PRIOR        (0x9d)  // Prior
#define HS_RETURN       (0x9e)  // Return
#define HS_SEPARATOR    (0x9f)  // Separator
#define HS_OUT          (0xa0)  // Out
#define HS_OPER         (0xa1)  // Oper
#define HS_CLEAR_AGAIN  (0xa2)  // Clear / Again
#define HS_CRSEL        (0xa3)  // CrSel / Props
#define HS_EXCEL        (0xa4)  // ExSel
#define HS_xA5          (0xa5)
#define HS_xA6          (0xa6)
#define HS_xA7          (0xa7)
#define HS_xA8          (0xa8)
#define HS_xA9          (0xa9)
#define HS_xAA          (0xaa)
#define HS_xAB          (0xab)
#define HS_xAC          (0xac)
#define HS_xAD          (0xad)
#define HS_xAE          (0xae)
#define HS_xAF          (0xaf)
#define HS_KP_00        (0xb0)  // Keypad 00
#define HS_KP_000       (0xb1)  // Keypad 000
#define HS_THOUS_SEP    (0xb2)  // Thousands Separator
#define HS_DEC_SEP      (0xb3)  // Decimal Separator
#define HS_CURRENCY_U   (0xb4)  // Currency Unit
#define HS_CURRENCY_SU  (0xb5)  // Currency Sub-unit
#define HS_KP_LPAREN    (0xb6)  // Keypad '('
#define HS_KP_RPAREN    (0xb7)  // Keypad ')'
#define HS_KP_LBRACE    (0xb8)  // Keypad '{'
#define HS_KP_RBRACE    (0xb9)  // Keypad '}'
#define HS_KP_TAB       (0xba)  // Keypad Tab
#define HS_KP_BACKSPACE (0xbb)  // Keypad Backspace
#define HS_KP_A         (0xbc)  // Keypad 'A'
#define HS_KP_B         (0xbd)  // Keypad 'B'
#define HS_KP_C         (0xbe)  // Keypad 'C'
#define HS_KP_D         (0xbf)  // Keypad 'D'
#define HS_KP_E         (0xc0)  // Keypad 'E'
#define HS_KP_F         (0xc1)  // Keypad 'F'
#define HS_KP_XOR       (0xc2)  // Keypad XOR
#define HS_KP_CARET     (0xc3)  // Keypad '^'
#define HS_KP_PERCENT   (0xc4)  // Keypad '%'
#define HS_KP_LESS      (0xc5)  // Keypad '<'
#define HS_KP_GREATER   (0xc6)  // Keypad '>'
#define HS_KP_AND       (0xc7)  // Keypad '&'
#define HS_KP_ANDAND    (0xc8)  // Keypad &&
#define HS_KP_OR        (0xc9)  // Keypad '|'
#define HS_KP_OROR      (0xca)  // Keypad ||
#define HS_KP_COLON     (0xcb)  // Keypad ':'
#define HS_KP_NUMBER    (0xcc)  // Keypad '#'
#define HS_KP_SPACE     (0xcd)  // Keypad Space
#define HS_KP_AT        (0xce)  // Keypad '@'
#define HS_KP_BANG      (0xcf)  // Keypad '!'
#define HS_KP_M_STORE   (0xd0)  // Keypad Memory Store
#define HS_KP_M_RECALL  (0xd1)  // Keypad Memory Recall
#define HS_KP_M_CLEAR   (0xd2)  // Keypad Memory Clear
#define HS_KP_M_ADD     (0xd3)  // Keypad Memory Add
#define HS_KP_M_MINUS   (0xd4)  // Keypad Memory Subtract
#define HS_KP_M_MUL     (0xd5)  // Keypad Memory Multiply
#define HS_KP_M_DIV     (0xd6)  // Keypad Memory Divide
#define HS_KP_PLUSMINUS (0xd7)  // Keypad +/-
#define HS_KP_CLEAR     (0xd8)  // Keypad Clear
#define HS_KP_CLEARENT  (0xd9)  // Keypad Clear Entry
#define HS_KP_BIN       (0xda)  // Keypad Binary
#define HS_KP_OCT       (0xdb)  // Keypad Octal
#define HS_KP_DEC       (0xdc)  // Keypad Decimal
#define HS_KP_HEX       (0xdd)  // Keypad Hexadecimal
#define HS_xDE          (0xde)
#define HS_xDF          (0xdf)
#define HS_LCTRL        (0xe0)  // Left Control
#define HS_LSHIFT       (0xe1)  // Left Shift
#define HS_LALT         (0xe2)  // Left Alt
#define HS_LMETA        (0xe3)  // Left Meta
#define HS_RCTRL        (0xe4)  // Right Control
#define HS_RSHIFT       (0xe5)  // Right Shift
#define HS_RALT         (0xe6)  // Right Alt
#define HS_RMETA        (0xe7)  // Right Meta
#define HS_MEDIA_PLAY   (0xe8)  // Media Play / Pause
#define HS_MEDIA_STOPCD (0xe9)  // Media Stop CD
#define HS_MEDIA_PREV   (0xea)  // Media Previous Song
#define HS_MEDIA_NEXT   (0xeb)  // Media Next Song
#define HS_MEDIA_EJECT  (0xec)  // Media Eject CD
#define HS_MEDIA_V_UP   (0xed)  // Media Volume Up
#define HS_MEDIA_V_DOWN (0xee)  // Media Volume Down
#define HS_MEDIA_MUTE   (0xef)  // Media Mute
#define HS_MEDIA_WWW    (0xf0)  // Media WWW
#define HS_MEDIA_BACK   (0xf1)  // Media Back
#define HS_MEDIA_FWD    (0xf2)  // Media Forward
#define HS_MEDIA_STOP   (0xf3)  // Media Stop
#define HS_MEDIA_FIND   (0xf4)  // Media Find
#define HS_MEDIA_S_UP   (0xf5)  // Media Scroll Up
#define HS_MEDIA_S_DOWN (0xf6)  // Media Scroll Down
#define HS_MEDIA_EDIT   (0xf7)  // Media Edit
#define HS_MEDIA_SLEEP  (0xf8)  // Media Sleep
#define HS_MEDIA_COFFEE (0xf9)  // Media Coffee
#define HS_MEDIA_AGAIN  (0xfa)  // Media Refresh
#define HS_MEDIA_CALC   (0xfb)  // Media Calc
#define HS_xFC          (0xfc)
#define HS_xFD          (0xfd)
#define HS_xFE          (0xfe)
#define HS_xFF          (0xff)

#endif /* _HID_KBD_CODES_H */
