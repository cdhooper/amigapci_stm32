/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Command implementations.
 */

#include "printf.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "board.h"
#include "main.h"
#include "cmdline.h"
#include <stdbool.h>
#include "timer.h"
#include "uart.h"
#include "cmds.h"
#include "gpio.h"
#include "pcmds.h"
#include "adc.h"
#include "utils.h"
#include "usb.h"
#include "irq.h"
#include "kbrst.h"
#include "config.h"
#include "power.h"
#include "sensor.h"
#include "led.h"

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/memorymap.h>
#define GPIOA_BASE GPIO_PORT_A_BASE
#define GPIOB_BASE GPIO_PORT_B_BASE
#define GPIOC_BASE GPIO_PORT_C_BASE
#define GPIOD_BASE GPIO_PORT_D_BASE
#define GPIOE_BASE GPIO_PORT_E_BASE

#ifndef SRAM_BASE
#define SRAM_BASE 0x20000000U
#endif

#define ROM_BANKS 8

#if defined(STM32F4)
#define FLASH_BASE FLASH_MEM_INTERFACE_BASE
#endif

const char cmd_cpu_help[] =
"cpu hardfault - cause CPU hard fault (bad address)\n"
"cpu regs      - show CPU registers";

const char cmd_gpio_help[] =
"gpio [name=value/mode/?] - display or set GPIOs";

const char cmd_reset_help[] =
"reset              - reset CPU\n"
"reset amiga [hold] - reset Amiga using KBRST (hold leaves it in reset)\n"
"reset amiga long   - reset Amiga with a long reset (change ROM image)\n"
"reset dfu[rom]     - reset into DFU programming mode\n"
"reset reason       - show STM32 previous reset reason\n"
"reset usb          - reset and restart USB interface";

const char cmd_set_help[] =
"set name <name>    - set board name\n"
"set pson <num>     - set power on mode (1=On at AC restore)";

const char cmd_power_help[] =
"power cycle - cycle the power supply off/on\n"
"power on    - turn on power supply\n"
"power off   - turn off power supply\n"
"power show  - display current power status";

const char cmd_snoop_help[] =
"snoop        - capture and report ROM transactions\n"
"snoop addr   - hardware capture A0-A19\n"
"snoop lo     - hardware capture A0-A15 D0-D15\n"
"snoop hi     - hardware capture A0-A15 D16-D31";

const char cmd_usb_help[] =
   "usb debug <mask> - set debug mask\n"
   "usb disable      - reset and disable USB\n"
   "usb kbd [on|off] - take input from USB keyboard\n"
   "usb off          - power off USB ports\n"
   "usb on           - power on USB ports\n"
   "usb regs         - display USB device registers\n"
   "usb reset        - reset and restart USB device\n"
   "usb stats        - USB statistics";

typedef struct {
    const char *const name;
    uintptr_t         addr;
} memmap_t;

static const memmap_t memmap[] = {
    { "ADC1",   ADC1_BASE },
    { "ADC2",   ADC2_BASE },
    { "ADC3",   ADC3_BASE },
    { "AHB1",   PERIPH_BASE_AHB1 },
    { "AHB2",   PERIPH_BASE_AHB2 },
    { "APB1",   PERIPH_BASE_APB1 },
    { "APB2",   PERIPH_BASE_APB2 },
#ifdef STM32F1
    { "AFIO",   AFIO_BASE },
    { "BKP",    BACKUP_REGS_BASE },
#endif
#if defined(STM32F103xE)
    { "BKP",    RTC_BKP_BASE },
#endif
    { "DAC",    DAC_BASE },
    { "DMA1",   DMA1_BASE },
    { "DMA2",   DMA2_BASE },
    { "EXTI",   EXTI_BASE },
    { "FLASH",  FLASH_BASE },
    { "FPEC",   FLASH_MEM_INTERFACE_BASE },
    { "GPIOA",  GPIOA_BASE },
    { "GPIOB",  GPIOB_BASE },
    { "GPIOC",  GPIOC_BASE },
    { "GPIOD",  GPIOD_BASE },
    { "GPIOE",  GPIOE_BASE },
    { "IWDG",   IWDG_BASE },
    { "PWR",    POWER_CONTROL_BASE },
    { "RCC",    RCC_BASE },
    { "RTC",    RTC_BASE },
    { "SCB",    SCB_BASE },
    { "SRAM",   SRAM_BASE },
    { "TIM1",   TIM1_BASE },
    { "TIM2",   TIM2_BASE },
    { "TIM3",   TIM3_BASE },
    { "TIM4",   TIM4_BASE },
    { "TIM5",   TIM5_BASE },
    { "TIM8",   TIM8_BASE },
    { "USART1", USART1_BASE },
    { "USART3", USART3_BASE },
    { "USB0",   USB0_BASE },
    { "USB1",   USB1_BASE },
    { "WWDG",   WWDG_BASE },
};

