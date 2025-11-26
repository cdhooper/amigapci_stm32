/*
 * becky
 * -----
 * Utility to manipulate AmigaHIDI keyboard mapping tables stored in BEC
 * (the Board Environment Controller).
 *
 * Copyright 2025 Chris Hooper. This program and source may be used
 * and distributed freely, for any purpose which benefits the Amiga
 * community. Commercial use of the binary, source, or algorithms requires
 * prior written approval from Chris Hooper <amiga@cdh.eebugs.com>.
 * All redistributions must retain this Copyright notice.
 *
 * DISCLAIMER: THE SOFTWARE IS PROVIDED "AS-IS", WITHOUT ANY WARRANTY.
 * THE AUTHOR ASSUMES NO LIABILITY FOR ANY DAMAGE ARISING OUT OF THE USE
 * OR MISUSE OF THIS UTILITY OR INFORMATION REPORTED BY THIS UTILITY.
 */
const char *version = "\0$VER: becky "VERSION" ("BUILD_DATE") \xA9 Chris Hooper";

#include <stdio.h>
#include "becmsg.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <libraries/keymap.h>
#include <libraries/asl.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <clib/dos_protos.h>
#include <inline/timer.h>
#include <inline/exec.h>
#include <inline/dos.h>
#include <inline/asl.h>
#include <inline/diskfont.h>
#include <inline/gadtools.h>
#include <libraries/gadtools.h>
#include "amiga_kbd_codes.h"
#include "hid_kbd_codes.h"
#include "becmsg.h"
#include "bec_cmd.h"

/*
 * Define compile-time assert. This macro relies on gcc's built-in
 * static assert checking which is available in C11.
 */
