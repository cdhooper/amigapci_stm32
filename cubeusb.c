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
#include "config.h"
#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "clock.h"
#include "hiden.h"
#include "keyboard.h"
#include "mouse.h"
#include "cubeusb.h"
#include "usb.h"
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
    APPLICATION_RUNNING,
    APPLICATION_DISCONNECT
} appstate_type;

static struct {
    HID_TypeTypeDef    hid_devtype;
    uint8_t            keyboard_count;
    uint8_t            mouse_count;
    uint64_t           hid_ready_timer;
    appstate_type      appstate;
} usbdev[2][MAX_HUB_PORTS + 1];

static struct {
    uint64_t           recovery_timer;
    uint8_t            recovery_state;
    uint8_t            disabled;
    uint8_t            connected;
    volatile uint      frame;
} usbport[2];

USBH_HandleTypeDef usb_handle[2][MAX_HUB_PORTS + 1];

static void cubeusb_init_port(uint port);
static void cubeusb_shutdown_port(uint port);

/*
 * _hHCD[0] is the Full speed (OTG_FS) port
 * _hHCD[1] is the High speed (OTG_HS) port
 */
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

/*
 * Decode for HOST_USER_* state macros
 *   1 HOST_USER_SELECT_CONFIGURATION
 *   2 HOST_USER_CLASS_ACTIVE
 *   3 HOST_USER_CLASS_SELECTED
 *   4 HOST_USER_CONNECTION
 *   5 HOST_USER_DISCONNECTION
 *   6 HOST_USER_UNRECOVERED_ERROR
 */
static const char * const host_user_types[] = {
    "Unknown", "SELECT_CONF", "ACTIVE", "SELECTED",
    "CONNECT", "DISCONNECT", "ERROR"
};

static uint
get_port(USBH_HandleTypeDef *phost, uint *portdev)
{
    uint cur;
    uint port;
    for (cur = 0; cur < MAX_HUB_PORTS + 1; cur++)
        for (port = 0; port < 2; port++)
            if (phost == &usb_handle[port][cur]) {
                *portdev = cur;
                return (port);
            }
    printf("[Can't find phost %p]", (void *) phost);
    *portdev = 0;
    return (0);
}

