/*
 * bec
 * ---
 * Utility to perform various operations with AmigaPCI STM32 Board Environment
 * Controller (BEC).
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
const char *version = "\0$VER: bec "VERSION" ("BUILD_DATE") © Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <exec/types.h>
#include <clib/dos_protos.h>
#include <inline/timer.h>
#include <inline/exec.h>
#include <inline/dos.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include "../fw/bec_cmd.h"
#include "crc32.h"

#ifndef ADDR8
#define ADDR8(x)    ((volatile uint8_t *)  ((uintptr_t)(x)))
#define ADDR16(x)   ((volatile uint16_t *) ((uintptr_t)(x)))
#define ADDR32(x)   ((volatile uint32_t *) ((uintptr_t)(x)))
#endif
#ifndef VADDR8
#define VADDR8(x)    ((volatile uint8_t *)  ((uintptr_t)(x)))
#define VADDR16(x)   ((volatile uint16_t *) ((uintptr_t)(x)))
#define VADDR32(x)   ((volatile uint32_t *) ((uintptr_t)(x)))
#endif

/*
 * gcc clib2 headers are bad (for example, no stdint definitions) and are
 * not being included by our build.  Because of that, we need to fix up
 * some stdio definitions.
 */
extern struct iob ** __iob;
#undef stdout
#define stdout ((FILE *)__iob[1])

struct ExecBase *DOSBase;
#if 0
struct Device   *TimerBase;
static struct    timerequest TimeRequest;
#endif

extern struct ExecBase *SysBase;

/*
 * ULONG has changed from NDK 3.9 to NDK 3.2.
 * However, PRI*32 did not. What is the right way to implement this?
 */
#if INCLUDE_VERSION < 47
#undef PRIu32
#define PRIu32 "lu"
#undef PRId32
#define PRId32 "ld"
#undef PRIx32
#define PRIx32 "lx"
#endif

#define BIT(x) (1U << (x))
#define ARRAY_SIZE(x) ((sizeof (x) / sizeof ((x)[0])))

#define VALUE_UNASSIGNED 0xffffffff

#define TEST_LOOPBACK_BUF 4096
#define TEST_LOOPBACK_MAX 64
#define MEM_LOOPS         1000000
#define ROM_WINDOW_SIZE   (512 << 10)  // 512 KB
#define MAX_CHUNK         (16 << 10)   // 16 KB

static const char cmd_options[] =
    "usage: bec <options>\n"
    "   debug        show debug output (-d)\n"
    "   identify     identify Board Environment Controller (BEC)\n"
    "   loop <num>   repeat the command a specified number of times (-l)\n"
    "   quiet        minimize test output\n"
    "   set <n> <v>  set BEC value <n>=\"name\" and <v> is string (-s)\n"
    "   term         open BEC firmware terminal [-T]\n"
    "   test[01234]  do interface test (-t)\n";

typedef struct {
    const char *const short_name;
    const char *const long_name;
} long_to_short_t;
long_to_short_t long_to_short_main[] = {
    { "-d", "debug" },
    { "-i", "inquiry" },
    { "-i", "identify" },
    { "-i", "id" },
    { "-l", "loop" },
    { "-q", "quiet" },
    { "-s", "set" },
    { "-t", "test" },
    { "-T", "term" },
};

BOOL __check_abort_enabled = 0;       // Disable gcc clib2 ^C break handling
uint flag_debug = 0;
uint8_t flag_quiet = 0;
uint8_t *test_loopback_buf = NULL;

/*
 * is_user_abort
 * -------------
 * Check for user break input (^C)
 */
static BOOL
is_user_abort(void)
{
    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
        static uint8_t printed = 0;
        if (printed++ == 0)
            printf("^C Abort\n");
        return (1);
    }
    return (0);
}

static void
usage(void)
{
    printf("%s\n\n%s", version + 7, cmd_options);
}

char *
ull_to_str(unsigned long long val, char *buf, int bufsize)
{
    char *ptr = buf + bufsize;

    if (ptr == buf)
        return (NULL);  // no space
    *(--ptr) = '\0';

    while (val != 0) {
        if (ptr == buf)
            return (NULL);  // overflow
        *(--ptr) = (val % 10) + '0';
        val /= 10;

    }
    return (ptr);
}

const char *
long_to_short(const char *ptr, long_to_short_t *ltos, uint ltos_count)
{
    uint cur;

    for (cur = 0; cur < ltos_count; cur++)
        if (strcmp(ptr, ltos[cur].long_name) == 0)
            return (ltos[cur].short_name);
    return (ptr);
}

/*
 * rand32
 * ------
 * Very simple pseudo-random number generator
 */
static uint32_t rand_seed = 0;
static uint32_t
rand32(void)
{
    rand_seed = (rand_seed * 25173) + 13849;
    return (rand_seed);
}

/*
 * srand32
 * -------
 * Very simple random number seed
 */
static void
srand32(uint32_t seed)
{
    rand_seed = seed;
}

void
local_memcpy(void *dst, void *src, size_t len)
{
    uint32_t *dst32 = dst;
    uint32_t *src32 = src;

    if ((uintptr_t) dst | (uintptr_t) src | len) {
        /* fast mode */
        uint xlen = len >> 2;
        len -= (xlen << 2);
        while (xlen > 0) {
            *(dst32++) = *(src32++);
            xlen--;
        }
    }
    if (len > 0) {
        uint8_t *dst8 = (uint8_t *) dst32;
        uint8_t *src8 = (uint8_t *) src32;
        while (len > 0) {
            *(dst8++) = *(src8++);
            len--;
        }
    }
}

