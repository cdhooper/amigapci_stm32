#include <stdint.h>

#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/nvic.h>

#include <libopencm3/stm32/f2/rtc.h>
#include <libopencm3/stm32/f2/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scb.h>

#include <string.h>
#include "printf.h"
#include "amigartc.h"
#include "main.h"
#include "gpio.h"
#include "uart.h"
#include "utils.h"
#include "rtc.h"
#include "cmdline.h"

#undef RESPOND_AS_RP5C01

#undef INTERRUPT_CAPTURE_RP5C01
#ifdef INTERRUPT_CAPTURE_RP5C01
static volatile uint16_t amigartc_cap[512];
static volatile uint16_t amigartc_cap_count = 0;
#endif

volatile uint8_t rtc_data[4][0x10];
volatile uint8_t rtc_cur_bank;
static volatile uint8_t rtc_timer_en;
static volatile uint8_t rtc_touched;
static volatile uint8_t rtc_ram_touched;

static const uint8_t rtc_mask[4][0x10] = {
    { 0xf, 0x7, 0xf, 0x7, 0xf, 0x3, 0x7, 0xf,
      0x3, 0xf, 0x1, 0xf, 0xf, 0xf, 0xf, 0xf },
    { 0x0, 0x0, 0xf, 0x7, 0xf, 0x3, 0x7, 0xf,
      0x3, 0x0, 0x1, 0x3, 0x0, 0xf, 0xf, 0xf },
    { 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
      0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf },
    { 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
      0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf },
};

/*
 * RP5C01 registers (each register is 4 bits)
 *
 *      MODE0              MODE1                MODE2   MODE3
 *   0  1-second counter   x                    RAM     RAM
 *   1  10-second counter  x                    RAM     RAM
 *   2  1-minute counter   1-minute alarm       RAM     RAM
 *   3  10-minute counter  10-minute alarm      RAM     RAM
 *   4  1-hour counter     1-hour alarm         RAM     RAM
 *   5  10-hour counter    10-hour alarm        RAM     RAM
 *   6  day-of-week count  day-of-week alarm    RAM     RAM
 *   7  1-day counter      1-day alarm          RAM     RAM
 *   8  10-day counter     10-day alarm         RAM     RAM
 *   9  1-month counter    x                    RAM     RAM
 *   A  10-mount counter   12/24 hour+AM/PM     RAM     RAM
 *   B  1-year counter     Leap year counter    RAM     RAM
 *   C  10-year counter    x (0 for AmigaOS)    RAM     RAM
 *   D  MODE register      MODE register        ditto   ditto
 *   E  TEST register      TEST register        ditto   ditto
 *   F  RESET register     RESET register       ditto   ditto
 *
 * Control registers    D3          D2          D1          D0
 * D  MODE register     TIMER_EN    ALARM_EN    MODE_M1     MODE_M0
 * E  TEST register     TEST3       TEST2       TEST1       TEST0
 * F  RESET register    1HZ         16HZ        TIMER_RESET ALARM_RESET
 *
 * D  MODE register     TIMER_EN    ALARM_EN    MODE_M1     MODE_M0
 *      TIMER_EN=1 to enable the RTCc counters
 *      ALARM_EN=1 to enable alarm output (not connected on Amiga)
 *      M1=0 M0=0  MODE00: Setting and read of time
 *      M1=0 M0=1  MODE01: Setting and read of alarm, 12/24 hour, and leap year
 *      M1=1 MO=0  MODE10: RAM Block10
 *      M1=1 MO=1  MODE11: RAM Block11
 *
 *      Documentation implies that setting TIMER_EN=0 for less than a second
 *      will not result in loss of time (the lower order tick still occurs,
 *      and will be applied after TIMER_EN=1)
 *
 * TEST register
 *      Undocumented
 *
 * RESET register
 *      D0=1 reset all alarm registers
 *      D1=1 reset divider stages for second and smaller units
 *      D2=0 16Hz pulse at ALARM output pin
 *      D3=0 1Hz pulse at ALARM output pin
 *
 * 12/24 hour+AM/PM register
 *      D0=1 for 24-hour system; D0=0 for 12-hour system
 *      D1=1 for p.m. (12-hour system); D1=0 for a.m. (12-hour system)
 *
 * Leap year counter
 *      Counter for years since last leap year (valid values 0-3)
 *      00 = This year is a leap year
 *
 */