static uint
time_check(const char *text, uint diff, uint min, uint max)
{
    int errs = 0;
    if ((min <= diff) && (max >= diff)) {
        printf("PASS: ");
    } else {
        printf("FAIL: ");
        errs++;
    }
    printf("%-24s %u usec\n", text, diff);
    return (errs);
}

static rc_t
timer_test(void)
{
    uint64_t start;
    uint64_t diff;
    uint     errs = 0;

    start = timer_tick_get();
    timer_delay_ticks(0);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_ticks(0)", (uint)diff, 0, 5);

    start = timer_tick_get();
    timer_delay_ticks(100);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_ticks(100)", (uint)diff, 2, 5);

    start = timer_tick_get();
    timer_delay_usec(1);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_usec(1)", (uint)diff, 1, 5);

    start = timer_tick_get();
    timer_delay_usec(10);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_usec(10)", (uint)diff, 10, 15);

    start = timer_tick_get();
    timer_delay_usec(1000);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_usec(1000)", (uint)diff, 1000, 1005);

    start = timer_tick_get();
    timer_delay_msec(1);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_msec(1)", (uint)diff, 1000, 1005);

    start = timer_tick_get();
    timer_delay_msec(10);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_msec(10)", (uint)diff, 10000, 10007);

    start = timer_tick_get();
    timer_delay_msec(1000);
    diff = timer_tick_to_usec(timer_tick_get() - start);
    errs += time_check("timer_delay_msec(1000)", (uint)diff, 1000000, 1000007);

    // XXX: Replace one second above with RTC tick?

    if (errs > 0)
        return (RC_FAILURE);
    return (RC_SUCCESS);
}

static rc_t
timer_watch(void)
{
    bool_t   fail = FALSE;
    uint64_t last = timer_tick_get();
    uint64_t now;

    while (1) {
        now = timer_tick_get();
        if (last >= now) {
            printf("\nLast=%llx now=%llx Current=%012llx",
                   (long long) last, (long long) now, timer_tick_get());
        } else {
            if ((last >> 32) != (now >> 32))
                putchar('.');
            last = now;
        }
        if (input_break_pending()) {
            printf("^C\n");
            break;
        }
    }
    return ((fail == TRUE) ? RC_FAILURE : RC_SUCCESS);
}

rc_t
cmd_time(int argc, char * const *argv)
{
    rc_t rc;

    if (argc <= 1)
        return (RC_USER_HELP);

    if (strncmp(argv[1], "cmd", 1) == 0) {
        uint64_t time_start;
        uint64_t time_diff;

        if (argc <= 2) {
            printf("error: time cmd requires command to execute\n");
            return (RC_USER_HELP);
        }
        time_start = timer_tick_get();
        rc = cmd_exec_argv(argc - 2, argv + 2);
        time_diff = timer_tick_get() - time_start;
        printf("%llu us\n", timer_tick_to_usec(time_diff));
        if (rc == RC_USER_HELP)
            rc = RC_FAILURE;
    } else if (strncmp(argv[1], "now", 1) == 0) {
        uint64_t now = timer_tick_get();
        printf("tick=0x%llx uptime=%llu usec\n", now, timer_tick_to_usec(now));
        rc = RC_SUCCESS;
    } else if (strncmp(argv[1], "watch", 1) == 0) {
        rc = timer_watch();
    } else if (strncmp(argv[1], "test", 1) == 0) {
        rc = timer_test();
    } else {
        printf("Unknown argument %s\n", argv[1]);
        return (RC_USER_HELP);
    }
    return (rc);
}

rc_t
cmd_map(int argc, char * const *argv)
{
    uint third = (ARRAY_SIZE(memmap) + 2) / 3;
    uint ent;
    for (ent = 0; ent < third; ent++) {
        printf("    %-6s %08x", memmap[ent].name, memmap[ent].addr);
        if (ent + third < ARRAY_SIZE(memmap))
            printf("    %-6s %08x",
                   memmap[ent + third].name, memmap[ent + third].addr);
        if (ent + third * 2 < ARRAY_SIZE(memmap))
            printf("    %-6s %08x",
                   memmap[ent + third * 2].name, memmap[ent + third * 2].addr);
        printf("\n");
    }
    return (RC_SUCCESS);
}

static void
shutdown_all(void)
{
    uart_flush();
    usb_shutdown();
    timer_delay_msec(30);
//  adc_shutdown();
    timer_shutdown();
    /* Put all peripherals in reset */
    /* Clear NVIC? */
}

