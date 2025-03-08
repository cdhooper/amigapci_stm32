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
#include "keyboard.h"
#include "mouse.h"
#include "cubeusb.h"
#include <usbh_cdc.h>  // CDC
#include <usbh_hid.h>  // HID
#include <usbh_msc.h>  // MSC
#include <usbh_mtp.h>  // MTP
#include <usbh_hub.h>  // HUB


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
    HID_TypeTypeDef    hid_devtype;
    uint64_t           hid_ready_timer;
    appstate_type      appstate;
} usb_t;
static usb_t usb[2][MAX_HUB_PORTS + 1];

USBH_HandleTypeDef usb_handle[2][MAX_HUB_PORTS + 1];

HCD_HandleTypeDef _hHCD[2];  // FS and HS device handles

/*
 * HOST_StateTypeDef gState gstate
 *   0 IDLE                  1 DEV_WAIT_FOR_ATTACHMENT
 *   2 DEV_ATTACHED          3 DEV_DISCONNECTED
 *   4 DETECT_DEVICE_SPEED   5 ENUMERATION
 *   6 CLASS_REQUEST         7 INPUT
 *   8 SET_CONFIGURATION     9 SET_WAKEUP_FEATURE
 *  10 CHECK_CLASS          11 CLASS
 *  12 SUSPENDED            13 ABORT_STATE
 *
 * ENUM_StateTypeDef EnumState estate
 *   0 IDLE                     1 GET_FULL_DEV_DESC
 *   2 SET_ADDR                 3 GET_CFG_DESC
 *   4 GET_FULL_CFG_DESC        5 GET_MFC_STRING_DESC
 *   6 GET_PRODUCT_STRING_DESC  7 GET_SERIALNUM_STRING_DESC
 *
 * CMD_StateTypeDef RequestState rstate
 *   0 IDLE                     1 SEND
 *   2 WAIT                     3 ERROR
 */
static const char * const host_user_types[] = {
    "SELECT_CONF", "ACTIVE", "SELECTED", "CONN", "DISCONN", "ERROR"
};

static uint
get_port(USBH_HandleTypeDef *phost)
{
    uint cur;
    uint port;
    for (cur = 0; cur < MAX_HUB_PORTS + 1; cur++)
        for (port = 0; port < 2; port++)
            if (phost == &usb_handle[port][cur])
                return (port);
    printf("[Can't find]");
    return (0);
}

static void
USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
    uint port = get_port(phost);
    uint devnum = 0;
    printf("USB%u %x %s\n", port, id, (id < ARRAY_SIZE(host_user_types)) ?
           host_user_types[id] : "Unknown");
    switch (id) {
        case HOST_USER_SELECT_CONFIGURATION:
            break;
        case HOST_USER_CLASS_ACTIVE:
            usb[port][devnum].appstate = APPLICATION_READY;
            break;
        case HOST_USER_CLASS_SELECTED:
            break;
        case HOST_USER_CONNECTION:
            usb[port][devnum].appstate = APPLICATION_START;
            break;
        case HOST_USER_DISCONNECTION:
            usb[port][devnum].appstate = APPLICATION_DISCONNECT;
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
    HAL_HCD_IRQHandler(&_hHCD[0]);
}

void
otg_hs_ep1_in_isr(void)
{
    usb_stats.otg_hs_ep1_in_ints++;
    printf("[Ihsepi]");
    HAL_HCD_IRQHandler(&_hHCD[1]);
}

void
otg_hs_ep1_out_isr(void)
{
    usb_stats.otg_hs_ep1_out_ints++;
    printf("[Ihsepo]");
    HAL_HCD_IRQHandler(&_hHCD[1]);
}

void
otg_hs_wkup_isr(void)
{
    usb_stats.otg_hs_wkup_ints++;
    printf("[Ihsw]");
    HAL_HCD_IRQHandler(&_hHCD[1]);
}

