/**
  ******************************************************************************
  * @file    usbh_hid_generic.c
  ******************************************************************************
  */

#include "usbh_hid_mouse.h"
#include "usbh_hid_parser.h"
#include "config.h"

#if 0
extern uint32_t                  mouse_report_data[2];
extern uint32_t                  mouse_rx_report_buf[2];
#endif

/**
  * @brief  USBH_HID_GenericInit
  *         The function init the HID mouse.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_GenericInit(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
    dprintf(DF_USB_MOUSE, "USB%u.%u genericinit\n", phost->id, phost->address);

    USBH_HID_Process_HIDReportDescriptor(phost, HID_Handle);

#if 0
    memset(mouse_report_data, 0, sizeof (mouse_report_data));
    memset(mouse_rx_report_buf, 0, sizeof (mouse_rx_report_buf));

    if (HID_Handle->length > sizeof (mouse_report_data)) {
        HID_Handle->length = sizeof (mouse_report_data);
    }
    HID_Handle->pData = (uint8_t *)(void *)mouse_rx_report_buf;
    USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data,
                      HID_QUEUE_SIZE * sizeof (mouse_report_data));
#else
    USBH_HID_PrepareFifo(phost, HID_Handle);
#endif

    return (USBH_OK);
}