rc_t
cmd_reset(int argc, char * const *argv)
{
    if (argc < 2) {
        printf("Resetting...\n");
        shutdown_all();
        reset_cpu();
        return (RC_FAILURE);
    } else if (strcmp(argv[1], "amiga") == 0) {
        uint hold = 0;
        uint longreset = 0;
        while (argc > 2) {
            if (strcmp(argv[2], "hold") == 0)
                hold = 1;
            else if (strcmp(argv[2], "long") == 0)
                longreset = 1;
            else
                printf("Invalid reset amiga \"%s\"\n", argv[2]);
            argc--;
            argv++;
        }
        (void) longreset;
//      kbrst_amiga(hold, longreset);
        if (hold)
            printf("Holding Amiga in reset\n");
        else
            printf("Resetting Amiga\n");
    } else if (strncmp(argv[1], "dfu", 3) == 0) {
        uint isrom = (argv[1][3] == 'r');
        printf("Resetting to DFU%s...\n", isrom ? " in ROM" : "");
        shutdown_all();
        reset_dfu(isrom);
    } else if (strcmp(argv[1], "reason") == 0) {
        show_reset_reason();
    } else if (strcmp(argv[1], "usb") == 0) {
        timer_delay_msec(1);
        usb_shutdown();
//      usb_signal_reset_to_host(1);
        usb_init();
    } else {
        printf("Unknown argument %s\n", argv[1]);
        return (RC_USER_HELP);
    }
    return (RC_SUCCESS);
}

rc_t
cmd_cpu(int argc, char * const *argv)
{
    if (argc < 2)
        return (RC_USER_HELP);
    if (strncmp(argv[1], "regs", 1) == 0) {
        fault_show_regs(NULL);
    } else if (strncmp(argv[1], "hardfault", 2) == 0) {
        fault_hard();
    } else {
        printf("Unknown argument %s\n", argv[1]);
        return (RC_USER_HELP);
    }
    return (RC_SUCCESS);
}

rc_t
cmd_power(int argc, char * const *argv)
{
    const char *cmd;
    cmd = skip(argv[0], "power");
    if (*cmd == '\0') {
        if (argc < 2)
            return (RC_USER_HELP);
        argc--;
        argv++;
        cmd = argv[0];
    }
    if (strcmp(cmd, "cycle") == 0) {
        power_set(POWER_STATE_CYCLE);
    } else if (strcmp(cmd, "on") == 0) {
        power_set(POWER_STATE_ON);
    } else if (strcmp(cmd, "off") == 0) {
        power_set(POWER_STATE_OFF);
    } else if (strncmp(cmd, "show", 1) == 0) {
        power_show();
        sensor_show();
    } else {
        printf("Unknown argument %s\n", cmd);
        return (RC_USER_HELP);
    }
    return (RC_SUCCESS);
}

rc_t
cmd_usb(int argc, char * const *argv)
{
    if (argc < 2)
        return (RC_USER_HELP);
    if (strncmp(argv[1], "debug", 2) == 0) {
        uint debug_mask;
        int  pos = 0;
        if (argc < 3) {
            printf("usb %s requires an argument\n", argv[1]);
            return (RC_USER_HELP);
        }
        if ((sscanf(argv[2], "%x%n", &debug_mask, &pos) != 1) ||
            (argv[2][pos] != '\0')) {
            printf("Invalid argument to debug: %s\n", argv[2]);
            return (RC_USER_HELP);
        }
        usb_debug_mask = debug_mask;
    } else if (strncmp(argv[1], "disable", 3) == 0) {
        timer_delay_msec(1);
        usb_shutdown();
//      usb_signal_reset_to_host(0);
        return (RC_SUCCESS);
    } else if ((strcmp(argv[1], "kbd") == 0) ||
               (strncmp(argv[1], "keyboard", 2) == 0)) {
        if (argc < 3) {
            printf("USB keyboard input: %s\n",
                   usb_keyboard_terminal ? "on" : "off");
        } else if (strcmp(argv[2], "on") == 0) {
            usb_keyboard_terminal = 1;
        } else if (strcmp(argv[2], "off") == 0) {
            usb_keyboard_terminal = 0;
        } else {
            printf("Invalid argument %s\n", argv[2]);
            return (RC_BAD_PARAM);
        }
    } else if (strcmp(argv[1], "off") == 0) {
        usb_set_power(0);
    } else if (strcmp(argv[1], "on") == 0) {
        usb_set_power(1);
    } else if (strncmp(argv[1], "regs", 3) == 0) {
        usb_show_regs();
    } else if (strcmp(argv[1], "reset") == 0) {
        timer_delay_msec(1);
        usb_shutdown();
//      usb_signal_reset_to_host(1);
        usb_init();
        return (RC_SUCCESS);
    } else if (strncmp(argv[1], "stat", 2) == 0) {
        usb_show_stats();
    } else {
        printf("Unknown argument %s\n", argv[1]);
        return (RC_USER_HELP);
    }
    return (RC_SUCCESS);
}