void
otg_hs_isr(void)
{
//  printf("Ihs");
    usb_stats.otg_hs_ints++;
    HAL_HCD_IRQHandler(&_hHCD[1]);
}

void usb_ls(void);
void usb_show_stats(void);

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
usb_ls(void)
{
    uint port;
    uint hubport;
    for (port = 0; port < 2; port++) {
        for (hubport = 0; hubport < MAX_HUB_PORTS + 1; hubport++) {
            USBH_HandleTypeDef *phost = &usb_handle[port][hubport];
            printf("%u.%u %s ", port, hubport,
                   phost->valid ? "valid" : "invalid");
            if (phost->valid) {
                if (phost->busy)
                    printf(" busy");
                printf(" gstate=%x estate=%x rstate=%x",
                       phost->gState, phost->EnumState, phost->RequestState);
                printf(" hub=%x addr=%x class=%lx interfaces=%x connected=%x",
                       phost->hub, phost->address, phost->ClassNumber,
                       phost->interfaces, phost->device.is_connected);
            }
            printf("\n");
        }
    }
}

void
USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
    uint port = get_port(phost);
    HID_TypeTypeDef devtype = USBH_HID_GetDeviceType(phost);
// printf("ecb:%x ", devtype);
    if (devtype == HID_MOUSE) {  // Mouse
        HID_MOUSE_Info_TypeDef *Mouse_Info;
        Mouse_Info = USBH_HID_GetMouseInfo(phost);  // Get the info
        int x_val = (int8_t)Mouse_Info->x;  // get the x value
        int y_val = (int8_t)Mouse_Info->y;  // get the y value
#if 0
        printf("X=%d, Y=%d, B1=%d, B2=%d, B3=%d\n",
               x_val, y_val, Mouse_Info->buttons[0],
               Mouse_Info->buttons[1], Mouse_Info->buttons[2]);
#endif
        mouse_action(x_val, y_val, Mouse_Info->buttons[0],
                     Mouse_Info->buttons[1], Mouse_Info->buttons[2]);
    } else if (devtype == HID_KEYBOARD) {  // Keyboard
        HID_KEYBD_Info_TypeDef *kinfo;
        kinfo = USBH_HID_GetKeybdInfo(phost);  // get the info
#if 0
        uint8_t key = USBH_HID_GetASCIICode(kinfo);  // get the key pressed
        printf("Key Pressed = %c %04x\n", key, key);
#endif

        usb_keyboard_report_t kinput;
        kinput.modifier = (kinfo->lctrl  ? KEYBOARD_MODIFIER_LEFTCTRL   : 0) |
                          (kinfo->lshift ? KEYBOARD_MODIFIER_LEFTSHIFT  : 0) |
                          (kinfo->lalt   ? KEYBOARD_MODIFIER_LEFTALT    : 0) |
                          (kinfo->lgui   ? KEYBOARD_MODIFIER_LEFTMETA   : 0) |
                          (kinfo->rctrl  ? KEYBOARD_MODIFIER_RIGHTCTRL  : 0) |
                          (kinfo->rshift ? KEYBOARD_MODIFIER_RIGHTSHIFT : 0) |
                          (kinfo->ralt   ? KEYBOARD_MODIFIER_RIGHTALT   : 0) |
                          (kinfo->rgui   ? KEYBOARD_MODIFIER_RIGHTMETA  : 0);
        kinput.reserved = 0;
        kinput.keycode[0] = kinfo->keys[0];
        kinput.keycode[1] = kinfo->keys[1];
        kinput.keycode[2] = kinfo->keys[2];
        kinput.keycode[3] = kinfo->keys[3];
        kinput.keycode[4] = kinfo->keys[4];
        kinput.keycode[5] = kinfo->keys[5];
        keyboard_usb_input(&kinput);
    } else {
        printf("USB%u Event\n", port);
    }
}