static void
print_us_diff(uint64_t start, uint64_t end)
{
    uint64_t diff = end - start;
    uint32_t diff2;
    char *scale = "ms";

    if ((diff >> 32) != 0)  // Finished before started?
        diff = 0;
    if (diff >= 100000) {
        diff /= 1000;
        scale = "sec";
    }
    diff2 = diff / 10;
    printf("%u.%02u %s\n", diff2 / 100, diff2 % 100, scale);
}

static void
show_test_state(const char * const name, int state)
{
    if (state == 0) {
        if (!flag_quiet)
            printf("PASS\n");
        return;
    }

    if (!flag_quiet || (state != -1))
        printf("  %-15s ", name);

    if (state == -1) {
        fflush(stdout);
        return;
    }
    printf("FAIL\n");
}

/* RTC offsets for Ricoh RP5C01 in AmigaPCI */
#define RP_ONE_SEC    (0x0 * 4 + 1)  /* Second One's */
#define RP_TEN_SEC    (0x1 * 4 + 1)  /* Second Ten's */
#define RP_ONE_MIN    (0x2 * 4 + 1)  /* Minute One's */
#define RP_TEN_MIN    (0x3 * 4 + 1)  /* Minute Ten's */
#define RP_ONE_HOUR   (0x4 * 4 + 1)  /* Hour One's */
#define RP_TEN_HOUR   (0x5 * 4 + 1)  /* Hour Ten's */
#define RP_DOW        (0x6 * 4 + 1)  /* Day of Week */
#define RP_ONE_DAY    (0x7 * 4 + 1)  /* Day of Month One's */
#define RP_TEN_DAY    (0x8 * 4 + 1)  /* Day of Month Ten's */
#define RP_ONE_MONTH  (0x9 * 4 + 1)  /* Month of Year One's */
#define RP_TEN_MONTH  (0xa * 4 + 1)  /* Month of Year Ten's */
#define RP_ONE_YEAR   (0xb * 4 + 1)  /* Year of Century One's */
#define RP_TEN_YEAR   (0xc * 4 + 1)  /* Year of Century Ten's */
#define RP_MODE       (0xd * 4 + 1)  /* Mode register */
#define RP_TEST       (0xe * 4 + 1)  /* Test register */
#define RP_RESET      (0xf * 4 + 1)  /* Reset controller, etc */
#define RP_MAGIC      (0x9 * 4 + 1)  /* Mode 1 AmigaPCI magic register */
#define RP_M1_12_24   (0xa * 4 + 1)  /* Mode 1 12/24 Hour select (0=AM, 1=24, 2=PM) */
#define RP_M1_LEAP    (0xb * 4 + 1)  /* Mode 1 Leap Year Counter (increments w/ year */

#define RP_MODE_M0         0      /* Mode 0 */
#define RP_MODE_M1         1      /* Mode 1 */
#define RP_MODE_BLK10      2      /* RAM Block 10 */
#define RP_MODE_BLK11      3      /* RAM Block 11 */
#define RP_MODE_ALARM_EN   BIT(2) /* Alarm Enable */
#define RP_MODE_TIMER_EN   BIT(3) /* Timer Enable */
#define RP_RESET_TIMER_ALL BIT(0) /* Reset all alarm registers */
#define RP_RESET_TIMER_SEC BIT(1) /* Reset divider stage for sec and smaller */
#define RP_RESET_16HZ_OFF  BIT(2) /* Turn off 16Hz clock pulse */
#define RP_RESET_1HZ_OFF   BIT(3) /* Turn off 1Hz clock pulse */

#define RTC_REG(x) (*((volatile uint8_t *) 0xdc0000 + x))

static const uint8_t bec_magic[] = { 0xc, 0xd, 0x6, 0x8 };

#define CIAA_TBLO        ADDR8(0x00bfe601)
#define CIAA_TBHI        ADDR8(0x00bfe701)

#define CIA_USEC(x)      (x * 715909 / 1000000)
#define INTERRUPTS_DISABLE() if (irq_disabled++ == 0) \
                                 Disable()  /* Disable interrupts */
#define INTERRUPTS_ENABLE()  if (--irq_disabled == 0) \
                                 Enable()   /* Enable Interrupts */
unsigned int irq_disabled;

uint
cia_ticks(void)
{
    uint8_t hi1;
    uint8_t hi2;
    uint8_t lo;

    hi1 = *CIAA_TBHI;
    lo  = *CIAA_TBLO;
    hi2 = *CIAA_TBHI;

    /*
     * The below operation will provide the same effect as:
     *     if (hi2 != hi1)
     *         lo = 0xff;  // rollover occurred
     */
    lo |= (hi2 - hi1);  // rollover of hi forces lo to 0xff value

    return (lo | (hi2 << 8));
}

void
cia_spin(unsigned int ticks)
{
    uint16_t start = cia_ticks();
    uint16_t now;
    uint16_t diff;

    while (ticks != 0) {
        now = cia_ticks();

        diff = start - now;
        if (diff >= ticks)
            break;
        ticks -= diff;
        start = now;
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
    }
}

