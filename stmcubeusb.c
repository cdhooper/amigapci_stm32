/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * ST-Micro CubeMX USB Host library handling.
 */

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "clock.h"
#include "stmcubeusb.h"
#include <usbh_cdc.h>  // CDC
#include <usbh_hid.h>  // HID
#include <usbh_msc.h>  // MSC
#include <usbh_mtp.h>  // MTP

void otg_fs_isr(void);
void otg_hs_ep1_out_isr(void);
void otg_hs_ep1_in_isr(void);
void otg_hs_wkup_isr(void);
void otg_hs_isr(void);
void usb_poll(void);

struct {
    uint otg_fs_ints;
    uint otg_hs_ints;
    uint otg_hs_ep1_in_ints;
    uint otg_hs_ep1_out_ints;
    uint otg_hs_wkup_ints;
} usb_stats;

/* USB port state */
typedef enum {
    APPLICATION_IDLE = 0,
    APPLICATION_START,
    APPLICATION_READY,
    APPLICATION_DISCONNECT
} appstate_type;

typedef struct {
    USBH_HandleTypeDef handle;
    HID_TypeTypeDef    hid_devtype;
    uint64_t           hid_ready_timer;
    appstate_type      appstate;
} usb_t;
static usb_t usb[2];

HCD_HandleTypeDef hhcd_USB_OTG_FS;
HCD_HandleTypeDef hhcd_USB_OTG_HS;

static const char * const host_user_types[] = {
    "SELECT_CONF", "ACTIVE", "SELECTED", "CONN", "DISCONN", "ERROR"
};
static void
USBH_UserProcess0(USBH_HandleTypeDef *phost, uint8_t id)
{
    printf("USB0 %x %s\n", id, (id < ARRAY_SIZE(host_user_types)) ?
           host_user_types[id] : "Unknown");
    switch (id) {
        case HOST_USER_SELECT_CONFIGURATION:
            break;
        case HOST_USER_CLASS_ACTIVE:
            usb[0].appstate = APPLICATION_READY;
            break;
        case HOST_USER_CLASS_SELECTED:
            break;
        case HOST_USER_CONNECTION:
            usb[0].appstate = APPLICATION_START;
            break;
        case HOST_USER_DISCONNECTION:
            usb[0].appstate = APPLICATION_DISCONNECT;
            break;
        case HOST_USER_UNRECOVERED_ERROR:
            break;
        default:
            break;
    }
}

static void
USBH_UserProcess1(USBH_HandleTypeDef *phost, uint8_t id)
{
    printf("USB1 %x %s\n", id, (id < ARRAY_SIZE(host_user_types)) ?
           host_user_types[id] : "Unknown");
    switch (id) {
        case HOST_USER_SELECT_CONFIGURATION:
            break;
        case HOST_USER_CLASS_ACTIVE:
            usb[1].appstate = APPLICATION_READY;
            break;
        case HOST_USER_CLASS_SELECTED:
            break;
        case HOST_USER_CONNECTION:
            usb[1].appstate = APPLICATION_START;
            break;
        case HOST_USER_DISCONNECTION:
            usb[1].appstate = APPLICATION_DISCONNECT;
            break;
        case HOST_USER_UNRECOVERED_ERROR:
            break;
        default:
            break;
    }
}

void
otg_fs_isr(void)
{
//  printf("Ifs");
    usb_stats.otg_fs_ints++;
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_FS);
}

void
otg_hs_ep1_in_isr(void)
{
    usb_stats.otg_hs_ep1_in_ints++;
    printf("[Ihsepi]");
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
}

void
otg_hs_ep1_out_isr(void)
{
    usb_stats.otg_hs_ep1_out_ints++;
    printf("[Ihsepo]");
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
}

void
otg_hs_wkup_isr(void)
{
    usb_stats.otg_hs_wkup_ints++;
    printf("[Ihsw]");
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
}

void
otg_hs_isr(void)
{
//  printf("Ihs");
    usb_stats.otg_hs_ints++;
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
}

void
usb_show_stats(void)
{
    printf("FS ints         %u\n"
           "HS ints         %u\n"
           "HS EP1 IN ints  %u\n"
           "HS EP1 OUT ints %u\n"
           "HS WKUP ints    %u\n",
           usb_stats.otg_fs_ints, usb_stats.otg_hs_ints,
           usb_stats.otg_hs_ep1_in_ints, usb_stats.otg_hs_ep1_out_ints,
           usb_stats.otg_hs_wkup_ints);
}


