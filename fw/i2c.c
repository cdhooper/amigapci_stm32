/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * I2C functions.
 */

#include <stdint.h>
#include <string.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include "main.h"
#include "adc.h"
#include "board.h"
#include "clock.h"
#include "cmdline.h"
#include "config.h"
#include "gpio.h"
#include "i2c.h"
#include "irq.h"
#include "led.h"
#include "main.h"
#include "printf.h"
#include "timer.h"
#include "uart.h"
#include "utils.h"
#include "crc8.h"
#include "cmdline.h"
#include "sched.h"
#include <limits.h>

#define MODE_MAXLEN 0xff

#define i2c_irq_disable() 0
#define i2c_irq_enable(x) (void) x

/* The following timeouts are all specified in milliseconds */
#define I2C_SCL_RISE_TIMEOUT     30  // Max time slave can hold SCL
#define I2C_SDA_RISE_TIMEOUT     1   // Max time for SDA to rise
#define I2C_RETRY_MAX            1   // I2C failed access retry limit (tries)
#define I2C_COMPARE_MAX          3   // I2C failed compare limit (tries)
#define I2C_RECOVER_BACKOFF_MSEC 60000  // 60 seconds maximum

/* GPIO signal positions in i2c_busdef_t */
#define I2C_GPIO_SCL       0  // I2C SCL (clock) GPIO
#define I2C_GPIO_SDA       1  // I2C SDA (data) GPIO

/* Default speed is ~300 kHz */
#define I2C_BUS_SPEED_50KHZ  0x01  // 50 KHz speed
#define I2C_BUS_SPEED_100KHZ 0x02  // 100 KHz speed
#define I2C_BUS_SPEED_5KHZ   0x04  // 5 KHz speed

/*
 * The below macros convert a peripheral address and bit number to the
 * equivalent bit-band peripheral address. It is used for bit control
 * of the SDA and SCL pins when performing software bit-bang I2C.
 *
 * The following is from the STM32F2 reference manual:
 *     bit_word_addr = bit_band_base + (byte_offset x 32) + (bit_number x 4)
 */
#define IO_BASE              0x40000000
#define BND_IO_BASE          0x42000000
#define GPIO_IDR_OFFSET      0x10  // Input Data Register offset
#define GPIO_ODR_OFFSET      0x14  // Output Data Register offset
#define BND_IO(byte, bit)    (BND_IO_BASE + ((byte) - IO_BASE) * 32 + (bit) * 4)
#define BND_ODR_TO_IDR(addr) ((addr) + (GPIO_IDR_OFFSET - GPIO_ODR_OFFSET) * 32)

const char cmd_i2c_help[] =
"i2c probe [bus <bus>][verbose] - Probe I2C bus for devices\n"
"i2c stats [clear|show]         - Show I2C statistics\n"
#ifdef UNDOCUMENTED_CMD
"i2c reset                      - Reset I2C masters (DEBUG)\n"
#endif
"i2c verbose                    - Enable/disable verbose I2C\n"
"";

static const char i2c_help_amigapci[] =
"  00 No devices are expected\n"
"";

typedef struct {
    char     name[8];
    uint32_t port[2];
    uint16_t pin[2];
    uint8_t  speed;   // speed flags
} i2c_busdef_t;

static const i2c_busdef_t i2c_busdef_amigapci[] = {
/*    Name      SCLport SDAport       SCLpin  SDApin    Flags    */
    { "A10A15",  {GPIOA,  GPIOA, }, { GPIO10, GPIO15, }, 0x00 },
};

/* I2C error message reason codes */
/* Don't change their order, they must correspond to i2c_dbg_description[] */
typedef enum {
    WHY_UNKNOWN = 0,        // Unknown
    WHY_SCL_RISE_TMO,       // SCL rise timeout
    WHY_SDA_RISE_TMO,       // SDA rise timeout
    WHY_SCL_STUCK,          // BitBang I2C: SCL stuck (and cant be recovered)
    WHY_SCL_RECOVER,        // SCL recovered (toggling SDA cleared it)
    WHY_SDA_STUCK,          // BitBang I2C: SDA stuck (and cant be recovered)
    WHY_SDA_RECOVER,        // SDA recovered (toggling SCL cleared it)
    WHY_ADDR_TIMEOUT,       // SDA recovered (toggling SCL cleared it)
    WHY_OFFSET_TIMEOUT,     // SDA recovered (toggling SCL cleared it)
    WHY_START_TIMEOUT,      // Start sequence timeout
    WHY_SLAVE_TIMEOUT,      // Slave data timeout
    WHY_STOP_TIMEOUT,       // Stop bit timeout
    WHY_MAX
} i2c_why_t;

/* Must fit into 16 bits */
CC_ASSERT(WHY_MAX < 16, WHY_MAX);

/* I2C debug description */
static const char *i2c_dbg_description[] = {
    "",                     // WHY_UNKNOWN
    "SCL rise timeout",     // WHY_SCL_RISE_TMO
    "SDA rise timeout",     // WHY_SDA_RISE_TMO
    "SCL stuck",            // WHY_SCL_STUCK
    "SCL recovered",        // WHY_SCL_RECOVER
    "SDA stuck",            // WHY_SDA_STUCK
    "SDA recovered",        // WHY_SDA_RECOVER
    "Addr timeout",         // WHY_ADDR_TIMEOUT
    "Offset timeout",       // WHY_OFFSET_TIMEOUT
    "Start timeout",        // WHY_START_TIMEOUT
    "Slave data timeout",   // WHY_SLAVE_TIMEOUT
    "Stop bit timeout"      // WHY_STOP_TIMEOUT
};

CC_ASSERT_ARRAY_SIZE(i2c_dbg_description, WHY_MAX);

/* I2C bus complaints */
static uint16_t            i2c_complaint[I2C_MAX_BUS];
static bool_t              i2c_probing;

/* The following globals will change depending on the board type */
static const i2c_busdef_t *i2c_busdef    = NULL;
static const char         *i2c_help_devs = NULL;
uint8_t                    i2c_bus_count = 0;

/* I2C bit-bang globals (to reduce parameter passing) */
static uint                i2c_bus;
static uint                i2c_dev;
static uint                i2c_offset;
static uint32_t            quarter_delay;

/* Runtime I2C access statistics for debug */
static struct {
    uint64_t read_good;
    uint64_t write_good;
    uint     read_compare_fail;
    uint     read_fail;
    uint     read_pec_fail;
    uint     read_probe_fail;
    uint     read_retry;
    uint     write_compare_fail;
    uint     write_fail;
    uint     write_probe_fail;
    uint     write_retry;
} i2c_stat;

/**
 * i2c_calc_pec() calculates the SMBus/PMBus PEC (Packet Error Check) code
 *                for the specified I2C transaction.
 *
 * @param [in]  dev    - I2C device address.
 * @param [in]  offset - Device offset.
 * @param [in]  len    - Number of bytes in data.
 * @param [in]  data   - Data bytes over which PEC is to be calculated.
 * @param [in]  rw     - I2C_READ: also read address in CRC.
 *                       I2C_WRITE: include only write address in CRC.
 *
 * @return      None.
 */
static uint8_t
i2c_calc_pec(uint dev, uint offset, uint len, const void *data, uint rw)
{
    uint    hdr_len = 0;  // addr
    uint8_t hdr[5];
    uint8_t crc;

    hdr[hdr_len++] = (uint8_t) (dev << 1);
    if (dev & I2C_FLAG_16BIT)
        hdr[hdr_len++] = (uint8_t) (offset >> 8);
    if ((dev & I2C_FLAG_NONE) == 0)
        hdr[hdr_len++] = (uint8_t) offset;
    if (rw == I2C_WRITE)
        if (dev & I2C_FLAG_BLOCK)
            hdr[hdr_len++] = (uint8_t) len;
    if (rw == I2C_READ)
        hdr[hdr_len++] = (uint8_t) ((dev << 1) + 1);

    /* Calculate header CRC */
    crc = crc8(0, hdr, hdr_len);

    return (crc8(crc, data, len));
}

/**
 * i2c_print_bdo() displays an I2C bus, device, and offset in a standardized
 *                 format depending on whether the device and offset have
 *                 been specified.
 *
 * @param [in]  buf     - Buffer to write output to. NULL if stdout.
 * @param [in]  bufsize - Size of buf in bytes.
 * @param [in]  bus     - Logical I2C bus.
 * @param [in]  dev     - I2C device address.
 * @param [in]  offset  - Device offset.
 *
 * @return      The number of bytes written to buf, not including the
 *              terminating null.
 */