// GPIO_IDR(A4_PORT);
//
// set 1:
//      GPIO_BSRR(GPIOx) = GPIO_Pins;
// set 0:
//      GPIO_BSRR(GPIOx) = GPIO_Pins << 16;

static void
amigartc_snoop_show(uint16_t *cap, uint count, int debug)
{
    uint cur;
    printf("Addr=");
    for (cur = 0; cur < count; cur++) {
        if (debug || ((cap[cur] & RTCEN_PIN) == 0))
            printf("%x", (cap[cur] >> 10) & 0xf);  // A2 = GPIO8
        else
            putchar(' ');
    }
    printf("\nData=");
    for (cur = 0; cur < count; cur++) {
        if (debug || ((cap[cur] & RTCEN_PIN) == 0))
            printf("%x", (cap[cur] >> 4) & 0xf);  // D16 = GPIO4
        else
            putchar(' ');
    }
    printf("\nOper=");
    for (cur = 0; cur < count; cur++) {
        if ((cap[cur] & RTCEN_PIN) != 0)
            putchar(' ');
        else if ((cap[cur] & R_WA_PIN))
            putchar('R');
        else
            putchar('W');
    }
    printf("\n");
}

void
amigartc_snoop(int debug)
{
    uint gpio_value;
    uint gpio_last = 0;
    uint count = 0;
    uint timeout = 2000000;
    uint first_cap = 1;
    uint16_t cap[512];
    printf("Press ^C to end\n");

    nvic_disable_irq(NVIC_EXTI0_IRQ);

    while (1) {
        if (--timeout == 0) {
dump_cap:
            if (input_break_pending()) {
                printf("^C\n");
                break;
            }
            if (count > first_cap) {
                amigartc_snoop_show(cap, count, debug);
                count = 0;
                first_cap = 0;
            }
            timeout = 2000000;
        }
        gpio_value = GPIO_IDR(A4_PORT);
        gpio_value &= (A2_PIN | A3_PIN | A4_PIN | A5_PIN |
                       D16_PIN | D17_PIN | D18_PIN | D19_PIN |
                       R_WA_PIN | RTCEN_PIN);
        if ((((gpio_value & RTCEN_PIN) == 0) ||
             ((gpio_last & RTCEN_PIN) == 0)) &&
            (gpio_last != gpio_value)) {
            gpio_last = gpio_value;
            cap[count++] = gpio_value;
            if (count == ARRAY_SIZE(cap))
                goto dump_cap;  // No more space
        }
    }
    nvic_enable_irq(NVIC_EXTI0_IRQ);
}

#define likely(x)   __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