static uint8_t
get_nibble(void)
{
    return (RTC_REG(RP_MAGIC) & 0x0f);
}

static uint8_t
get_byte(void)
{
    return ((get_nibble() << 4) | get_nibble());
}

static void
send_nibble(uint8_t nibble)
{
    RTC_REG(RP_MAGIC) = nibble;
}

static void
send_byte(uint8_t byte)
{
    send_nibble(byte >> 4);
    send_nibble(byte);
}

static uint
send_cmd(uint8_t cmd, void *arg, uint8_t arglen,
         void *reply, uint replymax, uint *replyalen)
{
    uint     pos;
    uint     timeout;
    uint8_t *argbuf = arg;
    uint8_t *replybuf = reply;
    uint8_t  got_magic[4];
    uint     bad_magic = 0;
    uint8_t  status;
    uint8_t  msglen;
    uint32_t crc;
    uint32_t got_crc;
    uint32_t calc_crc;

    for (pos = 0; pos < ARRAY_SIZE(bec_magic); pos++)
        send_nibble(bec_magic[pos]);
    send_byte(arglen);
    send_byte(cmd);
    send_byte(arglen);
    for (pos = 0; pos < arglen; pos++)
        send_byte(argbuf[pos]);
    crc = crc32(0, &cmd, 1);
    crc = crc32(crc, &arglen, 1);
    crc = crc32(crc, argbuf, arglen);
    send_byte(crc >> 24);
    send_byte(crc >> 16);
    send_byte(crc >> 8);
    send_byte(crc);

    /* Wait for reply */
    cia_spin(CIA_USEC(10));
    for (timeout = 100; timeout > 0; timeout--) {
        cia_spin(1);
        if ((got_magic[0] = get_nibble()) == bec_magic[0])
            break;
    }
    if (timeout == 0) {
        printf("AP Timeout\n");
        return (BEC_STATUS_TIMEOUT);
    }
    for (pos = 1; pos < ARRAY_SIZE(bec_magic); pos++) {
        uint8_t nibble = got_magic[pos] = get_nibble();
        if (nibble != bec_magic[pos])
            bad_magic++;
    }
    if (bad_magic) {
        printf("AP bad magic:");
        for (pos = 0; pos < ARRAY_SIZE(got_magic); pos++)
            printf(" %x", got_magic[pos]);
        printf("\n");
        return (BEC_STATUS_BADMAGIC);
    }

    /* Got magic -- get remainder of message */
    status = get_byte();
    msglen = get_byte();
    for (pos = 0; pos < msglen; pos++) {
        if (pos >= replymax)
            (void) get_byte();
        else
            replybuf[pos] = get_byte();
    }

    *replyalen = msglen;

    if (msglen > replymax)
        return (BEC_STATUS_REPLYLEN); // Too long; truncated

    /* Get CRC */
    got_crc = (get_byte() << 24) |
              (get_byte() << 16) |
              (get_byte() << 8) |
              get_byte();
    calc_crc = crc32(0, &status, 1);
    calc_crc = crc32(calc_crc, &msglen, 1);
    calc_crc = crc32(calc_crc, replybuf, msglen);
    if (calc_crc != got_crc) {
        printf("Bad CRC %08x !+ expected %08x rc=%x l=%x\n",
               calc_crc, got_crc, status, msglen);
        return (BEC_STATUS_REPLYCRC);
    }

    return (status);
}

#define DUMP_VALUE_UNASSIGNED 0xffffffff

static char
printable_ascii(uint8_t ch)
{
    if (ch >= ' ' && ch <= '~')
        return (ch);
    if (ch == '\t' || ch == '\r' || ch == '\n' || ch == '\0')
        return (' ');
    return ('.');
}

/*
 * dump_memory
 * -----------
 * Display hex and ASCII dump of data at the specified memory location.
 *
 * buf is address of the data.
 * len is the number of bytes to display.
 * dump_base is either an address/offset of DUMP_VALUE_UNASSIGNED if
 *     it should not be printed.
 */
void
dump_memory(void *buf, uint len, uint dump_base)
{
    uint pos;
    uint strpos;
    char str[20];
    uint32_t *src = buf;

    len = (len + 3) / 4;
    if (dump_base != DUMP_VALUE_UNASSIGNED)
        printf("%05x:", dump_base);
    for (strpos = 0, pos = 0; pos < len; pos++) {
        uint32_t val = src[pos];
        printf(" %08x", val);
        str[strpos++] = printable_ascii(val >> 24);
        str[strpos++] = printable_ascii(val >> 16);
        str[strpos++] = printable_ascii(val >> 8);
        str[strpos++] = printable_ascii(val);
        if ((pos & 3) == 3) {
            str[strpos] = '\0';
            strpos = 0;
            printf(" %s\n", str);
            if ((dump_base != DUMP_VALUE_UNASSIGNED) && ((pos + 1) < len)) {
                dump_base += 16;
                printf("%05x:", dump_base);
            }
        }
    }
    if ((pos & 3) != 0) {
        str[strpos] = '\0';
        printf("%*s%s\n", (4 - (pos & 3)) * 9 + 1, "", str);
    }
}