static int
i2c_print_bdo(char *buf, size_t bufsize, uint bus, uint dev, uint offset)
{
    int printed = 0;
    char *bufo = buf;
    if (dev & I2C_FLAG_PEC) {
        printed += snprintf(bufo, bufsize - printed, "PEC ");
        bufo = (buf != NULL) ? buf + printed : NULL;
    }
    if (dev & I2C_FLAG_BLOCK) {
        printed += snprintf(bufo, bufsize - printed, "Block ");
        bufo = (buf != NULL) ? buf + printed : NULL;
    }
    if (i2c_bus_count > 0)
        printed += snprintf(bufo, bufsize - printed, "%x", bus);
    bufo = (buf != NULL) ? buf + printed : NULL;

    if ((uint8_t) dev < I2C_ANY_DEV) {
        /* Show device address */
        if (i2c_bus_count > 0)
            printed += snprintf(bufo, bufsize - printed, ".");
        printed += snprintf(bufo, bufsize - printed, "%02x", (uint8_t) dev);
        if ((dev & I2C_FLAG_NONE) == 0) {
            bufo = (buf != NULL) ? buf + printed : NULL;
            /* Send device offset */
            if (dev & I2C_FLAG_16BIT)
                printed += snprintf(bufo, bufsize - printed, ".%04x",
                                    (uint16_t) offset);
            else
                printed += snprintf(bufo, bufsize - printed, ".%02x",
                                    (uint16_t) offset);
        }
    }
    return (printed);
}

/**
 * i2c_get_bus_rate() determines the rate at which to clock I2C (in kHz) for
 *                    the specified bus.
 *
 * @param [in]  bus - I2C bus number.
 *
 * @return      I2C bus rate in kHz.
 */
static uint
i2c_get_bus_rate(uint bus)
{
    uint i2c_khz;
    if (i2c_busdef[bus].speed & I2C_BUS_SPEED_50KHZ)
        i2c_khz = 50;
    else if (i2c_busdef[bus].speed & I2C_BUS_SPEED_100KHZ)
        i2c_khz = 100;
    else if (i2c_busdef[bus].speed & I2C_BUS_SPEED_5KHZ)
        i2c_khz = 5;
    else
        i2c_khz = 400;

    if ((config.i2c_min_speed != 0) && (i2c_khz < config.i2c_min_speed))
        i2c_khz = config.i2c_min_speed;

    if ((config.i2c_max_speed != 0) && (i2c_khz > config.i2c_max_speed))
        i2c_khz = config.i2c_max_speed;

    return (i2c_khz);
}

/**
 * i2c_printf() displays a message about the specified I2C device.
 *              Message is using a string of formatted parameters.
 *
 * @param [in]  why - Reason message is being printed (enum WHY_*).
 * @param [in]  do_report - If TRUE, report error on the console
 * @param [in]  fmt - The parameter part of fmt string, used for dbg log
 * @param [in]  ... - A variable list of arguments.
 *
 * @return      None.
 */
static void __attribute__((format(__printf__, 3, 4)))
i2c_printf(i2c_why_t why, bool_t do_report, const char *fmt, ...)
{
    va_list  args;
    const char *dbg_desc = i2c_dbg_description[why];
    uint32_t why_mask = 1U << (uint)why;
    uint     bus      = i2c_bus;
    uint     dev      = i2c_dev;
    uint     offset   = i2c_offset;
    uint     compbus  = bus % ARRAY_SIZE(i2c_complaint);

    if (i2c_complaint[compbus] & why_mask)
        do_report = FALSE;
    i2c_complaint[compbus] |= (uint16_t) why_mask;

    if (do_report) {
        printf("I2C ");
        if ((uint8_t) dev == I2C_ANY_DEV)
            printf("bus ");
        (void) i2c_print_bdo(NULL, 0, bus, dev, offset);
        if (fmt[0] != '\0') {
            putchar(' ');
            va_start(args, fmt);
            printf(dbg_desc);
            putchar(' ');
            (void) vprintf(fmt, args);
            va_end(args);
        }
        putchar('\n');
    }
}

/**
 * i2c_print() displays a message, and logs an error if needed,
 *             about the specified I2C device.
 *
 * @param [in]  why - Reason message is being printed (enum WHY_*).
 * @param [in]  do_report - If TRUE, report error on the console
 *
 * @return      None.
 */
static void
i2c_print(i2c_why_t why, bool_t do_report)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
    i2c_printf(why, do_report, "");
#pragma GCC diagnostic pop
}

/**
 * get_pin() reads the current value of the specified GPIO pin.
 *
 * @param [in]  reg - the address of the pin's GPIO bit band register.
 *
 * @return      0   - the external pin is currently low.
 * @return      1   - the external pin is currently high.
 */
static uint32_t
get_pin(uint32_t reg)
{
    reg = BND_ODR_TO_IDR(reg);

    return (*VADDR32(reg));
}

/**
 * set_pin_0() drives the specified GPIO pin low.
 *
 * @param [in]  reg - the address of the pin's GPIO bit band register.
 *
 * @return      None.
 */
static inline void
set_pin_0(uint32_t reg)
{
    *VADDR32(reg) = 0;
}

/**
 * set_pin_1() drives the specified GPIO pin high.
 *
 * @param [in]  reg - the address of the pin's GPIO bit band register.
 *
 * @return      None.
 */
static inline void
set_pin_1(uint32_t reg)
{
    *VADDR32(reg) = 1;
}

/*
 * i2c_sw_setup_gpios() returns the bit-band address of the output data
 *                      registers for the GPIOs controlling the I2C SDA and
 *                      SCL signals. The input data register addresses may
 *                      be computed from the output data register addresses.
 *
 * @param [in]  bus - I2C bus number.
 * @param [out] scl - a pointer to the I2C SCL GPIO output register.
 * @param [out] sda - a pointer to the I2C SDA GPIO output register.
 *
 * @return      RC_SUCCESS   - Bus located and GPIO setup complete.
 * @return      RC_BAD_PARAM - Invalid I2C bus number specified.
 */
static rc_t
i2c_sw_setup_gpios(uint bus, uint32_t *scl, uint32_t *sda)
{
    uint32_t sda_port;
    uint32_t scl_port;
    uint16_t sda_pin;
    uint16_t scl_pin;
    uint8_t  pupd;
    uint     i2c_khz;

    if (bus >= i2c_bus_count) {
        warnx("Invalid bus number %x", bus);
        return (RC_BAD_PARAM);
    }

    scl_port = i2c_busdef[bus].port[I2C_GPIO_SCL];
    sda_port = i2c_busdef[bus].port[I2C_GPIO_SDA];
    scl_pin  = i2c_busdef[bus].pin[I2C_GPIO_SCL];
    sda_pin  = i2c_busdef[bus].pin[I2C_GPIO_SDA];

    /*
     * Bit-bang I2C transactions are split into quarter cycles. Each
     * quarter cycle has a period which is 1/4 the entire period of a
     * full I2C SCL clock cycle.
     *
     * At 5 KHz, the period is approximately 200000 nsec.
     * At 50 KHz, the period is approximately 5000 nsec.
     * At 100 KHz, the period is approximately 2500 nsec.
     * At 400 KHz, the period is approximately 625 nsec.
     *
     * Because the code driving the I2C bit-bang has overhead, the
     * periods selected below were empirically determined to subtract
     * this overhead (~550 nsec).
     */
    i2c_khz = i2c_get_bus_rate(bus);

    if (i2c_khz == 400) {
        quarter_delay = timer_nsec_to_tick(300);   // ~290 KHz
    } else if (i2c_khz == 100) {
        quarter_delay = timer_nsec_to_tick(2000);  // 100 KHz
    } else if (i2c_khz == 50) {
        quarter_delay = timer_nsec_to_tick(4450);  // 50 KHz
    } else if (i2c_khz == 5) {
        quarter_delay = timer_nsec_to_tick(50000); // 5 KHz
    } else {
        /* Compute delay */
        uint nsec = 1000000 / 4 / i2c_khz;
        if (nsec > 550)
            nsec -= 450;
        else
            nsec = 100;
        quarter_delay = timer_nsec_to_tick(nsec);
    }

    /* Calculate the ODR register offset in the bit band region */
    *scl = BND_IO(scl_port + GPIO_ODR_OFFSET, low_bit(scl_pin));
    *sda = BND_IO(sda_port + GPIO_ODR_OFFSET, low_bit(sda_pin));

    /* Set output data to 1 */
    *VADDR32(*scl) = 1;
    *VADDR32(*sda) = 1;

    if (1)
        pupd = GPIO_PUPD_PULLUP;
    else
        pupd = GPIO_PUPD_NONE;

    /* Set OTYPER to open drain */
    gpio_set_output_options(scl_port, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ,
                            scl_pin);
    gpio_set_output_options(sda_port, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ,
                            sda_pin);

    /* Set MODER to output */
    gpio_mode_setup(scl_port, GPIO_MODE_OUTPUT, pupd, scl_pin);
    gpio_mode_setup(sda_port, GPIO_MODE_OUTPUT, pupd, sda_pin);

    return (RC_SUCCESS);
}

/*
 * i2c_delay_quarter() delays one quarter of an I2C data cycle.
 *                     At 100 KHz, the delay is approximately 2500 nsec.
 *                     At 400 KHz, the delay is approximately 625 nsec.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
static void
i2c_delay_quarter(void)
{
    uint32_t start = TIM2_CNT;

    while (TIM2_CNT - start < quarter_delay) {
        /* Empty */
    }
}

