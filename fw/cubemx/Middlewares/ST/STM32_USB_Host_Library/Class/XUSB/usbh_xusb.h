/*
 * @file    usbh_xusb.h
 */

#ifndef __USBH_XUSB_H
#define __USBH_XUSB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbh_core.h"

#define XUSB_MIN_POLL                                10U
// #define XUSB_REPORT_SIZE                             16U
#define XUSB_MAX_USAGE                               10U
#define XUSB_MAX_NBR_REPORT_FMT                      10U
#define XUSB_QUEUE_SIZE                              10U

#define  XUSB_ITEM_LONG                              0xFEU

#define  XUSB_ITEM_TYPE_MAIN                         0x00U
#define  XUSB_ITEM_TYPE_GLOBAL                       0x01U
#define  XUSB_ITEM_TYPE_LOCAL                        0x02U
#define  XUSB_ITEM_TYPE_RESERVED                     0x03U


#define  XUSB_MAIN_ITEM_TAG_INPUT                    0x08U
#define  XUSB_MAIN_ITEM_TAG_OUTPUT                   0x09U
#define  XUSB_MAIN_ITEM_TAG_COLLECTION               0x0AU
#define  XUSB_MAIN_ITEM_TAG_FEATURE                  0x0BU
#define  XUSB_MAIN_ITEM_TAG_ENDCOLLECTION            0x0CU


#define  XUSB_GLOBAL_ITEM_TAG_USAGE_PAGE             0x00U
#define  XUSB_GLOBAL_ITEM_TAG_LOG_MIN                0x01U
#define  XUSB_GLOBAL_ITEM_TAG_LOG_MAX                0x02U
#define  XUSB_GLOBAL_ITEM_TAG_PHY_MIN                0x03U
#define  XUSB_GLOBAL_ITEM_TAG_PHY_MAX                0x04U
#define  XUSB_GLOBAL_ITEM_TAG_UNIT_EXPONENT          0x05U
#define  XUSB_GLOBAL_ITEM_TAG_UNIT                   0x06U
#define  XUSB_GLOBAL_ITEM_TAG_REPORT_SIZE            0x07U
#define  XUSB_GLOBAL_ITEM_TAG_REPORT_ID              0x08U
#define  XUSB_GLOBAL_ITEM_TAG_REPORT_COUNT           0x09U
#define  XUSB_GLOBAL_ITEM_TAG_PUSH                   0x0AU
#define  XUSB_GLOBAL_ITEM_TAG_POP                    0x0BU


#define  XUSB_LOCAL_ITEM_TAG_USAGE                   0x00U
#define  XUSB_LOCAL_ITEM_TAG_USAGE_MIN               0x01U
#define  XUSB_LOCAL_ITEM_TAG_USAGE_MAX               0x02U
#define  XUSB_LOCAL_ITEM_TAG_DESIGNATOR_INDEX        0x03U
#define  XUSB_LOCAL_ITEM_TAG_DESIGNATOR_MIN          0x04U
#define  XUSB_LOCAL_ITEM_TAG_DESIGNATOR_MAX          0x05U
#define  XUSB_LOCAL_ITEM_TAG_STRING_INDEX            0x07U
#define  XUSB_LOCAL_ITEM_TAG_STRING_MIN              0x08U
#define  XUSB_LOCAL_ITEM_TAG_STRING_MAX              0x09U
#define  XUSB_LOCAL_ITEM_TAG_DELIMITER               0x0AU


/* States for XUSB State Machine */
typedef enum
{
    XUSB_INIT = 0,
    XUSB_FEATURE_REQUEST,
    XUSB_IDLE,
    XUSB_SEND_DATA,
    XUSB_BUSY,
    XUSB_GET_DATA,
    XUSB_SYNC,
    XUSB_POLL,
    XUSB_ERROR,
    XUSB_NO_SUPPORT,       // Unsupported device
}
XUSB_State_t;

