/*
 * becmsg
 * ------
 * Functions for AmigaPCI BEC messaging.
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
#include "../fw/bec_cmd.h"
#include "crc32.h"
#include "becmsg.h"

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

// struct ExecBase *DOSBase;

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

extern uint flag_debug;

/* RTC offsets for Ricoh RP5C01 in AmigaPCI */
#define RP_ONE_SEC   (0x0 * 4 + 1)  // M0 Second One's
#define RP_TEN_SEC   (0x1 * 4 + 1)  // M0 Second Ten's
#define RP_ONE_MIN   (0x2 * 4 + 1)  // M0 Minute One's
#define RP_TEN_MIN   (0x3 * 4 + 1)  // M0 Minute Ten's
#define RP_ONE_HOUR  (0x4 * 4 + 1)  // M0 Hour One's
#define RP_TEN_HOUR  (0x5 * 4 + 1)  // M0 Hour Ten's
#define RP_DOW       (0x6 * 4 + 1)  // M0 Day of Week
#define RP_ONE_DAY   (0x7 * 4 + 1)  // M0 Day of Month One's
#define RP_TEN_DAY   (0x8 * 4 + 1)  // M0 Day of Month Ten's
#define RP_ONE_MONTH (0x9 * 4 + 1)  // M0 Month of Year One's
#define RP_TEN_MONTH (0xa * 4 + 1)  // M0 Month of Year Ten's
#define RP_ONE_YEAR  (0xb * 4 + 1)  // M0 Year of Century One's
#define RP_TEN_YEAR  (0xc * 4 + 1)  // M0 Year of Century Ten's
#define RP_MODE      (0xd * 4 + 1)  // M0 Mode register
#define RP_TEST      (0xe * 4 + 1)  // M0 Test register
#define RP_RESET     (0xf * 4 + 1)  // M0 Reset controller, etc
#define RP_MAGIC     (0x9 * 4 + 1)  // M1 AmigaPCI magic register
#define RP_MAGIC_HI  (0x0 * 4 + 1)  // M1 AmigaPCI magic register hi
#define RP_MAGIC_LO  (0x1 * 4 + 1)  // M1 AmigaPCI magic register lo
#define RP_M1_12_24  (0xa * 4 + 1)  // M1 12/24 Hour select (0=AM, 1=24, 2=PM)
#define RP_M1_LEAP   (0xb * 4 + 1)  // M1 Leap Year Counter (increments w/ year

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
extern unsigned int irq_disabled;

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
    uint16_t diff = 0;

    while (1) {
        now = cia_ticks();

        diff = start - now;
        if (diff >= ticks)
            break;
        ticks -= diff;
        start = now;
        __asm__ __volatile__("nop");
    }
}

#if 0
static inline void
rtc_delay(void)
{
#if 0
//  for (int i = 0; i < 1000; i++)
        __asm("nop");
#endif
#if 0
    __asm("nop");
    __asm("nop");
    __asm("nop");
#endif
}
#else
#define rtc_delay()
#endif

static inline uint8_t
get_nibble_hi(void)
{
    uint8_t data;
    rtc_delay();
    data = RTC_REG(RP_MAGIC_HI) & 0x0f;
    rtc_delay();
    return (data);
}

static inline uint8_t
get_nibble_lo(void)
{
    uint8_t data;
    rtc_delay();
    data = RTC_REG(RP_MAGIC_LO) & 0x0f;
    rtc_delay();
    return (data);
}

static uint8_t
get_byte(void)
{
    return ((get_nibble_hi() << 4) | get_nibble_lo());
}

static void
send_nibble_hi(uint8_t nibble)
{
    rtc_delay();
    RTC_REG(RP_MAGIC_HI) = nibble;
    rtc_delay();
}

static void
send_nibble_lo(uint8_t nibble)
{
    rtc_delay();
    RTC_REG(RP_MAGIC_LO) = nibble;
    rtc_delay();
}