void
exti0_isr(void)
{
    uint count;
    uint32_t gpio_value = GPIO_IDR(A4_PORT);
#ifdef INTERRUPT_CAPTURE_RP5C01
    if (amigartc_cap_count < ARRAY_SIZE(amigartc_cap)) {
        amigartc_cap[amigartc_cap_count] = gpio_value;
        amigartc_cap_count++;
    }
#endif

    if ((gpio_value & RTCEN_PIN) == 0) {
        if (likely(gpio_value & R_WA_PIN)) {
            /* Amiga reading from RP5C01 */
#ifdef RESPOND_AS_RP5C01
            uint addr = (gpio_value >> 10) & 0xf;
            uint data = rtc_data[rtc_cur_bank][addr];
//          GPIO_BSRR(A4_PORT) = 0x00f40000 | (data << 4);  // Assert data pins
            GPIO_BSRR(A4_PORT) = 0x00f00000 | (data << 4);  // Assert data pins
#endif
// printf("%x", data);

            /* Wait until RTCEN is released */
            for (count = 0; count < 1000; count++)
                if (GPIO_IDR(A4_PORT) & RTCEN_PIN)
                    break;
//          GPIO_BSRR(A4_PORT) = 0x000000f4;  // De-assert data pins
            GPIO_BSRR(A4_PORT) = 0x000000f0;  // De-assert data pins
        } else {
            /* Amiga writing to RP5C01 */
            uint addr;
            uint data;
            uint bank;
#if 0
            uint32_t nvalue = GPIO_IDR(A4_PORT);
            if (((nvalue & RTCEN_PIN) == 0) && ((nvalue & R_WA_PIN) == 0)) {
                gpio_value = nvalue;
//              printf(".");
            }
#endif
            addr = (gpio_value >> 10) & 0xf;
            data = (gpio_value >> 4) & 0xf;
            bank = rtc_cur_bank;
            if (bank == 1) {
                // XXX: Could intercept magic writes here
                if ((addr == 0) || (addr == 1) || (addr == 9) || (addr == 12))
                    data = 0;  // Reserved register
            }
            rtc_data[bank][addr] = data & rtc_mask[bank][addr];
            switch (addr) {
                default:
                    if ((bank == 2) || (bank == 3))
                        rtc_ram_touched++;
                    else
                        rtc_touched++;
                    break;
                case 0x0d:  // MODE register
#define RTC_MODE_TIMER_EN    BIT(3)
#define RTC_MODE_TIMER_MODE (BIT(0) | BIT(1))
                    rtc_cur_bank = data & RTC_MODE_TIMER_MODE;
                    rtc_timer_en = data & RTC_MODE_TIMER_EN;
                    goto common_register;
                case 0x0e:  // TEST register
                    data = 0;
                    goto common_register;
                case 0x0f:  // RESET register
common_register:
                    rtc_data[0][addr] = data;
                    rtc_data[1][addr] = data;
                    rtc_data[2][addr] = data;
                    break;
            }

            /* Wait until RTCEN is released */
            for (count = 0; count < 1000; count++)
                if (GPIO_IDR(A4_PORT) & RTCEN_PIN)
                    break;
        }
    }

#define exti_reset_request(x) EXTI_PR = x
    exti_reset_request(EXTI0);
}

/*
 * STM32 RTC RAM map (must be writen as 32-bit values)
 *
 * RTC_BKPXR(x): 20 x 32-bit values
 *     0: Magic number 0xafc05039
 *     1: RP5C01 Alarm date/time
 *          Bits 0-3   alarm 1-minute
 *          Bits 4-7   alarm 10-minute
 *          Bits 8-11  alarm 1-hour
 *          Bits 12-15 alarm 10-hour
 *          Bits 16-19 alarm day-of-week
 *          Bits 20-23 alarm 1-day
 *          Bits 24-27 alarm 10-day
 *     2: RP5C01 SRAM BLOCK 10, first half
 *          Bits 0-3    A=0   Bits 16-19  A=4
 *          Bits 4-7    A=1   Bits 20-23  A=5
 *          Bits 8-11   A=2   Bits 24-27  A=6
 *          Bits 12-15  A=3   Bits 28-31  A=7
 *     3: RP5C01 SRAM BLOCK 10, second half
 *          Bits 0-3    A=8   Bits 12-15  A=B
 *          Bits 4-7    A=9   Bits 16-19  A=C
 *          Bits 8-11   A=A
 *     4: RP5C01 SRAM BLOCK 11, first half
 *          Bits 0-3    A=0   Bits 16-19  A=4
 *          Bits 4-7    A=1   Bits 20-23  A=5
 *          Bits 8-11   A=2   Bits 24-27  A=6
 *          Bits 12-15  A=3   Bits 28-31  A=7
 *     5: RP5C01 SRAM BLOCK 11, second half
 *          Bits 0-3    A=8   Bits 12-15  A=B
 *          Bits 4-7    A=9   Bits 16-19  A=C
 *          Bits 8-11   A=A
 */