static void
handle_dev(USBH_HandleTypeDef *dev, uint port, uint devnum)
{
    if (usb[port][devnum].appstate == APPLICATION_READY) {
        HID_TypeTypeDef devtype = USBH_HID_GetDeviceType(dev);
// printf("hd:%x ", devtype);
        if (usb[port][devnum].hid_devtype != devtype) {
            usb[port][devnum].hid_devtype = devtype;
#if 1
            printf("USB%u type %x %s\n", devnum, devtype,
                   (devtype == HID_KEYBOARD) ? "Keyboard" :
                   (devtype == HID_MOUSE) ? "Mouse" : "Unknown");
#endif
            usb[port][devnum].hid_ready_timer = timer_tick_plus_msec(500);
        }
        if (usb[port][devnum].hid_ready_timer) {
            if (!timer_tick_has_elapsed(usb[port][devnum].hid_ready_timer))
                return;
            usb[port][devnum].hid_ready_timer = 0;
        }
        if (devtype == HID_KEYBOARD) {
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
// XXX: No need to process here, as it can be handled by even callback
        }
#if 0
        if (devtype == HID_MOUSE) {
            HID_MOUSE_Info_TypeDef *Mouse_Info;
            Mouse_Info = USBH_HID_GetMouseInfo(dev);  // Get the info
            int x_val = (int8_t)Mouse_Info->x;  // get the x value
            int y_val = (int8_t)Mouse_Info->y;  // get the y value
            printf("X=%d, Y=%d, B1=%d, B2=%d, B3=%d\n",
                   x_val, y_val, Mouse_Info->buttons[0],
                   Mouse_Info->buttons[1], Mouse_Info->buttons[2]);
            mouse_action(x_val, y_val, Mouse_Info->buttons[0],
                         Mouse_Info->buttons[1], Mouse_Info->buttons[2]);
        }
#endif
    }
}

/**
  * @brief  USBH_LL_SetupEP0
  *         Setup endpoint with selected device info
  * @param  phost: Host handle
  * @retval Status
  */
HAL_StatusTypeDef USBH_LL_SetupEP0(USBH_HandleTypeDef *phost)
{
        HCD_HandleTypeDef *phHCD =  &_hHCD[phost->id];

__HAL_LOCK(phHCD);

        phHCD->hc[phost->Control.pipe_out].dev_addr   = phost->device.address;
        phHCD->hc[phost->Control.pipe_out].max_packet = phost->Control.pipe_size;
        phHCD->hc[phost->Control.pipe_out].speed      = phost->device.speed;

//phHCD->hc[phost->Control.pipe_out].ch_num     = phost->Control.pipe_out;
//phHCD->hc[phost->Control.pipe_out].toggle_out = phost->Control.toggle_out;
//phHCD->hc[phost->Control.pipe_out].data_pid = phost->Control.data_pid_out;

        phHCD->hc[phost->Control.pipe_in].dev_addr    = phost->device.address;
        phHCD->hc[phost->Control.pipe_in].max_packet  = phost->Control.pipe_size;
        phHCD->hc[phost->Control.pipe_in].speed       = phost->device.speed;

//phHCD->hc[phost->Control.pipe_in].ch_num      = phost->Control.pipe_in;
//phHCD->hc[phost->Control.pipe_in].toggle_in   = phost->Control.toggle_in;
//phHCD->hc[phost->Control.pipe_in].data_pid   = phost->Control.data_pid_in;

        phHCD->pData = phost;
        phost->pData = phHCD;

__HAL_UNLOCK(phHCD);

        return HAL_OK;
}

#define LOG(...) printf(__VA_ARGS__)