typedef enum
{
    XUSB_REQ_INIT = 0,
    XUSB_REQ_IDLE,
    XUSB_REQ_GET_REPORT_DESC,
    XUSB_REQ_GET_DESC,
    XUSB_REQ_SET_IDLE,
    XUSB_REQ_SET_PROTOCOL,
    XUSB_REQ_SET_REPORT,
}
XUSB_CtlState_t;

typedef enum
{
    XUSB_MOUSE    = 0x01,
    XUSB_KEYBOARD = 0x02,
    XUSB_UNKNOWN = 0xFF,
}
XUSB_Type_t;

/* Structure for XUSB process */
typedef struct _XUSB_Handle_t XUSB_Handle_t;
struct _XUSB_Handle_t
{
    uint8_t          interface;  // USB device interface for this handle
    uint8_t          OutPipe;
    uint8_t          InPipe;
    XUSB_State_t     state;
    uint8_t          OutEp;
    uint8_t          InEp;
    uint8_t         *pData;
    uint16_t         length;
    uint16_t         length_max;
    uint8_t          ep_addr;
    uint16_t         poll;
    uint32_t         timer;
    uint8_t          DataReady;
    uint8_t          error_count;
    XUSB_Handle_t   *next;
};

#define USB_XUSB_GET_REPORT                            0x01U
#define USB_XUSB_GET_IDLE                              0x02U
#define USB_XUSB_GET_PROTOCOL                          0x03U
#define USB_XUSB_SET_REPORT                            0x09U
#define USB_XUSB_SET_IDLE                              0x0AU
#define USB_XUSB_SET_PROTOCOL                          0x0BU


/* XUSB Class Codes (actually Vendor-specific code) */
#define USB_XUSB_CLASS                                 0xffU

extern USBH_ClassTypeDef  XUSB_Class;
#define USBH_XUSB_CLASS    &XUSB_Class

USBH_StatusTypeDef USBH_XUSB_SetReport(USBH_HandleTypeDef *phost,
                                       uint8_t reportType, uint8_t reportId,
                                       uint8_t *reportBuff, uint8_t reportLen);

USBH_StatusTypeDef USBH_XUSB_GetReport(USBH_HandleTypeDef *phost,
                                       uint8_t in_pipe);

USBH_StatusTypeDef USBH_XUSB_GetXUSBReportDescriptor(USBH_HandleTypeDef *phost,
                                                     uint16_t iface,
                                                     uint16_t length);

USBH_StatusTypeDef USBH_XUSB_GetXUSBDescriptor(USBH_HandleTypeDef *phost,
                                               uint16_t iface, uint16_t length);

USBH_StatusTypeDef USBH_XUSB_SetIdle(USBH_HandleTypeDef *phost,
                                     uint8_t duration, uint8_t reportId);

USBH_StatusTypeDef USBH_XUSB_SetProtocol(USBH_HandleTypeDef *phost,
                                         uint8_t protocol);

void USBH_XUSB_EventCallback(USBH_HandleTypeDef *phost,
                             XUSB_Handle_t *XUSB_Handle);

void USBH_XUSB_Process_XUSBReportDescriptor(USBH_HandleTypeDef *phost,
                                            XUSB_Handle_t *XUSB_Handle);

typedef struct
{
    uint16_t  buttons;     // Mouse buttons
    int16_t   mouse_x;     // Mouse X movement
    int16_t   mouse_y;     // Mouse Y movement
    int16_t   wheel_x;     // Mouse Left-Right movement
    int16_t   wheel_y;     // Mouse Wheel movement
    uint8_t   joypad;      // Bits: left, right, up, down
}
XUSB_MISC_Info_t;

void USBH_XUSB_DecodeReport(USBH_HandleTypeDef *phost, XUSB_Handle_t *XUSB_Handle, XUSB_MISC_Info_t *report_info);


#ifdef __cplusplus
}
#endif

#endif /* __USBH_XUSB_H */