#define RTC_RAM_MAGIC 0xafc05039
static void
amigartc_copy_ram_rp5c01_to_stm32(void)
{
    rtc_allow_writes(TRUE);
    RTC_BKPXR(0) = ~RTC_RAM_MAGIC;
    RTC_BKPXR(2) = rtc_data[2][0] |
                   (rtc_data[2][1] << 4) |
                   (rtc_data[2][2] << 8) |
                   (rtc_data[2][3] << 12) |
                   (rtc_data[2][4] << 16) |
                   (rtc_data[2][5] << 20) |
                   (rtc_data[2][6] << 24) |
                   (rtc_data[2][7] << 28);
    RTC_BKPXR(3) = rtc_data[2][8] |
                   (rtc_data[2][9] << 4) |
                   (rtc_data[2][10] << 8) |
                   (rtc_data[2][11] << 12) |
                   (rtc_data[2][12] << 16);
    RTC_BKPXR(4) = rtc_data[3][0] |
                   (rtc_data[3][1] << 4) |
                   (rtc_data[3][2] << 8) |
                   (rtc_data[3][3] << 12) |
                   (rtc_data[3][4] << 16) |
                   (rtc_data[3][5] << 20) |
                   (rtc_data[3][6] << 24) |
                   (rtc_data[3][7] << 28);
    RTC_BKPXR(5) = rtc_data[3][8] |
                   (rtc_data[3][9] << 4) |
                   (rtc_data[3][10] << 8) |
                   (rtc_data[3][11] << 12) |
                   (rtc_data[3][12] << 16);
    RTC_BKPXR(0) = RTC_RAM_MAGIC;
    rtc_allow_writes(FALSE);
}

static void
amigartc_copy_time_rp5c01_to_stm32(void)
{
    uint second   = rtc_data[0][0]  + rtc_data[0][1]  * 10;
    uint minute   = rtc_data[0][2]  + rtc_data[0][3]  * 10;
    uint hour     = rtc_data[0][4]  + rtc_data[0][5]  * 10;
    uint day      = rtc_data[0][7]  + rtc_data[0][8]  * 10;
    uint month    = rtc_data[0][9]  + rtc_data[0][10] * 10;
    uint year     = rtc_data[0][11] + rtc_data[0][12] * 10;
    uint dow      = rtc_data[0][6];
    uint hour_24  = rtc_data[1][0xa] & BIT(0);
    uint am_pm    = rtc_data[1][0xa] & BIT(1);

    rtc_allow_writes(TRUE);
    rtc_set_date(year, month, day, dow);
    rtc_set_time(hour, minute, second, am_pm, hour_24);
    rtc_allow_writes(FALSE);

    RTC_BKPXR(1) = rtc_data[1][2] |
                   (rtc_data[1][3] << 4) |
                   (rtc_data[1][4] << 8) |
                   (rtc_data[1][5] << 12) |
                   (rtc_data[1][6] << 16) |
                   (rtc_data[1][7] << 20) |
                   (rtc_data[1][8] << 24);
}

static void
amigartc_copy_time_stm32_to_rp5c01(void)
{
    uint tr       = RTC_TR;
    uint dr       = RTC_DR;
    uint alarm    = RTC_BKPXR(1);
    uint second   = rtc_bcd_to_binary(tr & 0x7f);
    uint minute   = rtc_bcd_to_binary((tr >> 8) & 0x7f);
    uint hour     = rtc_bcd_to_binary((tr >> 16) & 0x3f);
    uint day      = rtc_bcd_to_binary(dr & 0x3f);
    uint month    = rtc_bcd_to_binary((dr >> 8) & 0x1f);
    uint year     = rtc_bcd_to_binary(dr >> 16);
    uint dow      = (dr >> 13) & 7;
    uint hour_24  = (RTC_CR & RTC_CR_FMT) ? 1 : 0;
    uint am_pm    = ((dr >> 22) & 1) << 1;  // Shift left for BIT(1) position
    uint leap_c;

    if (year >= 70)
        leap_c = (year - 72) % 4;
    else
        leap_c = year % 4;  // XXX: How to represent 2000 as not a leap year?

#if 0
printf(" %02u:%02u:%02u", hour, minute, second);
printf(" %02u-%02u-%02u", year, month, day);
#endif
    rtc_data[0][0]  = second % 10;
    rtc_data[0][1]  = second / 10;
    rtc_data[0][2]  = minute % 10;
    rtc_data[0][3]  = minute / 10;
    rtc_data[0][4]  = hour % 10;
    rtc_data[0][5]  = hour / 10;
    rtc_data[0][6]  = dow;
    rtc_data[0][7]  = day % 10;
    rtc_data[0][8]  = day / 10;
    rtc_data[0][9]  = month % 10;
    rtc_data[0][10] = month / 10;
    rtc_data[0][11] = year % 10;
    rtc_data[0][12] = year / 10;
    rtc_data[1][10] = hour_24 | am_pm;
    rtc_data[1][11] = leap_c;

    if (RTC_BKPXR(0) != RTC_RAM_MAGIC)  // Invalid magic
        return;

    rtc_data[1][2] = alarm & 0xf;           // 1-minute alarm register
    rtc_data[1][3] = (alarm >> 4) & 0xf;    // 10-minute alarm register
    rtc_data[1][4] = (alarm >> 8) & 0xf;    // 1-hour alarm register
    rtc_data[1][5] = (alarm >> 12) & 0xf;   // 10-hour alarm register
    rtc_data[1][6] = (alarm >> 16) & 0xf;   // Day-of-the-week register
    rtc_data[1][7] = (alarm >> 20) & 0xf;   // 1-day alarm register
    rtc_data[1][8] = (alarm >> 24) & 0xf;   // 10-day alarm register
}