static void
hub_process(uint port)
{
    static uint cur[2];
    USBH_HandleTypeDef *phost = &usb_handle[port][cur[port]];

    if (phost != NULL) {
        switch (phost->valid) {
            case 0:  // Not valid
                break;
            case 1:
// printf("p[%u,%u]", port, cur[port]);
                USBH_Process(phost);
                break;
            case 3:
                LOG("PROCESSING ATTACH %d\n", phost->address);
                phost->valid = 1;
                phost->busy  = 1;
                break;
            default:
                printf("Unknown valid %u for port=%u cur=%u\n",
                       phost->valid, port, cur[port]);
                break;
        }

#if 1
        if ((phost->valid == 1) && (phost->busy == 0)) {
            HID_MOUSE_Info_TypeDef *minfo;
            minfo = USBH_HID_GetMouseInfo(phost);
            if (minfo != NULL) {
                LOG("BUTTON %d", minfo->buttons[0]);
            } else {
                HID_KEYBD_Info_TypeDef *kinfo;
                kinfo = USBH_HID_GetKeybdInfo(phost);
                if (kinfo != NULL) {
                    LOG("KEYB %d", kinfo->keys[0]);
                }
            }
        }
#endif
    }

    if (++cur[port] >= MAX_HUB_PORTS + 1)
        cur[port] = 0;

#ifdef NOT
    uint count;
    static uint8_t current_loop = -1;
    static USBH_HandleTypeDef *_phost = 0;

    if (_phost != NULL && _phost->valid == 1) {
        USBH_Process(_phost);

        if (_phost->busy)
            return;
    }

    for (count = 0; count < ARRAY_SIZE(hUSBHost); count++) {
        if (++current_loop > ARRAY_SIZE(hUSBHost))
            current_loop = 0;

        if (hUSBHost[current_loop].valid) {
            _phost = &hUSBHost[current_loop];
            USBH_LL_SetupEP0(_phost);

            if (_phost->valid == 3) {
                LOG("PROCESSING ATTACH %d", _phost->address);
                _phost->valid = 1;
                _phost->busy  = 1;
            }
            break;
        }
    }
    if (count == ARRAY_SIZE(hUSBHost))
        return;

    if (_phost != NULL && _phost->valid) {
        HID_MOUSE_Info_TypeDef *minfo;
        minfo = USBH_HID_GetMouseInfo(_phost);
        if (minfo != NULL) {
            LOG("BUTTON %d", minfo->buttons[0]);
        } else {
            HID_KEYBD_Info_TypeDef *kinfo;
            kinfo = USBH_HID_GetKeybdInfo(_phost);
            if (kinfo != NULL) {
                LOG("KEYB %d", kinfo->keys[0]);
            }
        }
    }
#endif
}

void
cubeusb_poll(void)
{
    /* USB Host Background task */
    for (uint port = 0; port < 2; port++) {
        USBH_Process(&usb_handle[port][0]);
        handle_dev(&usb_handle[port][0], port, 0);
        hub_process(port);
    }
// extern TIM_HandleTypeDef htim1;
// HAL_TIM_IRQHandler(&htim1);
}

#if 0
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
#endif