static void
USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
    uint devnum;
    uint port = get_port(phost, &devnum);
    dprintf(DF_USB_CONN, "USB%u.%u %x %s\n", port, devnum, id,
           (id < ARRAY_SIZE(host_user_types)) ?
           host_user_types[id] : "Unknown");
    switch (id) {
        case HOST_USER_SELECT_CONFIGURATION:
            break;
        case HOST_USER_CLASS_ACTIVE:
            usbdev[port][devnum].appstate = APPLICATION_READY;
            usbdev[port][devnum].hid_ready_timer = timer_tick_plus_msec(500);
            break;
        case HOST_USER_CLASS_SELECTED:
            break;
        case HOST_USER_CONNECTION:
            usbdev[port][devnum].appstate = APPLICATION_START;
            break;
        case HOST_USER_DISCONNECTION:
            usb_keyboard_count -= usbdev[port][devnum].keyboard_count;
            usb_mouse_count    -= usbdev[port][devnum].mouse_count;

            if (usb_mouse_count == 0)
                hiden_set(0);

            memset(&usbdev[port][devnum], 0, sizeof (usbdev[port][devnum]));
            usbdev[port][devnum].appstate = APPLICATION_DISCONNECT;
//          usbdev[port][devnum].keyboard_count = 0;
//          usbdev[port][devnum].mouse_count = 0;
//          usbdev[port][devnum].hid_devtype = 0;
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

static const char *const usb_class_codes[] =
{
    "IF",       // 0x00
    "Audio",    // 0x01 AC_CLASS
    "CDC",      // 0x02 USB_CDC_CLASS
    "HID",      // 0x03 USB_HID_CLASS
    "Unknown",  // 0x04
    "PID",      // 0x05
    "IMG",      // 0x06 USB_MTP_CLASS
    "Printer",  // 0x07
    "Storage",  // 0x08 USB_MSC_CLASS
    "HUB",      // 0x09
    "CDC-Data", // 0x0a DATA_INTERFACE_CLASS_CODE
    "SmartCrd", // 0x0b
    "Eth",      // 0x0c
    "Security", // 0x0d
    "Video",    // 0x0e
    "PHealth",  // 0x0f
    "AV",       // 0x10
    "Bboard",   // 0x11
    "C-Bridge", // 0x12
    "USB-Bulk", // 0x13
    "MCTP",     // 0x14
};

static void
usb_ls_classes(USBH_HandleTypeDef *phost)
{
    uint ifnum;
    uint numif = phost->device.CfgDesc.bNumInterfaces;
    uint ifclass;
    uint ifsclass;
    uint ifproto;
    char *protostr;

    if (numif > USBH_MAX_NUM_INTERFACES)
        numif = USBH_MAX_NUM_INTERFACES;

    for (ifnum = 0; ifnum < numif; ifnum++) {
        void *data = phost->pClass[phost->ClassNumber]->pData;
        ifclass = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceClass;
        if (ifclass == 0)
            continue;
        ifsclass = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceSubClass;
        ifproto  = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceProtocol;
        protostr = "";
        if (ifclass == USB_HID_CLASS) {
            switch (ifproto) {
                case HID_MOUSE_BOOT_CODE:
                    protostr = "Mouse";
                    break;
                case HID_KEYBRD_BOOT_CODE:
                    protostr = "Keyboard";
                    break;
                default:
                    protostr = "Unknown";
                    break;
            }
        }
        printf("    IF %u: class=%x %s subclass=%02x protocol=%02x %s\n",
               ifnum, ifclass,
               (ifclass < ARRAY_SIZE(usb_class_codes)) ?
                                     usb_class_codes[ifclass] : "Unknown",
               ifsclass, ifproto, protostr);
        if (ifclass == USB_HID_CLASS) {
            HID_HandleTypeDef *HID_Handle = data;
            for (; HID_Handle != NULL; HID_Handle = HID_Handle->next) {
                if (HID_Handle->interface != ifnum)
                    continue;
                printf("          OutEp=%x InEp=%x ep=%x\n",
                       HID_Handle->OutEp,
                       HID_Handle->InEp,
                       HID_Handle->ep_addr);
            }
        }
    }
}

void
usb_ls(uint verbose)
{
    uint port;
    uint hubport;
    for (port = 0; port < 2; port++) {
        for (hubport = 0; hubport < MAX_HUB_PORTS + 1; hubport++) {
            USBH_HandleTypeDef *phost = &usb_handle[port][hubport];
            USBH_ClassTypeDef  *cl;
            if (phost->valid == 0)
                continue;
            cl = phost->pClass[phost->ClassNumber];
            printf("%u.%u %04x.%04x ",
                   port, hubport,
                   phost->device.DevDesc.idProduct,
                   phost->device.DevDesc.idVendor);

            if (phost->device.is_connected == 0) {
                printf("No device\n");
                continue;
            }
            printf("%s: %s", phost->device.manufacturer_string,
                   phost->device.product_string);

            printf("\n          class=%x %s gstate=%x estate=%x ifs=%u ",
                   (cl == NULL) ? 0 : cl->ClassCode,
                   (cl == NULL) ? "?" : cl->Name,
                   phost->gState, phost->EnumState,
                   phost->device.CfgDesc.bNumInterfaces);
            printf("rstate=%x hub=%x hubifs=%x",
                   phost->RequestState,
                   phost->hub,
                   phost->interfaces);
            if (phost->busy)
                printf(" busy");
            printf("\n");
            usb_ls_classes(phost);
        }
    }
}

void
USBH_HID_EventCallback(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
    uint devnum;
    uint port = get_port(phost, &devnum);
    HID_TypeTypeDef devtype = USBH_HID_GetDeviceType(phost, HID_Handle->interface);
    if (devtype == HID_MOUSE) {  // Mouse
        HID_MOUSE_Info_TypeDef *info;
        info = USBH_HID_GetMouseInfo(phost, HID_Handle);  // Get the info
        mouse_action(info->x, info->y, info->wheel, info->buttons);
    } else if (devtype == HID_KEYBOARD) {  // Keyboard
        HID_KEYBD_Info_TypeDef *kinfo;
        kinfo = USBH_HID_GetKeybdInfo(phost, HID_Handle);  // get the info

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
handle_dev(USBH_HandleTypeDef *phost, uint port, uint devnum)
{
    HID_HandleTypeDef *HID_Handle;
    HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;
    uint iface;
    uint numif = phost->device.CfgDesc.bNumInterfaces;
    uint devtype;

    if (usbdev[port][devnum].appstate == APPLICATION_READY) {
        if (!timer_tick_has_elapsed(usbdev[port][devnum].hid_ready_timer))
            return;

        for (iface = 0; iface < numif; iface++) {
            devtype = USBH_HID_GetDeviceType(phost, iface);
            dprintf(DF_USB_CONN, "USB%u.%u.%u type %x %s\n",
                    port, devnum, iface, devtype,
                    (devtype == HID_KEYBOARD) ? "Keyboard" :
                    (devtype == HID_MOUSE) ? "Mouse" : "Unknown");
            if (devtype == HID_KEYBOARD) {
                usbdev[port][devnum].keyboard_count++;
                usb_keyboard_count++;
            } else if (devtype == HID_MOUSE) {
                usbdev[port][devnum].mouse_count++;
                usb_mouse_count++;
            }
        }
        if (usb_mouse_count > 0)
            hiden_set(1);
        usbdev[port][devnum].appstate = APPLICATION_RUNNING;
    }

    if (usbdev[port][devnum].appstate == APPLICATION_RUNNING) {
        for (iface = 0; iface < numif; iface++) {
            devtype = USBH_HID_GetDeviceType(phost, iface);
            if (devtype == HID_KEYBOARD) {
                HID_KEYBD_Info_TypeDef *info;
                info = USBH_HID_GetKeybdInfo(phost, HID_Handle);
                if (info != NULL) {
                    printf("lctrl = %d lshift = %d lalt   = %d\r\n"
                           "lgui  = %d rctrl  = %d rshift = %d\r\n"
                           "ralt  = %d rgui   = %d\r\n",
                           info->lctrl, info->lshift, info->lalt,
                           info->lgui, info->rctrl, info->rshift,
                           info->ralt, info->rgui);
                    // info->keys[]
                }
// XXX: No need to process here, as it can be handled by event callback
            }
        }
    }
}

/**
  * @brief  host_switch_to_dev
  *         Setup endpoint with selected device info
  * @param  phost: Host handle
  * @retval Status
  */
static HAL_StatusTypeDef
host_switch_to_dev(USBH_HandleTypeDef *phost)
{
    HCD_HandleTypeDef *hhcd = &_hHCD[phost->id];
    uint pipe_out = phost->Control.pipe_out;
    uint pipe_in  = phost->Control.pipe_in;

// printf("USB%u host_switch_to_dev id=%u\n", phost->address, phost->id);
    __HAL_LOCK(hhcd);

    hhcd->hc[pipe_out].dev_addr   = phost->device.address;
    hhcd->hc[pipe_out].max_packet = phost->Control.pipe_size;
    hhcd->hc[pipe_out].speed      = phost->device.speed;

//  hhcd->hc[pipe_out].ch_num     = phost->Control.pipe_out;
//  hhcd->hc[pipe_out].toggle_out = phost->Control.toggle_out;
//  hhcd->hc[pipe_out].data_pid   = phost->Control.data_pid_out;

    hhcd->hc[pipe_in].dev_addr    = phost->device.address;
    hhcd->hc[pipe_in].max_packet  = phost->Control.pipe_size;
    hhcd->hc[pipe_in].speed       = phost->device.speed;

//  hhcd->hc[pipe_in].ch_num      = phost->Control.pipe_in;
//  hhcd->hc[pipe_in].toggle_in   = phost->Control.toggle_in;
//  hhcd->hc[pipe_in].data_pid    = phost->Control.data_pid_in;

    hhcd->pData = phost;
    phost->pData = hhcd;

    __HAL_UNLOCK(hhcd);

    return (HAL_OK);
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
                host_switch_to_dev(phost);
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

#if 0
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
            host_switch_to_dev(_phost);

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

#if 0
static void
wait_sof(uint port)
{
    uint frame = usbport[port].frame;
    uint64_t timeout = timer_tick_plus_msec(1000);
    while (frame == usbport[port].frame) {
        if (timer_tick_has_elapsed(timeout)) {
            printf("USB%u SOF timeout\n", port);
            break;
        }
    }
}
#endif

void
cubeusb_poll(void)
{
    /* USB Host Background task */
    for (uint port = 0; port < 2; port++) {
        if ((usbport[port].recovery_state != 0) &&
            timer_tick_has_elapsed(usbport[port].recovery_timer)) {
            const uint usb_retry_fast_limit = 10;
            const uint usb_retry_slow_limit = 20;
            if (usbport[port].connected == 0) {
                /* Port is no longer connected (device pull?) */
                printf("USB is not connected\n");
                USBH_LL_Disconnect(_hHCD[port].pData);
                usbport[port].recovery_state = 0;
                break;
            }
            if (usbport[port].disabled == 0) {
                /* Port has recovered */
                printf("Recovered USB%u in %u tries\n",
                       port, usbport[port].recovery_state - 1);
                usbport[port].recovery_state = 0;
                break;
            }
            if (usbport[port].recovery_state++ < usb_retry_slow_limit) {
                uint msec;
                printf("\n\nUSB%u port disabled\n", port);
                usbport[port].disabled = 0;
#if 0
                cubeusb_shutdown();
                cubeusb_init();
#else
                cubeusb_shutdown_port(port);

#if 0
                if (port == 0) {
                    __HAL_RCC_USB_OTG_FS_FORCE_RESET();
                    __HAL_RCC_USB_OTG_FS_RELEASE_RESET();
                } else {
                    __HAL_RCC_USB_OTG_HS_FORCE_RESET();
                    __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
                }
#endif
//              wait_sof(port);

#if 0
                /* Disable USB core clock */
                if (port == 0)
                    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
                else
                    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
                timer_delay_usec(32 * usbport[port].recovery_state);

                /* Enable USB core clock */
                if (port == 0)
                    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
                else
                    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
                timer_delay_usec(32 * usbport[port].recovery_state);
#endif

                cubeusb_init_port(port);
#endif
                if (usbport[port].recovery_state++ < usb_retry_fast_limit) {
                    msec = 512 * usbport[port].recovery_state;
                } else {
                    msec = 10000 + 1024 * usbport[port].recovery_state;
                }
                usbport[port].recovery_timer = timer_tick_plus_msec(msec);
            } else {
                /* Give up */
                printf("USB%u port recovery failed after %u attempts\n",
                       port, usbport[port].recovery_state);
                usbport[port].recovery_state = 0;
            }
        }
        USBH_Process(&usb_handle[port][0]);
        handle_dev(&usb_handle[port][0], port, 0);
        hub_process(port);
    }
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
HAL_HCD_MspInit(HCD_HandleTypeDef *hhcd)
{
//  printf("HAL_HCD_MspInit\n");
}

void
HAL_HCD_MspDeInit(HCD_HandleTypeDef *hhcd)
{
    printf("HAL_HCD_MspDeInit\n");
}
void
HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hhcd)
{
    int port = (hhcd == &_hHCD[0]) ? 0 : 1;
    usbport[port].frame++;
    USBH_LL_IncTimer(hhcd->pData);
}

void
HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
    int port = (hhcd == &_hHCD[0]) ? 0 : (hhcd == &_hHCD[1]) ? 1 : -1;
    dprintf(DF_USB_CONN, "USB%d HAL_HCD_Connect\n", port);
    usbport[port].connected = 1;
    USBH_LL_Connect(hhcd->pData);
}

void
HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
    int port = (hhcd == &_hHCD[0]) ? 0 : (hhcd == &_hHCD[1]) ? 1 : -1;
    dprintf(DF_USB_CONN, "USB%d HAL_HCD_Disconnect\n", port);
    usbport[port].connected = 0;
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
    int port = (hhcd == &_hHCD[0]) ? 0 : (hhcd == &_hHCD[1]) ? 1 : -1;
    dprintf(DF_USB_CONN, "USB%d HAL_HCD_PortEnabled\n", port);
    USBH_LL_PortEnabled(hhcd->pData);
}

void
HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
    int port = (hhcd == &_hHCD[0]) ? 0 : (hhcd == &_hHCD[1]) ? 1 : -1;
    dprintf(DF_USB_CONN, "USB%d HAL_HCD_PortDisabled\n", port);
    USBH_LL_PortDisabled(hhcd->pData);
    if (usbport[port].connected) {
        usbport[port].disabled = 1;  // Mark it as (unexpected) disabled

        if (usbport[port].recovery_state == 0) {
            usbport[port].recovery_state = 1;  // Kickstart recovery
            usbport[port].recovery_timer = timer_tick_plus_msec(10);
        }
    }
}

USBH_StatusTypeDef
USBH_LL_Init(USBH_HandleTypeDef *phost)
{
    /* Init USB_IP */
    if (phost->id == HOST_FS) {
        /* Link the OTG_FS driver to the stack */
        _hHCD[0].pData = phost;
        _hHCD[0].Instance = USB_OTG_FS;
        _hHCD[0].Init.Host_channels = 8;
        _hHCD[0].Init.speed = HCD_SPEED_FULL;
        _hHCD[0].Init.dma_enable = DISABLE;
        _hHCD[0].Init.phy_itface = HCD_PHY_EMBEDDED;
        _hHCD[0].Init.Sof_enable = DISABLE;
        phost->pData = &_hHCD[0];

    } else if (phost->id == HOST_HS) {
        /* Link the OTG_HS driver to the stack */
        _hHCD[1].pData = phost;
        _hHCD[1].Instance = USB_OTG_HS;
        _hHCD[1].Init.Host_channels = 12;
        _hHCD[1].Init.speed = HCD_SPEED_FULL;
        _hHCD[1].Init.dma_enable = DISABLE;
        _hHCD[1].Init.phy_itface = USB_OTG_EMBEDDED_PHY;
        _hHCD[1].Init.Sof_enable = DISABLE;
        _hHCD[1].Init.low_power_enable = DISABLE;
        _hHCD[1].Init.vbus_sensing_enable = DISABLE;
        _hHCD[1].Init.use_external_vbus = DISABLE;
        phost->pData = &_hHCD[1];

    } else {
        return (USBH_FAIL);
    }
    if (HAL_HCD_Init(phost->pData) != HAL_OK) {
        printf("HAL_HCD_Init OTG_HS error\n");
        return (USBH_FAIL);
    }
    USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(phost->pData));
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
    HCD_HandleTypeDef *hhcd = phost->pData;

    if (hhcd->hc[pipe].ep_is_in) {
        hhcd->hc[pipe].toggle_in = toggle;
    } else {
        hhcd->hc[pipe].toggle_out = toggle;
    }
    return (USBH_OK);
}