static void
amigartc_copy_ram_stm32_to_rp5c01(void)
{
    uint32_t temp;
    if (RTC_BKPXR(0) != RTC_RAM_MAGIC)  // Invalid magic
        return;

    temp = RTC_BKPXR(2);
    rtc_data[2][0] = temp & 0xf;
    rtc_data[2][1] = (temp >> 4) & 0xf;
    rtc_data[2][2] = (temp >> 8) & 0xf;
    rtc_data[2][3] = (temp >> 12) & 0xf;
    rtc_data[2][4] = (temp >> 16) & 0xf;
    rtc_data[2][5] = (temp >> 20) & 0xf;
    rtc_data[2][6] = (temp >> 24) & 0xf;
    rtc_data[2][7] = (temp >> 28) & 0xf;
    temp = RTC_BKPXR(3);
    rtc_data[2][8] = temp & 0xf;
    rtc_data[2][9] = (temp >> 4) & 0xf;
    rtc_data[2][10] = (temp >> 8) & 0xf;
    rtc_data[2][11] = (temp >> 12) & 0xf;
    rtc_data[2][12] = (temp >> 16) & 0xf;
    temp = RTC_BKPXR(4);
    rtc_data[3][0] = temp & 0xf;
    rtc_data[3][1] = (temp >> 4) & 0xf;
    rtc_data[3][2] = (temp >> 8) & 0xf;
    rtc_data[3][3] = (temp >> 12) & 0xf;
    rtc_data[3][4] = (temp >> 16) & 0xf;
    rtc_data[3][5] = (temp >> 20) & 0xf;
    rtc_data[3][6] = (temp >> 24) & 0xf;
    rtc_data[3][7] = (temp >> 28) & 0xf;
    temp = RTC_BKPXR(5);
    rtc_data[3][8] = temp & 0xf;
    rtc_data[3][9] = (temp >> 4) & 0xf;
    rtc_data[3][10] = (temp >> 8) & 0xf;
    rtc_data[3][11] = (temp >> 12) & 0xf;
    rtc_data[3][12] = (temp >> 16) & 0xf;
}

// XXX: rtc_wkup_isr() is triggered by RTC_CR and RTC_WUTR
//      Maximum rate appears to be 0.5 Hz. Will probably need to change
//      to using some other timer so that the RP5C01 time is updated
//      every second.
void
rtc_wkup_isr(void)
{
//  printf(" WKUP");
    exti_reset_request(EXTI22);
    RTC_ISR &= ~RTC_ISR_WUTF;

    /* Sweep STM32 RTC to RP5C01 RAM */
    if (rtc_timer_en && !rtc_touched) {
        amigartc_copy_time_stm32_to_rp5c01();
    }
}

#if 0
void
rtc_alarm_isr(void)
{
    exti_reset_request(EXTI17);
    RTC_ISR &= ~(RTC_ISR_ALRAF | RTC_ISR_ALRBF);

    printf(" ALRM");
}
#endif

