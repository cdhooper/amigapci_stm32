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
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "cubeusb.h"
#include "power.h"
#include "usb.h"
#include <usbh_cdc.h>  // CDC
#include <usbh_hid.h>  // HID
#include <usbh_msc.h>  // MSC
#include <usbh_mtp.h>  // MTP
#include <usbh_hub.h>  // HUB
#include <usbh_xusb.h>  // XUSB
#include <usbh_hid_usage.h>  // HID_USAGE_*

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

static const char * const host_appstate[] = {
    "IDLE", "START", "READY", "RUNNING",
    "DISCONNECT",
};

static struct {
    uint8_t            keyboard_count;
    uint8_t            mouse_count;
    uint8_t            joystick_count;
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
static uint hp_cur[2];  // Next handle to be processed by process_usb_ports()

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
 *   8 DONE
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

/*
 * HOST_CLASS is the normal idle gstate. It runs background processing
 * for the whatever class (HID, HUB, etc) of the device.
 */
static const char * const host_gstate_types[] = {
    "HOST_IDLE", "HOST_DEV_WAIT_FOR_ATTACHMENT",
        "HOST_DEV_ATTACHED", "HOST_DEV_DISCONNECTED",
    "HOST_DETECT_DEVICE_SPEED", "HOST_ENUMERATION",
        "HOST_CLASS_REQUEST", "HOST_INPUT",
    "HOST_SET_CONFIGURATION", "HOST_SET_WAKEUP_FEATURE",
        "HOST_CHECK_CLASS", "HOST_CLASS",
    "HOST_SUSPENDED", "HOST_ABORT_STATE"
};

/*
 * This state machine runs while gState is HOST_ENUMERATION.
 *
 * phost->gState = HOST_ENUMERATION
 * phost->EnumState
 */
static const char * const host_enumstate_types[] = {
    "ENUM_IDLE", "ENUM_GET_FULL_DEV_DESC",
        "ENUM_SET_ADDR", "ENUM_GET_CFG_DESC",
    "ENUM_GET_FULL_CFG_DESC", "ENUM_GET_MFC_STRING_DESC",
        "ENUM_GET_PRODUCT_STRING_DESC", "ENUM_GET_SERIALNUM_STRING_DESC",
        "ENUM_DONE",
};

/*
 * CMD_SEND is the actual "idle" state. It means that the state
 * machine is ready to send.
 * CMD_WAIT means that a command is in progress, and that the
 *      control state machine is active
 */
static const char * const host_requeststate_types[] = {
    "CMD_IDLE", "CMD_SEND", "CMD_WAIT", "CMD_ERROR"
};

static const char * const host_controlstate_types[] = {
    "CTRL_IDLE", "CTRL_SETUP",
        "CTRL_SETUP_WAIT", "CTRL_DATA_IN",
    "CTRL_DATA_IN_WAIT", "CTRL_DATA_OUT",
        "CTRL_DATA_OUT_WAIT", "CTRL_STATUS_IN",
    "CTRL_STATUS_IN_WAIT", "CTRL_STATUS_OUT",
        "CTRL_STATUS_OUT_WAIT", "CTRL_ERROR",
    "CTRL_STALLED", "CTRL_COMPLETE",
};

static const char * const hid_state_types[] = {
    "HID_INIT", "HID_VENDOR", "HID_IDLE", "HID_SEND_DATA",
    "HID_BUSY", "HID_GET_DATA", "HID_SYNC", "HID_POLL",
    "HID_ERROR", "HID_NO_SUPPORT"
};

static const char * const hid_ctl_state_types[] = {
    "HID_REQ_INIT", "HID_REQ_IDLE",
    "HID_REQ_GET_REPORT_DESC", "HID_REQ_GET_HID_DESC",
    "HID_REQ_SET_IDLE", "HID_REQ_SET_PROTOCOL", "HID_REQ_SET_REPORT"
};

static const char * const hub_state_types[] = {
    "HUB_IDLE", "HUB_SYNC",
        "HUB_BUSY", "HUB_GET_DATA",
    "HUB_POLL", "HUB_LOOP_PORT_CHANGED",
        "HUB_LOOP_PORT_ENUM", "HUB_LOOP_PORT_WAIT",
    "HUB_PORT_CHANGED", "HUB_C_PORT_CONNECTION",
        "HUB_C_PORT_RESET", "HUB_RESET_DEVICE",
    "HUB_DEV_ATTACHED", "HUB_DEV_DETACHED",
        "HUB_C_PORT_OVER_CURRENT", "HUB_C_PORT_SUSPEND",
    "HUB_ERROR",
};

static const char * const hub_ctl_state_types[] = {
    "HUB_REQ_IDLE", "HUB_REQ_GET_DESCRIPTOR",
        "HUB_REQ_SET_POWER", "HUB_WAIT_PWRGOOD",
    "HUB_REQ_DONE",
};

static const char * const xusb_state_types[] = {
    "XUSB_INIT", "XUSB_FEATURE_REQUEST", "XUSB_IDLE", "XUSB_SEND_DATA",
    "XUSB_BUSY", "XUSB_GET_DATA", "XUSB_SYNC", "XUSB_POLL",
    "XUSB_ERROR" "XUSB_NO_SUPPORT",
};

uint
get_port(USBH_HandleTypeDef *phost)
{
    uint cur;
    uint port;
    for (cur = 0; cur < MAX_HUB_PORTS + 1; cur++)
        for (port = 0; port < 2; port++)
            if (phost == &usb_handle[port][cur])
                return (port);
    printf("[Can't find phost %p]", (void *) phost);
    return (0);
}

uint
get_portdev(USBH_HandleTypeDef *phost, uint *portdev)
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
    uint port = get_portdev(phost, &devnum);
    dprintf(DF_USB_CONN, "USB%u.%u %x %s\n", port, devnum, id,
           (id < ARRAY_SIZE(host_user_types)) ?
           host_user_types[id] : "Unknown");
    switch (id) {
        case HOST_USER_SELECT_CONFIGURATION:
            /*
             * Can be used to select one of multiple device configurations,
             * if the device implements multiple. An example is a composite
             * device supporting either HID or CDC. The host can pick one
             * of the two by setting here:
             *     phost->device.CfgDesc.bConfigurationValue
             */
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
            usb_joystick_count -= usbdev[port][devnum].joystick_count;

            memset(&usbdev[port][devnum], 0, sizeof (usbdev[port][devnum]));
            usbdev[port][devnum].appstate = APPLICATION_DISCONNECT;
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

/*
 * USB Class Codes
 *
 * These codes define both the bDeviceClass field in the Device Descriptor
 * and the bInterfaceClass field in the Interface Descriptor.
 */
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
                // 0xdc Diagnostic
                // 0xfe Application-specific
                // 0xff Vendor-specific
};

static const char *
get_devclass_str(uint8_t devclass)
{
    if (devclass < ARRAY_SIZE(usb_class_codes))
        return (usb_class_codes[devclass]);
    else if (devclass == 0xfe)
        return ("App");
    else if (devclass == 0xff)
        return ("Vendor");
    else
        return ("Unknown");
}

static const char *const usb_hid_usage_codes[] =
{
    "Undefined",      // 0x00
    "Pointer",        // 0x01
    "Mouse",          // 0x02
    "Reserved",       // 0x03
    "Joystick",       // 0x04
    "Gamepad",        // 0x05
    "Keyboard",       // 0x06
    "Keypad",         // 0x07
    "M-X Controller", // 0x08
};

static const char *
get_hid_usage_str(uint8_t usage)
{
    if (usage < ARRAY_SIZE(usb_hid_usage_codes))
        return (usb_hid_usage_codes[usage]);
    switch (usage) {
        case HID_USAGE_SYSCTL:
            return ("Sysctl");
        default:
            return ("Unknown");
    }
}

static void
usb_ls_classes(USBH_HandleTypeDef *phost, uint verbose)
{
    uint ifnum;
    uint numif = phost->device.CfgDesc.bNumInterfaces;
    uint ifclass;
    uint ifsclass;
    uint ifproto;
    char *protostr;
    char *ifsclass_str;

    if (numif > USBH_MAX_NUM_INTERFACES)
        numif = USBH_MAX_NUM_INTERFACES;

    for (ifnum = 0; ifnum < numif; ifnum++) {
        void *data = NULL;
        uint classnum;

        ifclass = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceClass;
        if (ifclass == 0)
            continue;
        ifsclass = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceSubClass;
        ifproto  = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceProtocol;
        protostr = "";
        ifsclass_str = "";

        for (classnum = 0; classnum < phost->ClassNumber; classnum++) {
            if (ifclass == phost->pClass[classnum]->ClassCode) {
                data = phost->pClass[classnum]->pData;
                break;
            }
        }
        switch (ifclass) {
            case USB_HID_CLASS:
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
                switch (ifsclass) {
                    case 0:
                        ifsclass_str = "None";
                        break;
                    case 1:
                        ifsclass_str = "Boot Interface";
                        break;
                }
                break;
        }
        printf("  %c IF %u: class=%x %s subclass=%02x %s protocol=%02x %s\n",
               (phost->iface_waiting & BIT(ifnum)) ? '>' : ' ',
               ifnum, ifclass, get_devclass_str(ifclass),
               ifsclass, ifsclass_str,
               ifproto, protostr);
        if (verbose) {
            switch (ifclass) {
                case USB_HID_CLASS: {
                    HID_HandleTypeDef *HID_Handle = data;
                    for (; HID_Handle != NULL; HID_Handle = HID_Handle->next) {
                        if (HID_Handle->interface != ifnum)
                            continue;

                        if (HID_Handle == NULL) {
                            printf("          NULL HID Handle\n");
                            continue;
                        }

                        printf("          OutEp=%x InEp=%x state=%s "
                               "ctl_state=%x %s\n",
                               HID_Handle->OutEp, HID_Handle->InEp,
                               (HID_Handle->state <
                                ARRAY_SIZE(hid_state_types)) ?
                               hid_state_types[HID_Handle->state] : "Unknown",
                               HID_Handle->ctl_state,
                               (HID_Handle->ctl_state <
                                ARRAY_SIZE(hid_ctl_state_types)) ?
                               hid_ctl_state_types[HID_Handle->ctl_state] :
                               "Unknown");
                        printf("          timer=%lx dataready=%x idmouse=%x "
                               "idconsumer=%x idsysctl=%x\n",
                               HID_Handle->timer, HID_Handle->DataReady,
                               HID_Handle->HID_RDesc.id_mouse,
                               HID_Handle->HID_RDesc.id_consumer,
                               HID_Handle->HID_RDesc.id_sysctl);
                        printf("          usage=%s\n",
                               get_hid_usage_str(HID_Handle->HID_RDesc.usage));
                    }
                    break;
                }
                case USB_XUSB_CLASS: {
                    XUSB_Handle_t *XUSB_Handle = data;
                    for (; XUSB_Handle != NULL; XUSB_Handle = XUSB_Handle->next) {
                        if ((XUSB_Handle->interface != ifnum) ||
                            (XUSB_Handle->interface != ifnum))
                            continue;

                        if (XUSB_Handle == NULL) {
                            printf("          NULL XUSB Handle\n");
                            continue;
                        }
                        printf("          OutEp=%x InEp=%x state=%s \n",
                               XUSB_Handle->OutEp, XUSB_Handle->InEp,
                               (XUSB_Handle->state <
                                ARRAY_SIZE(xusb_state_types)) ?
                               xusb_state_types[XUSB_Handle->state] :
                               "Unknown");
                        printf("          timer=%lx dataready=%x\n",
                               XUSB_Handle->timer, XUSB_Handle->DataReady);
                    }
                    break;
                }
                case USB_HUB_CLASS: {
                    extern __IO USB_PORT_CHANGE HUB_Change[2];
                    extern __IO uint8_t         HUB_CurPort[2];
                    extern uint8_t              HUB_NumPorts[2];
                    extern uint16_t             HUB_PwrGood[2];
                    HUB_HandleTypeDef *HUB_Handle;
                    uint port = get_port(phost);
                    HUB_Handle = phost->USBH_ClassTypeDef_pData[0];

                    printf("          Change=%02x CurPort=%x NumPort=%x "
                           "PwrGood=%04x\n",
                           HUB_Change[port].val, HUB_CurPort[port],
                           HUB_NumPorts[port], HUB_PwrGood[port]);
                    if (HUB_Handle == NULL) {
                        printf("          NULL HUB Handle\n");
                        break;
                    }
                    printf("          InEp=%x state=%s "
                           "ctl_state=%s\n",
                           HUB_Handle->InEp,
                           (HUB_Handle->state <
                            ARRAY_SIZE(hub_state_types)) ?
                           hub_state_types[HUB_Handle->state] : "Unknown",
                           (HUB_Handle->ctl_state <
                            ARRAY_SIZE(hub_ctl_state_types)) ?
                           hub_ctl_state_types[HUB_Handle->ctl_state] :
                           "Unknown");
                    printf("          timer=%lx DataReady=%x poll=%x "
                           "length=%x\n",
                           HUB_Handle->timer, HUB_Handle->DataReady,
                           HUB_Handle->poll, HUB_Handle->length);
                    break;
                }
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
            uint8_t devclass = phost->device.DevDesc.bDeviceClass;
            uint    classnum;
            if (phost->valid == 0)
                continue;
            printf("%u.%u %04x:%04x ",
                   port, hubport,
                   phost->device.DevDesc.idVendor,
                   phost->device.DevDesc.idProduct);

            if (phost->device.is_connected == 0) {
                printf("No device\n");
                continue;
            }

            printf("addr=%x %s: %s",
                   phost->device.address,
                   phost->device.manufacturer_string,
                   phost->device.product_string);
            if (hp_cur[port] == hubport)
                printf(" <<");

            for (classnum = 0; classnum < phost->ClassNumber; classnum++) {
                if (devclass == phost->pClass[classnum]->ClassCode)
                    break;
            }
            if (verbose) {
                printf("\n          class=%x %s ",
                       devclass, get_devclass_str(devclass));
                printf(" gstate=%s ",
                       (phost->gState < ARRAY_SIZE(host_gstate_types)) ?
                       host_gstate_types[phost->gState] : "Unknown");
                printf(" rstate=%s",
                       (phost->RequestState <
                        ARRAY_SIZE(host_requeststate_types)) ?
                       host_requeststate_types[phost->RequestState] :
                       "Unknown");
                printf("\n          estate=%s ",
                       (phost->EnumState < ARRAY_SIZE(host_enumstate_types)) ?
                       host_enumstate_types[phost->EnumState] : "Unknown");
                printf(" polls=%lu  usec %llu max %llu",
                       phost->poll_count, timer_tick_to_usec(phost->tick_total),
                       timer_tick_to_usec(phost->tick_max));
                printf("\n          ctlstate=%s ",
                       (phost->Control.state <
                        ARRAY_SIZE(host_controlstate_types)) ?
                       host_controlstate_types[phost->Control.state] :
                       "Unknown");
                printf(" ifs=%u hub=%x hubifs=%x",
                       phost->device.CfgDesc.bNumInterfaces,
                       phost->hub, phost->interfaces);
                printf(" out=%u in=%u",
                       phost->Control.pipe_out, phost->Control.pipe_in);
                printf("\n          lastreq=%02x val=%02x ind=%02x len=%02x",
                       phost->Control.setup.b.bRequest,
                       phost->Control.setup.b.wValue.w,
                       phost->Control.setup.b.wIndex.w,
                       phost->Control.setup.b.wLength.w);
                printf(" app=%s",
                       host_appstate[usbdev[port][hubport].appstate]);
                if (phost->busy) {
                    printf("  BUSY");
                    if (phost->busy > 1)
                        printf(" %x", phost->busy);
                }
            }
            printf("\n");
            usb_ls_classes(phost, verbose);
        }
    }
}

void
USBH_XUSB_EventCallback(USBH_HandleTypeDef *phost, XUSB_Handle_t *XUSB_Handle)
{
    static uint32_t last[4];
    static uint8_t last_was_joypad;
    static uint32_t buttons;
    static uint8_t joypad;
    static int16_t mouse_x;
    static int16_t mouse_y;
    static int16_t wheel_x;
    static int16_t wheel_y;
    static uint64_t throttle_timer;
    XUSB_MISC_Info_t info;
    uint32_t *data = (uint32_t *)XUSB_Handle->pData;
    uint pos;

    if (memcmp(last, data, sizeof (last)) == 0) {
        if (last_was_joypad) {
            if (joypad | buttons) {
                joystick_action(joypad & BIT(0), joypad & BIT(1),
                                joypad & BIT(2), joypad & BIT(3), buttons);
            }
        } else {
            /* Replay mouse */
            int16_t twheel_x;
            int16_t twheel_y;
            if (timer_tick_has_elapsed(throttle_timer)) {
                if ((wheel_x > 1) || (wheel_x < -1) ||
                    (wheel_y > 1) || (wheel_y < -1)) {
                    throttle_timer = timer_tick_plus_msec(50);
                } else {
                    throttle_timer = timer_tick_plus_msec(200);
                }
                twheel_x = wheel_x;
                twheel_y = wheel_y;
            } else {
                twheel_x = 0;
                twheel_y = 0;
            }
            if (mouse_x | mouse_y | wheel_x | wheel_y | buttons) {
                mouse_action(mouse_x, mouse_y, twheel_y, twheel_x, buttons);
            }
        }
        return;
    }

    if (config.debug_flag & DF_USB_DECODE_MISC) {
        for (pos = 0; pos < ARRAY_SIZE(last); pos++) {
            last[pos] = data[pos];
            printf(" %08lx", data[pos]);
        }
        printf(" ");
    }

    USBH_XUSB_DecodeReport(phost, XUSB_Handle, &info);
    if (info.joypad ||
        (last_was_joypad &&
         (info.mouse_x == 0) && (info.mouse_y == 0) &&
         (info.wheel_x == 0) && (info.wheel_y == 0))) {
        joypad = info.joypad;
        buttons = info.buttons;
        joystick_action(joypad & BIT(0), joypad & BIT(1),
                        joypad & BIT(2), joypad & BIT(3), buttons);
        last_was_joypad = 1;
    } else {
        int16_t twheel_x;
        int16_t twheel_y;
        mouse_x = info.mouse_x;
        mouse_y = info.mouse_y;
        wheel_x = info.wheel_x;
        wheel_y = info.wheel_y;
        buttons = info.buttons;
        if (timer_tick_has_elapsed(throttle_timer)) {
            if ((wheel_x > 1) || (wheel_x < -1) ||
                (wheel_y > 1) || (wheel_y < -1)) {
                throttle_timer = timer_tick_plus_msec(50);
            } else {
                throttle_timer = timer_tick_plus_msec(200);
            }
            twheel_x = wheel_x;
            twheel_y = wheel_y;
        } else {
            twheel_x = 0;
            twheel_y = 0;
        }
        dprintf(DF_USB_DECODE_MISC, "%d %d %d %d ",
                mouse_x, mouse_y, twheel_x, twheel_y);
        mouse_action(mouse_x, mouse_y, twheel_y, twheel_x, buttons);
        last_was_joypad = 0;
    }
    dprintf(DF_USB_DECODE_MISC, "\n");
}

void
USBH_HID_EventCallback(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
    uint devnum;
    uint port = get_portdev(phost, &devnum);
    HID_TypeTypeDef devtype = USBH_HID_GetDeviceType(phost, HID_Handle->interface);
    if ((devtype == HID_MOUSE) || (devtype == HID_UNKNOWN)) {
        /* Mouse or Generic HID (refer to the HID report descriptor) */
        HID_MISC_Info_TypeDef info;
        if (USBH_HID_DecodeReport(phost, HID_Handle, devtype, &info) != USBH_OK)
            return;
        if ((info.usage == HID_USAGE_GAMEPAD) ||
            (info.usage == HID_USAGE_JOYSTICK)) {
            uint8_t up    = info.jpad & BIT(0);
            uint8_t down  = info.jpad & BIT(1);
            uint8_t left  = info.jpad & BIT(2);
            uint8_t right = info.jpad & BIT(3);
            if (config.flags & CF_GAMEPAD_MOUSE) {
                mouse_action(info.x, info.y, -info.wheel, info.ac_pan,
                             info.buttons);
            } else {
                up |= (info.y < 0);
                down |= info.y > 0;
                left |= info.x < 0;
                right |= info.x > 0;
            }
            joystick_action(up, down, left, right, info.buttons);
        } else {
            mouse_action(info.x, info.y, -info.wheel, info.ac_pan,
                         info.buttons);
            keyboard_usb_input_mm(info.mm_key, ARRAY_SIZE(info.mm_key));
            power_sysctl(info.sysctl);
        }
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

static int
is_usb_joystick(USBH_HandleTypeDef *phost, uint ifnum)
{
    uint8_t ifclass = phost->device.CfgDesc.Itf_Desc[ifnum].bInterfaceClass;
    uint classnum;
    void *data = NULL;
    for (classnum = 0; classnum < phost->ClassNumber; classnum++) {
        if (ifclass == phost->pClass[classnum]->ClassCode) {
            data = phost->pClass[classnum]->pData;
            break;
        }
    }

    if (ifclass == USB_HID_CLASS) {
        if (data != NULL) {
            HID_HandleTypeDef *HID_Handle = data;
            switch (HID_Handle->HID_RDesc.usage) {
                case HID_USAGE_GAMEPAD:
                case HID_USAGE_JOYSTICK:
                    return (1);
            }
        }
    } else if (ifclass == USB_XUSB_CLASS) {
        /* Check for XBox-360 controller */
        if ((USBH_XUSB_CLASS)->Probe(phost) == USBH_OK)
            return (1);
    }
    return (0);
}

static void
handle_discovery(USBH_HandleTypeDef *phost, uint port, uint devnum)
{
    uint iface;
    uint numif = phost->device.CfgDesc.bNumInterfaces;
    uint devtype;
    uint8_t ifclass;

    if (usbdev[port][devnum].appstate == APPLICATION_READY) {
        if (!timer_tick_has_elapsed(usbdev[port][devnum].hid_ready_timer))
            return;

        for (iface = 0; iface < numif; iface++) {
            ifclass = phost->device.CfgDesc.Itf_Desc[iface].bInterfaceClass;
            if (ifclass == USB_HID_CLASS)
                devtype = USBH_HID_GetDeviceType(phost, iface);
            else
                devtype = 0;
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
            } else if (is_usb_joystick(phost, iface)) {
                usbdev[port][devnum].joystick_count++;
                usb_joystick_count++;
                break;
            }
        }
        if ((usb_mouse_count > 0) || (usb_joystick_count > 0))
            hiden_set(1);
        usbdev[port][devnum].appstate = APPLICATION_RUNNING;
    }
}

static void
USBH_reset_state_machine(int port)
{
    usb_handle[port][0].gState = HOST_IDLE;
    usb_handle[port][0].EnumState = ENUM_IDLE;
    usb_handle[port][0].RequestState = CMD_SEND;
    usb_handle[port][0].Timer = 0U;
    usb_handle[port][0].Control.state = CTRL_SETUP;
    usb_handle[port][0].Control.pipe_size = USBH_MPS_DEFAULT;
    usb_handle[port][0].Control.errorcount = 0U;

    /* XXX: Need to test the below line with Hub keyboard */
    usb_handle[port][0].device.is_connected = 0U;
}

static void
host_hc_init(USB_OTG_GlobalTypeDef *USBx, uint ch_num, uint epnum,
             uint dev_address, uint speed, uint ep_type, uint mps)
{
    uint32_t USBx_BASE = (uint32_t) USBx;
    uint32_t HCcharEpDir;
    uint32_t HCcharLowSpeed;
    uint32_t HostCoreSpeed;

    /* Program the HCCHAR register */
    if ((epnum & 0x80U) == 0x80U) {
        HCcharEpDir = (0x1U << 15) & USB_OTG_HCCHAR_EPDIR;
    } else {
        HCcharEpDir = 0U;
    }
#if 0
    HAL_HCD_HC_ClearHubInfo(hhcd, ch_num);
#endif

    HostCoreSpeed = USB_GetHostSpeed(USBx);

    /* LS device plugged to HUB */
    if ((speed == HPRT0_PRTSPD_LOW_SPEED) &&
        (HostCoreSpeed != HPRT0_PRTSPD_LOW_SPEED)) {
        HCcharLowSpeed = (0x1U << 17) & USB_OTG_HCCHAR_LSDEV;
    } else {
        HCcharLowSpeed = 0U;
    }

    USBx_HC(ch_num)->HCCHAR = ((dev_address << 22) & USB_OTG_HCCHAR_DAD) |
                              (((epnum & 0x7FU) << 11) & USB_OTG_HCCHAR_EPNUM) |
                              ((ep_type << 18) & USB_OTG_HCCHAR_EPTYP) |
                              (mps & USB_OTG_HCCHAR_MPSIZ) |
                              USB_OTG_HCCHAR_MC_0 | HCcharEpDir | HCcharLowSpeed;

    if ((ep_type == EP_TYPE_INTR) || (ep_type == EP_TYPE_ISOC)) {
      USBx_HC(ch_num)->HCCHAR |= USB_OTG_HCCHAR_ODDFRM;
    }
}

/*
 * @brief  USBH_switch_to_dev
 *         Setup endpoint with selected device info
 * @param  phost: Host handle
 * @retval Status
 */
HAL_StatusTypeDef
USBH_switch_to_dev(USBH_HandleTypeDef *phost)
{
    HCD_HandleTypeDef *hhcd = &_hHCD[phost->id];
    uint ch_out = phost->Control.pipe_out;
    uint ch_in  = phost->Control.pipe_in;

#if 0
    if ((hhcd->hc[ch_out].dev_addr != phost->device.address) ||
        (hhcd->hc[ch_in].dev_addr != phost->device.address)) {
       printf("USB%u.%u switch_to_dev\n", get_port(phost), phost->address);
    }
#endif
    __HAL_LOCK(hhcd);

    hhcd->hc[ch_out].dev_addr   = phost->device.address;
    hhcd->hc[ch_out].max_packet = phost->Control.pipe_size;
    hhcd->hc[ch_out].speed      = phost->device.speed;

    hhcd->hc[ch_out].do_ping = 0U;
    hhcd->hc[ch_out].ch_num     = phost->Control.pipe_out;
//  hhcd->hc[ch_out].toggle_out = phost->Control.toggle_out;
//  hhcd->hc[ch_out].data_pid   = phost->Control.data_pid_out;

    hhcd->hc[ch_in].dev_addr    = phost->device.address;
    hhcd->hc[ch_in].max_packet  = phost->Control.pipe_size;
    hhcd->hc[ch_in].speed       = phost->device.speed;

    hhcd->hc[ch_in].do_ping = 0U;
    hhcd->hc[ch_in].ch_num      = phost->Control.pipe_in;
//  hhcd->hc[ch_in].toggle_in   = phost->Control.toggle_in;
//  hhcd->hc[ch_in].data_pid    = phost->Control.data_pid_in;

    hhcd->pData = phost;
    phost->pData = hhcd;

    if ((phost->InEp & 0x80) != 0) {
        host_hc_init(hhcd->Instance, ch_in, 0x80, phost->device.address, phost->device.speed, USBH_EP_CONTROL, phost->Control.pipe_size);
        host_hc_init(hhcd->Instance, ch_out, phost->OutEp, phost->device.address, phost->device.speed, USBH_EP_CONTROL, phost->Control.pipe_size);
    }

    __HAL_UNLOCK(hhcd);

    return (HAL_OK);
}

USBH_HandleTypeDef *
USBH_get_root_device(USBH_HandleTypeDef *phost)
{
    uint port = get_port(phost);
    return (&usb_handle[port][0]);
}

void
USBH_remove_subdevices(USBH_HandleTypeDef *phost)
{
    uint port = get_port(phost);
    int i;

    for (i = 1; i < 5; ++i) {
        if (usb_handle[port][i].valid) {
            if (usb_handle[port][i].pActiveClass != NULL) {
                usb_handle[port][i].pActiveClass->DeInit(&usb_handle[port][i]);
                usb_handle[port][i].pActiveClass = NULL;
            }
            memset(&usb_handle[port][i], 0, sizeof (USBH_HandleTypeDef));
        }
    }
}


#define LOG(...) printf(__VA_ARGS__)


#if 0
    HID_HandleTypeDef *HID_Handle;
    HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;
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

#endif
static void
process_usb_ports(uint port)
{
    uint pos;
    USBH_HandleTypeDef *phost;
    uint64_t tick_enter;
    uint64_t tick_diff;

#if 0
    /*
     * If usb_debug_mask == 0, then don't do anything
     * If usb_debug_mask & 1, then only run hub
     * If usb_debug_mask & 2, then only run device
     */
    if (usb_debug_mask == 0)
        return;
    if (usb_debug_mask & 1)
        hp_cur[port] = 0;
    if (usb_debug_mask & 2)
        if (hp_cur[port] == 0)
            return;
#endif

    phost = &usb_handle[port][hp_cur[port]];

    /* Handle device class initialization done discovery */
    handle_discovery(phost, port, hp_cur[port]);

    if (phost != NULL) {
        switch (phost->valid) {
            case 0:  // Not valid
                break;
            case 1:
                tick_enter = timer_tick_get();
                USBH_switch_to_dev(phost);
                USBH_Process(phost);
                tick_diff = timer_tick_get() - tick_enter;
                if (phost->tick_max < tick_diff)
                    phost->tick_max = tick_diff;
                phost->tick_total += tick_diff;
                phost->poll_count++;
                if (phost->busy)
                    return;  // Don't go to next device until this one is done
                for (pos = 0; pos < ARRAY_SIZE(usb_handle[port]); pos++)
                    if (usb_handle[port][pos].busy) {
                        hp_cur[port] = pos;  // Move to next "busy" device
                        return;
                    }
                break;
            case 3:
                LOG("USB%u.%u PROCESSING ATTACH\n", get_port(phost), phost->address);

                phost->valid = 1;
                break;
            default:
                printf("USB%u.%u Unknown valid %u for cur=%u\n",
                       get_port(phost), port, phost->valid, hp_cur[port]);
                break;
        }
    }

    if (++hp_cur[port] >= ARRAY_SIZE(usb_handle[port]))
        hp_cur[port] = 0;
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
#if 1
        if ((usbport[port].recovery_state != 0) &&
            timer_tick_has_elapsed(usbport[port].recovery_timer)) {
            const uint usb_retry_fast_limit = 10;
            const uint usb_retry_slow_limit = 20;
            if (usbport[port].connected == 0) {
                /* Port is no longer connected (device pull?) */
                printf("USB is not connected\n");
                USBH_LL_Disconnect(_hHCD[port].pData);
                USBH_reset_state_machine(port);
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
                cubeusb_shutdown_port(port);
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
#endif
        /* Handle host and hub port processing */
        process_usb_ports(port);
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
#if 1
//  XXX: CDH - disable below line to debug EasySMX game controller
    USBH_reset_state_machine(port);
#endif
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
    USBH_HandleTypeDef *phost = hhcd->pData;
    uint portdev;
    port = get_portdev(phost, &portdev);
    dprintf(DF_USB_CONN, "USB%d.%d HAL_HCD_PortDisabled\n", port, portdev);
    USBH_LL_PortDisabled(phost);
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

static USBH_StatusTypeDef
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

/* It's not clear that this code from STM32F4HUB is needed */
USBH_StatusTypeDef
USBH_LL_StopHC(USBH_HandleTypeDef *phost, uint8_t chnum)
{
    HAL_HCD_StopHC(phost->pData, chnum);
    return (USBH_OK);
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

USBH_StatusTypeDef
USBH_register_class(USBH_HandleTypeDef *handle, USBH_ClassTypeDef *class,
                    uint size)
{
    USBH_ClassTypeDef *nclass = USBH_malloc(size);
    memcpy(nclass, class, size);
    nclass->pData = NULL;
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
    handle->address = USBH_DEVICE_ADDRESS;  // Assigned device address
    handle->Pipes = USBH_malloc(sizeof (uint32_t) * USBH_MAX_PIPES_NBR);

    rc = USBH_Init(handle, USBH_UserProcess, host_dev);
    if (rc != USBH_OK) {
        printf("USB%u %s init fail: %d\n", port, pname, rc);
        return;
    }
    if (USBH_register_class(handle, USBH_CDC_CLASS, sizeof (*USBH_CDC_CLASS)) ||
        USBH_register_class(handle, USBH_MSC_CLASS, sizeof (*USBH_MSC_CLASS)) ||
        USBH_register_class(handle, USBH_HID_CLASS, sizeof (*USBH_HID_CLASS)) ||
        USBH_register_class(handle, USBH_HUB_CLASS, sizeof (*USBH_HUB_CLASS)) ||
//      USBH_register_class(handle, USBH_MTP_CLASS, sizeof (*USBH_MTP_CLASS)) ||
        USBH_register_class(handle, USBH_XUSB_CLASS,
                                                sizeof (*USBH_XUSB_CLASS))) {
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