/*
 * i2c_delay_half() delays one half of an I2C data cycle.
 *                  At 100 KHz, the delay is approximately 5000 nsec.
 *                  At 400 KHz, the delay is approximately 1250 nsec.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
static void
i2c_delay_half(void)
{
    uint32_t start = TIM2_CNT;

    while (TIM2_CNT - start < quarter_delay * 2) {
        /* Empty */
    }
}

/*
 * i2c_sw_wait_scl_high() waits until SCL transitions high. This should be a
 *                        very short delay unless the slave is holding SCL low.
 *
 * @param [in]  scl - the I2C SCL GPIO output register.
 *
 * @return      RC_SUCCESS - SCL is high.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 */
static rc_t
i2c_sw_wait_scl_high(uint32_t scl)
{
    uint64_t timeout = timer_tick_to_usec(I2C_SCL_RISE_TIMEOUT);

try_again:
    while (get_pin(scl) == 0)
        if (timer_tick_has_elapsed(timeout))
            break;

    if (get_pin(scl) == 0) {
        if (timer_tick_has_elapsed(timeout) == FALSE)
            goto try_again;  // Debounce glitch
        i2c_print(WHY_SCL_RISE_TMO, TRUE);
        return (RC_TIMEOUT);  // SCL did not go high
    }

    return (RC_SUCCESS);
}

/*
 * i2c_sw_wait_sda_high() waits until SDA transitions high. This should be a
 *                        very short delay unless the slave is holding SDA low.
 *
 * @param [in]  sda - the I2C SDA GPIO output register.
 *
 * @return      RC_SUCCESS - SDA is high.
 * @return      RC_FAILURE - SDA failed to go high (slave holding it low).
 */
static rc_t
i2c_sw_wait_sda_high(uint32_t sda)
{
    uint64_t timeout = timer_tick_to_usec(I2C_SDA_RISE_TIMEOUT);

try_again:
    while (get_pin(sda) == 0)
        if (timer_tick_has_elapsed(timeout))
            break;

    if (get_pin(sda) == 0) {
        if (timer_tick_has_elapsed(timeout) == FALSE)
            goto try_again;  // Debounce glitch
        i2c_print(WHY_SDA_RISE_TMO, TRUE);
        return (RC_FAILURE);  // SDA did not go high
    }

    return (RC_SUCCESS);
}

/**
 * i2c_sw_recover() recovers an I2C bus from a fault state (such as stuck
 *                  SDA or SCL signals).
 *
 * @param [in]  scl - the I2C SCL GPIO output register.
 * @param [in]  sda - the I2C SDA GPIO output register.
 * @param [in]  verbose - TRUE  - Display verbose error status.
 *                        FALSE - Silently attempt recovery.
 *
 * @return      None.
 */
static void
i2c_sw_recover(uint32_t scl, uint32_t sda, bool_t verbose)
{
    rc_t rc;
    uint i;

    set_pin_1(scl);
    set_pin_1(sda);

    rc = i2c_sw_wait_scl_high(scl);
    if (rc != RC_SUCCESS) {
        /* Attempt to unstick SCL by toggling SDA (not likely to work) */
        for (i = 0; i < 9; i++) {
            set_pin_0(sda);
            i2c_delay_half();
            set_pin_1(sda);
            i2c_delay_half();
            if (get_pin(scl) == 1)
                break;
        }
        if (get_pin(scl) == 0) {
            i2c_print(WHY_SCL_STUCK, verbose);
            return;  // Nothing more which can be done
        } else {
            i2c_print(WHY_SCL_RECOVER, verbose);
        }
    }

    rc = i2c_sw_wait_sda_high(sda);
    if (rc != RC_SUCCESS) {
        /* Attempt to unstick SDA by toggling SCL */
        for (i = 0; i < 9; i++) {
            set_pin_0(scl);
            i2c_delay_half();
            set_pin_1(scl);
            i2c_delay_half();
        }
        if (get_pin(sda) == 0) {
            i2c_print(WHY_SDA_STUCK, verbose);
        } else {
            i2c_print(WHY_SDA_RECOVER, verbose);
        }
    }
}

/*
 * i2c_sw_send_start() performs an I2C start transaction (SDA transitions
 *                     low while SCL is high). For a restart transaction,
 *                     SCL is initially low. SDA is brought high first,
 *                     then SCL. Following that, the normal I2C start
 *                     sequence is performed. That is, SDA=0 followed by
 *                     SCL=0.
 *        _____
 *   SCL  __   \_
 *   SDA    \___~
 *
 * @param [in]  scl - the I2C SCL GPIO output register.
 * @param [in]  sda - the I2C SDA GPIO output register.
 *
 * @return      RC_SUCCESS - start successfully sent.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_send_start(uint32_t scl, uint32_t sda)
{
    rc_t rc;

    if (get_pin(scl) == 0) {
        /* Restart transaction */
        i2c_delay_quarter();
        set_pin_1(sda);
        i2c_delay_quarter();
        set_pin_1(scl);
    }
    rc = i2c_sw_wait_scl_high(scl);
    if (rc != RC_SUCCESS)
        goto finish;

    i2c_delay_quarter();

    /* SDA should be high unless slave is confused or there is another master */
    rc = i2c_sw_wait_sda_high(sda);
    if (rc != RC_SUCCESS)
        goto finish;

    set_pin_0(sda);
    i2c_delay_half();

    if (get_pin(scl) == 0) {
        rc = RC_FAILURE;  // Should not happen unless slave is confused
        goto finish;
    }

    set_pin_0(scl);
finish:

    return (rc);
}

/*
 * i2c_sw_send_stop() performs an I2C stop transaction (SDA transitions
 *                    high while SCL is high). The sequence ensures that
 *                    there is a transition of SCL=0, SDA=0, SCL=1, and
 *                    then SDA=1.
 *           ____
 *   SCL  __/  __
 *   SDA  ~___/
 *
 * @param [in]  scl - the I2C SCL GPIO output register.
 * @param [in]  sda - the I2C SDA GPIO output register.
 *
 * @return      RC_SUCCESS - stop successfully sent.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_send_stop(uint32_t scl, uint32_t sda)
{
    rc_t rc;

    set_pin_0(scl);
    i2c_delay_half();

    if (config.debug_flag & DF_I2C_LL)
        printf("[Stop] ");
    if (get_pin(sda) == 1) {
        set_pin_0(sda);
        i2c_delay_quarter();
    }
    set_pin_1(scl);
    i2c_delay_quarter();

    rc = i2c_sw_wait_scl_high(scl);
    if (rc != RC_SUCCESS) {
        set_pin_1(sda);
        return (rc);
    }

    bool_t en = i2c_irq_disable();  // SCL=1
    i2c_delay_quarter();
    set_pin_1(sda);
    i2c_irq_enable(en);  // SDA=1 (release)

    rc = i2c_sw_wait_sda_high(sda);
    if (rc != RC_SUCCESS)
        return (rc);

    i2c_delay_quarter();
    if ((get_pin(scl) == 0) || (get_pin(sda) == 0))
        return (RC_FAILURE);  // Should not happen unless slave is confused
    i2c_delay_quarter();

    return (RC_SUCCESS);
}

/*
 * i2c_sw_send_bit() clocks out one bit to the slave. SDA is sampled
 *                   by the slave while SCL is high.
 *           __
 *   SCL  __/  \
 *
 *   SDA  ~xxxx~
 *
 * @param [in]  scl    - the I2C SCL GPIO output register.
 * @param [in]  sda    - the I2C SDA GPIO output register.
 * @param [in]  data   - Data bit to send.
 *
 * @return      RC_SUCCESS - bit successfully sent.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_send_bit(uint32_t scl, uint32_t sda, uint data)
{
    rc_t rc;

    /*
     * I2C data (SDA) transitions only while the clock (SCL) is low.
     * When SCL is high, the slave reads SDA.
     */
    i2c_delay_quarter();
    if (data) {
        set_pin_1(sda);
        rc = i2c_sw_wait_sda_high(sda);
        if (rc != RC_SUCCESS) {
            set_pin_1(scl);
            return (rc);
        }
    } else {
        set_pin_0(sda);
    }
    i2c_delay_quarter();

    set_pin_1(scl);
    rc = i2c_sw_wait_scl_high(scl);
    if (rc != RC_SUCCESS) {
        set_pin_1(sda);
        return (rc);
    }
    bool_t en = i2c_irq_disable();  // SCL=1
    i2c_delay_half();
    set_pin_0(scl);
    i2c_irq_enable(en);  // SCL=0

    return (RC_SUCCESS);
}