#define STATIC_ASSERT(test_for_true) \
    static_assert((test_for_true), "(" #test_for_true ") failed")

#define BIT(x) (1U << (x))
#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))

uint flag_debug = 0;
static uint is_northamerican = 1;  // Default to North American style keyboard

static uint amiga_keyboard_left;
static uint amiga_keyboard_top;
static uint hid_keyboard_left;
static uint hid_keyboard_top;

static uint win_width  = 500;
static uint win_height = 220;
static uint kbd_width = 400;
static uint kbd_height = 100;

typedef struct {
    uint8_t  shaded;
    uint16_t x;
    uint16_t y;
} keysize_t;

#define NUM_SCANCODES 256
static uint8_t hid_scancode_to_amiga[NUM_SCANCODES][4];

/* 1u 1.25u 1.5u 2u 2.25u 9u */
#define U      191
#define PLAIN  0
#define SHADED 1
static const keysize_t amiga_keywidths[] = {
    { PLAIN,  1 * U,     1 * U, },  //  0: Standard key ('1' key)
    { SHADED, 1 * U,     1 * U, },  //  1: Standard shaded key (ESC key)
    { SHADED, 1.25 * U,  1 * U, },  //  2: Function and modifier key (F1 key)
    { SHADED, 1.5 * U,   1 * U, },  //  3: Tilde and backtick key
    { SHADED, 1.5 * U,   1 * U, },  //  4: Del and Help keys
    { SHADED, 2.1 * U,   1 * U, },  //  5: Tab key
    { SHADED, 1.75 * U,  1 * U, },  //  6: Left shift (wider for North America)
    { SHADED, 2.9  * U,  1 * U, },  //  7: Right shift
    { SHADED, 1 * U,   2.1 * U, },  //  8: Keypad enter key
    { PLAIN,  9.5  * U,  1 * U, },  //  9: Spacebar
    { PLAIN,  2 * U,     1 * U, },  // 10: Keypad 0
    { PLAIN,  1 * U,     1 * U, },  // 11: Extra keys (not North American)
    { SHADED, 1.50 * U, 2 * U, }, // 12: North American Enter key
    { SHADED, 1.30 * U, 2 * U, }, // 13: European Enter key
};

typedef struct {
    uint8_t  scancode;
    uint8_t  type;  // Type (for dimensions) and color
    uint16_t x;
    uint16_t y;
    char    *name;
} keypos_t;

#define C(x) ((char *)(uintptr_t)(x))

static const keypos_t amiga_keypos[] = {
    { AS_ESC,         1,  5086,  5050, "ESC" },
    { AS_F1,          2,  8190,  5050, "F1" },
    { AS_F2,          2, 10578,  5050, "F2" },
    { AS_F3,          2, 12966,  5050, "F3" },
    { AS_F4,          2, 15354,  5050, "F4" },
    { AS_F5,          2, 17742,  5050, "F5" },
    { AS_F6,          2, 20985,  5050, "F6" },
    { AS_F7,          2, 23373,  5050, "F7" },
    { AS_F8,          2, 25761,  5050, "F8" },
    { AS_F9,          2, 28149,  5050, "F9" },
    { AS_F10,         2, 30537,  5050, "F10" },

    { AS_BACKTICK,    3,  5553,  7917, C('`') },
    { AS_1,           0,  7931,  7917, C('1') },
    { AS_2,           0,  9836,  7917, C('2') },
    { AS_3,           0, 11741,  7917, C('3') },
    { AS_4,           0, 13646,  7917, C('4') },
    { AS_5,           0, 15551,  7917, C('5') },
    { AS_6,           0, 17456,  7917, C('6') },
    { AS_7,           0, 19361,  7917, C('7') },
    { AS_8,           0, 21266,  7917, C('8') },
    { AS_9,           0, 23171,  7917, C('9') },
    { AS_0,           0, 25076,  7917, C('0') },
    { AS_MINUS,       0, 26981,  7917, C('-') },
    { AS_EQUAL,       0, 28886,  7917, C('=') },
    { AS_BACKSLASH,   0, 30791,  7917, C('\\') },
    { AS_BACKSPACE,   1, 32696,  7917, "<-" },
    { AS_DELETE,      4, 35956,  7917, "Del" },
    { AS_HELP,        4, 38815,  7917, "Help" },
    { AS_KP_LPAREN,   1, 42156,  7917, C('(') },
    { AS_KP_RPAREN,   1, 44061,  7917, C(')') },
    { AS_KP_DIV,      1, 45966,  7917, C('/') },
    { AS_KP_MUL,      1, 47871,  7917, C('*') },

    { AS_TAB,         5,  6026,  9822, "->" },

    { AS_Q,           0,  8870,  9822, C('Q') },
    { AS_W,           0, 10775,  9822, C('W') },
    { AS_E,           0, 12680,  9822, C('E') },
    { AS_R,           0, 14585,  9822, C('R') },
    { AS_T,           0, 16490,  9822, C('T') },
    { AS_Y,           0, 18395,  9822, C('Y') },
    { AS_U,           0, 20300,  9822, C('U') },
    { AS_I,           0, 22205,  9822, C('I') },
    { AS_O,           0, 24110,  9822, C('O') },
    { AS_P,           0, 26015,  9822, C('P') },
    { AS_LBRACKET,    0, 27920,  9822, C('[') },
    { AS_RBRACKET,    0, 29825,  9822, C(']') },
#define NEW_ENTER
#ifdef NEW_ENTER
    { AS_ENTER,      12, 32250,  9822, "<_/" },  // North American Enter
    { AS_ENTER,      13, 32469,  9822, "<_/" },  // European Enter
#endif
    { AS_KP_7,        0, 42156,  9822, C('7') },
    { AS_KP_8,        0, 44061,  9822, C('8') },
    { AS_KP_9,        0, 45966,  9822, C('9') },
    { AS_KP_MINUS,    1, 47871,  9822, C('-') },

#ifndef NEW_ENTER
    { AS_ENTER,      12, 32250, 10790, "<_/" },  // North American Enter
    { AS_ENTER,      13, 32469, 10790, "<_/" },  // European Enter
#endif

    { AS_CTRL,        2,  5315, 11727, "Ctrl" },
    { AS_CAPSLOCK,    1,  7448, 11727, "Caps" },
    { AS_A,           0,  9353, 11727, C('A') },
    { AS_S,           0, 11258, 11727, C('S') },
    { AS_D,           0, 13163, 11727, C('D') },
    { AS_F,           0, 15068, 11727, C('F') },
    { AS_G,           0, 16973, 11727, C('G') },
    { AS_H,           0, 18878, 11727, C('H') },
    { AS_J,           0, 20783, 11727, C('J') },
    { AS_K,           0, 22688, 11727, C('K') },
    { AS_L,           0, 24593, 11727, C('L') },
    { AS_SEMICOLON,   0, 26498, 11727, C(';') },
    { AS_APOSTROPHE,  0, 28403, 11727, C('\'') },
    { AS_EXTRA1,     11, 30308, 11727, "E1"  },
    { AS_UP,          0, 37388, 11727, C('^') },
    { AS_KP_4,        0, 42156, 11727, C('4') },
    { AS_KP_5,        0, 44061, 11727, C('5') },
    { AS_KP_6,        0, 45966, 11727, C('6') },
    { AS_KP_PLUS,     1, 47871, 11727, C('+') },

    { AS_LEFTSHIFT,   6,  5798, 13632, "Shift" },
    { AS_EXTRA2,     11,  8413, 13632, "E2" },
    { AS_Z,           0, 10318, 13632, C('Z') },
    { AS_X,           0, 12223, 13632, C('X') },
    { AS_C,           0, 14128, 13632, C('C') },
    { AS_V,           0, 16033, 13632, C('V') },
    { AS_B,           0, 17938, 13632, C('B') },
    { AS_N,           0, 19843, 13632, C('N') },
    { AS_M,           0, 21748, 13632, C('M') },
    { AS_COMMA,       0, 23653, 13632, C(',') },
    { AS_DOT,         0, 25558, 13632, C('.') },
    { AS_SLASH,       0, 27463, 13632, C('/') },
    { AS_RIGHTSHIFT,  7, 31045, 13632, "Shift" },
    { AS_LEFT,        0, 35483, 13632, C('<') },
    { AS_DOWN,        0, 37388, 13632, C('v') },
    { AS_RIGHT,       0, 39293, 13632, C('>') },
    { AS_KP_1,        0, 42156, 13632, C('1') },
    { AS_KP_2,        0, 44061, 13632, C('2') },
    { AS_KP_3,        0, 45966, 13632, C('3') },

#ifdef NEW_ENTER
    { AS_KP_ENTER,    8, 47871, 13632, "_/" },
#else
    { AS_KP_ENTER,    8, 47871, 14577, "_/" },
#endif

    { AS_LEFTALT,     2,  6686, 15537, "Alt" },
    { AS_LEFTAMIGA,   2,  9074, 15537, "A" },
    { AS_SPACE,       9, 18840, 15537, "" },
    { AS_RIGHTAMIGA,  2, 28606, 15537, "A" },
    { AS_RIGHTALT,    2, 30994, 15537, "Alt" },
    { AS_KP_0,       10, 43121, 15537, C('0') },
    { AS_KP_DOT,      0, 45966, 15537, C('.') },
};


typedef struct {
    SHORT x_min;
    SHORT y_min;
    SHORT x_max;
    SHORT y_max;
} key_bbox_t;


#define AKB_MIN_X   5000
#define AKB_MIN_Y   5000
#define AKB_MAX_X  45500
#define AKB_MAX_Y  15800

#define HIDKB_MIN_X  5000
#define HIDKB_MIN_Y  5000
#define HIDKB_MAX_X 44500
#define HIDKB_MAX_Y 15800

// 1u    Alphanumeric keys (A, B, C, etc.), number keys, function keys
// 1.25u Ctrl, Alt, GUI (Windows/Command) keys (bottom row)
// 1.5u  Tab
// 1.75u Caps Lock
// 2u    Backspace, Numpad + and Enter
// 2.25u Left Shift, Enter
// 2.75u Right Shift
// 6.25u Spacebar (some are 7u)
//
// Measured (mm)
//       Top  Base
//       13.8 18.4  Alphanumeric
//       23.8 23.5  Ctrl, Meta, Alt, Fn
//       24.6       Tab, backslash
//       27.9       Capslock
//       33.1 36.3  Backspace, Numpad 0, KP + height, KP enter height
//       38.4 41.1  Left shift
//            42.6  Enter
//       46.4       Right shift
//      113.2       Spacebar

static const keysize_t hid_keywidths[] = {
    { PLAIN,  1 * U,     1 * U, },  //  0: Standard key ('1' key)
    { SHADED, 1 * U,     1 * U, },  //  1: Standard shaded key (ESC key)
    { SHADED, 1.25 * U,  1 * U, },  //  2: Modifier key (Ctrl, Meta, Alt, Fn)
    { SHADED, 2 * U,     1 * U, },  //  3: Backspace
    { SHADED, 1.5 * U,   1 * U, },  //  4: Del and Help keys
    { SHADED, 1.46 * U,  1 * U, },  //  5: Tab key
    { SHADED, 2.25 * U,  1 * U, },  //  6: Left shift
    { SHADED, 2.75 * U,  1 * U, },  //  7: Right shift
    { SHADED, 1 * U,   2.2 * U, },  //  8: Keypad enter key
    { PLAIN,  6.6 * U,   1 * U, },  //  9: Spacebar
    { PLAIN,  2 * U,     1 * U, },  // 10: Keypad 0
    { PLAIN,  1 * U,     1 * U, },  // 11: Extra keys (not North American)
    { SHADED, 2.25 * U,  1 * U, },  // 12: Enter key
    { SHADED, 1.75 * U,  1 * U, },  // 13: Capslock
};

static const keypos_t hid_keypos[] = {
    { HS_ESC,         1,  5086,  5050, "ESC" },
    { HS_F1,          1,  8895,  5050, "F1" },
    { HS_F2,          1, 10800,  5050, "F2" },
    { HS_F3,          1, 12705,  5050, "F3" },
    { HS_F4,          1, 14610,  5050, "F4" },
    { HS_F5,          1, 17468,  5050, "F5" },
    { HS_F6,          1, 19373,  5050, "F6" },
    { HS_F7,          1, 21278,  5050, "F7" },
    { HS_F8,          1, 23183,  5050, "F8" },
    { HS_F9,          1, 26041,  5050, "F9" },
    { HS_F10,         1, 27946,  5050, "F10" },
    { HS_F11,         1, 29851,  5050, "F11" },
    { HS_F12,         1, 31756,  5050, "F12" },

    { HS_PRTSCN,      1, 34613,  5050, "PrtSc" },
    { HS_SCROLL_LOCK, 1, 36518,  5050, "ScrLk" },
    { HS_PAUSE,       1, 38423,  5050, "Pause" },

    { HS_BACKTICK,    1,  5086,  7917, C('`') },
    { HS_1,           0,  6991,  7917, C('1') },
    { HS_2,           0,  8896,  7917, C('2') },
    { HS_3,           0, 10801,  7917, C('3') },
    { HS_4,           0, 12706,  7917, C('4') },
    { HS_5,           0, 14611,  7917, C('5') },
    { HS_6,           0, 16516,  7917, C('6') },
    { HS_7,           0, 18421,  7917, C('7') },
    { HS_8,           0, 20326,  7917, C('8') },
    { HS_9,           0, 22231,  7917, C('9') },
    { HS_0,           0, 24136,  7917, C('0') },
    { HS_MINUS,       0, 26041,  7917, C('-') },
    { HS_EQUAL,       0, 27946,  7917, C('=') },
    { HS_BACKSPACE,   3, 30803,  7917, "<-" },

    { HS_INSERT,      1, 34613,  7917, "Ins" },
    { HS_HOME,        1, 36518,  7917, "Home" },
    { HS_PAGEUP,      1, 38423,  7917, "PgUp" },

    { HS_NUMLOCK,     1, 41280,  7917, "NumLk" },
    { HS_KP_DIV,      1, 43185,  7917, C('/') },
    { HS_KP_MUL,      1, 45090,  7917, C('*') },
    { HS_KP_MINUS,    1, 46995,  7917, C('-') },

    { HS_TAB,         5,  5567,  9822, "->" },

    { HS_Q,           0,  7948,  9822, C('Q') },
    { HS_W,           0,  9853,  9822, C('W') },
    { HS_E,           0, 11758,  9822, C('E') },
    { HS_R,           0, 13663,  9822, C('R') },
    { HS_T,           0, 15568,  9822, C('T') },
    { HS_Y,           0, 17473,  9822, C('Y') },
    { HS_U,           0, 19378,  9822, C('U') },
    { HS_I,           0, 21283,  9822, C('I') },
    { HS_O,           0, 23188,  9822, C('O') },
    { HS_P,           0, 25093,  9822, C('P') },
    { HS_LBRACKET,    0, 26998,  9822, C('[') },
    { HS_RBRACKET,    0, 28903,  9822, C(']') },
    { HS_BACKSLASH,   5, 31284,  9822, C('\\') },

    { HS_DELETE,      1, 34613,  9822, "Del" },
    { HS_END,         1, 36518,  9822, "End" },
    { HS_PAGEDOWN,    1, 38423,  9822, "PgDn" },

    { HS_KP_7,        0, 41280,  9822, C('7') },
    { HS_KP_8,        0, 43184,  9822, C('8') },
    { HS_KP_9,        0, 45090,  9822, C('9') },

    { HS_KP_PLUS,     8, 46995, 10775, C('+') },

    { HS_CAPSLOCK,   13,  5800, 11727, "Caps" },
    { HS_A,           0,  8419, 11727, C('A') },
    { HS_S,           0, 10324, 11727, C('S') },
    { HS_D,           0, 12229, 11727, C('D') },
    { HS_F,           0, 14134, 11727, C('F') },
    { HS_G,           0, 16069, 11727, C('G') },
    { HS_H,           0, 17944, 11727, C('H') },
    { HS_J,           0, 19849, 11727, C('J') },
    { HS_K,           0, 21754, 11727, C('K') },
    { HS_L,           0, 23659, 11727, C('L') },
    { HS_SEMICOLON,   0, 25564, 11727, C(';') },
    { HS_APOSTROPHE,  0, 27469, 11727, C('\'') },
    { HS_ENTER,      12, 30517, 11727, "<_/" },  // Enter

    { HS_KP_4,        0, 41280, 11727, C('4') },
    { HS_KP_5,        0, 43184, 11727, C('5') },
    { HS_KP_6,        0, 45090, 11727, C('6') },

    { HS_LSHIFT,     12,  6276, 13632, "Shift" },
    { HS_Z,           0,  9371, 13632, C('Z') },
    { HS_X,           0, 11276, 13632, C('X') },
    { HS_C,           0, 13181, 13632, C('C') },
    { HS_V,           0, 15086, 13632, C('V') },
    { HS_B,           0, 16991, 13632, C('B') },
    { HS_N,           0, 18896, 13632, C('N') },
    { HS_M,           0, 20801, 13632, C('M') },
    { HS_COMMA,       0, 22706, 13632, C(',') },
    { HS_DOT,         0, 24611, 13632, C('.') },
    { HS_SLASH,       0, 26516, 13632, C('/') },
    { HS_RSHIFT,      7, 30087, 13632, "Shift" },
    { HS_UP,          0, 36518, 13632, C('^') },
    { HS_KP_1,        0, 41280, 13632, C('1') },
    { HS_KP_2,        0, 43184, 13632, C('2') },
    { HS_KP_3,        0, 45090, 13632, C('3') },

    { HS_KP_ENTER,    8, 46995, 14577, "_/" },

    { HS_LCTRL,       2,  5324, 15537, "Ctrl" },
    { HS_LALT,        2,  7705, 15537, "Alt" },
    { HS_LMETA,       2, 10086, 15537, "Meta" },
    { HS_SPACE,       9, 17182, 15537, "" },
    { HS_RALT,        2, 24278, 15537, "Alt" },
    { 0,              2, 26659, 15537, "Fn" },
    { HS_RMETA,       2, 29040, 15537, "Meta" },
    { HS_RCTRL,       2, 31421, 15537, "Ctrl" },
    { HS_LEFT,        0, 34613, 15537, C('<') },
    { HS_DOWN,        0, 36518, 15537, C('v') },
    { HS_RIGHT,       0, 38423, 15537, C('>') },
    { HS_KP_0,       10, 42232, 15537, C('0') },
    { HS_KP_DOT,      0, 45090, 15537, C('.') },
};

static key_bbox_t amiga_key_bbox[ARRAY_SIZE(amiga_keypos)];
static key_bbox_t hid_key_bbox[ARRAY_SIZE(hid_keypos)];
static keysize_t  amiga_keysize[ARRAY_SIZE(amiga_keywidths)];
static keysize_t  hid_keysize[ARRAY_SIZE(amiga_keywidths)];
static uint8_t    amiga_scancode_to_capnum[256];
static uint8_t    hid_scancode_to_capnum[256];
static uint8_t    amiga_key_mapped[ARRAY_SIZE(amiga_keypos)];
static uint8_t    hid_key_mapped[ARRAY_SIZE(hid_keypos)];

static const char cmd_options[] =
    "usage: bec <options>\n"
    "   amiga        start by capturing Amiga scancodes (-a)\n"
    "   debug        show debug output (-d)\n"
    "   euro         handle Euro style keyboard (-e)\n"
    "";

typedef struct {
    const char *const short_name;
    const char *const long_name;
} long_to_short_t;
static const long_to_short_t long_to_short_main[] = {
    { "-a", "amiga" },
    { "-d", "debug" },
    { "-e", "euro" },
};

static void
usage(void)
{
    printf("%s\n\n%s", version + 7, cmd_options);
}

const char *
long_to_short(const char *ptr, const long_to_short_t *ltos, uint ltos_count)
{
    uint cur;

    for (cur = 0; cur < ltos_count; cur++)
        if (strcmp(ptr, ltos[cur].long_name) == 0)
            return (ltos[cur].short_name);
    return (ptr);
}

static char *
strcasestr(const char *haystack, const char *needle)
{
    char ch;
    char sc;
    size_t len;

    if ((ch = *needle++) != 0) {
        ch = tolower((unsigned char) ch);
        len = strlen(needle);
        do {
            do {
                if ((sc = *haystack++) == 0)
                    return (NULL);
            } while ((char)tolower((unsigned char) sc) != ch);
        } while (strncasecmp(haystack, needle, len) != 0);
        haystack--;
    }
    return ((char *) haystack);
}

static void __attribute__((format(__printf__, 1, 2)))
err_printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    (void) vsnprintf(buf, sizeof (buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof (buf) - 1] = '\0';
    printf("%s", buf);
}


#include <proto/intuition.h>
#include <proto/graphics.h>
#include <classes/window.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>

/* Global pointers to libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *DiskfontBase  = NULL;
struct Library       *GadToolsBase  = NULL;
struct Library       *AslBase       = NULL;
extern struct ExecBase *DOSBase;
extern struct ExecBase *SysBase;
struct Window *window = NULL;
struct RastPort *rp;

/* Function to open libraries */
static BOOL
OpenAmigaLibraries(void)
{
    /* Use version 39 for 3.x compatibility */
    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary("intuition.library", 37L);
    if (IntuitionBase == NULL) {
        err_printf("Failed to open %s\n", "intuition.library");
        return (FALSE);
    }

    GfxBase = (struct GfxBase *) OpenLibrary("graphics.library", 37L);
    if (GfxBase == NULL) {
        err_printf("Failed to open %s\n", "graphics.library");
        return (FALSE);
    }

    GadToolsBase = OpenLibrary("gadtools.library", 37L);
    if (GadToolsBase == NULL) {
        err_printf("Failed to open %s\n", "gadtools.library");
        return (FALSE);
    }

    AslBase = OpenLibrary("asl.library", 37L);
    if (AslBase == NULL) {
        err_printf("Failed to open %s\n", "asl.library");
        return (FALSE);
    }
    return (TRUE);
}

/* Function to close libraries */
static void
CloseAmigaLibraries(void)
{
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    if (GfxBase) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (GadToolsBase) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
    if (AslBase) {
        CloseLibrary(AslBase);
        AslBase = NULL;
    }
}

/* Function to open the window */
static struct Window *
OpenAWindow(void)
{
    struct Screen screen_data;
    uint screen_height = 240;
    uint screen_width = 640;

    /* Get screen size */
    if (GetScreenData(&screen_data, sizeof (struct Screen),
                      WBENCHSCREEN, NULL)) {
        screen_width  = screen_data.Width;
        screen_height = screen_data.Height;
        win_width = screen_width;
        win_height = screen_height - 16;
        if (win_height > 240)  // debug
            win_height = 240;
    }
    return (OpenWindowTags(NULL,
        WA_Left, 0,
        WA_Top, 14 + 200,
        WA_MinWidth, 40,
        WA_MinHeight, 40,
        WA_MaxWidth, screen_width,
        WA_MaxHeight, screen_height,
        WA_Width, win_width,
        WA_Height, win_height,
        WA_IDCMP, IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                  IDCMP_NEWSIZE | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS |
                  IDCMP_MENUPICK,
        WA_Flags, WFLG_DRAGBAR | WFLG_CLOSEGADGET | WFLG_DEPTHGADGET |
                  WFLG_SIZEGADGET | WFLG_ACTIVATE | WFLG_REPORTMOUSE |
//                WFLG_GIMMEZEROZERO |
                  WFLG_NOCAREREFRESH,
        WA_Title,       (ULONG) "Becky Key Mapping Tool",
        WA_ScreenTitle, (ULONG) "Becky Key Mapping Tool",
        WA_NewLookMenus, TRUE,
        TAG_DONE));
}

static const struct NewMenu becky_menu[] = { // name key flags mutex userdata
    { NM_TITLE, "File",             NULL, 0,  0, NULL },
    {  NM_ITEM, "About",            NULL, 0,  0, NULL },
    {  NM_ITEM, NM_BARLABEL,        NULL, 0,  0, NULL }, // Separator bar
    {  NM_ITEM, "Load",             "L",  0,  0, NULL },
    {  NM_ITEM, "Save",             "S",  0,  0, NULL },
    {  NM_ITEM, NM_BARLABEL,        NULL, 0,  0, NULL }, // Separator bar
    {  NM_ITEM, "Quit",             "Q",  0,  0, NULL },
    { NM_TITLE, "BEC",              NULL, 0,  0, NULL },
    {  NM_ITEM, "Load from BEC",    NULL, 0,  0, NULL },
    {  NM_ITEM, "Save to BEC",      NULL, 0,  0, NULL },
    { NM_TITLE, "Mode",             NULL, 0,  0, NULL },
    {  NM_ITEM, "Live keys",        NULL, CHECKIT | MENUTOGGLE, 0, NULL },
    {  NM_ITEM, NM_BARLABEL,        NULL, 0,  0, NULL }, // Separator bar
    {  NM_ITEM, "Amiga scancode",   NULL, CHECKIT | MENUTOGGLE, 0x0f ^ 4, NULL },
    {  NM_ITEM, "HID scancode",     NULL, CHECKIT | MENUTOGGLE, 0x0f ^ 8, NULL },
    {  NM_ITEM, NM_BARLABEL,        NULL, 0,  0, NULL }, // Separator bar
    {  NM_ITEM, "Map Amiga to HID", NULL, CHECKIT | MENUTOGGLE,
                                                             0xf0 ^ 0x20, NULL },
    {  NM_ITEM, "Map HID to Amiga", NULL, CHECKIT | MENUTOGGLE,
                                                             0xf0 ^ 0x40, NULL },
    {   NM_END, NULL,               NULL, 0,  0, NULL }  // End of menu
};
#define ITEMNUM_AMIGA_SCANCODE    (SHIFTMENU(2) | SHIFTITEM(2))
#define ITEMNUM_HID_SCANCODE      (SHIFTMENU(2) | SHIFTITEM(3))
#define ITEMNUM_AMIGA_TO_HID      (SHIFTMENU(2) | SHIFTITEM(5))
#define ITEMNUM_HID_TO_AMIGA      (SHIFTMENU(2) | SHIFTITEM(6))
#define MENU_INDEX_LIVE_KEYS      11
#define MENU_INDEX_AMIGA_SCANCODE 13
#define MENU_INDEX_HID_SCANCODE   14
#define MENU_INDEX_AMIGA_TO_HID   16
#define MENU_INDEX_HID_TO_AMIGA   17
// STATIC_ASSERT(ARRAY_SIZE(becky_menu) == 16);

static struct Menu *menus = NULL;
static APTR        *visual_info;
static uint8_t      rawkey_mode = 1;
static uint8_t      capture_hid_scancodes = 1;
static uint8_t      map_hid_to_amiga = 0;

static void
create_menu(void)
{
    struct NewMenu *menu;
    menu = AllocVec(sizeof (becky_menu), MEMF_PUBLIC);
    if (menu == NULL) {
        err_printf("AllocVec failed\n");
        return;
    }
    memcpy(menu, becky_menu, sizeof (becky_menu));
    if (menu[MENU_INDEX_HID_SCANCODE].nm_MutualExclude == (0xf ^ 8)) {
        /* Add checkmark to "Live keys" menu item */
        if (rawkey_mode)
            menu[MENU_INDEX_LIVE_KEYS].nm_Flags |= CHECKED;
        /* Add checkmark to "Amiga scancode" or "HID scancode" menu item */
        if (capture_hid_scancodes == 0)
            menu[MENU_INDEX_AMIGA_SCANCODE].nm_Flags |= CHECKED;
        else
            menu[MENU_INDEX_HID_SCANCODE].nm_Flags |= CHECKED;

        /* Add checkmark to "Amiga to HID" or "HID to Amiga" menu item */
        if (map_hid_to_amiga == 0)
            menu[MENU_INDEX_AMIGA_TO_HID].nm_Flags |= CHECKED;
        else
            menu[MENU_INDEX_HID_TO_AMIGA].nm_Flags |= CHECKED;
    } else {
        err_printf("Bug: becky_menu changed\n");
    }

    visual_info = GetVisualInfo(window->WScreen, TAG_END);
    if (visual_info != NULL) {
        menus = CreateMenus(menu, TAG_END);
        if (menus != NULL) {
            LayoutMenus(menus, visual_info, GTMN_NewLookMenus, TRUE, TAG_END);
            SetMenuStrip(window, menus);
        }
    }
}

static void
close_menu(void)
{
    if (menus != NULL) {
        FreeMenus(menus);
        menus = NULL;
    }
    if (visual_info != NULL)
        FreeVisualInfo(visual_info);
}

static void
scale_key_dimensions(void)
{
    uint cur;
    uint mul_x = win_width - 8;
    uint mul_y = kbd_height;  // Need space for two keyboards
    uint div_x = 25 * U * 2;  // Room for about 25 columns of keys
    uint div_y = 9 * U * 2;   // Room for about 9 rows of keys
    /* 2 above is because the dimensions are +/- pixels from center */

    for (cur = 0; cur < ARRAY_SIZE(amiga_keywidths); cur++) {
        amiga_keysize[cur].x = amiga_keywidths[cur].x * mul_x / div_x;
        amiga_keysize[cur].y = amiga_keywidths[cur].y * mul_y / div_y;
        amiga_keysize[cur].shaded = amiga_keywidths[cur].shaded;
    }

    for (cur = 0; cur < ARRAY_SIZE(hid_keywidths); cur++) {
        hid_keysize[cur].x = hid_keywidths[cur].x * mul_x / div_x;
        hid_keysize[cur].y = hid_keywidths[cur].y * mul_y / div_y;
        hid_keysize[cur].shaded = hid_keywidths[cur].shaded;
    }

    /*
     * For North American keyboards, need to remove Extra1 and Extra2
     * and make left shift key wider. Rendered will need to deal with
     * non-square Return key.
     */
    if (is_northamerican) {
        amiga_keysize[11].shaded = 0xff;    // Extra keys = invisible
        amiga_keysize[13].shaded = 0xff;    // European Enter = invisible
        amiga_keysize[6].x += 1 * U * mul_x / div_x;  // Left shitt
    } else {
        amiga_keysize[12].shaded = 0xff;    // North American Enter = invisible
    }
}

static void
box(struct RastPort *rp, SHORT xmin, SHORT ymin, SHORT xmax, SHORT ymax)
{
    Move(rp, xmin, ymin);
    Draw(rp, xmax, ymin);
    Draw(rp, xmax, ymax);
    Draw(rp, xmin, ymax);
    Draw(rp, xmin, ymin);
}

static SHORT amiga_enter_wxmax;
static SHORT amiga_enter_ymid;

static void
box_enterkey_euro(struct RastPort *rp, SHORT xpos, SHORT ypos,
                  SHORT wx, SHORT wy)
{
    SHORT ymin  = ypos - wy;
    SHORT ymax  = ypos + wy;
    SHORT xmax  = xpos + wx;
    SHORT xmin  = xpos - amiga_enter_wxmax;

    Move(rp, xmin, ymin);
    Draw(rp, xmax, ymin);
    Draw(rp, xmax, ymax);
    Draw(rp, xpos - wx, ymax);
    Draw(rp, xpos - wx, amiga_enter_ymid);
    Draw(rp, xmin, amiga_enter_ymid);
    Draw(rp, xmin, ymin);
}

static void
box_enterkey_america(struct RastPort *rp, SHORT xpos, SHORT ypos,
                     SHORT wx, SHORT wy)
{
    SHORT ymin  = ypos - wy;
    SHORT ymax  = ypos + wy;
    SHORT xmax  = xpos + wx;
    SHORT xmin  = xpos - amiga_enter_wxmax;

    Move(rp, xpos - wx, ymin);
    Draw(rp, xmax, ymin);
    Draw(rp, xmax, ymax);
    Draw(rp, xmin, ymax);
    Draw(rp, xmin, amiga_enter_ymid);
    Draw(rp, xpos - wx, amiga_enter_ymid);
    Draw(rp, xpos - wx, ymin);
}

static struct TextFont *keycap_font = NULL;

/* Pen colors */
static BYTE pen_cap_white;             // White key cap
static BYTE pen_cap_shaded;            // Shaded key cap
static BYTE pen_cap_pressed;           // Pressed key cap
static BYTE pen_cap_text;              // Keycap text
static BYTE pen_cap_text_pressed;      // Keycap text when pressed
static BYTE pen_cap_outline_lo;        // Normal outline around cap
static BYTE pen_cap_outline_hi;        // Highlighted outline around cap
static BYTE pen_keyboard_background;   // Keyboard case color
static BYTE pen_status_fg;             // Black
static BYTE pen_status_bg;             // White

static void
select_pens(void)
{
    switch (rp->BitMap->Depth) {
        case 1:
            /* Two colors available:
             *    0 = Black
             *    1 = White
             */
            pen_cap_white           = 0;  // White
            pen_cap_shaded          = 0;  // White
            pen_cap_pressed         = 1;  // Black
            pen_cap_text            = 1;  // Black
            pen_cap_text_pressed    = 0;  // White
            pen_cap_outline_lo      = 0;  // White
            pen_cap_outline_hi      = 1;  // Black
            pen_keyboard_background = 1;  // Background color
            pen_status_fg           = 1;  // Black
            pen_status_bg           = 0;  // White
            break;
        case 0:
        case 2:
            /*
             * Four colors available. Workbench default:
             *    0 = Gray
             *    1 = White
             *    2 = Black
             *    3 = Light Blue
             */
            pen_cap_white           = 2;  // White
            pen_cap_shaded          = 0;  // Background color
            pen_cap_pressed         = 3;  // Blue
            pen_cap_text            = 1;  // Black
            pen_cap_text_pressed    = 1;  // Black
            pen_cap_outline_lo      = 1;  // Black
            pen_cap_outline_hi      = 3;  // Blue
            pen_keyboard_background = 0;  // Background color
            pen_status_fg           = 1;  // Black
            pen_status_bg           = 2;  // White
            break;
        default:
            /*
             * Eight or more colors available. Workbench default:
             *    0 = Gray
             *    1 = Black
             *    2 = White
             *    3 = Blue
             *    4 = Red
             *    5 = Green
             *    6 = Dark Blue
             *    7 = Orange
             */
            pen_cap_white           = 2;  // White
            pen_cap_shaded          = 0;  // Background color
            pen_cap_pressed         = 3;  // Blue
            pen_cap_text            = 1;  // Black
            pen_cap_text_pressed    = 1;  // Black
            pen_cap_outline_lo      = 1;  // Black
            pen_cap_outline_hi      = 4;  // Red
            pen_keyboard_background = 6;  // Dark Blue
            pen_status_fg           = 1;  // Black
            pen_status_bg           = 2;  // White
            break;
    }
}

static void
open_font(void)
{
    struct TextAttr font_attr = {
        "topaz.font",
        8,
        FS_NORMAL,
        FPF_ROMFONT
    };
    DiskfontBase = OpenLibrary("diskfont.library", 0);
    if (DiskfontBase == NULL)
        return;

    keycap_font = OpenDiskFont(&font_attr);
    if (keycap_font) {
        SetFont(rp, keycap_font); // Set the new font
    } else {
        err_printf("Could not open topaz.font 8; using default\n");
    }
}

static void
close_font(void)
{
    if (keycap_font != NULL)
        CloseFont(keycap_font);
    if (DiskfontBase != NULL) {
        CloseLibrary(DiskfontBase);
        DiskfontBase = NULL;
    }
}

static void
center_text(struct RastPort *rp, SHORT pos_x, SHORT pos_y, SHORT max_x,
            const char *str)
{
    struct TextFont *tf = rp->Font;
    SHORT height = tf->tf_YSize;
    SHORT width = 0;
    uint len;
    char buf[8];

    /* Caution: str might not be a string! */
    if ((uintptr_t) str < 0x100) {
        /* Single character */
        buf[0] = (uintptr_t) str;
        buf[1] = '\0';
        str = buf;
    }
    len = strlen(str);
    width = TextLength(rp, str, len);
    while ((len > 1) && (width >= max_x + 2)) {
        len--;
        width = TextLength(rp, str, len);
    }
    Move(rp, pos_x - width / 2, pos_y + height / 2 - 1);
    Text(rp, str, len);
}

static void __attribute__((format(__printf__, 1, 2)))
gui_printf(const char *fmt, ...)
{
    static SHORT width_last;
    SHORT pos_x = window->BorderLeft + 4;
    SHORT pos_y = hid_keyboard_top - 4;
    SHORT width;
    uint len;
    char buf[80];
    va_list args;
    va_start(args, fmt);
    (void) vsnprintf(buf, sizeof (buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof (buf) - 1] = '\0';
    len = strlen(buf);
    if (len == 0)
        return;
    SetAPen(rp, pen_status_fg);
    SetBPen(rp, pen_status_bg);
    Move(rp, pos_x, pos_y);
    Text(rp, buf, len);
    width = TextLength(rp, buf, len);
    if (width_last > width) {
        SetAPen(rp, 0);
        RectFill(rp, pos_x + width, pos_y - 7, pos_x + width_last, pos_y + 1);
    }
    width_last = width;
}

static void
draw_amiga_key(uint cur, uint pressed)
{
    const keypos_t *ke = &amiga_keypos[cur];
    uint8_t scancode = amiga_keypos[cur].scancode;
    uint ktype = amiga_keypos[cur].type;
    uint pos_x;
    uint pos_y;
    uint ke_x = ke->x;
    uint ke_y = ke->y;
    uint wx = amiga_keysize[ktype].x;
    uint wy = amiga_keysize[ktype].y;
    uint shaded;
    uint keycap_fg_pen;
    uint keycap_bg_pen;

    if (is_northamerican && (scancode == AS_LEFTSHIFT)) {
        ke_x += 1905 / 2;  // Increase width of North American left shift
    }
    if (amiga_keywidths[ktype].y > 1 * U) {
        ke_y += 1905 / 2;  // Fixup center for tall keys
    }

    pos_x = (ke_x  - AKB_MIN_X) * kbd_width / AKB_MAX_X + amiga_keyboard_left;
    pos_y = (ke_y - AKB_MIN_Y) * kbd_height / AKB_MAX_Y + amiga_keyboard_top;

    if (amiga_keysize[amiga_keypos[cur].type].shaded == 0xff)
        return;  // Key not present in this keymap

    if (pressed) {
        shaded = 3;
    } else if (!amiga_key_mapped[cur]) {
        shaded = 2;
    } else {
        shaded = amiga_keysize[amiga_keypos[cur].type].shaded;
    }

    switch (shaded) {
        default:
        case 0:  // Normal white key
            keycap_fg_pen = pressed ? pen_cap_text_pressed : pen_cap_text;
            keycap_bg_pen = pen_cap_white;
            break;
        case 1:  // Normal shaded key
            keycap_fg_pen = pressed ? pen_cap_text_pressed : pen_cap_text;
            keycap_bg_pen = pen_cap_shaded;
            break;
        case 2:  // Key that has not been mapped (Black)
            keycap_fg_pen = pen_cap_white;
            keycap_bg_pen = pressed ? pen_cap_text_pressed : pen_cap_text;
            break;
        case 3:  // Key that has been pressed (Blue)
            keycap_fg_pen = pen_cap_text;
            keycap_bg_pen = pen_cap_pressed;
            break;
    }

    SetAPen(rp, keycap_bg_pen);
    if (scancode == AS_ENTER) {
        /* Special rendering for non-rectangular Enter */
        if (is_northamerican && (ktype == 12)) {
            amiga_enter_ymid = pos_y + wy * 10 / 100;
            amiga_enter_wxmax = wx * 215 / 100;
            RectFill(rp, pos_x - wx, pos_y - wy, pos_x + wx, pos_y + wy);
            RectFill(rp, pos_x - amiga_enter_wxmax, amiga_enter_ymid,
                     pos_x, pos_y + wy);
            SetAPen(rp, pen_cap_outline_lo);
            box_enterkey_america(rp, pos_x, pos_y, wx, wy);
        } else if (!is_northamerican && (ktype == 13)) {
            amiga_enter_ymid = pos_y - wy * 10 / 100;
            amiga_enter_wxmax = wx * 140 / 100;
            RectFill(rp, pos_x - wx, pos_y - wy, pos_x + wx, pos_y + wy);
            RectFill(rp, pos_x - amiga_enter_wxmax, pos_y - wy,
                     pos_x, amiga_enter_ymid);
            SetAPen(rp, pen_cap_outline_lo);
            box_enterkey_euro(rp, pos_x, pos_y, wx, wy);
        }
        SetAPen(rp, keycap_fg_pen);
        SetBPen(rp, keycap_bg_pen);
        center_text(rp, pos_x, pos_y, wx * 2, amiga_keypos[cur].name);
    } else {
        RectFill(rp, pos_x - wx, pos_y - wy, pos_x + wx, pos_y + wy);
        SetAPen(rp, keycap_fg_pen);
        SetBPen(rp, keycap_bg_pen);
        center_text(rp, pos_x, pos_y, wx * 2, amiga_keypos[cur].name);
        SetAPen(rp, pen_cap_outline_lo);
        box(rp, pos_x - wx, pos_y - wy, pos_x + wx, pos_y + wy);
    }
    amiga_key_bbox[cur].x_min = pos_x - wx;
    amiga_key_bbox[cur].y_min = pos_y - wy;
    amiga_key_bbox[cur].x_max = pos_x + wx;
    amiga_key_bbox[cur].y_max = pos_y + wy;
}

static void
draw_hid_key(uint cur, uint pressed)
{
    const keypos_t *ke = &hid_keypos[cur];
    uint shaded = hid_keysize[hid_keypos[cur].type].shaded;

    uint pos_x = (ke->x - HIDKB_MIN_X) * kbd_width / HIDKB_MAX_X + hid_keyboard_left;
    uint pos_y = (ke->y - HIDKB_MIN_Y) * kbd_height / HIDKB_MAX_Y +
                 hid_keyboard_top + hid_keysize[0].y * 2;
    uint ktype = hid_keypos[cur].type;
    uint wx = hid_keysize[ktype].x;
    uint wy = hid_keysize[ktype].y;
    uint keycap_fg_pen;
    uint keycap_bg_pen;

    if (hid_keysize[hid_keypos[cur].type].shaded == 0xff)
        return;  // Key not present in this keymap

    if (pressed) {
        shaded = 3;
    } else if (!hid_key_mapped[cur]) {
        shaded = 2;
    } else {
        shaded = hid_keysize[hid_keypos[cur].type].shaded;
    }

    switch (shaded) {
        default:
        case 0:  // Normal white key
            keycap_fg_pen = pressed ? pen_cap_text_pressed : pen_cap_text;
            keycap_bg_pen = pen_cap_white;
            break;
        case 1:  // Normal shaded key
            keycap_fg_pen = pressed ? pen_cap_text_pressed : pen_cap_text;
            keycap_bg_pen = pen_cap_shaded;
            break;
        case 2:  // Key that has not been mapped (Black)
            keycap_fg_pen = pen_cap_white;
            keycap_bg_pen = pressed ? pen_cap_text_pressed : pen_cap_text;
            break;
        case 3:  // Key that has been pressed (Blue)
            keycap_fg_pen = pen_cap_text;
            keycap_bg_pen = pen_cap_pressed;
            break;
    }

    SetAPen(rp, keycap_bg_pen);
    RectFill(rp, pos_x - wx, pos_y - wy, pos_x + wx, pos_y + wy);
    SetAPen(rp, keycap_fg_pen);
    SetBPen(rp, keycap_bg_pen);
    center_text(rp, pos_x, pos_y, wx * 2, hid_keypos[cur].name);
    SetAPen(rp, pen_cap_outline_lo);
    box(rp, pos_x - wx, pos_y - wy, pos_x + wx, pos_y + wy);
    hid_key_bbox[cur].x_min = pos_x - wx;
    hid_key_bbox[cur].y_min = pos_y - wy;
    hid_key_bbox[cur].x_max = pos_x + wx;
    hid_key_bbox[cur].y_max = pos_y + wy;
}

static BOOL
draw_win(void)
{
    uint cur;

    win_width = window->Width - window->BorderLeft - window->BorderRight + 3;
    win_height = window->Height;

    kbd_width     = win_width - 4;
    kbd_height    = win_height * 8 / 20;

    scale_key_dimensions();

    /* Draw Amiga keyboard case */
    uint draw_kbd_height = kbd_height * 18 / 20;
    uint win_left = window->BorderLeft;
    uint win_right = win_width;
    hid_keyboard_top = win_height - kbd_height + window->BorderBottom;

    SetAPen(rp, pen_keyboard_background);
    RectFill(rp, win_left, window->BorderTop,
             win_right, window->BorderTop + draw_kbd_height);

    /* Empty area between keyboards */
    SetAPen(rp, 0);
    RectFill(rp, win_left, window->BorderTop + draw_kbd_height,
             win_right, hid_keyboard_top);
    /* Draw HID keyboard case */
    SetAPen(rp, pen_keyboard_background);
    RectFill(rp, win_left, hid_keyboard_top,
             win_right, win_height - window->BorderBottom);

    SetAPen(rp, pen_cap_white);
    box(rp, win_left, window->BorderTop,
        win_right, window->BorderTop + draw_kbd_height);
    SetAPen(rp, pen_cap_white);
    box(rp, win_left, hid_keyboard_top,
        win_right, win_height - window->BorderBottom);

    /* Draw Amiga keyboard */
    amiga_keyboard_left = window->BorderLeft + amiga_keysize[0].x + 4;
    amiga_keyboard_top  = window->BorderTop  + amiga_keysize[0].y + 4;

    for (cur = 0; cur < ARRAY_SIZE(amiga_keypos); cur++)
        draw_amiga_key(cur, 0);

    /* Draw HID keyboard */
    hid_keyboard_left = window->BorderLeft + hid_keysize[0].x + 4;

    for (cur = 0; cur < ARRAY_SIZE(hid_keypos); cur++)
        draw_hid_key(cur, 0);

    return (TRUE);
}

static void
draw_amiga_key_box(uint cur, uint do_mark)
{
    uint8_t scancode = amiga_keypos[cur].scancode;
    SetAPen(rp, do_mark ? pen_cap_outline_hi : pen_cap_outline_lo);
    if (scancode == AS_ENTER) {
        /* Special rendering for non-rectangular Enter */
        uint ktype = amiga_keypos[cur].type;
        uint wx = amiga_keysize[ktype].x;
        uint wy = amiga_keysize[ktype].y;

        if (is_northamerican && (ktype == 12)) {
            box_enterkey_america(rp, amiga_key_bbox[cur].x_min + wx,
                              amiga_key_bbox[cur].y_min + wy, wx, wy);
        } else if (!is_northamerican && (ktype == 13)) {
            box_enterkey_euro(rp, amiga_key_bbox[cur].x_min + wx,
                              amiga_key_bbox[cur].y_min + wy, wx, wy);
        }
    } else {
        box(rp, amiga_key_bbox[cur].x_min,
                amiga_key_bbox[cur].y_min,
                amiga_key_bbox[cur].x_max,
                amiga_key_bbox[cur].y_max);
    }
}

static void
draw_hid_key_box(uint cur, uint do_mark)
{
    SetAPen(rp, do_mark ? pen_cap_outline_hi : pen_cap_outline_lo);
    box(rp, hid_key_bbox[cur].x_min,
            hid_key_bbox[cur].y_min,
            hid_key_bbox[cur].x_max,
            hid_key_bbox[cur].y_max);
}

static void
enter_leave_amiga_key(uint pos, uint do_mark)
{
    uint8_t amiga_scancode = amiga_keypos[pos].scancode;
    uint8_t match_scancode;
    char    printbuf[80];
    char   *printbufptr = printbuf;
    uint    cur;
    uint    map;
    uint8_t hid_cur;

    draw_amiga_key_box(pos, do_mark);
    for (cur = 0; cur < ARRAY_SIZE(hid_scancode_to_amiga); cur++) {
        for (map = 0; map < ARRAY_SIZE(hid_scancode_to_amiga[0]); map++) {
            match_scancode = hid_scancode_to_amiga[cur][map];
            if (match_scancode == 0xff)
                break;  // End of list
            if ((match_scancode == 0) && (map > 0))
                continue;
            if (amiga_scancode == match_scancode) {
                if (do_mark) {
                    sprintf(printbufptr, " %02x", cur);
                    printbufptr += strlen(printbufptr);
                }
                hid_cur = hid_scancode_to_capnum[cur];
                if (hid_cur != 0xff)
                    draw_hid_key_box(hid_cur, do_mark);
                break;
            }
        }
    }
    if (do_mark) {
        *printbufptr = '\0';
        gui_printf("Amiga %02x <-%s", amiga_scancode, printbuf);
    }
}

static void
enter_leave_hid_key(uint pos, uint do_mark)
{
    uint8_t hid_scancode = hid_keypos[pos].scancode;
    uint8_t amiga_scancode;
    uint    amigakey;
    uint    map;
    char    printbuf[80];
    char   *printbufptr = printbuf;

    draw_hid_key_box(pos, do_mark);
    for (map = 0; map < ARRAY_SIZE(hid_scancode_to_amiga[0]); map++) {
        amiga_scancode = hid_scancode_to_amiga[hid_scancode][map];
        if (amiga_scancode == 0xff)
            break;  // End of list
        if ((amiga_scancode == 0) && (map > 0))
            continue;
        if (do_mark) {
            sprintf(printbufptr, " %02x", amiga_scancode);
            printbufptr += strlen(printbufptr);
        }
        amigakey = amiga_scancode_to_capnum[amiga_scancode];
        if (amigakey != 0xff)
            draw_amiga_key_box(amigakey, do_mark);
    }
    if (do_mark) {
        *printbufptr = '\0';
        gui_printf("HID %02x ->%s", hid_scancode, printbuf);
    }
}

static void
mouse_move(SHORT x, SHORT y)
{
    static uint8_t last_cur = 0xff;
    static uint8_t last_was_amiga = 0;
    uint cur;
    if ((x < 0) || (y < 0))
        return;

    x += window->BorderLeft;
    y += window->BorderTop;

    for (cur = 0; cur < ARRAY_SIZE(amiga_key_bbox); cur++) {
        if (amiga_key_bbox[cur].y_max < y)
            continue;
        if (amiga_key_bbox[cur].y_min > y)
            break;  // Not found
        if (amiga_key_bbox[cur].x_max < x)
            continue;
        if (amiga_key_bbox[cur].x_min > x) {
            if ((amiga_keypos[cur].scancode == AS_ENTER) ||
                (amiga_keypos[cur].scancode == AS_KP_ENTER)) {
                continue;  // Key spans multiple rows
            }
            break;  // Not found
        }

        if (amiga_keysize[amiga_keypos[cur].type].shaded == 0xff)
            continue;  // Key not present in this keymap

        /* Inside a box */

        if ((last_cur == cur) && last_was_amiga)
            return;  // Still in the same box

        if (last_cur != 0xff) {
            /* Redraw original bounding box */
            if (last_was_amiga)
                enter_leave_amiga_key(last_cur, 0);
            else
                enter_leave_hid_key(last_cur, 0);
        }
        /* Draw new highlight bounding box */
        enter_leave_amiga_key(cur, 1);
        last_cur = cur;
        last_was_amiga = 1;
        return;
    }

    for (cur = 0; cur < ARRAY_SIZE(hid_key_bbox); cur++) {
        if (hid_key_bbox[cur].y_max < y)
            continue;
        if (hid_key_bbox[cur].y_min > y)
            break;  // Not found
        if (hid_key_bbox[cur].x_max < x)
            continue;
        if (hid_key_bbox[cur].x_min > x) {
            if ((hid_keypos[cur].scancode == HS_KP_PLUS) ||
                (hid_keypos[cur].scancode == HS_KP_ENTER))
                continue;  // Key spans multiple rows
            break;  // Not found
        }

        /* Inside a box */
        if ((last_cur == cur) && !last_was_amiga)
            return;  // Still in the same box

        if (last_cur != 0xff) {
            /* Redraw original bounding box */
            if (last_was_amiga)
                enter_leave_amiga_key(last_cur, 0);
            else
                enter_leave_hid_key(last_cur, 0);
        }
        /* Draw new highlight bounding box */
        enter_leave_hid_key(cur, 1);
        last_cur = cur;
        last_was_amiga = 0;
        return;
    }

//  printf("%d %d\n", x, y);

    /* Mouse is not in a bounding box */
    if (last_cur != 0xff) {
        /* Redraw original bounding box */
        if (last_was_amiga)
            enter_leave_amiga_key(last_cur, 0);
        else
            enter_leave_hid_key(last_cur, 0);
        last_cur = 0xff;
    }
}

uint
find_amiga_scancode_keypos(uint8_t scancode)
{
    uint cur;
    for (cur = 0; cur < ARRAY_SIZE(amiga_keypos); cur++)
        if (amiga_keypos[cur].scancode == scancode)
            return (cur);
    return (0xff);
}

static void
amiga_rawkey(uint8_t code)
{
    uint           cur;
    uint           scancode = code & ~0x80;
    static uint8_t last_code = 0xff;

    switch (rawkey_mode) {
        case 0:
            /* Default mode is last key pressed stays highlighted */
            if (code & 0x80)
                return;
            cur = find_amiga_scancode_keypos(scancode);
            if ((last_code != cur) && (last_code != 0xff))
                draw_amiga_key(last_code, 0);
            if (cur != 0xff)
                draw_amiga_key(cur, 1);
            last_code = cur;
            break;
        case 1:
            /* Just highlight all keys which are currently pressed */
            cur = find_amiga_scancode_keypos(scancode);
            if (cur != 0xff) {
                if (code & 0x80)
                    draw_amiga_key(cur, 0);
                else
                    draw_amiga_key(cur, 1);
            }
            break;
    }
}

static void
unmap_all_keycaps(void)
{
    uint cap;
    memset(amiga_key_mapped, 0, sizeof (amiga_key_mapped));
    memset(hid_key_mapped, 0, sizeof (hid_key_mapped));
    for (cap = 0; cap < ARRAY_SIZE(amiga_keypos); cap++)
        draw_amiga_key(cap, 0);
    for (cap = 0; cap < ARRAY_SIZE(hid_keypos); cap++)
        draw_hid_key(cap, 0);
}

static void
get_bec_keymap(void)
{
    bec_keymap_t  req;
    bec_keymap_t *reply;
    uint          rc;
    uint          pos;
    uint          maxpos;
    uint          cur;
    uint          rlen;
    uint          key;
    uint          maxkeys;
    uint8_t       replybuf[256];
    uint8_t      *data;
    req.bkm_which = BKM_WHICH_KEYMAP;
    req.bkm_start = 0;
    req.bkm_len   = 0;
    req.bkm_count = 128;

    for (cur = 0; cur < 256; ) {
        req.bkm_start = cur;
        rc = send_cmd_retry(BEC_CMD_GET_MAP, &req, sizeof (req),
                            replybuf, sizeof (replybuf), &rlen);
        if (rc != 0) {
            gui_printf("BEC get map fail: %s", bec_err(rc));
            return;
        }
        if (rlen < sizeof (*reply)) {
            gui_printf("Got bad map reply from BEC: %u", rlen);
            return;
        }
        rlen -= sizeof (bec_keymap_t *);
        if (rlen == 0) {
            gui_printf("BEC map ended early: missing %u and higher", cur);
            return;
        }
        reply = (bec_keymap_t *) replybuf;
        data = (void *) (reply + 1);
        maxkeys = reply->bkm_len;
        if (maxkeys > ARRAY_SIZE(hid_scancode_to_amiga[256]))
            maxkeys = ARRAY_SIZE(hid_scancode_to_amiga[256]);
        maxpos = reply->bkm_count;
        if (maxpos > NUM_SCANCODES - cur)
            maxpos = NUM_SCANCODES - cur;
        gui_printf("Got %u ents at %u from BEC",
                   reply->bkm_count, reply->bkm_start);
        for (pos = 0; pos < maxpos; pos++) {
            uint cap;
            for (key = 0; key < maxkeys; key++) {
                cap = amiga_scancode_to_capnum[*data];
                hid_scancode_to_amiga[cur][key] = *data;
                if (amiga_key_mapped[cap] == 0) {
                    amiga_key_mapped[cap] = 1;
                    draw_amiga_key(cap, 0);
                }
                data++;
            }
            if (maxkeys < reply->bkm_len) {
                /* BEC per-key map is smaller than STM32 map */
                data += reply->bkm_len - maxkeys;
            } else if (maxkeys > reply->bkm_len) {
                /* BEC per-key map is larger than STM32 map */
                memset(&hid_scancode_to_amiga[cur][key], 0,
                       maxkeys - reply->bkm_len);
            }
            cap = hid_scancode_to_capnum[cur];
            if (hid_key_mapped[cap] == 0) {
                hid_key_mapped[cap] = 1;
                draw_hid_key(cap, 0);
            }
            cur++;
        }
    }
    gui_printf("Done loading keymap from BEC");
}

static void
save_bec_keymap(void)
{
    bec_keymap_t *req;
    uint          rc;
    uint          pos;
    uint          maxpos = 60;
    uint          cur;
    uint          rlen;
    uint          key;
    uint          maxkeys = 4;
    uint8_t       sendbuf[256];
    uint8_t      *data;
    uint          sendlen;

    req = (void *)sendbuf;
    if (maxpos > (sizeof (sendbuf) - sizeof (*req) - 10) / 4)
        maxpos = (sizeof (sendbuf) - sizeof (*req) - 10);

    for (cur = 0; cur < 256; ) {
        data = (void *) (req + 1);
        if (maxpos > 256 - cur)
            maxpos = 256 - cur;
        req->bkm_which = BKM_WHICH_KEYMAP;
        req->bkm_start = cur;
        req->bkm_len   = maxkeys;
        req->bkm_count = maxpos;
        for (pos = 0; pos < maxpos; pos++) {
            for (key = 0; key < maxkeys; key++)
                *(data++) = hid_scancode_to_amiga[cur][key];
            cur++;
        }
        sendlen = (uintptr_t) data - (uintptr_t) sendbuf;
        rc = send_cmd(BEC_CMD_SET_MAP, sendbuf, sendlen, NULL, 0, &rlen);
        if (rc != 0) {
            gui_printf("BEC set map fail: %s", bec_err(rc));
            return;
        }
        gui_printf("Sent %u ents at %u to BEC",
                   req->bkm_count, req->bkm_start);
    }
    gui_printf("Done saving keymap to BEC");
}

static void
load_keymap_from_file(void)
{
    char                 *kptr;
    char                 *ptr;
    FILE                 *fp;
    struct FileRequester *req;
    char   linebuf[300];
    char   full_path[300];
    uint   line;
    uint   err_count = 0;
    req = AllocAslRequestTags(ASL_FileRequest,
                              ASLFR_Window,        (ULONG)window,
                              ASLFR_TitleText,     (ULONG)"Load File...",
                              ASLFR_PositiveText,  (ULONG)"Load",
                              ASLFR_InitialFile,   (ULONG)"bec_keymap.txt",
//                            ASLFR_InitialDrawer, (ULONG)"RAM:",
                              TAG_DONE);
    if (req == NULL)
        return;

    if (AslRequestTags(req, TAG_DONE) == 0) {
        /* User Canceled */
        return;
    }
    strcpy(full_path, req->fr_Drawer);
    AddPart(full_path, req->rf_File, sizeof (full_path));
    FreeAslRequest(req);

    /* Open file for Load... */
    fp = fopen(full_path, "r");
    if (fp == NULL) {
        gui_printf("Failed to open %s", full_path);
        return;
    }
    unmap_all_keycaps();

    line = 0;
    while (fgets(linebuf, sizeof (linebuf) - 1, fp) != NULL) {
        linebuf[sizeof (linebuf) - 1] = '\0';
        line++;
        if ((ptr = strchr(linebuf, '#')) != NULL)
            *ptr = '\0';
        if ((ptr = strchr(linebuf, '\n')) != NULL)
            *ptr = '\0';
        if ((ptr = strcasestr(linebuf, "MAP")) != NULL) {
            kptr = strcasestr(ptr + 3, "KEY");
            if (kptr != NULL) {
                int  pos = 0;
                uint hid_code;
                uint amiga_code;
                uint sc_count = 0;
                uint hcap;
                uint acap;
                kptr += 3;
                if ((sscanf(kptr, "%x%n", &hid_code, &pos) != 1) ||
                    ((kptr[pos] != ' ') && (kptr[pos] != '\0')) ||
                    (hid_code > 0xff)) {
                    err_printf("%u: Invalid HID scancode \"%.*s\":\n%s\n",
                               line, pos, kptr, linebuf);
load_parse_error:
                    if (err_count++ > 8) {
                        err_printf("Too many errors; giving up\n");
                        break;
                    }
                    continue;
                }

                ptr = strcasestr(kptr + pos, " TO ");
                if (ptr == NULL) {
                    err_printf("%u: missing \"TO\" in MAP KEY command:\n:%s\n",
                               line, linebuf);
                    goto load_parse_error;
                }
                kptr = ptr + 4;
                amiga_code = 0xff;
                while (sc_count < 5) {
                    if ((sscanf(kptr, "%x%n", &amiga_code, &pos) != 1) ||
                        ((kptr[pos] != ' ') && (kptr[pos] != '\0')) ||
                        (amiga_code > 0xff)) {
                        ptr = kptr;
                        while (*ptr == ' ')
                            ptr++;
                        if (*ptr == '\0')
                            break;  // End of linej

                        err_printf("%u: Invalid Amiga scancode \"%.*s\":\n%s\n",
                                   line, pos, kptr, linebuf);
                        goto load_parse_error;
                    }
                    if (sc_count == 4) {
                        err_printf("%u: too many Amiga scancodes at %x:\n%s\n",
                                   line, amiga_code, linebuf);
                        goto load_parse_error;
                    }
                    hid_scancode_to_amiga[hid_code][sc_count] = amiga_code;
                    acap = amiga_scancode_to_capnum[amiga_code];
                    if (amiga_key_mapped[acap] == 0) {
                        amiga_key_mapped[acap] = 1;
                        draw_amiga_key(acap, 0);
                    }
                    sc_count++;
                    kptr += pos;
                }
                hcap = hid_scancode_to_capnum[hid_code];
                if (hid_key_mapped[hcap] == 0) {
                    hid_key_mapped[hcap] = 1;
                    draw_hid_key(hcap, 0);
                }
            }
        }
    }
#if 0
    /* Do the Fn key for completeness */
    hid_key_mapped[cap] = 0;
    draw_hid_key(0, 0);
#endif
    fclose(fp);
    gui_printf("Read keymap from %s", full_path);
    // XXX: open file
}

static void
save_keymap_to_file(void)
{
    FILE                 *fp;
    struct FileRequester *req;
    struct tm            *timeinfo;
    time_t now;
    char   full_path[300];
    uint   cur;
    uint   map;
    req = AllocAslRequestTags(ASL_FileRequest,
                              ASLFR_Window,        (ULONG)window,
                              ASLFR_TitleText,     (ULONG)"Save File As...",
                              ASLFR_PositiveText,  (ULONG)"Save",
                              ASLFR_InitialFile,   (ULONG)"bec_keymap.txt",
                              ASLFR_DoSaveMode,    TRUE,
//                            ASLFR_InitialDrawer, (ULONG)"RAM:",
                              TAG_DONE);
    if (req == NULL)
        return;

    if (AslRequestTags(req, TAG_DONE) == 0) {
        /* User Canceled */
        return;
    }
    strcpy(full_path, req->fr_Drawer);
    AddPart(full_path, req->rf_File, sizeof (full_path));
    FreeAslRequest(req);

    /* Open file for Save... */
    fp = fopen(full_path, "w");
    if (fp == NULL) {
        gui_printf("Failed to open %s", full_path);
        return;
    }
    time(&now);
    timeinfo = localtime(&now);
    fprintf(fp, "#\n"
                "# BEC HID keymap by Becky %s\n"
                "# %s\n"
                "#\n", VERSION, asctime(timeinfo));
    for (cur = 0; cur < ARRAY_SIZE(hid_scancode_to_amiga); cur++) {
        uint  printed = 0;
        char  amiga_buf[32];
        char *amiga_buf_ptr = amiga_buf;

        for (map = 0; map < ARRAY_SIZE(hid_scancode_to_amiga[0]); map++) {
            uint8_t amiga_scancode = hid_scancode_to_amiga[cur][map];
            if (amiga_scancode == 0xff)
                break;  // End of list
            if ((amiga_scancode == 0) && (map > 0))
                continue;
            if (printed++ == 0)
                fprintf(fp, "MAP KEY %02x TO", cur);
            fprintf(fp, " %02x", amiga_scancode);

            uint cap = amiga_scancode_to_capnum[amiga_scancode];
            if (cap != 0xff) {
                char *name = amiga_keypos[cap].name;
                char buf[8];
                if ((uintptr_t) name < 0x100) {
                    /* Single character */
                    buf[0] = (uintptr_t) name;
                    buf[1] = '\0';
                    name = buf;
                }
                sprintf(amiga_buf_ptr, " \"%s\"", name);
                amiga_buf_ptr += strlen(amiga_buf_ptr);
            }
        }
        if (printed) {
            uint cap = hid_scancode_to_capnum[cur];
            fprintf(fp, "  #");
            if (cap != 0xff) {
                char *name = hid_keypos[cap].name;
                char hidkey_buf[8];
                if ((uintptr_t) name < 0x100) {
                    /* Single character */
                    hidkey_buf[0] = (uintptr_t) name;
                    hidkey_buf[1] = '\0';
                    name = hidkey_buf;
                }
                fprintf(fp, " \"%s\"", name);
            }
            *amiga_buf_ptr = '\0';
            fprintf(fp, " ->%s\n", amiga_buf);
        }
    }
    fclose(fp);
}

#if 0
static void
showdatestamp(struct DateStamp *ds, uint usec)
{
    struct DateTime  dtime;
    char             datebuf[32];
    char             timebuf[32];

    dtime.dat_Stamp.ds_Days   = ds->ds_Days;
    dtime.dat_Stamp.ds_Minute = ds->ds_Minute;
    dtime.dat_Stamp.ds_Tick   = ds->ds_Tick;
    dtime.dat_Format          = FORMAT_DOS;
    dtime.dat_Flags           = 0x0;
    dtime.dat_StrDay          = NULL;
    dtime.dat_StrDate         = datebuf;
    dtime.dat_StrTime         = timebuf;
    DateToStr(&dtime);
    printf("%s %s.%06u\n", datebuf, timebuf, usec);
}

static void
showsystime(uint sec, uint usec)
{
    struct DateStamp ds;
    uint min  = sec / 60;
    uint day  = min / (24 * 60);

    ds.ds_Days   = day;
    ds.ds_Minute = min % (24 * 60);
    ds.ds_Tick   = (sec % 60) * TICKS_PER_SECOND;
    showdatestamp(&ds, usec);
}
#endif

static void
handle_win(void)
{
    /* Event loop */
    struct IntuiMessage *msg;
    ULONG class;
    UWORD icode;
    BOOL done = FALSE;

    while (!done) {
        WaitPort(window->UserPort);
//      while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
        while ((msg = GT_GetIMsg(window->UserPort))) {
            class = msg->Class;
            icode = msg->Code;

//          printf("class %x\n", class);
            switch (class) {
                case IDCMP_CLOSEWINDOW:
                    done = TRUE;
                    break;
                case IDCMP_MENUPICK: {
                    int cnt;
                    USHORT menu_item = icode;
                    for (cnt = 0; (menu_item != MENUNULL) && (cnt < 10); cnt++) {
                        /*
                         *    Menu number is bits 0..4
                         *    Item number is bits 5..10
                         * Subitem number is bits 11..15
                         */
                        ULONG menu_num = MENUNUM(menu_item);
                        ULONG item_num = ITEMNUM(menu_item);
                        // ULONG subitem_num = SUBITEMNUM(menu_item);
#if 0
                        printf("item=%x menu=%x item=%x\n",
                               menu_item, menu_num, item_num);
#endif
                        switch (menu_num) {
                            case 0:
                                switch (item_num) {
                                    case 0:
                                        printf("Menu: About\n");
                                        break;
                                    case 2:
                                        load_keymap_from_file();
                                        break;
                                    case 3:
                                        save_keymap_to_file();
                                        break;
                                    case 5:
                                        done = TRUE;
                                        break;
                                }
                                break;
                            case 1:
                                switch (item_num) {
                                    case 0:
                                        gui_printf("Loading from BEC");
                                        unmap_all_keycaps();
                                        get_bec_keymap();
                                        break;
                                    case 1:
                                        gui_printf("Saving to BEC");
                                        save_bec_keymap();
                                        break;
                                }
                                break;
                            case 2: {
                                struct MenuItem *item =
                                            ItemAddress(menus, menu_item);
                                uint checked = 0;
                                ULONG whichitem;
                                if ((item != NULL) && (item->Flags & CHECKED))
                                    checked = 1;

                                switch (item_num) {
                                    case 0:
                                        rawkey_mode = checked;
                                        break;
                                    case ITEMNUM(ITEMNUM_AMIGA_SCANCODE):
                                        if (checked) {
                                            capture_hid_scancodes = 0;
                                        } else {
                                            capture_hid_scancodes = 1;
                                            /*
                                             * Need to force selection of
                                             * HID scancodes
                                             */
                                            whichitem = ITEMNUM_HID_SCANCODE;
set_menu_checked:
                                            item = ItemAddress(menus, whichitem);
                                            if (item != NULL) {
                                                ClearMenuStrip(window);
                                                item->Flags |= CHECKED;
                                                ResetMenuStrip(window, menus);
                                            } else {
                                                err_printf("menu item %x NULL\n",
                                                           whichitem);
                                            }
                                        }
                                        break;
                                    case ITEMNUM(ITEMNUM_HID_SCANCODE):
                                        if (checked) {
                                            capture_hid_scancodes = 1;
                                        } else {
                                            capture_hid_scancodes = 0;
                                            /*
                                             * Need to force selection of
                                             * Amiga scancodes
                                             */
                                            whichitem = ITEMNUM_AMIGA_SCANCODE;
                                            goto set_menu_checked;
                                        }
                                        break;
                                    case ITEMNUM(ITEMNUM_AMIGA_TO_HID):
                                        if (checked) {
                                            map_hid_to_amiga = 0;
                                        } else {
                                            map_hid_to_amiga = 1;
                                            /*
                                             * Need to force selection of
                                             * Map HID to Amiga
                                             */
                                            whichitem = ITEMNUM_HID_TO_AMIGA;
                                            goto set_menu_checked;
                                        }
                                        break;
                                    case ITEMNUM(ITEMNUM_HID_TO_AMIGA):
                                        if (checked) {
                                            map_hid_to_amiga = 1;
                                        } else {
                                            map_hid_to_amiga = 0;
                                            /*
                                             * Need to force selection of
                                             * Map Amiga to HID
                                             */
                                            whichitem = ITEMNUM_AMIGA_TO_HID;
                                            goto set_menu_checked;
                                        }
                                        break;
                                }
                                break;
                            }
                        }
                        /*
                         * Get the next menu item if it was part of a
                         * selection list
                         *
                         * Note: Actual next item is usually in a
                         *       structure passed via message
                         */
                        menu_item = msg->Qualifier;
                        break;
                    }
                    break;
                }
                case IDCMP_MOUSEMOVE:
                    mouse_move(window->GZZMouseX, window->GZZMouseY);
                    break;
                case IDCMP_MOUSEBUTTONS:
                    if (icode == SELECTDOWN) {
                        printf("Left mouse button clicked in window\n");
                    }
                    break;
                case IDCMP_NEWSIZE:
//                  printf("newsize %u %u\n", window->Width, window->Height);
//                  BeginRefresh(window);
                    draw_win();
//                  EndRefresh(window, TRUE);
                    break;
                case IDCMP_RAWKEY:
                    amiga_rawkey(icode);
                    switch (icode) {
                        case RAWKEY_ESC: // key down ESC
                            done = TRUE;
                            break;
                        default:
                            gui_printf("Amiga %02x", icode);
                            break;
                    }
                    break;
                case IDCMP_REFRESHWINDOW:
                    BeginRefresh(window);
                    draw_win();
                    EndRefresh(window, TRUE);
                    break;
            }
            GT_ReplyIMsg(msg);
        }
    }
}

static void
generate_scancode_to_capnum(void)
{
    uint cap;
    memset(amiga_scancode_to_capnum, 0xff, sizeof (amiga_scancode_to_capnum));
    memset(hid_scancode_to_capnum, 0xff, sizeof (hid_scancode_to_capnum));

    for (cap = 0; cap < ARRAY_SIZE(amiga_keypos); cap++) {
        uint8_t scancode = amiga_keypos[cap].scancode;
        if (scancode == AS_ENTER) {
            if ((!is_northamerican && (amiga_keypos[cap].type == 12)) ||
                (is_northamerican && (amiga_keypos[cap].type == 13)))
                continue;
        }
        amiga_scancode_to_capnum[amiga_keypos[cap].scancode] = cap;
    }

    for (cap = 0; cap < ARRAY_SIZE(hid_keypos); cap++)
        hid_scancode_to_capnum[hid_keypos[cap].scancode] = cap;

#if 0
    memset(amiga_key_mapped, 1, sizeof (amiga_key_mapped));
    memset(hid_key_mapped, 1, sizeof (hid_key_mapped));
#endif
}

int
main(int argc, char *argv[])
{
    generate_scancode_to_capnum();
    if (argc > 0) {
        /* Started by AmigaOS CLI */
        int arg;
        for (arg = 1; arg < argc; arg++) {
            const char *ptr;
            ptr = long_to_short(argv[arg], long_to_short_main,
                                ARRAY_SIZE(long_to_short_main));
            if (*ptr == '-') {
                for (++ptr; *ptr != '\0'; ptr++) {
                    switch (*ptr) {
                        case 'a':  // amiga
                            capture_hid_scancodes = 0;
                            break;
                        case 'd':  // debug
                            flag_debug++;
                            break;
                        case 'e':  // euro
                            is_northamerican = 0;
                            break;
                        default:
                            err_printf("Unknown argument %s\n", ptr);
                            usage();
                            exit(1);
                    }
                }
            } else {
                err_printf("Error: unknown argument %s\n", ptr);
                usage();
                exit(1);
            }
        }
    } else {
        /* Started by Workbench */
        struct WBStartup *wbs = (struct WBStartup *) argv;
        (void) wbs;
    }

    if (OpenAmigaLibraries()) {
        window = OpenAWindow();
        if (window != NULL) {
            rp = window->RPort;
            select_pens();
            open_font();
            create_menu();
            if (draw_win()) {
                get_bec_keymap();
                handle_win();
            }
            close_menu();
            CloseWindow(window);
            close_font();
        } else {
            err_printf("Failed to open window.\\n");
        }
    }
    CloseAmigaLibraries();
    exit(EXIT_SUCCESS);
}