void
USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
    uint port = (phost == &usb[0].handle) ? 0 : 1;
    printf("USB%u Event\n", port);
    if (USBH_HID_GetDeviceType(phost) == HID_MOUSE) {  // Mouse
        HID_MOUSE_Info_TypeDef *Mouse_Info;
        Mouse_Info = USBH_HID_GetMouseInfo(phost);  // Get the info
        int X_Val = Mouse_Info->x;  // get the x value
        int Y_Val = Mouse_Info->y;  // get the y value
        if (X_Val > 127)
            X_Val -= 255;
        if (Y_Val > 127)
            Y_Val -= 255;
        printf("X=%d, Y=%d, Button1=%d, Button2=%d, Button3=%d\n",
               X_Val, Y_Val, Mouse_Info->buttons[0],
               Mouse_Info->buttons[1], Mouse_Info->buttons[2]);
    }

    if (USBH_HID_GetDeviceType(phost) == HID_KEYBOARD) {  // Keyboard
        uint8_t key;
        HID_KEYBD_Info_TypeDef *Keyboard_Info;
        Keyboard_Info = USBH_HID_GetKeybdInfo(phost);  // get the info
        key = USBH_HID_GetASCIICode(Keyboard_Info);  // get the key pressed
        printf("Key Pressed = %c\n", key);
    }
}

static void
handle_dev(USBH_HandleTypeDef *dev, uint devnum)
{
    if (usb[devnum].appstate == APPLICATION_READY) {
        HID_TypeTypeDef type = USBH_HID_GetDeviceType(dev);
        if (usb[devnum].hid_devtype != type) {
            usb[devnum].hid_devtype = type;
            printf("USB%u type %x %s\n", devnum, type,
                   (type == HID_KEYBOARD) ? "Keyboard" :
                   (type == HID_MOUSE) ? "Mouse" : "Unknown");
            usb[devnum].hid_ready_timer = timer_tick_plus_msec(500);
        }
        if (usb[devnum].hid_ready_timer) {
            if (!timer_tick_has_elapsed(usb[devnum].hid_ready_timer))
                return;
            usb[devnum].hid_ready_timer = 0;
        }
        if (type == HID_KEYBOARD) {
            HID_KEYBD_Info_TypeDef *info;
            info = USBH_HID_GetKeybdInfo(dev);
            if (info != NULL) {
                printf("lctrl = %d lshift = %d lalt   = %d\r\n"
                       "lgui  = %d rctrl  = %d rshift = %d\r\n"
                       "ralt  = %d rgui   = %d\r\n",
                       info->lctrl, info->lshift, info->lalt,
                       info->lgui, info->rctrl, info->rshift,
                       info->ralt, info->rgui);
                // info->keys[]
            }
        }
        if (type == HID_MOUSE) {
        }
    }
}

void
usb_poll(void)
{
    /* USB Host Background task */
    USBH_Process(&usb[0].handle);
    USBH_Process(&usb[1].handle);
    handle_dev(&usb[0].handle, 0);
    handle_dev(&usb[1].handle, 1);
// extern TIM_HandleTypeDef htim1;
// HAL_TIM_IRQHandler(&htim1);
}

uint32_t
HAL_GetTick(void)
{
    printf("HAL_GetTick %lu\n", uwTick);
    return (uwTick);
}

HAL_TickFreqTypeDef
HAL_GetTickFreq(void)
{
    printf("HAL_GetTickFreq\n");
    return (uwTickFreq);
}

uint32_t
HAL_GetTickPrio(void)
{
    printf("HAL_GetTickPrio\n");
    return (uwTickPrio);
}

void
HAL_Delay(uint32_t Delay)
{
    timer_delay_msec(Delay);
}

void
HAL_ResumeTick(void)
{
    /* Enable SysTick Interrupt */
    printf("HAL_ResumeTick\n");
    SysTick->CTRL  |= SysTick_CTRL_TICKINT_Msk;
}

void
HAL_SuspendTick(void)
{
    /* Disable SysTick Interrupt */
    printf("HALSuspendTick\n");
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
}

#define GPIO11 BIT(11)
#define GPIO12 BIT(12)
#define GPIO14 BIT(14)
#define GPIO15 BIT(15)
#define USB1_DM_PORT        GPIOA
#define USB1_DM_PIN             GPIO11
#define USB1_DP_PORT        GPIOA
#define USB1_DP_PIN             GPIO12
#define USB2_DM_PORT        GPIOB
#define USB2_DM_PIN             GPIO14
#define USB2_DP_PORT        GPIOB
#define USB2_DP_PIN             GPIO15

#if 0
void
HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
    printf("HAL_GPIO_Init %p %08lx MODE=%08lx\n",
           (void *)GPIOx, GPIO_Init->Pin, GPIO_Init->Mode);
}
#endif

void
HAL_HCD_MspInit(HCD_HandleTypeDef* hcdHandle)
{
//  printf("HAL_HCD_MspInit\n");
}