/*
 * i2c_sw_read_bit() clocks in one bit from the slave. SDA is sampled
 *                   by this code in the middle of the SCL high pulse.
 *           __
 *   SCL  __/  \
 *
 *   SDA  ~~~xx~
 *
 * @param [in]  scl    - the I2C SCL GPIO output register.
 * @param [in]  sda    - the I2C SDA GPIO output register.
 * @param [out] data   - Data bit which was read.
 *
 * @return      RC_SUCCESS - bit successfully read.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_read_bit(uint32_t scl, uint32_t sda, uint *data)
{
    rc_t rc;

    i2c_delay_quarter();
    set_pin_1(sda);
    i2c_delay_quarter();

    set_pin_1(scl);
    rc = i2c_sw_wait_scl_high(scl);
    if (rc != RC_SUCCESS)
        return (rc);

    bool_t en = i2c_irq_disable();  // SCL=1
    i2c_delay_quarter();
    *data = get_pin(sda);
    i2c_delay_quarter();
    set_pin_0(scl);
    i2c_irq_enable(en);  // SCL=0

    return (RC_SUCCESS);
}

/*
 * i2c_sw_read_byte() byte clocks in eight bits from the I2C slave. SDA is
 *                    sampled when SCL is high. At the end of the eight-bit
 *                    transfer, the master will either ACK (0) or NAK (1) the
 *                    transaction. Typically a master will NAK the last byte
 *                    to terminate a transfer.
 *            __    __    __    __    __    __    __    __    __
 *   SCL  \__/  \__/  \__/  \__/  \__/  \__/  \__/  \__/  \__/  \
 *
 *   SDA  ~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~\___~
 *
 *   bit       7     6     5     4     3     2     1     0    ACK
 *
 * @param [in]  scl    - the I2C SCL GPIO output register.
 * @param [in]  sda    - the I2C SDA GPIO output register.
 * @param [in]  do_nak - 1=Generate NAK following transaction;
                         0=Generate ACK following transaction.
 * @param [out] data   - Data byte which is read.
 *
 * @return      RC_SUCCESS - byte successfully read.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_read_byte(uint32_t scl, uint32_t sda, bool_t do_nak, uint8_t *data)
{
    uint8_t value = 0;
    int     bit;
    uint    bitval;
    rc_t    rc;

    set_pin_0(scl);
    i2c_delay_quarter();
    set_pin_1(sda);

    for (bit = 7; bit >= 0; bit--) {
        rc = i2c_sw_read_bit(scl, sda, &bitval);
        if (rc != RC_SUCCESS)
            return (rc);
        value = (uint8_t) ((value << 1) | bitval);
    }
    if (config.debug_flag & DF_I2C_LL)
        printf("%02x[%s]", value, do_nak ? "NAK" : "A");

    /* Send ACK or NAK */
    rc = i2c_sw_send_bit(scl, sda, do_nak ? 1 : 0);
    if (rc != RC_SUCCESS)
        return (rc);

    *data = value;
    return (RC_SUCCESS);
}

/*
 * i2c_sw_send_byte() clocks out eight bits to the slave. SDA is sampled
 *                    by the slave while SCL is high. The slave will
 *                    respond with an ACK (0) or NAK (1) at the end of the
 *                    8-bit transfer.
 *            __    __    __    __    __    __    __    __    __
 *   SCL  \__/  \__/  \__/  \__/  \__/  \__/  \__/  \__/  \__/  \
 *
 *   SDA  ~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~xxxx~~\___~
 *
 *   bit       7     6     5     4     3     2     1     0    ACK
 *
 * @param [in]  scl    - the I2C SCL GPIO output register.
 * @param [in]  sda    - the I2C SDA GPIO output register.
 * @param [in]  data   - Data byte which is sent.
 *
 * @return      RC_SUCCESS - byte successfully sent.
 * @return      RC_TIMEOUT - SCL failed to go high (slave holding it low).
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_send_byte(uint32_t scl, uint32_t sda, uint8_t data)
{
    int  bit;
    uint bitval;
    rc_t rc;

    if (config.debug_flag & DF_I2C_LL)
        printf("[%02x]", data);
    for (bit = 7; bit >= 0; bit--) {
        rc = i2c_sw_send_bit(scl, sda, data & (1 << bit));
        if (rc != RC_SUCCESS)
            return (rc);
    }

    /* Wait for ACK */
    rc = i2c_sw_read_bit(scl, sda, &bitval);
    if (rc != RC_SUCCESS)
        return (rc);

    if (bitval != 0) {
        if (config.debug_flag & DF_I2C_LL)
            printf("NAK");
        return (RC_TIMEOUT);  // Device did not ACK transaction (NAK)
    } else {
        if (config.debug_flag & DF_I2C_LL)
            printf("A");
    }

    return (RC_SUCCESS);  // Device did ACK transaction
}

/*
 * i2c_sw_send_address() sends an I2C start, slave address and (optionally)
 *                       an 8-bit or 16-bit offset on that device.
 *
 * @param [in]  dev    - I2C device address.
 *                       Example: an I2C device with a binary address of
 *                                1010000[0|1] would be specified as 0x50.
 *                       I2C_FLAG_NONE, I2C_FLAG_16BIT, or I2C_FLAG_32BIT
 *                       may be or'd with this value.
 * @param [in]  scl    - the I2C SCL GPIO output register.
 * @param [in]  sda    - the I2C SDA GPIO output register.
 * @param [in]  offset - Offset onto the device unless I2C_FLAG_NONE.
 *                       is specified.
 *
 * @return      RC_SUCCESS - address sent and ACK'd by slave.
 * @return      RC_TIMEOUT - slave did not ACK address or SCL held too long.
 * @return      RC_FAILURE - SDA held low (slave is confused).
 */
static rc_t
i2c_sw_send_address(uint dev, uint32_t scl, uint32_t sda, uint offset)
{
    rc_t rc;

    /* Send Start */
    if (config.debug_flag & DF_I2C_LL)
        printf("[S]");
    rc = i2c_sw_send_start(scl, sda);
    if (rc != RC_SUCCESS)
        return (rc);

    /* Function is either called by read with offset or a write */
    rc = i2c_sw_send_byte(scl, sda, (uint8_t) (dev << 1) | I2C_WRITE);
    if (rc != RC_SUCCESS) {
        i2c_print(WHY_ADDR_TIMEOUT, config.debug_flag & DF_I2C_LL);
        return (rc);
    }

    if ((dev & I2C_FLAG_NONE) == 0) {
        /* Send device address */
        if (dev & I2C_FLAG_32BIT) {
            /* Send 32-bit device offset (high bits first) */
            rc = i2c_sw_send_byte(scl, sda, (uint8_t) (offset >> 24));
            if (rc != RC_SUCCESS) {
                i2c_print(WHY_OFFSET_TIMEOUT, config.debug_flag & DF_I2C_LL);
                return (rc);
            }
            rc = i2c_sw_send_byte(scl, sda, (uint8_t) (offset >> 16));
            if (rc != RC_SUCCESS) {
                i2c_print(WHY_OFFSET_TIMEOUT, config.debug_flag & DF_I2C_LL);
                return (rc);
            }

        }
        if (dev & (I2C_FLAG_16BIT | I2C_FLAG_32BIT)) {
            /* Send 16-bit device offset (high bits first) */
            rc = i2c_sw_send_byte(scl, sda, (uint8_t) (offset >> 8));
            if (rc != RC_SUCCESS) {
                i2c_print(WHY_OFFSET_TIMEOUT, config.debug_flag & DF_I2C_LL);
                return (rc);
            }
        }

        /* Send low 8 bits of device offset */
        rc = i2c_sw_send_byte(scl, sda, (uint8_t) offset);
        if (rc != RC_SUCCESS) {
            i2c_print(WHY_OFFSET_TIMEOUT, config.debug_flag & DF_I2C_LL);
            return (rc);
        }
    }
    return (RC_SUCCESS);
}

/**
 * i2c_sw_read() reads bytes from an I2C device using software bit-bang of
 *               CPU GPIO pins.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 *                       Example: an I2C device with a binary address of
 *                                1010000[0|1] would be specified as 0x50.
 *                       I2C_FLAG_NONE, I2C_FLAG_16BIT, or I2C_FLAG_32BIT
 *                       may be or'd with this value.
 * @param [in]  offset - Offset onto the device unless I2C_FLAG_NONE.
 *                       is specified.
 * @param [in]  len    - Number of bytes to read.
 * @param [out] data   - Buffer into which len data bytes will be written.
 *
 * @return      RC_SUCCESS - read completed successfully.
 * @return      RC_TIMEOUT - A timeout occurred.
 * @return      RC_FAILURE - read failure.
 *
 * @see         i2c_write()
 */
