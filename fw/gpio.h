/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Low level STM32 GPIO access.
 */

#ifndef _GPIO_H
#define _GPIO_H

#include <libopencm3/stm32/gpio.h>

#define POWER_LED_PORT      GPIOA
#define POWER_LED_PIN           GPIO8   // PA8

#define USB_ENABLE_PORT     GPIOA
#define USB_ENABLE_PIN          GPIO9   // PA9 Enable power to USB port
#define USB1_DM_PORT        GPIOA
#define USB1_DM_PIN             GPIO11  // PA11
#define USB1_DP_PORT        GPIOA
#define USB1_DP_PIN             GPIO12  // PA12
#define USB2_DM_PORT        GPIOB
#define USB2_DM_PIN             GPIO14  // PB14
#define USB2_DP_PORT        GPIOB
#define USB2_DP_PIN             GPIO15  // PB15

#define I2C_SCL_PORT        GPIOA
#define I2C_SCL_PIN             GPIO10  // PA10 I2C SCL
#define I2C_SDA_PORT        GPIOA
#define I2C_SDA_PIN             GPIO15  // PA15 I2C SDA

#define RTCEN_PORT          GPIOB
#define RTCEN_PIN               GPIO0   // PB0 RTC bus active (low)
#define R_WA_PORT           GPIOB
#define R_WA_PIN                GPIO1   // PB1 RTC read/write operation
#define HIDEN_PORT          GPIOB
#define HIDEN_PIN               GPIO2   // PB2
#define D16_PORT            GPIOB
#define D16_PIN                 GPIO4   // PB4
#define D17_PORT            GPIOB
#define D17_PIN                 GPIO5   // PB5
#define D18_PORT            GPIOB
#define D18_PIN                 GPIO6   // PB6
#define D19_PORT            GPIOB
#define D19_PIN                 GPIO7   // PB7
#define A2_PORT             GPIOB
#define A2_PIN                  GPIO10  // PB10
#define A3_PORT             GPIOB
#define A3_PIN                  GPIO11  // PB11
#define A4_PORT             GPIOB
#define A4_PIN                  GPIO12  // PB12
#define A5_PORT             GPIOB
#define A5_PIN                  GPIO13  // PB13

#define FANPWM_PORT         GPIOB
#define FANPWM_PIN              GPIO8   // PB8 Fan speed select
#define FANTACH_PORT        GPIOB
#define FANTACH_PIN             GPIO9   // PB9 Fan speed measurement

#define PWRSW_PORT          GPIOC
#define PWRSW_PIN               GPIO12  // PC12 User power switch/button
#define STMRSTA_PORT        GPIOC
#define STMRSTA_PIN             GPIO13  // PC13 Signal STM32 to reset USB, etc
#define PSON_PORT           GPIOD
#define PSON_PIN                GPIO2   // PCD2 Power supply "button"

#define LSCLKIN_PORT        GPIOC
#define LSCLKIN_PIN             GPIO14  // PC14
#define LSCLKOUT_PORT       GPIOC
#define LSCLKOUT_PIN            GPIO15  // PC15

#define PotX_PORT           GPIOA
#define PotX_PIN                GPIO4  // PA4
#define PotY_PORT           GPIOA
#define PotY_PIN                GPIO5  // PA5
#define FORWARD_PORT        GPIOC
#define FORWARD_PIN             GPIO0  // PC2
#define BACK_PORT           GPIOC
#define BACK_PIN                GPIO1  // PC3
#define LEFT_PORT           GPIOC
#define LEFT_PIN                GPIO2  // PC4
#define RIGHT_PORT          GPIOC
#define RIGHT_PIN               GPIO3  // PC5
#define FIRE_PORT           GPIOC
#define FIRE_PIN                GPIO6  // PC6
#define KBRST_PORT          GPIOC
#define KBRST_PIN               GPIO7  // PC7
#define KBDATA_PORT         GPIOC
#define KBDATA_PIN              GPIO8  // PC8
#define KBCLK_PORT          GPIOC
#define KBCLK_PIN               GPIO9  // PC9

#define VMON5_PORT          GPIOA
#define VMON5_PIN               GPIO0  // PA0 5V
#define VMON5SB_PORT        GPIOA
#define VMON5SB_PIN             GPIO1  // PA1 5V Standby
#define VMON3V3_PORT        GPIOA
#define VMON3V3_PIN             GPIO2  // PA2 3.3V
#define VMON1V2_PORT        GPIOA
#define VMON1V2_PIN             GPIO3  // PA3 1.2V
#define VMONx_PORT          GPIOA
#define VMONx_PIN               GPIO6  // PA6
#define VMONy_PORT          GPIOA
#define VMONy_PIN               GPIO7  // PA7
#define VMON12_PORT         GPIOC
#define VMON12_PIN              GPIO4  // PC4 12V
#define VMONNEG12_PORT      GPIOC
#define VMONNEG12_PIN           GPIO5  // PC5 -12V Analog


/*
 * gpio_setmode() and gpio_getmode() definitions
 *
 * Bit values below are based on:
 *      AltFunc  Unused  MODER OTYPER  PUPDR OSPEEDR
 *         xxxx  x       xx    x       xx    xx
 *   Upper-Byte  -----Upper-Nibble---  -Lower-Nibble-
 *
 * MODER          OTYPER          PUPDR           OSPEEDR
 * 00 = Input     0=Push-pull     00=None         00=2 MHz
 * 01 = Output    1=Open-drain    01=Pull-up      01=25 MHz
 * 10 = AltFunc                   10=Pull-down    10=50 MHz
 * 11 = Analog                    11=Rsvd         11=100 MHz
 */