uint8_t
USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    uint8_t toggle = 0;
    HCD_HandleTypeDef *hhcd = phost->pData;

    if (hhcd->hc[pipe].ep_is_in) {
        toggle = hhcd->hc[pipe].toggle_in;
    } else {
        toggle = hhcd->hc[pipe].toggle_out;
    }
    return (toggle);
}

USBH_StatusTypeDef USBH_Get_USB_Status(HAL_StatusTypeDef hal_status);
USBH_StatusTypeDef
USBH_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
    USBH_StatusTypeDef usb_status = USBH_OK;

    switch (hal_status) {
        case HAL_OK:
            usb_status = USBH_OK;
            break;
        case HAL_ERROR:
            usb_status = USBH_FAIL;
            break;
        case HAL_BUSY:
            usb_status = USBH_BUSY;
            break;
        case HAL_TIMEOUT:
            usb_status = USBH_FAIL;
            break;
        default:
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
    HAL_StatusTypeDef hal_status;
    USBH_StatusTypeDef usb_status;

    hal_status = HAL_HCD_HC_Init(phost->pData, pipe_num, epnum,
                                 dev_address, speed, ep_type, mps);

    usb_status = USBH_Get_USB_Status(hal_status);

    return (usb_status);
}

USBH_StatusTypeDef
USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    HAL_StatusTypeDef hal_status;
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
        case 0:
            speed = USBH_SPEED_HIGH;
            break;
        case 1:
            speed = USBH_SPEED_FULL;
            break;
        case 2:
            speed = USBH_SPEED_LOW;
            break;
        default:
            speed = USBH_SPEED_FULL;
            break;
    }
    return (speed);
}