static const char *const bec_status_s[] = {
    "OK",                                // BEC_STATUS_OK
    "BEC Failure",                       // BEC_STATUS_FAIL
    "BEC reports CRC bad",               // BEC_STATUS_CRC
    "BEC detected unknown command",      // BEC_STATUS_UNKCMD
    "BEC reports bad command argument",  // BEC_STATUS_BADARG
    "BEC reports bad length",            // BEC_STATUS_BADLEN
    "BEC reports no data available",     // BEC_STATUS_NODATA
    "BEC reports resource locked",       // BEC_STATUS_LOCKED
    "No response from BEC",              // BEC_STATUS_TIMEOUT
    "BEC response header from BEC",      // BEC_STATUS_BADMAGIC
    "BEC response is too large",         // BEC_STATUS_REPLYLEN
    "BEC response has bad CRC",          // BEC_STATUS_REPLYCRC
};

/*
 * bec_err
 * -------
 * Converts BEC_STATUS_* value to a readable string
 *
 * status is the error status code to convert.
 */
const char *
bec_err(uint status)
{
    static char buf[64];
    const char *str = "Unknown";

    if (status < ARRAY_SIZE(bec_status_s))
        str = bec_status_s[status];
    sprintf(buf, "%d %s", status, str);
    return (buf);
}

static uint64_t
bec_time(void)
{
    uint64_t usecs;
    if (send_cmd(BEC_CMD_UPTIME, NULL, 0, &usecs, sizeof (usecs), NULL))
        return (0);
    return (usecs);
}

static uint
bec_identify(void)
{
    uint64_t   usecs;
    uint       sec;
    uint       usec;
    bec_id_t id;
    uint       rlen;
    uint       rc;

    memset(&id, 0, sizeof (id));
    rc = send_cmd(BEC_CMD_ID, NULL, 0, &id, sizeof (id), &rlen);

    if (rc != 0) {
        printf("Reply message failure: (%s)\n", bec_err(rc));
        if (flag_debug)
            dump_memory(&id, rlen, DUMP_VALUE_UNASSIGNED);
        return (rc);
    }
    if (flag_quiet == 0) {
        printf("ID\n");
        printf("  APCTrL %u.%u built %02u%02u-%02u-%02u %02u:%02u:%02u\n",
               id.bid_version[0], id.bid_version[1],
               id.bid_date[0], id.bid_date[1],
               id.bid_date[2], id.bid_date[3],
               id.bid_time[0], id.bid_time[1], id.bid_time[2]);
        printf("   Serial \"%s\"  Name \"%s\"\n",
               id.bid_serial, id.bid_name);
    }

    usecs = bec_time();
    if (flag_quiet == 0) {
        if (usecs != 0) {
            sec  = usecs / 1000000;
            usec = usecs % 1000000;
            printf("  Uptime: %u.%06u sec\n", sec, usec);
        }
    }

#if 0
    uint8_t nvdata[32];
    uint16_t what = 0x0020;  // Get all 32 bytes of NV data
    rc = send_cmd(BEC_CMD_GET | BEC_GET_NV, &what, sizeof (what),
                   nvdata, sizeof (nvdata), &rlen);
    printf("  NV Data:");
    if (rc == 0) {
        uint pos;
        for (pos = 0; pos < sizeof (nvdata); pos++) {
            if (pos == 16)
                printf("\n          ");
            printf(" %02x", nvdata[pos]);
        }
        printf("\n");
    } else {
        if (rc == KS_STATUS_UNKCMD)
            printf(" Unavailable\n");
        else
            printf(" Fail (%s)\n", bec_err(rc));
    }
#endif

    return (0);
}

static const uint8_t test_pattern[] = {
    0xaa, 0x55, 0xcc, 0x33,
    0xee, 0x11, 0xff, 0x00,
    0x01, 0x02, 0x04, 0x08,
    0x10, 0x20, 0x40, 0x80,
    0xfe, 0xfd, 0xfb, 0xf7,
    0xef, 0xdf, 0xbf, 0x7f,
};

static int
bec_test_pattern(void)
{
    uint32_t reply_buf[64];
    uint start;
    uint pos;
    uint err_count = 0;
    uint rlen;
    uint rc;

    show_test_state("Test pattern", -1);

    memset(reply_buf, 0, sizeof (reply_buf));
#ifdef SEND_TESTPATT_ALT
    reply_buf[0] = 0xa5a5a5a5;
    rc = send_cmd(BEC_CMD_TESTPATT, reply_buf, 2, reply_buf,
                  sizeof (reply_buf), &rlen);
#else
    rc = send_cmd(BEC_CMD_TESTPATT, NULL, 0, reply_buf,
                  sizeof (reply_buf), &rlen);
#endif
    if (rc != 0) {
        printf("Reply message failure: (%s)\n", bec_err(rc));
        if (flag_debug)
            dump_memory(reply_buf, rlen, DUMP_VALUE_UNASSIGNED);
        show_test_state("Test pattern", rc);
        return (rc);
    }

    for (start = 0; start < ARRAY_SIZE(reply_buf); start++)
        if (reply_buf[start] == test_pattern[0])  // Found first data
            break;

    if (start == ARRAY_SIZE(reply_buf)) {
        printf("No test pattern marker [%08x] in reply\n", test_pattern[0]);
fail:
        dump_memory(reply_buf, sizeof (reply_buf), DUMP_VALUE_UNASSIGNED);
        show_test_state("Test pattern", 1);
        return (1);
    }

    if (start != 0) {
        printf("Pattern start 0x%x is not at beginning of buffer\n",
               start);
        err_count++;
    }
    for (pos = 0; pos < ARRAY_SIZE(test_pattern); pos++) {
        if (reply_buf[start + pos] != test_pattern[pos]) {
            printf("At pos=%x reply %08x != expected %08x (diff %08x)\n", pos,
                   reply_buf[start + pos], test_pattern[pos],
                   reply_buf[start + pos] ^ test_pattern[pos]);
            if (err_count > 6)
                goto fail;
        }
    }
    if (err_count > 6)
        goto fail;
    if (flag_debug > 1)
        dump_memory(reply_buf, sizeof (reply_buf), DUMP_VALUE_UNASSIGNED);
    show_test_state("Test pattern", 0);
    return (0);
}