void
HAL_Delay(uint32_t Delay)
{
    timer_delay_msec(Delay);
}

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
    uint port = (hhcd == &_hHCD[0]) ? 0 : 1;
    printf("HAL_HCD_Connect_Callback %u\n", port);
    USBH_LL_Connect(hhcd->pData);
}
void
HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
    uint port = (hhcd == &_hHCD[0]) ? 0 : 1;
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
    uint port = (hhcd == &_hHCD[0]) ? 0 : 1;
    printf("HAL_HCD_PortEnabled_Callback %u\n", port);
    USBH_LL_PortEnabled(hhcd->pData);
}
void
HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
    uint port = (hhcd == &_hHCD[0]) ? 0 : 1;
    printf("HAL_HCD_PortDisabled_Callback %u\n", port);
    USBH_LL_PortDisabled(hhcd->pData);
}
USBH_StatusTypeDef
USBH_LL_Init(USBH_HandleTypeDef *phost)
{
    /* Init USB_IP */
    if (phost->id == HOST_FS) {
        /* Link the driver to the stack. */
        _hHCD[0].pData = phost;
        phost->pData = &_hHCD[0];

        _hHCD[0].Instance = USB_OTG_FS;
        _hHCD[0].Init.Host_channels = 8;
        _hHCD[0].Init.speed = HCD_SPEED_FULL;
        _hHCD[0].Init.dma_enable = DISABLE;
        _hHCD[0].Init.phy_itface = HCD_PHY_EMBEDDED;
        _hHCD[0].Init.Sof_enable = DISABLE;
        if (HAL_HCD_Init(&_hHCD[0]) != HAL_OK) {
            printf("HAL_HCD_Init error\n");
            return (USBH_FAIL);
        }
        USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&_hHCD[0]));
    }
    if (phost->id == HOST_HS) {
        /* Link the driver to the stack. */
        _hHCD[1].pData = phost;
        phost->pData = &_hHCD[1];

        _hHCD[1].Instance = USB_OTG_HS;
        _hHCD[1].Init.Host_channels = 12;
        _hHCD[1].Init.speed = HCD_SPEED_FULL;
        _hHCD[1].Init.dma_enable = DISABLE;
        _hHCD[1].Init.phy_itface = USB_OTG_EMBEDDED_PHY;
        _hHCD[1].Init.Sof_enable = DISABLE;
        _hHCD[1].Init.low_power_enable = DISABLE;
        _hHCD[1].Init.vbus_sensing_enable = DISABLE;
        _hHCD[1].Init.use_external_vbus = DISABLE;
        if (HAL_HCD_Init(&_hHCD[1]) != HAL_OK) {
            printf("HAL_HCD_Init OTG_HS error\n");
            return (USBH_FAIL);
        }
        USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&_hHCD[1]));
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

/*
 * Use the following call to send data to the HID device. Can be used
 * for setting LEDs, etc. Need to research the data format.
 *    USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, report, len);
 *
 * There is also this call:
 *    USBH_HID_SetReport()
 */

void
cubeusb_init(void)
{
    uint port;
//    HAL_InitTick(TICK_INT_PRIORITY);
//    XXX: Instead just figure out what the HAL tick function is
//    doing and copy that in my own code.
//    Need a 1ms timer base
    printf("cubeusb_init\n");

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    memset(usb_handle, 0, sizeof (usb_handle));

    for (port = 0; port < 2; port++) {
        const char *pname = (port == 0) ? "FS" : "HS";
        uint     host_dev = (port == 0) ? HOST_FS : HOST_HS;
        int rc;
        usb_handle[port][0].valid = 1;
        usb_handle[port][0].address = port;
        usb_handle[port][0].Pipes =
                    malloc(sizeof (uint32_t) * USBH_MAX_PIPES_NBR);

        rc = USBH_Init(&usb_handle[port][0], USBH_UserProcess, host_dev);
        if (rc != USBH_OK) {
            printf("USB %s init fail: %d\n", pname, rc);
            continue;
        }
        if ((USBH_RegisterClass(&usb_handle[port][0], USBH_CDC_CLASS)) ||
            (USBH_RegisterClass(&usb_handle[port][0], USBH_MSC_CLASS)) ||
            (USBH_RegisterClass(&usb_handle[port][0], USBH_HID_CLASS)) ||
            (USBH_RegisterClass(&usb_handle[port][0], USBH_HUB_CLASS)) ||
            (USBH_RegisterClass(&usb_handle[port][0], USBH_MTP_CLASS))) {
            printf("USB %s register fail\n", pname);
            continue;
        }
        if (USBH_Start(&usb_handle[port][0]) != USBH_OK) {
            printf("USB %s start fail\n", pname);
            continue;
        }
    }
}

void
cubeusb_shutdown(void)
{
    for (uint port = 0; port < 2; port++) {
        USBH_DeInit(&usb_handle[port][0]);
    }
}