static void
send_byte(uint8_t byte)
{
    send_nibble_hi(byte >> 4);
    send_nibble_lo(byte);
}

uint
send_cmd(uint8_t cmd, void *arg, uint16_t arglen,
         void *reply, uint replymax, uint *replyalen)
{
    uint     pos;
    uint     timeout;
    uint8_t *argbuf = arg;
    uint8_t *replybuf = reply;
    uint8_t  got_magic[4];
    uint     bad_magic = 0;
    uint8_t  status;
    uint16_t msglen;
    uint32_t crc;
    uint32_t got_crc;
    uint32_t calc_crc;

    Forbid();
    RTC_REG(RP_MODE) = RP_MODE_M1 | RP_MODE_TIMER_EN;
    rtc_delay();
    send_nibble_hi(bec_magic[0]);
    send_nibble_lo(bec_magic[1]);
    send_nibble_hi(bec_magic[2]);
    send_nibble_lo(bec_magic[3]);
    send_byte(cmd);
    send_byte(arglen >> 8);
    send_byte(arglen);
    for (pos = 0; pos < arglen; pos++)
        send_byte(argbuf[pos]);
    crc = crc32(0, &cmd, 1);
    crc = crc32(crc, &arglen, 2);
    crc = crc32(crc, argbuf, arglen);
    send_byte(crc >> 24);
    send_byte(crc >> 16);
    send_byte(crc >> 8);
    send_byte(crc);

    /* Wait for reply (up to 500ms) */
    for (timeout = 25000; timeout > 0; timeout--) {
        cia_spin(CIA_USEC(50));
        if ((got_magic[0] = get_nibble_hi()) == bec_magic[0])
            break;
    }

    if (timeout == 0)
        return (BEC_STATUS_TIMEOUT);

    got_magic[1] = get_nibble_lo();
    got_magic[2] = get_nibble_hi();
    got_magic[3] = get_nibble_lo();

    for (pos = 1; pos < ARRAY_SIZE(bec_magic); pos++)
        if (got_magic[pos] != bec_magic[pos])
            bad_magic++;

    if (bad_magic) {
        if (flag_debug) {
            printf("BEC bad magic:");
            for (pos = 0; pos < ARRAY_SIZE(got_magic); pos++)
                printf(" %x", got_magic[pos]);
            printf("\n");
        }
        return (BEC_STATUS_BADMAGIC);
    }

    /* Got magic -- get remainder of message */
    status = get_byte();
    msglen = (uint16_t) get_byte() << 8;
    msglen |= get_byte();
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
    got_crc  = (get_byte() << 24);
    got_crc |= (get_byte() << 16);
    got_crc |= (get_byte() << 8);
    got_crc |= get_byte();
    Permit();

    calc_crc = crc32(0, &status, 1);
    calc_crc = crc32(calc_crc, &msglen, 2);
    calc_crc = crc32(calc_crc, replybuf, msglen);
    if (calc_crc != got_crc) {
        if (flag_debug) {
            printf("Bad CRC %08x != calc %08x rc=%x l=%x\n",
                   got_crc, calc_crc, status, msglen);
        }
        return (BEC_STATUS_REPLYCRC);
    }

    return (status);
}

uint
send_cmd_retry(uint8_t cmd, void *arg, uint16_t arglen,
               void *reply, uint replymax, uint *replyalen)
{
    uint tries = 10;
    uint rc;

    do {
        rc = send_cmd(cmd, arg, arglen, reply, replymax, replyalen);
        if ((rc != BEC_STATUS_CRC) &&
            (rc != BEC_STATUS_REPLYLEN) &&
            (rc != BEC_STATUS_REPLYCRC) &&
            (rc != BEC_STATUS_BADMAGIC) &&
            (rc != BEC_STATUS_TIMEOUT)) {
            break;
        }
    } while (--tries > 0);
    return (rc);
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