void
amigartc_print(void)
{
    uint second   = rtc_data[0][0]  + rtc_data[0][1]  * 10;
    uint minute   = rtc_data[0][2]  + rtc_data[0][3]  * 10;
    uint hour     = rtc_data[0][4]  + rtc_data[0][5]  * 10;
    uint day      = rtc_data[0][7]  + rtc_data[0][8]  * 10;
    uint month    = rtc_data[0][9]  + rtc_data[0][10] * 10;
    uint year     = rtc_data[0][11] + rtc_data[0][12] * 10;
    uint dow      = rtc_data[0][6];
    uint a_minute = rtc_data[1][2] + rtc_data[0][3]  * 10;
    uint a_hour   = rtc_data[1][4] + rtc_data[0][5]  * 10;
    uint a_day    = rtc_data[1][7] + rtc_data[0][8]  * 10;
    uint a_month  = rtc_data[1][9] + rtc_data[0][10] * 10;
    uint a_dow    = rtc_data[1][6];
    uint hour_24  = rtc_data[1][0xa] & BIT(0);
    uint am_pm    = rtc_data[1][0xa] & BIT(1);

    if (year >= 70)
        year += 1900;
    else
        year += 2000;
    printf("artc=%04u-%02u-%02u %02u:%02u:%02u dow=%u %u-hr%s  "
           "alarm=%02u-%02u %02u:%02u dow=%u EN=%u\n",
           year, month, day, hour, minute, second, dow,
           hour_24 ? 24 : 12, hour_24 ? "" : am_pm ? " p.m." : " a.m.",
           a_month, a_day, a_hour, a_minute, a_dow, !!rtc_timer_en);
}

void
amigartc_poll(void)
{
#ifdef INTERRUPT_CAPTURE_RP5C01
    if (amigartc_cap_count != 0) {
        static uint last;
        if (last != amigartc_cap_count) {
            last = amigartc_cap_count;
            return;  // Wait until it stops chaning
        }
        printf("cap count=%u\n", amigartc_cap_count);
        amigartc_cap_count = 0;
        amigartc_snoop_show((uint16_t *) amigartc_cap, last, 0);
        last = 0;
    }
#endif
    if (rtc_touched && rtc_timer_en) {
//  if (rtc_touched) {
        rtc_touched = 0;

        /* Sweep RP5C01 date/time to STM32 RTC */
        amigartc_copy_time_rp5c01_to_stm32();
        amigartc_print();
    }
    if (rtc_ram_touched) {
        rtc_ram_touched = 0;

        /* Sweep RP5C01 RAM to STM32 RTC */
        amigartc_copy_ram_rp5c01_to_stm32();
        printf("RP->STM32 RAM\n");
    }
}

void
amigartc_init(void)
{
    rtc_cur_bank    = 0;
    rtc_touched     = 0;
    rtc_ram_touched = 0;
    rtc_timer_en    = 1;

    amigartc_copy_time_stm32_to_rp5c01();
    amigartc_copy_ram_stm32_to_rp5c01();

    rcc_periph_clock_enable(RCC_SYSCFG);

    nvic_set_priority(NVIC_EXTI0_IRQ, 0x10);
    nvic_enable_irq(NVIC_EXTI0_IRQ);

    /* Map PB0 to EXTI0 */
    exti_select_source(EXTI0, GPIOB);
    exti_set_trigger(EXTI0, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXTI0);
    exti_reset_request(EXTI0);

// EXTI line 17 is connected to the RTC Alarm event
// EXTI line 21 is connected to the RTC Tamper and TimeStamp events
// EXTI line 22 is connected to the RTC Wakeup event
    exti_set_trigger(EXTI7, EXTI_TRIGGER_RISING);   // ALARM
    exti_enable_request(EXTI7);
    exti_reset_request(EXTI7);
//  exti_set_trigger(EXTI21, EXTI_TRIGGER_RISING);
//  exti_enable_request(EXTI21);
//  exti_reset_request(EXTI21);
    exti_set_trigger(EXTI22, EXTI_TRIGGER_RISING);  // WKUP
    exti_enable_request(EXTI22);
    exti_reset_request(EXTI22);
}