void *
USBH_malloc(size_t size)
{
    void *ptr = malloc(size);
#undef MALLOC_DEBUG
#ifdef MALLOC_DEBUG
    printf("+ MALLOC(%x %x)\n", size, (uintptr_t) ptr);
#endif
    return (ptr);
}

void
USBH_free(void *ptr)
{
#ifdef MALLOC_DEBUG
    size_t size = 0;
    if (ptr != NULL) {
        size = ((uint32_t *)ptr)[-1];
        size = (size & ~1) - 8;
    }
    printf("- FREE(%x %x)\n", size, (uintptr_t) ptr);
#endif
    free(ptr);
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

static USBH_StatusTypeDef
register_class(USBH_HandleTypeDef *handle, USBH_ClassTypeDef *class, uint size)
{
    USBH_ClassTypeDef *nclass = USBH_malloc(size);
    memcpy(nclass, class, size);
    return (USBH_RegisterClass(handle, nclass));
}

void
cubeusb_init_port(uint port)
{
    memset(&usb_handle[port], 0, sizeof (usb_handle[port]));

    const char *pname = (port == 0) ? "FS" : "HS";
    uint     host_dev = (port == 0) ? HOST_FS : HOST_HS;
    int rc;
    USBH_HandleTypeDef *handle = &usb_handle[port][0];
    handle->valid = 1;
    handle->address = port;  // XXX: Same as id??
    handle->Pipes = USBH_malloc(sizeof (uint32_t) * USBH_MAX_PIPES_NBR);

    rc = USBH_Init(handle, USBH_UserProcess, host_dev);
    if (rc != USBH_OK) {
        printf("USB%u %s init fail: %d\n", port, pname, rc);
        return;
    }
    if (register_class(handle, USBH_CDC_CLASS, sizeof (*USBH_CDC_CLASS)) ||
        register_class(handle, USBH_MSC_CLASS, sizeof (*USBH_MSC_CLASS)) ||
        register_class(handle, USBH_HID_CLASS, sizeof (*USBH_HID_CLASS)) ||
        register_class(handle, USBH_HUB_CLASS, sizeof (*USBH_HUB_CLASS)) ||
        register_class(handle, USBH_MTP_CLASS, sizeof (*USBH_MTP_CLASS))) {
        printf("USB%u %s register fail\n", port, pname);
        return;
    }
    if (USBH_Start(&usb_handle[port][0]) != USBH_OK) {
        printf("USB%u %s start fail\n", port, pname);
        return;
    }
}

void
cubeusb_init(void)
{
    uint port;

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    memset(usb_handle, 0, sizeof (usb_handle));

    for (port = 0; port < 2; port++)
        cubeusb_init_port(port);
}

static void
cubeusb_shutdown_port(uint port)
{
    USBH_HandleTypeDef *handle = &usb_handle[port][0];
    if (handle->Pipes == NULL)
        return;
    USBH_DeInit(handle);

// XXX: if this code is called from the user input handler (interrupt), then the USB interrupt can't run until the command returns.
    while (handle->ClassNumber-- > 0)
        USBH_free(handle->pClass[handle->ClassNumber]);
    USBH_free(handle->Pipes);
    handle->Pipes = NULL;
    handle->valid = 0;
}

void
cubeusb_shutdown(void)
{
    for (uint port = 0; port < 2; port++)
        cubeusb_shutdown_port(port);

// XXX: need to deinit hub ports?
}