static int
bec_test_loopback(void)
{
    uint8_t   *tx_buf;
    uint8_t   *rx_buf;
    uint       cur;
    uint       rc;
    uint       count;
    uint       rlen = 0;
    uint       nums = (rand32() % (TEST_LOOPBACK_MAX - 1)) + 1;
    uint64_t   time_start;
    uint64_t   time_end;

    show_test_state("Test loopback", -1);

    tx_buf = &test_loopback_buf[0];
    rx_buf = &test_loopback_buf[TEST_LOOPBACK_BUF];
    memset(rx_buf, 0, TEST_LOOPBACK_MAX * 4);
    for (cur = 0; cur < nums; cur++)
        tx_buf[cur] = (uint8_t) (rand32() >> 8);

    /* Measure IOPS */
    INTERRUPTS_DISABLE();
    time_start = bec_time();
    for (count = 0; count < 10; count++) {
        rc = send_cmd(BEC_CMD_LOOPBACK, tx_buf, 4,
                      rx_buf, TEST_LOOPBACK_MAX, &rlen);
        if ((rc != BEC_CMD_LOOPBACK))
            break;
    }
    time_end = bec_time();
    INTERRUPTS_ENABLE();

    if (rc == BEC_CMD_LOOPBACK) {
        /* Test loopback accuracy */
        rc = send_cmd(BEC_CMD_LOOPBACK, tx_buf, nums,
                      rx_buf, TEST_LOOPBACK_MAX, &rlen);
    }
    if (rc == BEC_CMD_LOOPBACK) {
        rc = 0;
    } else {
        printf("FAIL: (%s)\n", bec_err(rc));
fail_handle_debug:
        if (flag_debug) {
            uint pos;
            uint8_t *txp = (uint8_t *)tx_buf;
            uint8_t *rxp = (uint8_t *)rx_buf;
            for (pos = 0; pos < nums; pos++)
                if (rxp[pos] != txp[pos])
                    break;
            dump_memory(tx_buf, nums, DUMP_VALUE_UNASSIGNED);
            if (pos < nums) {
                printf("--- Tx above  Rx below --- ");
                printf("First diff at 0x%x of 0x%x\n", pos, nums);
                dump_memory(rx_buf, rlen, DUMP_VALUE_UNASSIGNED);
            } else {
                printf("Tx and Rx buffers (len=0x%x) match\n", nums);
            }
        }
        goto fail_loopback;
    }
    if (rlen != nums) {
        printf("FAIL: rlen=%u != sent %u\n", rlen, nums);
        rc = BEC_STATUS_REPLYLEN;
        goto fail_handle_debug;
    }
    for (cur = 0; cur < nums; cur++) {
        if (rx_buf[cur] != tx_buf[cur]) {
            if (rc++ == 0)
                printf("\nLoopback data miscompare\n");
            if (rc < 5) {
                printf("    [%02x] %02x != expected %02x\n",
                       cur, rx_buf[cur], tx_buf[cur]);
            }
        }
    }
    if (rc >= 4)
        printf("%u miscompares\n", rc);
    if ((rc == 0) && (flag_quiet == 0)) {
        uint diff = (uint) (time_end - time_start);
        if (diff == 0)
            diff = 1;
        printf("PASS  %u IOPS\n", 1000000 * count / diff);
        return (0);
    }
fail_loopback:
    show_test_state("Test loopback", rc);
    return (rc);
}

static int
bec_test_loopback_perf(void)
{
    const uint lb_size  = 250;
    const uint xfers    = 100;
    const uint lb_alloc = lb_size + BEC_MSG_HDR_LEN + BEC_MSG_CRC_LEN;
    uint32_t  *buf;
    uint       cur;
    uint       rc;
    uint       diff;
    uint       total;
    uint       perf;
    uint64_t   time_start;
    uint64_t   time_end;

    show_test_state("Loopback perf", -1);

    buf = AllocMem(lb_alloc, MEMF_PUBLIC);
    if (buf == NULL) {
        printf("Memory allocation failure\n");
        return (1);
    }
    memset(buf, 0xa5, lb_size);

    time_start = bec_time();

    for (cur = 0; cur < xfers; cur++) {
        rc = send_cmd(BEC_CMD_LOOPBACK, buf, lb_size, buf, lb_size, NULL);
        if (rc == BEC_CMD_LOOPBACK) {
            rc = 0;
        } else {
            printf("FAIL: (%s)\n", bec_err(rc));
            if (flag_debug) {
                dump_memory(buf, lb_size, DUMP_VALUE_UNASSIGNED);
            }
            goto cleanup;
        }
    }

    if (flag_quiet == 0) {
        time_end = bec_time();
        diff = (uint) (time_end - time_start);
        if (diff == 0)
            diff = 1;
        total = xfers * (lb_size + BEC_MSG_HDR_LEN + BEC_MSG_CRC_LEN);
        perf = total * 1000 / diff;
        perf *= 2;  // Write data + Read (reply) data
        printf("PASS  %u KB/sec\n", perf);
    }

cleanup:
    if (buf != NULL)
        FreeMem(buf, lb_alloc);
    return (rc);
}