static rc_t
i2c_sw_read(uint bus, uint dev, uint offset, uint len, uint8_t *data)
{
    uint32_t sda;
    uint32_t scl;
    rc_t     rc;
    bool_t   block_mode = (dev & I2C_FLAG_BLOCK) ? TRUE : FALSE;
    bool_t   pec_mode   = (dev & I2C_FLAG_PEC)   ? TRUE : FALSE;
    uint8_t *datap      = data;
    uint     olen       = len & MODE_MAXLEN;
    uint     rlen       = olen;

    len = olen;

    /* Look up and configure GPIOs */
    rc = i2c_sw_setup_gpios(bus, &scl, &sda);
    if (rc != RC_SUCCESS)
        return (rc);

    if ((dev & I2C_FLAG_NONE) == 0) {
        /* Issue start, send device address, and offset to read */
        rc = i2c_sw_send_address(dev, scl, sda, offset);
        if (rc != RC_SUCCESS)
            goto i2c_failure;
    }

    if (config.debug_flag & DF_I2C_LL)
        printf("[R]");
    /* Issue start (or restart) */
    rc = i2c_sw_send_start(scl, sda);
    if (rc != RC_SUCCESS) {
i2c_failure:
        if (i2c_sw_send_stop(scl, sda) != RC_SUCCESS) {
            i2c_sw_recover(scl, sda, TRUE);
            (void) i2c_sw_send_stop(scl, sda);
        }
        return (rc);
    }

    /* Send slave address (READ) */
    rc = i2c_sw_send_byte(scl, sda, (uint8_t) (dev << 1) | I2C_READ);
    if (rc != RC_SUCCESS)
        goto i2c_failure;

    /* Read data bytes */
    while (len-- > 0) {
        bool_t do_nak = ((len == 0) && (pec_mode == FALSE)) ? TRUE : FALSE;
        rc = i2c_sw_read_byte(scl, sda, do_nak, data);
        if (rc != RC_SUCCESS)
            goto i2c_failure;
        if (block_mode) {
            /* In block mode, the first byte is the payload length */
            if (len > *data)
                len = *data;
            olen = len + 1;
            block_mode = FALSE;
        }
        data++;
    }

    /* Zero remainder of block */
    if (rlen > olen)
        memset(data, 0x00, rlen - olen);

    if (pec_mode) {
        uint8_t pec;
        uint8_t pec_calc;
        rc = i2c_sw_read_byte(scl, sda, TRUE, &pec);
        if (rc != RC_SUCCESS)
            goto i2c_failure;
        pec_calc = i2c_calc_pec(dev, offset, olen, datap, I2C_READ);
        if (pec != pec_calc) {
            if (config.debug_flag & DF_I2C) {
                if (i2c_bus_count > 1)
                    printf("%x.", bus);
                printf("I2C CRC %x.%x len=%x",
                       (uint8_t) dev, offset, olen);
                while (olen-- > 0)
                    printf(" %02x", *(datap++));
                printf("  %02x [%02x]\n", pec, pec_calc);
            }
            rc = RC_FAILURE;
            goto i2c_failure;
        }
    }

    /* Terminate transaction with a STOP */
    rc = i2c_sw_send_stop(scl, sda);
    if (rc != RC_SUCCESS)
        i2c_sw_recover(scl, sda, TRUE);

    return (rc);
}

/**
 * i2c_sw_write() writes bytes to an I2C device using software bit-bang of
 *                CPU GPIO pins.
 *
 * @param [in]  bus    - I2C bus number.
 *                       I2C_FLAG_NONE, I2C_FLAG_16BIT, or I2C_FLAG_32BIT
 *                       may be or'd with this value.
 * @param [in]  dev    - I2C device address
 *                       Example: an I2C device with a binary address of
 *                                1010000[0|1] would be specified as 0x50.
 * @param [in]  offset - Offset onto the device unless I2C_FLAG_NONE.
 *                       is specified.
 * @param [in]  len    - Number of bytes to write.
 * @param [in]  data   - A pointer to a buffer to be written to the
 *                       specified I2C device.
 *
 * @return      RC_SUCCESS - write completed successfully.
 * @return      RC_TIMEOUT - A timeout occurred.
 * @return      RC_FAILURE - write failure.
 *
 * @see         i2c_sw_read()
 */
static rc_t
i2c_sw_write(uint8_t bus, uint8_t dev, uint offset, uint len,
             const uint8_t *data)
{
    uint32_t sda;
    uint32_t scl;
    rc_t     rc;
    bool_t   block_mode = (dev & I2C_FLAG_BLOCK) ? TRUE : FALSE;
    bool_t   pec_mode   = (dev & I2C_FLAG_PEC)   ? TRUE : FALSE;
    uint8_t  pec        = 0;

    len &= MODE_MAXLEN;

    /* Look up and configure GPIOs */
    rc = i2c_sw_setup_gpios(bus, &scl, &sda);
    if (rc != RC_SUCCESS)
        return (rc);

    /* Issue start, send device address, and offset to write */
    rc = i2c_sw_send_address(dev, scl, sda, offset);
    if (rc != RC_SUCCESS) {
i2c_failure:
        if (i2c_sw_send_stop(scl, sda) != RC_SUCCESS) {
            i2c_sw_recover(scl, sda, TRUE);
            (void) i2c_sw_send_stop(scl, sda);
        }
        return (rc);
    }

    if (block_mode) {
        /* In block mode, a byte count is sent first */
        if ((rc = i2c_sw_send_byte(scl, sda, (uint8_t) len)) != RC_SUCCESS)
            goto i2c_failure;
    }

    if (pec_mode)
        pec = i2c_calc_pec(dev, offset, len, data, I2C_WRITE);

    /* Write data bytes */
    while (len-- > 0) {
        rc = i2c_sw_send_byte(scl, sda, *(data++));
        if (rc != RC_SUCCESS)
            goto i2c_failure;
    }

    if (pec_mode) {
        rc = i2c_sw_send_byte(scl, sda, pec);
        if (rc != RC_SUCCESS)
            goto i2c_failure;
    }

    /* Terminate transaction with a STOP */
    rc = i2c_sw_send_stop(scl, sda);
    if (rc != RC_SUCCESS)
        i2c_sw_recover(scl, sda, TRUE);
    return (rc);
}

/**
 * i2c_bus_avail_backoff() implements a backoff retry algorithm for I2C
 *                         buses which are currently unavailable. If the bus
 *                         continues to be unavailable, the backoff will
 *                         double at each interval, with an upper bound of
 *                         60 seconds. For memory efficiency, all devices
 *                         which are currently unavailable are grouped into
 *                         a single backoff class.
 *
 * @param [in]  bus - I2C bus requiring recovery.
 *
 * @return      TRUE  - Bus recovery should be retried.
 * @return      FALSE - Bus recovery should not be retried.
 */
static bool_t
i2c_bus_avail_backoff(uint bus)
{
    static uint64_t interval_last_tick = 0;
    static uint16_t bus_backoff[I2C_MAX_BUS] = { 0 };
    uint64_t        now = timer_tick_get();
    uint64_t        usec;
    uint32_t        msec;
    uint            cur;

    if (bus >= I2C_MAX_BUS)
        bus = I2C_MAX_BUS - 1;  // Guard

    usec = timer_tick_to_usec(now - interval_last_tick);
    msec = usec / 1000;

    if (msec >= I2C_RECOVER_BACKOFF_MSEC)
        msec = I2C_RECOVER_BACKOFF_MSEC;

    if (msec >= 1) {
        /* Decrement all backoff values */
        for (cur = 0; cur < ARRAY_SIZE(bus_backoff); cur++)
            if (bus_backoff[cur] > msec)
                bus_backoff[cur] -= (uint16_t) msec;
            else
                bus_backoff[cur] = 0;
        interval_last_tick = now;
    }

    if (bus_backoff[bus] < 2) {
        /* Minimum backoff is 30ms */
        bus_backoff[bus] = 30;
        return (TRUE);
    } else {
        /* Backoff increases each time the bus is still in failed state */
        uint32_t new_backoff = bus_backoff[bus] + (bus_backoff[bus] >> 1);
        if (new_backoff > I2C_RECOVER_BACKOFF_MSEC)
            new_backoff = I2C_RECOVER_BACKOFF_MSEC;
        bus_backoff[bus] = (uint16_t) new_backoff;
        return (FALSE);
    }
}

/**
 * i2c_bus_avail() determines whether an I2C bus is accessible.
 *
 * @param [in]  bus     - I2C bus number.
 * @param [in]  mode    - RECOVER_NONE  - Do not attempt bus recovery.
 *                        RECOVER_AUTO  - Automatic recovery (quiet).
 *                        RECOVER_FORCE - Force recovery (verbose).
 *
 * @return      TRUE  - The specified I2C bus is available.
 * @return      FALSE - The specified I2C bus is not available.
 */