#define GPIO_SETMODE_INPUT               0x00  // Floating input (reset state)
#define GPIO_SETMODE_INPUT_PU            0x04  // Input, pull-up
#define GPIO_SETMODE_INPUT_PD            0x08  // Input, pull-down
#define GPIO_SETMODE_OUTPUT_2            0x20  // Output, push-pull, 2 Mhz
#define GPIO_SETMODE_OUTPUT_25           0x21  // Output, push-pull, 25 Mhz
#define GPIO_SETMODE_OUTPUT_50           0x22  // Output, push-pull, 50 Mhz
#define GPIO_SETMODE_OUTPUT_100          0x23  // Output, push-pull, 100 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_2     0x30  // Output, open-drain, 2 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_25    0x31  // Output, open-drain, 25 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_50    0x32  // Output, open-drain, 50 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_100   0x33  // Output, open-drain, 100 Mhz
#define GPIO_SETMODE_ALTFUNC_2           0x40  // Alt Func, push-pull, 2 Mhz
#define GPIO_SETMODE_ALTFUNC_25          0x41  // Alt Func, push-pull, 25 Mhz
#define GPIO_SETMODE_ALTFUNC_50          0x42  // Alt Func, push-pull, 50 Mhz
#define GPIO_SETMODE_ALTFUNC_100         0x43  // Alt Func, push-pull, 100 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_2    0x50  // Alt Func, open-drain, 2 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_25   0x51  // Alt Func, open-drain, 25 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_50   0x52  // Alt Func, open-drain, 50 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_100  0x53  // Alt Func, open-drain, 100 Mhz
#define GPIO_SETMODE_ANALOG              0x60  // Analog

/* gpio_setmode() and gpio_getmode() or masks */
#define GPIO_SETMODE_SPEED_2             0x00  // Speed 2 MHz (or mask)
#define GPIO_SETMODE_SPEED_25            0x01  // Speed 25 MHz (or mask)
#define GPIO_SETMODE_SPEED_50            0x02  // Speed 50 MHz (or mask)
#define GPIO_SETMODE_SPEED_100           0x03  // Speed 100 MHz (or mask)
#define GPIO_SETMODE_PU                  0x04  // Pull-up (or mask)
#define GPIO_SETMODE_PD                  0x08  // Pull-down (or mask)
#define GPIO_SETMODE_OUTPUT              0x20  // Output
#define GPIO_SETMODE_OUTPUT_ODRAIN       0x30  // Output, open-drain
#define GPIO_SETMODE_AF_AF0             0x040  // Alt Function AF0 (or mask)
#define GPIO_SETMODE_AF_AF1             0x140  // Alt Function AF1 (or mask)
#define GPIO_SETMODE_AF_AF2             0x240  // Alt Function AF2 (or mask)
#define GPIO_SETMODE_AF_AF3             0x340  // Alt Function AF3 (or mask)
#define GPIO_SETMODE_AF_AF4             0x440  // Alt Function AF4 (or mask)
#define GPIO_SETMODE_AF_AF5             0x540  // Alt Function AF5 (or mask)
#define GPIO_SETMODE_AF_AF6             0x640  // Alt Function AF6 (or mask)
#define GPIO_SETMODE_AF_AF7             0x740  // Alt Function AF7 (or mask)
#define GPIO_SETMODE_AF_AF8             0x840  // Alt Function AF8 (or mask)
#define GPIO_SETMODE_AF_AF9             0x940  // Alt Function AF9 (or mask)
#define GPIO_SETMODE_AF_AF10            0xa40  // Alt Function AF10 (or mask)
#define GPIO_SETMODE_AF_AF11            0xb40  // Alt Function AF11 (or mask)
#define GPIO_SETMODE_AF_AF12            0xc40  // Alt Function AF12 (or mask)
#define GPIO_SETMODE_AF_AF13            0xd40  // Alt Function AF13 (or mask)
#define GPIO_SETMODE_AF_AF14            0xe40  // Alt Function AF14 (or mask)
#define GPIO_SETMODE_AF_AF15            0xf40  // Alt Function AF15 (or mask)

/*
 * STM32F2 Alternate Functions
 *  AF0  System
 *  AF1  TIM1, TIM2
 *  AF2  TIM3, TIM4, TIM5
 *  AF3  TIM8, TIM9, TIM10, TIM11
 *  AF4  I2C1, I2C2, I2C3
 *  AF5  SPI1, SPI2
 *  AF6  SPI3
 *  AF7  USART1, USART2, USART3
 *  AF8  USART4, USART5, USART6
 *  AF9  CAN1, CAN1, TIM12, TIM13, TIM14
 *  AF10 OTG_FS, OTG_HS
 *  AF11 ETH
 *  AF12 FSMC, SDIO, OTH_HS
 *  AF13 DCMI
 *  AF14 -
 *  AF15 EVENTOUT
 */

#define NUM_GPIO_BANKS 6

void gpio_setv(uint32_t GPIOx, uint16_t GPIO_Pins, int value);
void gpio_setmode(uint32_t GPIOx, uint16_t GPIO_Pins, uint value);
uint gpio_getmode(uint32_t GPIOx, uint pin);
void gpio_init(void);
void gpio_init_early(void);
void gpio_show(int whichport, int pins);
void gpio_assign(int whichport, int pins, const char *assign);
uint gpio_name_match(const char **name, uint16_t pins[NUM_GPIO_BANKS]);
char *gpio_to_str(uint32_t port, uint16_t pin);
uint32_t gpio_num_to_gpio(uint num);
const char *gpio_to_name(int port, int pin);

#endif /* _GPIO_H */