#if 0
static uint
calc_kb_sec(uint usecs, uint bytes)
{
    if (usecs == 0)
        usecs = 1;
    while (bytes > 4000000) {
        bytes >>= 1;
        usecs >>= 1;
    }
    return (bytes * 1000 / usecs);
}
#endif

static int
bec_test_commands(void)
{
    int errs = 0;
    int rc;
    int rc2;
    uint rlen;
    __attribute__((aligned(4))) uint8_t buf[256];

    show_test_state("Commands", -1);

    /* BEC_CMD_NOP */
    rc = send_cmd(BEC_CMD_NOP, NULL, 0, NULL, 0, NULL);
    if (rc != 0) {
        printf("FAIL: NOP (%s)\n", bec_err(rc));
        errs++;
        goto test_commands_fail;
    }

    /* BEC_CMD_ID */
    bec_id_t id;
    rc = send_cmd(BEC_CMD_ID, NULL, 0, &id, sizeof (id), &rlen);
    if ((rc != 0) || (id.bid_rev != 0x0001)) {
        printf("FAIL: ID Rev=%04x (%s)\n", id.bid_rev, bec_err(rc));
        errs++;
    }

    /* BEC_CMD_UPTIME */
    uint64_t usecs1;
    uint64_t usecs2;
    rc = send_cmd(BEC_CMD_UPTIME, NULL, 0, &usecs1, sizeof (usecs1), NULL);
    Delay(1);
    rc2 = send_cmd(BEC_CMD_UPTIME, NULL, 0, &usecs2, sizeof (usecs2), NULL);

    if ((rc != 0) || (rc2 != 0)) {
        printf("FAIL: UPTIME (%s) (%s)\n", bec_err(rc), bec_err(rc2));
        errs++;
        goto test_commands_fail;

    } else {
        uint64_t diff = usecs2 - usecs1;
        if ((diff < 19500) || (diff > 50000)) {
            printf("FAIL: UPTIME not accurate: 20000 expected, but got %s: ",
                   ull_to_str(diff, buf, sizeof (buf)));
            print_us_diff(usecs1, usecs2);
            errs++;
        }
    }

#if 0
    /* BEC_CMD_GET */
    uint count;
    uint8_t nvdata1[32];
    uint8_t nvdata2[32];
    uint16_t what = 0x0010;  // Get 16 bytes of NV data
    rc = send_cmd(BEC_CMD_GET | KS_GET_NV, &what, sizeof (what),
                  nvdata1, sizeof (nvdata1), &rlen);
    if (rc != 0) {
fail_get_nv:
        printf("FAIL: GET NV (%s)\n", bec_err(rc));
        errs++;
    } else {
        for (count = 0; count < 10; count++) {
            rc = send_cmd(BEC_CMD_GET | KS_GET_NV, &what, sizeof (what),
                          nvdata2, sizeof (nvdata2), &rlen);
            if (rc != 0)
                goto fail_get_nv;
            if (memcmp(nvdata1, nvdata2, what) != 0) {
                printf("FAIL: GET NV miscompare:\n");
                printf("  Pass 1: ");
                dump_memory(nvdata1, what, DUMP_VALUE_UNASSIGNED);
                printf("  Pass 2: ");
                dump_memory(nvdata2, what, DUMP_VALUE_UNASSIGNED);
                errs++;
                break;
            }
        }
    }
#endif

    /* BEC_CMD_CONS_OUTPUT */
    uint16_t maxlen = 0;
    buf[0] = 0xa5;
    buf[1] = 0x5a;
    rc = send_cmd(BEC_CMD_CONS_OUTPUT, &maxlen, sizeof (maxlen),
                  buf, sizeof (buf), &rlen);
    if (rc != 0) {
        printf("FAIL: CONS_OUTPUT (%s)\n", bec_err(rc));
        errs++;
    } else if ((rlen != 0) || (buf[0] != 0xa5) || (buf[1] != 0x5a)) {
        printf("FAIL: CONS_OUTPUT rlen=%u buf=%02x %02x\n",
               rlen, buf[0], buf[1]);
        errs++;
    }

test_commands_fail:
    show_test_state("Commands", errs);

    return (errs);
}


static int
bec_test(uint mask)
{
    int rc = 0;
    if (mask & BIT(0)) {
        rc = bec_test_pattern();
        if (rc != 0)
            return (rc);
    }

    if (is_user_abort())
        return (1);

    if (mask & BIT(1)) {
        rc = bec_test_loopback();
        if (rc != 0)
            return (rc);
    }

    if (is_user_abort())
        return (1);

    if (mask & BIT(2)) {
        rc = bec_test_loopback_perf();
        if (rc != 0)
            return (rc);
    }

    if (is_user_abort())
        return (1);

    if (mask & BIT(3)) {
        rc = bec_test_commands();
        if (rc != 0)
            return (rc);
    }

    return (0);
}