static bool_t
i2c_bus_avail(uint bus, i2c_recover_t mode)
{
    uint   port;
    bool_t verbose = (mode == RECOVER_FORCE) ? TRUE : FALSE;

    if (bus >= i2c_bus_count)
        return (FALSE);

    for (port = I2C_GPIO_SCL; port <= I2C_GPIO_SDA; port++) {
        uint32_t gpio = i2c_busdef[bus].port[port];
        uint16_t pin  = i2c_busdef[bus].pin[port];
        if (gpio == 0)
            return (FALSE);  // Invalid GPIO port

        if (gpio_get(gpio, pin) == 0) {
            /* SCL or SDA is stuck */
            uint32_t sda;
            uint32_t scl;
            uint     i2c_bus_save;
            uint     i2c_dev_save;

            if (mode == RECOVER_NONE)
                return (FALSE);  // Pin stuck
            if ((mode == RECOVER_AUTO) && (port == I2C_GPIO_SCL))
                return (FALSE);  // SCL pin stuck
            if (i2c_bus_avail_backoff(bus) == FALSE)
                return (FALSE);

            /* Look up and configure GPIOs */
            if (i2c_sw_setup_gpios(bus, &scl, &sda) != RC_SUCCESS)
                return (FALSE);

            i2c_bus_save = i2c_bus;
            i2c_dev_save = i2c_dev;
            i2c_bus = bus;
            i2c_dev = I2C_ANY_DEV;
            i2c_sw_recover(scl, sda, verbose);
            i2c_bus = i2c_bus_save;
            i2c_dev = i2c_dev_save;

            if (gpio_get(gpio, pin) == 0)
                return (FALSE); // Still stuck
        }
    }

    return (TRUE);
}

/**
 * i2c_bus_complaint_check() determines if an I2C bus has an outstanding
 *                           complaint which has not been cleared. The
 *                           typical cause is a stuck SCL or SDA line.
 *
 * @param [in]  bus    - I2C bus number.
 *
 * @return      RC_SUCCESS - Bus is in good condition.
 * @return      RC_FAILURE - Bus should not be used.
 */
static rc_t
i2c_bus_complaint_check(uint bus)
{
    uint compbus = bus & (ARRAY_SIZE(i2c_complaint) - 1);
    rc_t rc;

    if (i2c_complaint[compbus] & (WHY_SCL_STUCK | WHY_SDA_STUCK)) {
        uint32_t scl;
        uint32_t sda;
        rc = i2c_sw_setup_gpios(bus, &scl, &sda);
        if (rc != RC_SUCCESS)
            return (rc);

        if ((get_pin(scl) == 0) || (get_pin(sda) == 0))
            return (RC_FAILURE);

        i2c_complaint[compbus] &= ~(WHY_SCL_STUCK | WHY_SDA_STUCK);
    }
    return (RC_SUCCESS);
}

/**
 * i2c_read() reads bytes from an I2C device.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 *                       Example: an I2C device with a binary address of
 *                                1010000[0|1] would be specified as 0x50.
 *                       I2C_FLAG_NONE, I2C_FLAG_16BIT, or I2C_FLAG_32BIT
 *                       may be or'd with this value.
 * @param [in]  offset - Offset onto the device unless I2C_FLAG_NONE
 *                       is specified.
 * @param [in]  len    - Number of bytes to read.
 * @param [out] datap  - A pointer to a buffer containing space into
 *                       which len data bytes will be written.
 *
 * @return      RC_SUCCESS - read completed successfully.
 * @return      RC_TIMEOUT - A timeout occurred.
 * @return      RC_PROTECT - Bus is controlled by another master.
 * @return      RC_FAILURE - read failure.
 *
 * @see         i2c_write()
 */
rc_t
i2c_read(uint bus, uint dev, uint offset, uint len, void *datap)
{
    uint8_t  compare_buf[32];
    uint     compare_len = len & MODE_MAXLEN;
    uint     compare_max = I2C_COMPARE_MAX;
    uint     retry_max   = I2C_RETRY_MAX;
    uint8_t *data        = datap;
    bool_t   first_try   = TRUE;
    rc_t     rc;
    uint     i2c_bus_save;
    uint     i2c_dev_save;
    uint     i2c_offset_save;

    if (compare_len > sizeof (compare_buf))
        compare_len = sizeof (compare_buf);

    if (dev & (I2C_FLAG_NO_RETRY | I2C_FLAG_NONE))
        retry_max = 0;
    if (dev & (I2C_FLAG_NO_CHECK | I2C_FLAG_NONE))
        compare_max = 1;  // Do not compare

    if (bus >= i2c_bus_count) {
        warnx("Invalid I2C bus %x", bus);
        return (RC_BAD_PARAM);
    }

    /* The following are for debug message output */
    i2c_bus_save    = i2c_bus;
    i2c_dev_save    = i2c_dev;
    i2c_offset_save = i2c_offset;
    i2c_bus         = bus;
    i2c_dev         = dev;
    i2c_offset      = offset;

    /* Check for outstanding I2C bus complaints */
    rc = i2c_bus_complaint_check(bus);
    if (rc != RC_SUCCESS) {
        goto i2c_bus_restore;
    }

    while (compare_max-- > 0) {
        rc = i2c_sw_read(bus, dev, offset, len, data);

        if (rc != RC_SUCCESS) {
            /* Operation failed -- retry? */
            if (retry_max > 0) {
                retry_max--;
                compare_max++;
            }
            if (compare_max > 0)
                i2c_stat.read_retry++;
            else if (rc == RC_FAILURE)
                i2c_stat.read_pec_fail++;
            else if (i2c_probing == TRUE)
                i2c_stat.read_probe_fail++;
            else
                i2c_stat.read_fail++;
            continue;
        }

        if (dev & I2C_FLAG_PEC) {
            i2c_stat.read_good++;
            break;  // Trust all results which pass PEC
        }

        if (compare_max > 0) {
            if (first_try == TRUE) {
                first_try = FALSE;
                memcpy(compare_buf, data, compare_len);
            } else {
                if (memcmp(compare_buf, data, compare_len) == 0) {
                    i2c_stat.read_good++;
                    break;  // Success
                }
                i2c_stat.read_compare_fail++;
            }
        } else if (dev & (I2C_FLAG_NO_CHECK | I2C_FLAG_NONE)) {
            /* Assume read was good */
            i2c_stat.read_good++;
        }
    }

    if (config.debug_flag & DF_I2C) {
        uint8_t dlen = (uint8_t) len;

        printf("I2C read  ");
        (void) i2c_print_bdo(NULL, 0, bus, dev, offset);
        printf(" = ");

        if (rc == RC_TIMEOUT) {
            printf("NAK");
        } else if (rc == RC_FAILURE) {
            printf("FAIL");
        } else {
            if (dev & I2C_FLAG_BLOCK)
                dlen = (*data) + 1;
            while (dlen-- > 0)
                printf("%02x", *(data++));
            if (dev & I2C_FLAG_PEC)
                printf("%02x", i2c_calc_pec(dev, offset, len, datap, I2C_READ));
        }
        putchar('\n');
    }

    /* Restore saved I2C device (in case I2C called from ISR) */
i2c_bus_restore:
    i2c_bus    = i2c_bus_save;
    i2c_dev    = i2c_dev_save;
    i2c_offset = i2c_offset_save;

    return (rc);
}

/**
 * i2c_write() write bytes to an I2C device.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address (bit 0 is always ignored).
 *                       Example: an I2C device with a binary address of
 *                                1010000[0|1] would be specified as 0x50.
 *                       I2C_FLAG_NONE, I2C_FLAG_16BIT, or I2C_FLAG_32BIT
 *                       may be or'd with this value.
 * @param [in]  offset - Offset onto the device unless I2C_FLAG_NONE
 *                       is specified.
 * @param [in]  len    - Number of bytes to write.
 * @param [in]  datap  - A pointer to a buffer to be written to the
 *                       specified I2C device.
 *
 * @return      RC_SUCCESS - write completed successfully.
 * @return      RC_TIMEOUT - A timeout occurred.
 * @return      RC_PROTECT - Bus is controlled by another master.
 * @return      RC_FAILURE - write failure.
 *
 * @see         i2c_read()
 */