rc_t
cmd_gpio(int argc, char * const *argv)
{
    int arg;
    if (argc < 2) {
        gpio_show(-1, 0xffff);
        return (RC_SUCCESS);
    }

    for (arg = 1; arg < argc; arg++) {
        const char *ptr = argv[arg];
        int port = -1;
        uint16_t pins[NUM_GPIO_BANKS];
        const char *assign = NULL;

        memset(pins, 0, sizeof (pins));

        if (gpio_name_match(&ptr, pins) != 0) {
            if ((*ptr == 'p') || (*ptr == 'P'))
                ptr++;

            /* Find port, if specified */
            if ((*ptr >= 'a') && (*ptr <= 'f'))
                port = *(ptr++) - 'a';
            else if ((*ptr >= 'A') && (*ptr <= 'F'))
                port = *(ptr++) - 'A';

            /* Find pin, if specified */
            if ((*ptr >= '0') && (*ptr <= '9')) {
                uint pin = *(ptr++) - '0';
                if ((*ptr >= '0') && (*ptr <= '9'))
                    pin = pin * 10 + *(ptr++) - '0';
                if (pin > 15) {
                    printf("Invalid argument %s\n", argv[arg]);
                    return (RC_BAD_PARAM);
                }
                pins[port] = BIT(pin);
            } else if (*ptr == '*') {
                ptr++;
                pins[port] = 0xffff;
            }
        }

        if (*ptr == '=') {
            assign = ptr + 1;
            ptr = "";
            if (port == -1) {
                uint tport;
                for (tport = 0; tport < NUM_GPIO_BANKS; tport++)
                    if (pins[tport] != 0)
                        break;
                if (tport == NUM_GPIO_BANKS) {
                    printf("You must specify the GPIO to assign: %s\n",
                           argv[arg]);
                    return (RC_BAD_PARAM);
                }
            }
        }
        if (*ptr != '\0') {
            printf("Invalid argument %s\n", argv[arg]);
            return (RC_BAD_PARAM);
        }

        if (assign != NULL) {
            if (port == -1) {
                for (port = 0; port < NUM_GPIO_BANKS; port++) {
                    if (pins[port] != 0) {
                        gpio_assign(port, pins[port], assign);
                    }
                }
            } else {
                gpio_assign(port, pins[port], assign);
            }
        } else {
            if (port == -1) {
                for (port = 0; port < NUM_GPIO_BANKS; port++)
                    gpio_show(port, pins[port]);
            } else {
                gpio_show(port, pins[port]);
            }
        }
    }

    return (RC_SUCCESS);
}

rc_t
cmd_set(int argc, char * const *argv)
{
    if (argc <= 1) {
        printf("Board name:  ");
        config_name(NULL);
        printf("PS On:       Power %s at AC restored\n",
               config.ps_on_mode ? "on" : "off");
        return (RC_SUCCESS);
    }
    if ((strcmp(argv[1], "help") == 0) ||
               (strcmp(argv[1], "?") == 0)) {
        return (RC_USER_HELP);
    } else if (strcmp(argv[1], "name") == 0) {
        config_name((argc < 2) ? NULL : argv[2]);
        return (RC_SUCCESS);
    } else if (strcmp(argv[1], "pson") == 0) {
        if (argc <= 2)
            return (RC_USER_HELP);
        if (strcmp(argv[2], "on") == 0) {
            config.ps_on_mode |= 1;
        } else if (strcmp(argv[2], "off") == 0) {
            config.ps_on_mode &= 1;
        } else {
            uint mode;
            int  pos = 0;
            if ((sscanf(argv[2], "%x%n", &mode, &pos) != 1) ||
                (argv[2][pos] != '\0') || (mode > 0xff)) {
                printf("Invalid argument: %s\n", argv[2]);
                return (RC_USER_HELP);
            }
            config.ps_on_mode = mode;
            config_updated();
        }
    } else {
        printf("set \"%s\" unknown argument\n", argv[1]);
        return (RC_USER_HELP);
    }

    return (RC_SUCCESS);
}


rc_t
cmd_snoop(int argc, char * const *argv)
{
    printf("Snoop not implemented\n");
    return (RC_USER_HELP);
#if 0
    uint mode = CAPTURE_SW;
    if (argc > 1) {
        if (strcmp(argv[1], "addr") == 0) {
            mode = CAPTURE_ADDR;
        } else if (strncmp(argv[1], "low", 2) == 0) {
            mode = CAPTURE_DATA_LO;
        } else if (strncmp(argv[1], "high", 2) == 0) {
            mode = CAPTURE_DATA_HI;
        } else {
            printf("snoop \"%s\" unknown argument\n", argv[1]);
            return (RC_USER_HELP);
        }
    }
    bus_snoop(mode);

    return (RC_SUCCESS);
#endif
}