/*
 * are_you_sure() prompts the user to confirm that an operation is intended.
 *
 * @param  [in]  None.
 *
 * @return       TRUE  - User has confirmed (Y).
 * @return       FALSE - User has denied (N).
 */
int
are_you_sure(const char *prompt)
{
    int ch;
ask_again:
    printf("%s - are you sure? (y/n) ", prompt);
    fflush(stdout);
    while ((ch = getchar()) != EOF) {
        if (is_user_abort())
            return (FALSE);
        if ((ch == 'y') || (ch == 'Y'))
            return (TRUE);
        if ((ch == 'n') || (ch == 'N'))
            return (FALSE);
        if (!isspace(ch))
            goto ask_again;
    }
    return (FALSE);
}

#if 0
static uint
get_file_size(const char *filename)
{
    struct FileInfoBlock fb;
    BPTR lock;
    fb.fib_Size = VALUE_UNASSIGNED;
    lock = Lock(filename, ACCESS_READ);
    if (lock == 0L) {
        printf("Lock %s failed\n", filename);
        return (VALUE_UNASSIGNED);
    }
    if (Examine(lock, &fb) == 0) {
        printf("Examine %s failed\n", filename);
        UnLock(lock);
        return (VALUE_UNASSIGNED);
    }
    UnLock(lock);
    return (fb.fib_Size);
}
#endif

/*
 * cmd_set
 * -------
 * Set a KickSmash value, such as board name, or NVRAM value available to
 * AmigaOS.
 */
int
cmd_set(int argc, char *argv[])
{
    int rc;
    int arg;

    if (argc < 2) {
cmd_set_usage:
        printf("set name <string>    - up to 15 characters for board name\n"
               "set nv<n> <value...> - set non-volatile data\n"
               "    nv0 is switcher timeout and nv1 is ROM bank to use\n");
        return (1);
    }
    if (strcmp(argv[1], "name") == 0) {
        char buf[30];
        int len = 0;
        int maxlen;
        uint bufpos = 5;
#define BOARD_NAME_MAX 15
        strcpy(buf, "name");
        for (arg = 2; arg < argc; arg++) {
            int clen = strlen(argv[arg]);
            if (len > 0)
                buf[bufpos + len++] = ' ';

            if (len + clen >= BOARD_NAME_MAX) {
                printf("Name too long: \"");
                for (arg = 2; arg < argc; arg++) {
                    if (arg != 2)
                        printf(" ");
                    printf("%s", argv[arg]);
                }
                printf("\"\n");
                return (1);
            }
            maxlen = BOARD_NAME_MAX - len;
            strncpy(buf + bufpos + len, argv[arg], maxlen);
            len += clen;
        }
        buf[bufpos + len] = '\0';
        rc = send_cmd(BEC_CMD_SET, buf, bufpos + len + 1, NULL, 0, NULL);
        if (rc != 0) {
            printf("Failed to set board name: (%s)\n", bec_err(rc));
        } else {
            printf("Set board name to \"%s\"\n", buf);
        }
        return (rc);
    } else {
        printf("Unknown set %s\n", argv[1]);
        goto cmd_set_usage;
    }
    return (0);
}

/*
 * cmd_term
 * --------
 * Open a KickSmash terminal.
 */
int
cmd_term(int argc, char *argv[])
{
    int ch;
    uint rlen;
    uint rc;
    uint is_poll = 0;
    ULONG ihandle = Input();
    __attribute__((aligned(4))) uint8_t buf[256];
    uint16_t maxlen = sizeof (buf) - 4;

    (void) argc;
    (void) argv;

    printf("Press ^X to exit\n");
    setvbuf(stdout, NULL, _IONBF, 0);  // Raw output mode
    SetMode(Input(), 1);
    SetMode(Output(), 1);

#define KEY_CTRL_X 0x18
    while (1) {
        /* Poll for keystroke input */
        if (WaitForChar(ihandle, 0)) {
            if (Read(ihandle, buf, 1) == 0) {
                setvbuf(stdout, NULL, _IOLBF, 0);  // Line output mode
                printf("\nRead fail\n");
                break;
            }
            ch = buf[0];
            if (ch == KEY_CTRL_X) {
                printf("\n");
                break;
            }
            rc = send_cmd(BEC_CMD_CONS_INPUT, buf, 1, NULL, 0, &rlen);
            if (rc != 0)
                break;
        }

        /* Poll for AP Controler output */
        maxlen = sizeof (buf) - 2;
        rc = send_cmd(BEC_CMD_CONS_OUTPUT, &maxlen, sizeof (maxlen),
                      buf, sizeof (buf), &rlen);
#if 0
        if (rc == MSG_STATUS_BAD_CRC) {
            uint pos;
            printf("[Bad CRC rc=%d l=%04x s=????", (int) rc, rlen);
            for (pos = 0; pos < rlen + 8; pos++)
                printf(" %02x", buf[pos]);
            printf("]\n");
        }
#endif
        if (rc != 0) {
            is_poll = 1;
            break;
        }
        if ((rlen > 0) && Write(Output(), buf, rlen) == 0) {
            setvbuf(stdout, NULL, _IOLBF, 0);  // Line output mode
            printf("\nWrite fail\n");
            break;
        }
    }

    /* Restore cooked mode */
    SetMode(Input(), 0);
    SetMode(Output(), 0);
    setvbuf(stdout, NULL, _IOLBF, 0);  // Line output mode

    if (rc != 0) {
        printf("\n%s Fail (%s)\n", is_poll ? "Poll" : "Send", bec_err(rc));
        return (EXIT_FAILURE);
    }
    return (0);
}