rc_t
i2c_write(uint bus, uint dev, uint offset, uint len, const void *datap)
{
    uint8_t        compare_buf[32];
    uint           compare_len = len & MODE_MAXLEN;
    uint           compare_max = I2C_COMPARE_MAX;
    uint           retry_max   = I2C_RETRY_MAX;
    const uint8_t *data        = (const uint8_t *) datap;
    rc_t           rc;
    uint           i2c_bus_save;
    uint           i2c_dev_save;
    uint           i2c_offset_save;

    if (compare_len > sizeof (compare_buf))
        compare_len = sizeof (compare_buf);
    memset(compare_buf, 0, compare_len);

    if (dev & I2C_FLAG_NO_RETRY)
        retry_max = 0;
    if (dev & (I2C_FLAG_NO_CHECK | I2C_FLAG_NONE))
        compare_max = 1;  // Do not compare

    if (bus >= i2c_bus_count) {
        warnx("Invalid I2C bus %x", bus);
        return (RC_BAD_PARAM);
    }

    /* The following are for debug message output */
    i2c_bus_save    = i2c_bus;
    i2c_dev_save    = i2c_dev;
    i2c_offset_save = i2c_offset;
    i2c_bus         = bus;
    i2c_dev         = dev;
    i2c_offset      = offset;

    /* Check for outstanding I2C bus complaints */
    rc = i2c_bus_complaint_check(bus);
    if (rc != RC_SUCCESS) {
        goto i2c_bus_restore;
    }

    while (compare_max-- > 0) {
        rc = i2c_sw_write(bus, dev, offset, len, data);

        if (rc != RC_SUCCESS) {
            /* Operation failed -- retry? */
            if (retry_max > 0) {
                retry_max--;
                compare_max++;
            }
            if (compare_max > 0)
                i2c_stat.write_retry++;
            else if (i2c_probing == TRUE)
                i2c_stat.write_probe_fail++;
            else
                i2c_stat.write_fail++;
            continue;
        }

        if (dev & I2C_FLAG_PEC) {
            i2c_stat.write_good++;
            break;  // Trust all results which pass PEC
        }

        if (compare_max > 0) {
            /* Read back the value to verify it was written correctly */
try_read_again:
            rc = i2c_sw_read(bus, dev, offset, compare_len, compare_buf);
            if (rc != RC_SUCCESS) {
                /* Operation failed -- retry? */
                if (retry_max > 0) {
                    retry_max--;
                    goto try_read_again;
                }
                i2c_stat.read_fail++;
                break;  // Failure
            }

            if (memcmp(compare_buf, data, compare_len) == 0) {
                i2c_stat.write_good++;
                break;  // Success
            }
            i2c_stat.write_compare_fail++;
            /*
             * XXX: If compare_max == 0, consider setting RC_FAILURE here.
             *      Doing this might break existing code which writes to
             *      registers which can not be read back with the same value.
             */
        } else if (dev & (I2C_FLAG_NO_CHECK | I2C_FLAG_NONE)) {
            /* Assume write was good */
            i2c_stat.write_good++;
        }
    }

    if (0)  // is_slow_dev(bus, dev)
        timer_delay_usec(1);  // 1us minimum between I2C transactions (tBUF)

    if (config.debug_flag & DF_I2C) {
        uint8_t dlen = (uint8_t) len;

        printf("I2C write ");
        (void) i2c_print_bdo(NULL, 0, bus, dev, offset);
        printf(" = ");

        if (rc == RC_TIMEOUT) {
            printf("NAK");
        } else if (rc == RC_FAILURE) {
            printf("FAIL");
        } else {
            if (dev & I2C_FLAG_BLOCK)
                printf("%02x", len);
            while (dlen-- > 0)
                printf("%02x", *(data++));
            if (dev & I2C_FLAG_PEC)
                printf("%02x",
                       i2c_calc_pec(dev, offset, len, datap, I2C_WRITE));
        }
        putchar('\n');
    }

    /* Restore saved I2C device (in case I2C called from ISR) */
i2c_bus_restore:
    i2c_bus    = i2c_bus_save;
    i2c_dev    = i2c_dev_save;
    i2c_offset = i2c_offset_save;

    return (rc);
}

/**
 * i2c_read_check() reads from an I2C device and reports an error message
 *                  if that read fails.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 * @param [in]  offset - The device string register to read.
 * @param [in]  len    - The maximum data buffer length.
 * @param [out] buf    - The buffer to return the read data.
 *
 * @return      RC_SUCCESS   - Read completed successfully.
 * @return      RC_TIMEOUT   - Device failed to respond.
 */
rc_t
i2c_read_check(uint bus, uint dev, uint offset, uint len, void *buf)
{
    rc_t rc = i2c_read(bus, dev, offset, len, buf);
    if (rc != RC_SUCCESS) {
        printf("I2C read ");
        (void) i2c_print_bdo(NULL, 0, bus, dev, offset);
        printf(" %s\n", (rc == RC_TIMEOUT) ? "timeout" : "failure");
    }
    return (rc);
}

/**
 * i2c_write_check() writes to an I2C device and reports an error message
 *                   if that write fails.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 * @param [in]  offset - The device string register to write.
 * @param [in]  len    - The maximum data buffer length.
 * @param [out] buf    - The buffer containing the data to write.
 *
 * @return      RC_SUCCESS   - Write completed successfully.
 * @return      RC_TIMEOUT   - Device failed to respond.
 */
rc_t
i2c_write_check(uint bus, uint dev, uint offset, uint len, const void *buf)
{
    rc_t rc = i2c_write(bus, dev, (uint) offset, len, buf);
    if (rc != RC_SUCCESS) {
        printf("I2C write ");
        (void) i2c_print_bdo(NULL, 0, bus, dev, offset);
        printf(" %s\n", (rc == RC_TIMEOUT) ? "timeout" : "failure");
    }
    return (rc);
}

/**
 * i2c_show_buses() displays available I2C buses and associated GPIOs
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
static void
i2c_show_buses(void)
{
    uint        bus;
    uint        sig;

    printf("  Bus Name      SCL GPIO   SDA GPIO   Speed\n");
    for (bus = 0; bus < i2c_bus_count; bus++) {
        printf("%4x  %-8.8s  ", bus, i2c_busdef[bus].name);
        for (sig = 0; sig < 2; sig++) {
            uint32_t gpio    = i2c_busdef[bus].port[sig];
            uint16_t pin     = i2c_busdef[bus].pin[sig];
            int      pin_bit = low_bit(pin);

            printf("%s%s [%u]   ",
                   gpio_to_str(gpio, pin), (pin_bit < 10) ? " " : "",
                   gpio_get(i2c_busdef[bus].port[sig],
                            i2c_busdef[bus].pin[sig]) ? 1 : 0);
        }
        printf("%3dkHz\n",
               (i2c_busdef[bus].speed & I2C_BUS_SPEED_50KHZ)  ? 50 :
               (i2c_busdef[bus].speed & I2C_BUS_SPEED_100KHZ) ? 100 :
               (i2c_busdef[bus].speed & I2C_BUS_SPEED_5KHZ)   ? 5 : 200);
    }
}

/**
 * i2c_nonaddressed_access_ok() returns TRUE if the specified bus and device
 *                              supports non-addressed accesses. Not all
 *                              devices support non-addressed accesses.
 *                              Accessing them without providing a device
 *                              offset will likely result in a NAK.
 *                              NOTE: Because the I2C protocol does not provide
 *                              a method for a master to probe a device, the
 *                              access method can only be determined by advanced
 *                              knowledge of device types assigned to specific
 *                              addresses and bus numbers.
 *
 * @param [in]  bus   - I2C Bus number of the device.
 * @param [in]  addr  - I2C address number of the device.
 *
 * @return      TRUE  - Device should be accessed with 16-bit addressing.
 * @return      FALSE - Device should be accessed with 8-bit addressing.
 */
static bool_t
i2c_nonaddressed_access_ok(uint bus, uint addr)
{
    /*
     * Avoid non-addressed accesses only on devices which fail badly.
     *
     * None currently defined. Some devices can not handle not having
     * an address specified.
     */
    return (TRUE);
}

/**
 * i2c_addressed_access_ok() returns TRUE if the specified bus and device
 *                           supports addressed accesses. Not all devices
 *                           (specifically simple I2C GPIO expanders)
 *                           support addressed accesses. Accessing them in
 *                           this mode may in fact change register contents.
 *                           NOTE: Because the I2C protocol does not provide
 *                           a method for a master to probe a device, the
 *                           access method can only be determined by advanced
 *                           knowledge of device types assigned to specific
 *                           addresses and bus numbers.
 *
 * @param [in]  bus    - I2C Bus number of the device.
 * @param [in]  addr   - I2C address number of the device.
 * @param [in]  offset - Device offset.
 *
 * @return      TRUE  - Device should be accessed with 16-bit addressing.
 * @return      FALSE - Device should be accessed with 8-bit addressing.
 */
static bool_t
i2c_addressed_access_ok(uint bus, uint addr, uint offset)
{
    /*
     * Avoid addressed accesses only on devices which interpret an offset
     * as a command code.
     *
     * None currently defined, but an I2C GPIO like the PCF8574 would
     * be a good example of one.
     */
    return (TRUE);
}

/**
 * i2c_16bit_addressing() returns TRUE if the specified bus and device supports
 *                        16-bit addressing. NOTE: Because the I2C
 *                        protocol does not provide a method for a master to
 *                        probe a device, the access method can only be
 *                        determined by advanced knowledge of device types
 *                        assigned to specific addresses and bus numbers.
 *
 * @param [in]  bus   - I2C Bus number of the device.
 * @param [in]  addr  - I2C address number of the device.
 *
 * @return      TRUE  - Device should be accessed with 16-bit addressing.
 * @return      FALSE - Device should be accessed with 8-bit addressing.
 */
static bool_t
i2c_16bit_addressing(uint bus, uint addr)
{
    /*
     * Check if 16-bit addressing should be performed on this specific device.
     * Unfortunately there is no I2C standard way to probe for this, so we
     * must hard-code any bus and addr values we want.
     */
    if ((bus == 0) && (addr >= 0xa0) && (addr <= 0xaf))
        return (TRUE);
    return (FALSE);
}

/**
 * i2c_probe_bus() scans an individual I2C bus for devices.
 *
 * @param [in]  bus       - Bus number to scan.
 * @param [in]  verbose   - Verbose probe output flag.
 * @param [out] dev_found - Pointer to boolean which will be set TRUE if a
 *                          device is found. The caller is responsible for
 *                          initializing this parameter.
 *
 * @return      RC_SUCCESS   - command successfully completed.
 * @return      RC_FAILURE   - I2C device read failure occurred (not timeout).
 */