void
HAL_HCD_MspDeInit(HCD_HandleTypeDef* hcdHandle)
{
    printf("HAL_HCD_MspDeInit\n");
}
void
HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
    USBH_LL_IncTimer(hhcd->pData);
}

void
HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
    uint port = (hhcd == &hhcd_USB_OTG_FS) ? 0 : 1;
    printf("HAL_HCD_Connect_Callback %u\n", port);
    USBH_LL_Connect(hhcd->pData);
}
void
HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
    uint port = (hhcd == &hhcd_USB_OTG_FS) ? 0 : 1;
    printf("HAL_HCD_Disconnect_Callback %u\n", port);
    USBH_LL_Disconnect(hhcd->pData);
}
void
HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *hhcd, uint8_t chnum,
                                    HCD_URBStateTypeDef urb_state)
{
//  printf("HAL_HCD_HC_NotifyURBChange_Callback\n");
#if (USBH_USE_OS == 1)
    /* To be used with OS to sync URB state with the global state machine */
    USBH_LL_NotifyURBChange(hhcd->pData);
#endif
}
void
HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
    uint port = (hhcd == &hhcd_USB_OTG_FS) ? 0 : 1;
    printf("HAL_HCD_PortEnabled_Callback %u\n", port);
    USBH_LL_PortEnabled(hhcd->pData);
}
void
HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
    uint port = (hhcd == &hhcd_USB_OTG_FS) ? 0 : 1;
    printf("HAL_HCD_PortDisabled_Callback %u\n", port);
    USBH_LL_PortDisabled(hhcd->pData);
}
USBH_StatusTypeDef
USBH_LL_Init(USBH_HandleTypeDef *phost)
{
    /* Init USB_IP */
    printf("USBH_LL_Init\n");
    if (phost->id == HOST_FS) {
        /* Link the driver to the stack. */
        hhcd_USB_OTG_FS.pData = phost;
        phost->pData = &hhcd_USB_OTG_FS;

        hhcd_USB_OTG_FS.Instance = USB_OTG_FS;
        hhcd_USB_OTG_FS.Init.Host_channels = 8;
        hhcd_USB_OTG_FS.Init.speed = HCD_SPEED_FULL;
        hhcd_USB_OTG_FS.Init.dma_enable = DISABLE;
        hhcd_USB_OTG_FS.Init.phy_itface = HCD_PHY_EMBEDDED;
        hhcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
        if (HAL_HCD_Init(&hhcd_USB_OTG_FS) != HAL_OK) {
            printf("HAL_HCD_Init error\n");
            return (USBH_FAIL);
        }
        USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_FS));
    }
    if (phost->id == HOST_HS) {
        /* Link the driver to the stack. */
        hhcd_USB_OTG_HS.pData = phost;
        phost->pData = &hhcd_USB_OTG_HS;

        hhcd_USB_OTG_HS.Instance = USB_OTG_HS;
        hhcd_USB_OTG_HS.Init.Host_channels = 12;
        hhcd_USB_OTG_HS.Init.speed = HCD_SPEED_FULL;
        hhcd_USB_OTG_HS.Init.dma_enable = DISABLE;
        hhcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
        hhcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
        hhcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
        hhcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
        hhcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
        if (HAL_HCD_Init(&hhcd_USB_OTG_HS) != HAL_OK) {
            printf("HAL_HCD_Init OTG_HS error\n");
            return (USBH_FAIL);
        }
        USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_HS));
    }
    return (USBH_OK);
}
USBH_URBStateTypeDef
USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return (USBH_URBStateTypeDef) HAL_HCD_HC_GetURBState(phost->pData, pipe);
}
USBH_StatusTypeDef
USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
    return (USBH_OK);
}
USBH_StatusTypeDef
USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t toggle)
{
    HCD_HandleTypeDef *pHandle;
    pHandle = phost->pData;

    if (pHandle->hc[pipe].ep_is_in) {
        pHandle->hc[pipe].toggle_in = toggle;
    } else {
        pHandle->hc[pipe].toggle_out = toggle;
    }
    return (USBH_OK);
}
uint8_t
USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    uint8_t toggle = 0;
    HCD_HandleTypeDef *pHandle;
    pHandle = phost->pData;

    if (pHandle->hc[pipe].ep_is_in) {
        toggle = pHandle->hc[pipe].toggle_in;
    } else {
        toggle = pHandle->hc[pipe].toggle_out;
    }
    return (toggle);
}
USBH_StatusTypeDef USBH_Get_USB_Status(HAL_StatusTypeDef hal_status);
USBH_StatusTypeDef
USBH_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
    USBH_StatusTypeDef usb_status = USBH_OK;

    switch (hal_status) {
        case HAL_OK :
            usb_status = USBH_OK;
            break;
        case HAL_ERROR :
            usb_status = USBH_FAIL;
            break;
        case HAL_BUSY :
            usb_status = USBH_BUSY;
            break;
        case HAL_TIMEOUT :
            usb_status = USBH_FAIL;
            break;
        default :
            usb_status = USBH_FAIL;
            break;
    }
    return (usb_status);
}
uint32_t
USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return (HAL_HCD_HC_GetXferCount(phost->pData, pipe));
}
USBH_StatusTypeDef
USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe_num, uint8_t epnum,
                 uint8_t dev_address, uint8_t speed, uint8_t ep_type,
                 uint16_t mps)
{
    HAL_StatusTypeDef hal_status = HAL_OK;
    USBH_StatusTypeDef usb_status = USBH_OK;

    hal_status = HAL_HCD_HC_Init(phost->pData, pipe_num, epnum,
                                 dev_address, speed, ep_type, mps);

    usb_status = USBH_Get_USB_Status(hal_status);

    return (usb_status);
}