#if 0
static int
OpenTimer(void)
{
    int rc;

    rc = OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ,
                    (struct IORequest *) &TimeRequest, 0L);
    if (rc != 0) {
        printf("Timer open failed\n");
        return (rc);
    }

    TimerBase = (struct Device *) TimeRequest.tr_node.io_Device;
    return (0);
}

static void
CloseTimer(void)
{
    CloseDevice((struct IORequest *) &TimeRequest);
    TimerBase = NULL;
}
#endif

#if 0
static void
setsystime(uint sec, uint usec)
{
    TimeRequest.tr_node.io_Command = TR_SETSYSTIME;
    TimeRequest.tr_time.tv_secs = sec;
    TimeRequest.tr_time.tv_micro = usec;
    DoIO((struct IORequest *) &TimeRequest);
}

static uint
getsystime(uint *usec)
{
    TimeRequest.tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *) &TimeRequest);
    *usec = TimeRequest.tr_time.tv_micro;
    return (TimeRequest.tr_time.tv_secs);
}

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

int
main(int argc, char *argv[])
{
    int      arg;
    uint     loop;
    uint     loops = 1;
    uint     flag_inquiry = 0;
    uint     flag_test = 0;
    uint     flag_test_mask = 0;
    uint     errs = 0;
    uint     do_multiple = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds="
    SysBase = *(struct ExecBase **)4UL;
#pragma GCC diagnostic pop

    for (arg = 1; arg < argc; arg++) {
        const char *ptr;
        ptr = long_to_short(argv[arg], long_to_short_main,
                            ARRAY_SIZE(long_to_short_main));
        if (*ptr == '-') {
            for (++ptr; *ptr != '\0'; ptr++) {
                switch (*ptr) {
                    case 'd':  // debug
                        flag_debug++;
                        break;
                    case 'i':  // inquiry
                        flag_inquiry++;
                        break;
                    case 'l':  // loop count
                        if (++arg >= argc) {
                            printf("%s requires an argument\n", ptr);
                            exit(1);
                        }
                        loops = atoi(argv[arg]);
                        break;
                    case 'q':  // quiet
                        flag_quiet++;
                        break;
                    case 's':  // set
                        exit(cmd_set(argc - arg, argv + arg));
                        break;
                    case '0':  // pattern test
                    case '1':  // loopback test
                    case '2':  // loopback perf test
                    case '3':  // Commands
                        flag_test_mask |= BIT(*ptr - '0');
                        flag_test++;
                        break;
                    case 't':  // test
                        flag_test++;
                        break;
                    case 'T':  // AP Controler firmware terminal
                        exit(cmd_term(argc - 1, argv + 1));
                        break;
                    default:
                        printf("Unknown argument %s\n", ptr);
                        usage();
                        exit(1);
                }
            }
        } else if ((*ptr >= '0') && (*ptr <= '3') && (ptr[1] == '\0')) {
            flag_test_mask |= BIT(*ptr - '0');
            flag_test++;
        } else {
            printf("Error: unknown argument %s\n", ptr);
            usage();
            exit(1);
        }
    }

    if ((flag_inquiry | flag_test) == 0) {
        printf("You must specify an operation to perform\n");
        usage();
        exit(1);
    }
    if (flag_test_mask == 0)
        flag_test_mask = ~0;
    if (flag_test) {
        srand32(time(NULL));
        test_loopback_buf = AllocMem(TEST_LOOPBACK_BUF * 2, MEMF_PUBLIC);
        if (flag_test_mask & (flag_test_mask - 1))
            do_multiple = 1;
    }
    if (flag_inquiry & (flag_inquiry - 1))
        do_multiple = 1;
    if (!!flag_test + !!flag_inquiry > 1)
        do_multiple = 1;

    for (loop = 0; loop < loops; loop++) {
        if (loops > 1) {
            if (flag_quiet) {
                if ((loop & 0xff) == 0) {
                    printf(".");
                    fflush(stdout);
                }
            } else {
                printf("Pass %-4u ", loop + 1);
                if (do_multiple)
                    printf("\n");
            }
        }
        if (flag_inquiry) {
            if (flag_inquiry &&
                bec_identify() && ((loops == 1) || (loop > 1))) {
                errs++;
                break;
            }
        }
        if (flag_test) {
            if (bec_test(flag_test_mask) && ((loops == 1) || (loop > 1))) {
                errs++;
                break;
            }
        }
        if (is_user_abort())
            goto end_now;
    }
    if (loop < loops) {
        printf("Failed");
end_now:
        if (loops > 1)
            printf(" at pass %u", loop + 1);
        if (errs != 0)
            printf(" (%u errors)", errs);
        printf("\n");
    } else if (flag_quiet && (errs == 0)) {
        printf("Pass %u done\n", loop);
    }
    if (test_loopback_buf != NULL)
        FreeMem(test_loopback_buf, TEST_LOOPBACK_BUF * 2);

    exit(errs ? EXIT_FAILURE : EXIT_SUCCESS);
}