static rc_t
i2c_probe_bus(uint bus, bool_t verbose, bool_t *dev_found)
{
    uint    addr;
    uint    fails = 0;
    uint8_t data;
    rc_t    rc;

    for (addr = 0; addr < I2C_MAX_ADDR; addr++) {
        bool_t found = FALSE;
        uint   offset;
        uint   add = I2C_FLAG_NO_RETRY | I2C_FLAG_NO_CHECK;

        if (i2c_nonaddressed_access_ok(bus, addr)) {
            bool_t read_good = TRUE;
            rc = i2c_read(bus, addr | add | I2C_FLAG_NONE, 0, 1, &data);
            if ((rc == RC_TIMEOUT) && (addr != 0)) {
                rc = i2c_write(bus, addr | add | I2C_FLAG_NONE, 0, 0, &data);
                read_good = FALSE;
                if ((rc != RC_SUCCESS) && (rc != RC_TIMEOUT))
                    printf("rc=%d\n", rc);
            }
            if (rc == RC_SUCCESS) {
                if (i2c_bus_count > 1)
                    printf("%x.", bus);
                if (read_good == TRUE)
                    printf("%02x [%02x]", addr, data);
                else
                    printf("%02x [XX]", addr);
                found      = TRUE;
                *dev_found = TRUE;
            } else if (rc != RC_TIMEOUT) {
                if (verbose) {
                    if (i2c_bus_count > 1)
                        printf("%x.", bus);
                    printf("%02x Fail\n", addr);
                }
                if (fails++ > 4)
                    return (rc);  // Too many failures -- abort on this bus
                goto endloop;
            } else if (verbose) {
                if (i2c_bus_count > 1)
                    printf("%x.", bus);
                printf("%02x Timeout\n", addr);
                goto endloop;
            }
        }
        if (i2c_16bit_addressing(bus, addr))
            add |= I2C_FLAG_16BIT;
        for (offset = 0; offset < 16; offset++) {
            if (i2c_addressed_access_ok(bus, addr, offset) == FALSE)
                continue;
            rc = i2c_read(bus, addr | add, offset, 1, &data);
            if (rc == RC_SUCCESS) {
                if (found == FALSE) {
                    if (i2c_bus_count > 1)
                        printf("%x.", bus);
                    printf("%02x [  ]%*s", addr, offset * 3, "");
                    found      = TRUE;
                    *dev_found = TRUE;
                }
                printf(" %02x", data);
            } else {
                if (found == TRUE) {
                    printf(" --");
                } else if (rc == RC_TIMEOUT) {
                    break;
                } else {
                    goto endloop;
                }
            }
        }

endloop:
        if (input_break_pending()) {
            puts("^C");
            return (RC_USR_ABORT);
        }
        if (found)
            printf("\n");
    }
    return (RC_SUCCESS);
}

/**
 * i2c_probe() scans all I2C buses for devices.
 *
 * @param [in]  argc - Count of user arguments.
 * @param [in]  argv - Array of user arguments.
 *              A specific bus number to scan may be specified with the
 *              optional "bus" command.
 *
 * @return      RC_SUCCESS   - command successfully completed.
 * @return      RC_BAD_PARAM - bad user input.
 */
static rc_t
i2c_probe(int argc, char * const *argv)
{
    int     arg;
    uint    bus;
    bool_t  found     = FALSE;
    bool_t  verbose   = FALSE;
    rc_t    rc        = RC_SUCCESS;

    for (arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "bus") == 0) {
            uint count;
            int  first_arg = arg + 1;

            found = FALSE;
            if (first_arg >= argc) {
                warnx("bus number required");
                return (RC_BAD_PARAM);
            }
            for (arg = first_arg; arg < argc; arg++) {
                if ((sscanf(argv[arg], "%x%n", &bus, &count) != 1) ||
                    (argv[arg][count] != '\0') ||
                    ((bus & ~0x80) >= i2c_bus_count)) {
                    warnx("Invalid I2C bus %s", argv[arg]);
                    return (RC_BAD_PARAM);
                }
                rc = i2c_probe_bus(bus, verbose, &found);
                if (rc == RC_USR_ABORT)
                    return (rc);
                if (found == FALSE) {
                    if (i2c_bus_avail(bus, RECOVER_FORCE) == FALSE)
                        printf("Bus %x is not available\n", bus);
                    else
                        printf("Found no I2C devices on bus %x\n", bus);
                }
            }
            return (RC_SUCCESS);
        } else if (strcmp(argv[arg], "verbose") == 0) {
            verbose = TRUE;
        } else {
            warnx("invalid i2c probe argument '%s'", argv[arg]);
            return (RC_BAD_PARAM);
        }
    }

    for (bus = 0; bus < i2c_bus_count; bus++) {
        if (i2c_bus_avail(bus, RECOVER_AUTO) == FALSE)
            continue;
        rc = i2c_probe_bus(bus, verbose, &found);
        if (rc == RC_USR_ABORT)
            return (rc);
    }

    if (found == FALSE)
        printf("Found no I2C devices\n");

    return (RC_SUCCESS);
}

/**
 * i2c_stats() displays I2C access statistics.
 *
 * @param [in]  argc - Count of user arguments.
 * @param [in]  argv - Array of user arguments.
 *
 * @return      RC_SUCCESS   - Stats displayed.
 * @return      RC_BAD_PARAM - Invalid argument specified.
 */
static rc_t
i2c_stats(int argc, char * const argv[])
{
    int    arg;
    bool_t show_stats  = FALSE;
    bool_t clear_stats = FALSE;

    for (arg = 1; arg < argc; arg++) {
        const char *cmd = argv[arg];
        if (strcmp(cmd, "clear") == 0) {
            clear_stats = TRUE;
        } else if (strcmp(cmd, "show") == 0) {
            show_stats = TRUE;
        } else {
            warnx("Invalid argument %s\n"
                  "only clear and show are valid", cmd);
            return (RC_BAD_PARAM);
        }
    }
    if (argc <= 1)
        show_stats = TRUE;

    if (show_stats) {
        printf("             Good    Retry Miscompare PEC_Fail Misc_Fail\n"
               "Read   %10llx %8x   %8x %8x  %8x\n"
               "Write  %10llx %8x   %8x           %8x\n",
               (long long) i2c_stat.read_good, i2c_stat.read_retry,
               i2c_stat.read_compare_fail, i2c_stat.read_pec_fail,
               i2c_stat.read_fail,
               (long long) i2c_stat.write_good, i2c_stat.write_retry,
               i2c_stat.write_compare_fail, i2c_stat.write_fail);
    }
    if (clear_stats)
        memset(&i2c_stat, 0, sizeof (i2c_stat));

    return (RC_SUCCESS);
}

/**
 * cmd_i2c() provides the user "i2c" command.
 *
 * @param [in]  argc - Count of user arguments.
 * @param [in]  argv - Array of user arguments.
 *
 * @return      RC_SUCCESS   - Successful completion.
 * @return      RC_TIMEOUT   - I2C operation timeout.
 * @return      RC_FAILURE   - I2C failure.
 * @return      RC_BAD_PARAM - Invalid parameter.
 */
rc_t
cmd_i2c(int argc, char * const argv[])
{
    if (argc < 2) {
        puts(cmd_i2c_help);
        if (i2c_help_devs[0] != '\0') {
            puts("Expected devices:");
            printf(i2c_help_devs);
        }
        return (RC_BAD_PARAM);
    }
    if (strncmp(argv[1], "bus", 3) == 0) {
        i2c_show_buses();
    } else if (strcmp(argv[1], "help") == 0) {
        printf(cmd_i2c_help);
    } else if (strncmp(argv[1], "probe", 1) == 0) {
        return (i2c_probe(--argc, ++argv));
    } else if (strcmp(argv[1], "reset") == 0) {
        i2c_init();
    } else if (strncmp(argv[1], "stats", 4) == 0) {
        return (i2c_stats(--argc, ++argv));
    } else if (strncmp(argv[1], "verbose", 1) == 0) {
        if (config.debug_flag & DF_I2C)
            config.debug_flag &= ~(DF_I2C | DF_I2C_LL);
        else
            config.debug_flag |= DF_I2C | DF_I2C_LL;
        printf("I2C verbose mode %s\n",
               (config.debug_flag & DF_I2C) ? "on" : "off");
    } else {
        printf("Invalid i2c command \"%s\"\n", argv[1]);
        printf(cmd_i2c_help);
        return (RC_BAD_PARAM);
    }
    return (RC_SUCCESS);
}

/**
 * i2c_init() configures and enables the I2C interfaces of the STM32 CPU.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
void
i2c_init(void)
{
    i2c_help_devs  = i2c_help_amigapci;
    i2c_busdef     = i2c_busdef_amigapci;
    i2c_bus_count  = ARRAY_SIZE(i2c_busdef_amigapci);
}