USBH_StatusTypeDef
USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    HAL_StatusTypeDef hal_status = HAL_OK;

    hal_status = HAL_HCD_HC_Halt(phost->pData, pipe);

    return (USBH_Get_USB_Status(hal_status));
}

USBH_StatusTypeDef
USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t direction,
                  uint8_t ep_type, uint8_t token, uint8_t *pbuff,
                  uint16_t length, uint8_t do_ping)
{
    HAL_StatusTypeDef hal_status;

    hal_status = HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction,
                                          ep_type, token, pbuff, length,
                                          do_ping);
    return (USBH_Get_USB_Status(hal_status));
}

USBH_StatusTypeDef
USBH_LL_Start(USBH_HandleTypeDef *phost)
{
    HAL_StatusTypeDef hal_status = HAL_HCD_Start(phost->pData);

    return (USBH_Get_USB_Status(hal_status));
}

USBH_StatusTypeDef
USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
    HAL_StatusTypeDef hal_status = HAL_HCD_Stop(phost->pData);

    return (USBH_Get_USB_Status(hal_status));
}
USBH_SpeedTypeDef
USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
    USBH_SpeedTypeDef speed = USBH_SPEED_FULL;

    switch (HAL_HCD_GetCurrentSpeed(phost->pData)) {
        case 0 :
            speed = USBH_SPEED_HIGH;
            break;
        case 1 :
            speed = USBH_SPEED_FULL;
            break;
        case 2 :
            speed = USBH_SPEED_LOW;
            break;
        default:
            speed = USBH_SPEED_FULL;
            break;
    }
    return (speed);
}
USBH_StatusTypeDef
USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
    HAL_StatusTypeDef hal_status = HAL_OK;

    hal_status = HAL_HCD_ResetPort(phost->pData);

    return (USBH_Get_USB_Status(hal_status));
}
void
USBH_Delay(uint32_t Delay)
{
    timer_delay_msec(Delay);
}

void
cubeusb_init(void)
{
//    HAL_InitTick(TICK_INT_PRIORITY);
//    XXX: Instead just figure out what the HAL tick function is
//    doing and copy that in my own code.
//    Need a 1ms timer base
    printf("cubeusb_init\n");

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    if (USBH_Init(&usb[0].handle, USBH_UserProcess0, HOST_FS) != USBH_OK) {
        printf("USB FS init fail\n");
    } else if ((USBH_RegisterClass(&usb[0].handle, USBH_CDC_CLASS)) ||
               (USBH_RegisterClass(&usb[0].handle, USBH_MSC_CLASS)) ||
               (USBH_RegisterClass(&usb[0].handle, USBH_HID_CLASS)) ||
               (USBH_RegisterClass(&usb[0].handle, USBH_MTP_CLASS))) {
        printf("USB FS register fail\n");
    } else if (USBH_Start(&usb[0].handle) != USBH_OK) {
        printf("USB FS start fail\n");
    }
    if (USBH_Init(&usb[1].handle, USBH_UserProcess1, HOST_HS) != USBH_OK) {
        printf("USB HS init fail\n");
    } else if ((USBH_RegisterClass(&usb[1].handle, USBH_CDC_CLASS)) ||
               (USBH_RegisterClass(&usb[1].handle, USBH_MSC_CLASS)) ||
               (USBH_RegisterClass(&usb[1].handle, USBH_HID_CLASS)) ||
               (USBH_RegisterClass(&usb[1].handle, USBH_MTP_CLASS))) {
        printf("USB HS register fail\n");
    } else if (USBH_Start(&usb[1].handle) != USBH_OK) {
        printf("USB HS start fail\n");
    }
}

void
cubeusb_shutdown(void)
{
    USBH_DeInit(&usb[0].handle);
    USBH_DeInit(&usb[1].handle);
}
