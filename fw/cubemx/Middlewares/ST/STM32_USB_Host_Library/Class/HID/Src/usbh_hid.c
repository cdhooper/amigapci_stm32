/**
  ******************************************************************************
  * @file    usbh_hid.c
  * @author  MCD Application Team
  * @brief   This file is the HID Layer Handlers for USB Host HID class.
  *
  * @verbatim
  *
  *          ===================================================================
  *                                HID Class  Description
  *          ===================================================================
  *           This module manages the HID class V1.11 following the "Device Class Definition
  *           for Human Interface Devices (HID) Version 1.11 Jun 27, 2001".
  *           This driver implements the following aspects of the specification:
  *             - The Boot Interface Subclass
  *             - The Mouse and Keyboard protocols
  *
  *  @endverbatim
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}{adafruit}_lcd.c"
- "stm32xxxxx_{eval}{discovery}_sdram.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbh_hid.h"
#include "usbh_hid_parser.h"


/** @addtogroup USBH_LIB
* @{
*/

/** @addtogroup USBH_CLASS
* @{
*/

/** @addtogroup USBH_HID_CLASS
* @{
*/

/** @defgroup USBH_HID_CORE
* @brief    This file includes HID Layer Handlers for USB Host HID class.
* @{
*/

/** @defgroup USBH_HID_CORE_Private_TypesDefinitions
* @{
*/
/**
* @}
*/


/** @defgroup USBH_HID_CORE_Private_Defines
* @{
*/
/**
* @}
*/


/** @defgroup USBH_HID_CORE_Private_Macros
* @{
*/
/**
* @}
*/


/** @defgroup USBH_HID_CORE_Private_Variables
* @{
*/

/**
* @}
*/


/** @defgroup USBH_HID_CORE_Private_FunctionPrototypes
* @{
*/

static USBH_StatusTypeDef USBH_HID_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_SOFProcess(USBH_HandleTypeDef *phost);
static void  USBH_HID_ParseHIDDesc(HID_DescTypeDef *desc, uint8_t *buf);

extern USBH_StatusTypeDef USBH_HID_MouseInit(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle);
extern USBH_StatusTypeDef USBH_HID_KeybdInit(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle);

USBH_ClassTypeDef  HID_Class =
{
  "HID",
  USB_HID_CLASS,
  USBH_HID_InterfaceInit,
  USBH_HID_InterfaceDeInit,
  USBH_HID_ClassRequest,
  USBH_HID_Process,
  USBH_HID_SOFProcess,
  NULL,
};
/**
* @}
*/


/** @defgroup USBH_HID_CORE_Private_Functions
* @{
*/

#include <stdbool.h>
#include "config.h"
#include "timer.h"
#include "utils.h"
#define DEBUG_HIDREPORT_DESCRIPTOR
#ifdef DEBUG_HIDREPORT_DESCRIPTOR
#define DPRINTF(...) dprintf(DF_USB_REPORT, __VA_ARGS__)
#else
#define DPRINTF(...)
#endif

/** @defgroup USBH_HID_MOUSE_Private_Functions
  * @{
  */
void USBH_HID_Process_HIDReportDescriptor(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
    uint pos;
    uint desc_len = HID_Handle->HID_Desc.wItemLength;
    uint coll_depth = 0;
    uint8_t report_size = 0;  // in bits
    uint8_t report_count = 0;
    uint8_t usage_page = 0;
    uint16_t usage = 0;
    uint    bitpos = 0;  // First byte is usually record #
    uint    button = 0;
    uint    temp;
    uint    value;
    HID_RDescTypeDef *rd = &HID_Handle->HID_RDesc;
    uint16_t usage_array[16];
    uint8_t  usage_count = 0;
    uint8_t  report_id;
    const uint8_t *desc = (const uint8_t *) phost->device.Data;
#ifdef DEBUG_HIDREPORT_DESCRIPTOR
    const char *spaces = "                 ";
#endif
    DPRINTF("USB%u.%u.%u Process_HIDReportDescriptor rlen=%x\n",
            get_port(phost), phost->address, HID_Handle->interface, desc_len);

    if (desc_len > 512)
        desc_len = 512;  // Let's not go crazy here

    /* Request HID Report Descriptor (some mice require 20ms) */
    uint timeout = 100;
    while (USBH_HID_GetHIDReportDescriptor(phost, HID_Handle->interface,
                                           desc_len) != USBH_OK) {
        if (--timeout == 0) {
            printf("USB%u.%u.%u Get report descriptor failed\n",
                   get_port(phost), phost->address, HID_Handle->interface);
            return;
        }
        timer_delay_msec(1);
    }

#if 0
    for (pos = 0; pos < desc_len; pos++)
        printf(" %02x", desc[pos]);
    printf("\n");
#endif
    for (pos = 0; pos < desc_len; ) {
        const uint8_t tag = desc[pos] >> 4;
        const uint8_t type = (desc[pos] >> 2) & 3;
        const uint8_t size = desc[pos] & 3;

        if ((type == 0) && (tag == 0)) {
            pos++;
            continue;
        }
        DPRINTF("%02x:", pos);
        for (temp = 0; temp <= 2; temp++) {
            if (temp <= size) {
                DPRINTF(" %02x", desc[pos + temp]);
            } else {
                DPRINTF("   ");
            }
        }
        pos++;
        DPRINTF(" type %x tag %x size %x  ", type, tag, size);
        switch (type) {
            case HID_ITEM_TYPE_MAIN:  // type 0
                switch (tag) {
                    case HID_MAIN_ITEM_TAG_INPUT:
                        value = desc[pos];
                        DPRINTF("%.*s%s", coll_depth, spaces, "INPUT");
                        DPRINTF(" size=%u count=%u", report_size, report_count);
                        while ((usage_count < report_count) &&
                               (usage_count < ARRAY_SIZE(usage_array))) {
                            usage_array[usage_count++] = usage;
                        }
                        for (temp = 0; temp < report_count; temp++) {
                            int x = temp;
                            if (value & BIT(0)) {
                                /* Constant data */
                                DPRINTF(" C");
                            } else if (usage_page == HID_USAGE_PAGE_BUTTON) {
                                DPRINTF(" Button%u=%u", button, bitpos);
                                if (button < ARRAY_SIZE(rd->pos_button))
                                    rd->pos_button[button] = bitpos;
                                button++;
                                rd->num_buttons = button;
                            } else if (usage_page == HID_USAGE_PAGE_CONSUMER) {
                                DPRINTF(" Consumer usage=%x", usage_array[x]);
                                switch (usage_array[x]) {
                                    case 0x01:  // Generic control
                                        if (rd->num_keys <
                                            ARRAY_SIZE(rd->pos_key)) {
                                            rd->bits_key = report_size;
                                            rd->pos_key[rd->num_keys] = bitpos;
                                            rd->num_keys++;
                                        }
                                        DPRINTF(" key=%u", bitpos);
                                        break;
                                    case HID_USAGE_AC_PAN:
                                        DPRINTF(" AC_PAN=%u", bitpos);
                                        rd->pos_ac_pan = bitpos;
                                        rd->bits_ac_pan = report_size;
                                        break;
                                    default: {
                                        /* Attempt to handle as MM Button */
                                        uint num = rd->num_mmbuttons;
                                        if (report_size != 1)
                                            break;
                                        if (num >= ARRAY_SIZE(rd->val_mmbutton))
                                            break;
                                        DPRINTF(" MM %x %x=%u", num,
                                                usage_array[x], bitpos);
                                        rd->val_mmbutton[num] = usage_array[x];
                                        rd->pos_mmbutton[num] = bitpos;
                                        rd->id_mmbutton[num] = report_id;
                                        rd->num_mmbuttons++;
                                    }
                                }
                            } else if (x <= usage_count) {
                                switch (usage_array[x]) {
                                    case HID_USAGE_X:
                                        DPRINTF(" X=%u", bitpos);
                                        rd->pos_x = bitpos;
                                        rd->bits_x = report_size;
                                        break;
                                    case HID_USAGE_Y:
                                        DPRINTF(" Y=%u", bitpos);
                                        rd->pos_y = bitpos;
                                        rd->bits_y = report_size;
                                        break;
                                    case HID_USAGE_WHEEL:
                                        DPRINTF(" WHEEL=%u", bitpos);
                                        rd->pos_wheel = bitpos;
                                        rd->bits_wheel = report_size;
                                        break;
                                    case HID_USAGE_SYSCTL:
                                    case HID_USAGE_SLEEP:
                                    case HID_USAGE_PWDOWN:
                                    case HID_USAGE_WAKEUP:
                                        DPRINTF(" SYSCTL=%u", bitpos);
                                        rd->pos_sysctl = bitpos;
                                        rd->bits_sysctl = report_size;
                                        if (rd->bits_sysctl > 16)
                                            rd->bits_sysctl = 16;
                                        break;
                                }
                            }
                            bitpos += report_size;
                        }
                        break;
                    case HID_MAIN_ITEM_TAG_OUTPUT:
                        DPRINTF("%.*s%s", coll_depth, spaces, "OUTPUT");
                        // End output feature
                        break;
                    case HID_MAIN_ITEM_TAG_COLLECTION:
                        DPRINTF("%.*s%s", coll_depth, spaces, "COLL");
                        coll_depth++;
                        break;
                    case HID_MAIN_ITEM_TAG_FEATURE:
                        DPRINTF("%.*s%s", coll_depth, spaces, "FEATURE");
                        break;
                    case HID_MAIN_ITEM_TAG_ENDCOLLECTION:
                        coll_depth--;
                        if (coll_depth == 0) {
                            bitpos = 0;
                            usage = 0;
                            report_count = 0;
                        }
                        DPRINTF("%.*s%s", coll_depth, spaces, "END COLL");
                        break;
                }
                usage_count = 0;
                break;
            case HID_ITEM_TYPE_GLOBAL:  // type 1
                switch (tag) {
                    case HID_GLOBAL_ITEM_TAG_USAGE_PAGE:
                        usage_page = desc[pos];
                        DPRINTF("%.*s%s", coll_depth, spaces, "USAGE PAGE");
                        switch (usage_page) {
                            case HID_USAGE_PAGE_GEN_DES:
                                /* Mouse X, Y */
                                DPRINTF(" Generic Desktop");
                                break;
                            case HID_USAGE_PAGE_GAME_CTR:
                                DPRINTF(" Game Controller");
                                break;
                            case HID_USAGE_PAGE_KEYB:
                                DPRINTF(" Keyboard");
                                break;
                            case HID_USAGE_PAGE_LED:
                                DPRINTF(" LED");
                                break;
                            case HID_USAGE_PAGE_BUTTON:
                                DPRINTF(" Button");
                                break;
                            case HID_USAGE_PAGE_CONSUMER:
                                DPRINTF(" Consumer");
                                break;
                            case HID_USAGE_PAGE_BARCODE:
                                DPRINTF(" Barcode");
                                break;
                        }
                        break;
                    case HID_GLOBAL_ITEM_TAG_LOG_MIN:
                        DPRINTF("%.*s%s", coll_depth, spaces, "LOG MIN");
                        break;
                    case HID_GLOBAL_ITEM_TAG_LOG_MAX:
                        DPRINTF("%.*s%s", coll_depth, spaces, "LOG MAX");
                        break;
                    case HID_GLOBAL_ITEM_TAG_PHY_MIN:
                        DPRINTF("%.*s%s", coll_depth, spaces, "PHY MIN");
                        break;
                    case HID_GLOBAL_ITEM_TAG_PHY_MAX:
                        DPRINTF("%.*s%s", coll_depth, spaces, "PHY MAX");
                        break;
                    case HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT:
                        DPRINTF("%.*s%s", coll_depth, spaces, "UNIT EXP");
                        break;
                    case HID_GLOBAL_ITEM_TAG_UNIT:
                        DPRINTF("%.*s%s", coll_depth, spaces, "UNIT");
                        break;
                    case HID_GLOBAL_ITEM_TAG_REPORT_SIZE:
                        report_size = desc[pos];  // bits per report
                        DPRINTF("%.*s%s", coll_depth, spaces, "REP SIZE");
                        DPRINTF("=%u bits", report_size);
                        break;
                    case HID_GLOBAL_ITEM_TAG_REPORT_ID:
                        DPRINTF("%.*s%s", coll_depth, spaces, "REP ID");
                        DPRINTF("=%u", desc[pos]);
                        report_id = desc[pos];
                        switch (usage_page) {
                            case HID_USAGE_PAGE_GEN_DES:
                                switch (usage) {
                                    case HID_USAGE_POINTER:
                                    case HID_USAGE_MOUSE:
                                        rd->id_mouse = report_id;
                                        break;
                                    case HID_USAGE_SYSCTL:
                                        rd->id_sysctl = report_id;
                                        break;
                                }
                                break;
                            case HID_USAGE_PAGE_CONSUMER:
                                rd->id_consumer = report_id;
                                break;
                        }
                        bitpos += 8;
                        break;
                    case HID_GLOBAL_ITEM_TAG_REPORT_COUNT:
                        report_count = desc[pos];  // number of reports
                        DPRINTF("%.*s%s", coll_depth, spaces, "REP COUNT");
                        DPRINTF("=%u", report_count);
                        break;
                    case HID_GLOBAL_ITEM_TAG_PUSH:
                        DPRINTF("%.*s%s", coll_depth, spaces, "PUSH");
                        break;
                    case HID_GLOBAL_ITEM_TAG_POP:
                        DPRINTF("%.*s%s", coll_depth, spaces, "POP");
                        break;
                        break;
                }
                break;
            case HID_ITEM_TYPE_LOCAL:  // type 2
                switch (tag) {
                    case HID_LOCAL_ITEM_TAG_USAGE:
                        DPRINTF("%.*s%s", coll_depth, spaces, "USAGE");
                        if (size == 1)
                            usage = desc[pos];
                        else
                            usage = desc[pos] | (desc[pos + 1] << 8);
                        switch (usage_page) {
                            case HID_USAGE_PAGE_GEN_DES:
                                switch (usage) {
                                    case HID_USAGE_POINTER:
                                        DPRINTF(" Pointer");
                                        break;
                                    case HID_USAGE_MOUSE:
                                        DPRINTF(" Mouse");
                                        break;
                                    case HID_USAGE_JOYSTICK:
                                        DPRINTF(" Joystick");
                                        break;
                                    case HID_USAGE_GAMEPAD:
                                        DPRINTF(" Gamepad");
                                        break;
                                    case HID_USAGE_KBD:
                                        DPRINTF(" Keyboard");
                                        break;
                                    case HID_USAGE_X:
                                        DPRINTF(" X");
                                        break;
                                    case HID_USAGE_Y:
                                        DPRINTF(" Y");
                                        break;
                                    case HID_USAGE_WHEEL:
                                        DPRINTF(" WHEEL");
                                        break;
                                    case HID_USAGE_SYSCTL:
                                        DPRINTF(" SYSCTL");
                                        break;
                                    case HID_USAGE_PWDOWN:
                                        DPRINTF(" PWDOWN");
                                        break;
                                    case HID_USAGE_SLEEP:
                                        DPRINTF(" SLEEP");
                                        break;
                                    case HID_USAGE_WAKEUP:
                                        DPRINTF(" WAKEUP");
                                        break;
                                }
                                break;
                            case HID_USAGE_PAGE_CONSUMER:
                                switch (usage) {
                                    case 0x01:
                                        DPRINTF(" Control");
                                        break;
                                    case HID_USAGE_AC_PAN:
                                        DPRINTF(" AC_PAN");
                                        break;
                                }
                                break;
                        }
                        if (coll_depth == 0) {
                            if (rd->usage == 0)
                                rd->usage = usage;
                        } else {
                            if (usage_count < ARRAY_SIZE(usage_array))
                                usage_array[usage_count++] = usage;
                        }
                        break;
                    case HID_LOCAL_ITEM_TAG_USAGE_MIN:
                        DPRINTF("%.*s%s", coll_depth, spaces, "USAGEMIN");
                        break;
                    case HID_LOCAL_ITEM_TAG_USAGE_MAX:
                        DPRINTF("%.*s%s", coll_depth, spaces, "USAGEMAX");
                        break;
                    case HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX:
                        DPRINTF("%.*s%s", coll_depth, spaces, "DES INDEX");
                        break;
                    case HID_LOCAL_ITEM_TAG_DESIGNATOR_MIN:
                        DPRINTF("%.*s%s", coll_depth, spaces, "DES MIN");
                        break;
                    case HID_LOCAL_ITEM_TAG_DESIGNATOR_MAX:
                        DPRINTF("%.*s%s", coll_depth, spaces, "DES MAX");
                        break;
                    case HID_LOCAL_ITEM_TAG_STRING_INDEX:
                    case HID_LOCAL_ITEM_TAG_STRING_MIN:
                    case HID_LOCAL_ITEM_TAG_STRING_MAX:
                    case HID_LOCAL_ITEM_TAG_DELIMITER:
                        DPRINTF("%.*s%s", coll_depth, spaces, "DELIM");
                        break;
                        break;
                }
                break;
            case HID_ITEM_TYPE_RESERVED:  // type 3
                break;
        }
        DPRINTF("\n");
        pos += size;
    }
}


void timer_delay_msec(uint msec);
#define BIT(x) (1U << (x))


static USBH_StatusTypeDef USBH_HID_InterfaceInit_ll(USBH_HandleTypeDef *phost, uint8_t interface)
{
  USBH_StatusTypeDef status;
  HID_HandleTypeDef *HID_Handle;
  uint8_t max_ep;
  uint8_t num = 0U;

  status = USBH_SelectInterface(phost, interface);

  if (status != USBH_OK)
  {
    return USBH_FAIL;
  }

  HID_Handle = (HID_HandleTypeDef *)USBH_malloc(sizeof(HID_HandleTypeDef));
  if (HID_Handle == NULL)
  {
    USBH_DbgLog("Cannot allocate memory for HID Handle");
    return USBH_FAIL;
  }

  /* Initialize hid handler */
  USBH_memset(HID_Handle, 0, sizeof(HID_HandleTypeDef));

#if 0
  /* pData list in reverse order */
  HID_Handle->next = phost->pActiveClass->pData;
  phost->pActiveClass->pData = HID_Handle;
#else
  /*
   * pData list must be in insertion order because some HID devices
   * such as the Dell USB Hub Keyboard require that interfaces be
   * processed in order.
   */
  HID_HandleTypeDef *prev = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  HID_Handle->next = NULL;

  if (prev == NULL) {
    phost->pActiveClass->pData = HID_Handle;
  } else {
    while (prev->next != NULL)
        prev = prev->next;
    prev->next = HID_Handle;
  }
#endif

  HID_Handle->interface = interface;
  HID_Handle->state = HID_ERROR;

  /* Decode Bootclass Protocol: Mouse or Keyboard */
  if (phost->device.CfgDesc.Itf_Desc[interface].bInterfaceProtocol == HID_KEYBRD_BOOT_CODE)
  {
    USBH_UsrLog("USB%u.%u.%u Keyboard device found", get_port(phost), phost->address, interface);
    HID_Handle->Init = USBH_HID_KeybdInit;
  }
  else if (phost->device.CfgDesc.Itf_Desc[interface].bInterfaceProtocol == HID_MOUSE_BOOT_CODE)
  {
    USBH_UsrLog("USB%u.%u.%u Mouse device found", get_port(phost), phost->address, interface);
    HID_Handle->Init = USBH_HID_MouseInit;
  }
  else
  {
    USBH_UsrLog("USB%u.%u.%u Generic device found", get_port(phost), phost->address, interface);
    HID_Handle->Init = USBH_HID_GenericInit;
//  return USBH_FAIL;
  }

  HID_Handle->state     = HID_INIT;
  HID_Handle->ctl_state = HID_REQ_INIT;
  HID_Handle->ep_addr   = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].bEndpointAddress;
  HID_Handle->length    = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].wMaxPacketSize;
  HID_Handle->poll      = phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[0].bInterval;

  if (HID_Handle->poll  < HID_MIN_POLL)
  {
    HID_Handle->poll = HID_MIN_POLL;
  }

  /* Check fo available number of endpoints */
  /* Find the number of EPs in the Interface Descriptor */
  /* Choose the lower number in order not to overrun the buffer allocated */
  max_ep = ((phost->device.CfgDesc.Itf_Desc[interface].bNumEndpoints <= USBH_MAX_NUM_ENDPOINTS) ?
             phost->device.CfgDesc.Itf_Desc[interface].bNumEndpoints : USBH_MAX_NUM_ENDPOINTS);


  /* Decode endpoint IN and OUT address from interface descriptor */
  for (num = 0U; num < max_ep; num++)
  {
    if (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[num].bEndpointAddress & 0x80U)
    {
      HID_Handle->InEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[num].bEndpointAddress);
      HID_Handle->InPipe = USBH_AllocPipe(phost, HID_Handle->InEp);

      /* Open pipe for IN endpoint */
      USBH_OpenPipe(phost, HID_Handle->InPipe, HID_Handle->InEp, phost->device.address,
                    phost->device.speed, USB_EP_TYPE_INTR, HID_Handle->length);

      USBH_LL_SetToggle(phost, HID_Handle->InPipe, 0U);
    }
    else
    {
      HID_Handle->OutEp = (phost->device.CfgDesc.Itf_Desc[interface].Ep_Desc[num].bEndpointAddress);
      HID_Handle->OutPipe  = USBH_AllocPipe(phost, HID_Handle->OutEp);

      /* Open pipe for OUT endpoint */
      USBH_OpenPipe(phost, HID_Handle->OutPipe, HID_Handle->OutEp, phost->device.address,
                    phost->device.speed, USB_EP_TYPE_INTR, HID_Handle->length);

      USBH_LL_SetToggle(phost, HID_Handle->OutPipe, 0U);
    }
  }

  return USBH_OK;
}

/**
  * @brief  USBH_HID_InterfaceInit
  *         The function init the HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_InterfaceInit(USBH_HandleTypeDef *phost)
{
  uint8_t interface;
  USBH_StatusTypeDef status = USBH_FAIL;
  USBH_StatusTypeDef t_status;
  uint numif = phost->device.CfgDesc.bNumInterfaces;

  if (numif > USBH_MAX_NUM_INTERFACES)
      numif = USBH_MAX_NUM_INTERFACES;
  for (interface = 0; interface < numif; interface++) {
      t_status = USBH_HID_InterfaceInit_ll(phost, interface);
      if (t_status == USBH_OK)
          status = t_status;
  }

  if (status == USBH_FAIL) { /* No Valid Interface */
    USBH_DbgLog("Cannot Find the interface for %s class.", phost->pActiveClass->Name);
  }
  return (status);
}

static USBH_StatusTypeDef USBH_HID_InterfaceDeInit_ll(USBH_HandleTypeDef *phost)
{
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  if (HID_Handle == NULL)
    return USBH_FAIL;

  if (HID_Handle->InPipe != 0x00U)
  {
    USBH_LL_StopHC(phost, HID_Handle->InPipe);

    USBH_ClosePipe(phost, HID_Handle->InPipe);
    USBH_FreePipe(phost, HID_Handle->InPipe);
    HID_Handle->InPipe = 0U;     /* Reset the pipe as Free */
  }

  if (HID_Handle->OutPipe != 0x00U)
  {
    USBH_LL_StopHC(phost, HID_Handle->OutPipe);

    USBH_ClosePipe(phost, HID_Handle->OutPipe);
    USBH_FreePipe(phost, HID_Handle->OutPipe);
    HID_Handle->OutPipe = 0U;     /* Reset the pipe as Free */
  }

  phost->pActiveClass->pData = HID_Handle->next;
  USBH_free(HID_Handle);

  return USBH_OK;
}

/**
  * @brief  USBH_HID_InterfaceDeInit
  *         The function DeInit the Pipes used for the HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
  USBH_StatusTypeDef status = USBH_FAIL;

  while (phost->pActiveClass->pData != NULL)
    status = USBH_HID_InterfaceDeInit_ll(phost);

  return status;
}

/**
  * @brief  USBH_HID_ClassRequest_ll
  *         The function is responsible for handling Standard requests
  *         for HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_ClassRequest_ll(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{

  USBH_StatusTypeDef status         = USBH_BUSY;
  USBH_StatusTypeDef classReqStatus = USBH_BUSY;

  /* Switch HID state machine */
  switch (HID_Handle->ctl_state)
  {
    case HID_REQ_INIT:
    case HID_REQ_GET_HID_DESC:

      /* Get HID Desc */
      if (USBH_HID_GetHIDDescriptor(phost, HID_Handle->interface, USB_HID_DESC_SIZE) == USBH_OK)
      {
        USBH_HID_ParseHIDDesc(&HID_Handle->HID_Desc, phost->device.Data);
        HID_Handle->ctl_state = HID_REQ_GET_REPORT_DESC;
      }

      break;
    case HID_REQ_GET_REPORT_DESC:
      /* Get Report Desc */
      if (USBH_HID_GetHIDReportDescriptor(phost, HID_Handle->interface, HID_Handle->HID_Desc.wItemLength) == USBH_OK)
      {
        /* The descriptor is available in phost->device.Data */
//      USBH_HID_Process_HIDReportDescriptor(phost, HID_Handle);
        HID_Handle->ctl_state = HID_REQ_SET_IDLE;
      }

      break;

    case HID_REQ_SET_IDLE:

      classReqStatus = USBH_HID_SetIdle(phost, 0U, 0U);

      /* set Idle */
      if (classReqStatus == USBH_OK)
      {
        HID_Handle->ctl_state = HID_REQ_SET_PROTOCOL;
      }
      else
      {
        if (classReqStatus == USBH_NOT_SUPPORTED)
        {
          HID_Handle->ctl_state = HID_REQ_SET_PROTOCOL;
        }
      }
      break;

    case HID_REQ_SET_PROTOCOL:
      /* set protocol */
      status = USBH_HID_SetProtocol(phost, 0U);
      if ((status == USBH_OK) || (status == USBH_NOT_SUPPORTED))
      {
        HID_Handle->ctl_state = HID_REQ_IDLE;

        /* all requests performed*/
        phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
        status = USBH_OK;
      }
      break;

    case HID_REQ_IDLE:
    default:
      break;
  }

  return status;
}

/**
  * @brief  USBH_HID_ClassRequest
  *         The function is responsible for handling Standard requests
  *         for HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_ClassRequest(USBH_HandleTypeDef *phost)
{
    static uint32_t waiting = 0;
    uint bit = 0;
    USBH_StatusTypeDef status;
    HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

    /*
     * Serialize class requests because only one may be active
     * on the USB host at any time.
     */
    while (HID_Handle != NULL) {
        if (waiting) {
            if (waiting & BIT(bit)) {
                status = USBH_HID_ClassRequest_ll(phost, HID_Handle);
                if (status == USBH_OK)
                    waiting &= ~BIT(bit);
            }
        } else {
            status = USBH_HID_ClassRequest_ll(phost, HID_Handle);
            if (status != USBH_OK) {
                waiting |= BIT(bit);
                break;  // Check again later
            }
        }
        bit++;
        HID_Handle = HID_Handle->next;
    }
    return (waiting ? USBH_FAIL: USBH_OK);
}

static USBH_StatusTypeDef USBH_HID_Process_ll(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
  USBH_StatusTypeDef status = USBH_OK;
  uint32_t XferSize;

  switch (HID_Handle->state)
  {
    case HID_INIT:
      HID_Handle->Init(phost, HID_Handle);
      HID_Handle->state = HID_IDLE;

#if (USBH_USE_OS == 1U)
      phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
      (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
      (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
      break;

    case HID_IDLE:
      status = USBH_HID_GetReport(phost, 0x01U, 0U, HID_Handle->pData, (uint8_t)HID_Handle->length);
      if (status == USBH_OK)
      {
        HID_Handle->state = HID_SYNC;
      }
      else if (status == USBH_BUSY)
      {
        HID_Handle->state = HID_IDLE;
        status = USBH_OK;
      }
      else if (status == USBH_NOT_SUPPORTED)
      {
        HID_Handle->state = HID_SYNC;
        status = USBH_OK;
      }
      else
      {
        HID_Handle->state = HID_ERROR;
        status = USBH_FAIL;
      }

#if (USBH_USE_OS == 1U)
      phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
      (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
      (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
      break;

    case HID_SYNC:
      /* Sync with start of Even Frame */
      if (phost->Timer & 1U)
      {
        HID_Handle->state = HID_GET_DATA;
      }

#if (USBH_USE_OS == 1U)
      phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
      (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
      (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
      break;

    case HID_GET_DATA:
      USBH_InterruptReceiveData(phost, HID_Handle->pData,
                                (uint8_t)HID_Handle->length,
                                HID_Handle->InPipe);

      HID_Handle->state = HID_POLL;
      HID_Handle->timer = phost->Timer;
      HID_Handle->DataReady = 0U;
      break;

    case HID_POLL:
      if (USBH_LL_GetURBState(phost, HID_Handle->InPipe) == USBH_URB_DONE)
      {
        XferSize = USBH_LL_GetLastXferSize(phost, HID_Handle->InPipe);

        if ((HID_Handle->DataReady == 0U) && (XferSize != 0U))
        {
          USBH_HID_FifoWrite(&HID_Handle->fifo, HID_Handle->pData, HID_Handle->length);
          HID_Handle->DataReady = 1U;
          USBH_HID_EventCallback(phost, HID_Handle);

#if (USBH_USE_OS == 1U)
          phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
          (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
          (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
        }
      }
      else
      {
        /* IN Endpoint Stalled */
        if (USBH_LL_GetURBState(phost, HID_Handle->InPipe) == USBH_URB_STALL)
        {
          /* Issue Clear Feature on interrupt IN endpoint */
          if (USBH_ClrFeature(phost, HID_Handle->ep_addr) == USBH_OK)
          {
            /* Change state to issue next IN token */
            HID_Handle->state = HID_GET_DATA;
          }
        }
      }
      break;

    default:
      break;
  }

  return status;
}

/**
  * @brief  USBH_HID_Process
  *         The function is for managing state machine for HID data transfers
  *         It is the background processor for the class (BgndProcess).
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_Process(USBH_HandleTypeDef *phost)
{
  USBH_StatusTypeDef tstatus;
  USBH_StatusTypeDef status = USBH_OK;
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  for (; HID_Handle != NULL; HID_Handle = HID_Handle->next) {
    tstatus = USBH_HID_Process_ll(phost, HID_Handle);
    if (status == USBH_OK)
        status = tstatus;
  }
  return status;
}

static USBH_StatusTypeDef USBH_HID_SOFProcess_ll(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
  if (HID_Handle->state == HID_POLL)
  {
    if ((phost->Timer - HID_Handle->timer) >= HID_Handle->poll)
    {
      HID_Handle->state = HID_GET_DATA;

#if (USBH_USE_OS == 1U)
      phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
      (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
      (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
    }
  }
  return USBH_OK;
}

/**
  * @brief  USBH_HID_SOFProcess
  *         The function is for managing the SOF Process
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_SOFProcess(USBH_HandleTypeDef *phost)
{
  USBH_StatusTypeDef tstatus;
  USBH_StatusTypeDef status = USBH_OK;
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  while (HID_Handle != NULL) {
    tstatus = USBH_HID_SOFProcess_ll(phost, HID_Handle);
    if (status == USBH_OK)
        status = tstatus;
    HID_Handle = HID_Handle->next;
  }
  return status;
}

/**
* @brief  USBH_Get_HID_ReportDescriptor
  *         Issue report Descriptor command to the device. Once the response
  *         received, parse the report descriptor and update the status.
  * @param  phost: Host handle
  * @param  iface: Which interface on the device
  * @param  Length : HID Report Descriptor Length
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_GetHIDReportDescriptor(USBH_HandleTypeDef *phost,
                                                   uint16_t iface, uint16_t length)
{

  USBH_StatusTypeDef status;

  status = USBH_GetDescriptor(phost,
                              USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_STANDARD,
                              USB_DESC_HID_REPORT, iface,
                              phost->device.Data,
                              length);

  /* HID report descriptor is available in phost->device.Data.
  In case of USB Boot Mode devices for In report handling ,
  HID report descriptor parsing is not required.
  In case, for supporting Non-Boot Protocol devices and output reports,
  user may parse the report descriptor*/


  return status;
}


/**
  * @brief  USBH_Get_HID_Descriptor
  *         Issue HID Descriptor command to the device. Once the response
  *         received, parse the report descriptor and update the status.
  * @param  phost: Host handle
  * @param  iface: Which interface on the device
  * @param  Length : HID Descriptor Length
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_GetHIDDescriptor(USBH_HandleTypeDef *phost,
                                             uint16_t iface, uint16_t length)
{
  USBH_StatusTypeDef status;

  status = USBH_GetDescriptor(phost,
                              USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_STANDARD,
                              USB_DESC_HID, iface,
                              phost->device.Data,
                              length);

  return status;
}

/**
  * @brief  USBH_Set_Idle
  *         Set Idle State.
  * @param  phost: Host handle
  * @param  duration: Duration for HID Idle request
  * @param  reportId : Targeted report ID for Set Idle request
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_SetIdle(USBH_HandleTypeDef *phost,
                                    uint8_t duration,
                                    uint8_t reportId)
{

  phost->Control.setup.b.bmRequestType = USB_H2D | USB_REQ_RECIPIENT_INTERFACE | \
                                         USB_REQ_TYPE_CLASS;


  phost->Control.setup.b.bRequest = USB_HID_SET_IDLE;
  phost->Control.setup.b.wValue.w = (uint16_t)(((uint32_t)duration << 8U) | (uint32_t)reportId);

  phost->Control.setup.b.wIndex.w = 0U;
  phost->Control.setup.b.wLength.w = 0U;

  return USBH_CtlReq(phost, 0U, 0U);
}


/**
  * @brief  USBH_HID_Set_Report
  *         Issues Set Report
  * @param  phost: Host handle
  * @param  reportType  : Report type to be sent
  * @param  reportId    : Targeted report ID for Set Report request
  * @param  reportBuff  : Report Buffer
  * @param  reportLen   : Length of data report to be send
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_SetReport(USBH_HandleTypeDef *phost,
                                      uint8_t reportType,
                                      uint8_t reportId,
                                      uint8_t *reportBuff,
                                      uint8_t reportLen)
{

  phost->Control.setup.b.bmRequestType = USB_H2D | USB_REQ_RECIPIENT_INTERFACE | \
                                         USB_REQ_TYPE_CLASS;


  phost->Control.setup.b.bRequest = USB_HID_SET_REPORT;
  phost->Control.setup.b.wValue.w = (uint16_t)(((uint32_t)reportType << 8U) | (uint32_t)reportId);

  phost->Control.setup.b.wIndex.w = 0U;
  phost->Control.setup.b.wLength.w = reportLen;

  return USBH_CtlReq(phost, reportBuff, (uint16_t)reportLen);
}


/**
  * @brief  USBH_HID_GetReport
  *         retreive Set Report
  * @param  phost: Host handle
  * @param  reportType  : Report type to be sent
  * @param  reportId    : Targeted report ID for Set Report request
  * @param  reportBuff  : Report Buffer
  * @param  reportLen   : Length of data report to be send
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_GetReport(USBH_HandleTypeDef *phost,
                                      uint8_t reportType,
                                      uint8_t reportId,
                                      uint8_t *reportBuff,
                                      uint8_t reportLen)
{

  phost->Control.setup.b.bmRequestType = USB_D2H | USB_REQ_RECIPIENT_INTERFACE | \
                                         USB_REQ_TYPE_CLASS;


  phost->Control.setup.b.bRequest = USB_HID_GET_REPORT;
  phost->Control.setup.b.wValue.w = (uint16_t)(((uint32_t)reportType << 8U) | (uint32_t)reportId);

  phost->Control.setup.b.wIndex.w = 0U;
  phost->Control.setup.b.wLength.w = reportLen;

  return USBH_CtlReq(phost, reportBuff, (uint16_t)reportLen);
}

/**
  * @brief  USBH_Set_Protocol
  *         Set protocol State.
  * @param  phost: Host handle
  * @param  protocol : Set Protocol for HID : boot/report protocol
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_SetProtocol(USBH_HandleTypeDef *phost,
                                        uint8_t protocol)
{
  phost->Control.setup.b.bmRequestType = USB_H2D | USB_REQ_RECIPIENT_INTERFACE
                                         | USB_REQ_TYPE_CLASS;

  phost->Control.setup.b.bRequest = USB_HID_SET_PROTOCOL;
  if (protocol)
  {
    phost->Control.setup.b.wValue.w = 0U;
  }
  else
  {
    phost->Control.setup.b.wValue.w = 1U;
  }

  phost->Control.setup.b.wIndex.w = 0U;
  phost->Control.setup.b.wLength.w = 0U;

  return USBH_CtlReq(phost, 0U, 0U);

}

/**
  * @brief  USBH_ParseHIDDesc
  *         This function Parse the HID descriptor
  * @param  desc: HID Descriptor
  * @param  buf: Buffer where the source descriptor is available
  * @retval None
  */
static void  USBH_HID_ParseHIDDesc(HID_DescTypeDef *desc, uint8_t *buf)
{

  desc->bLength                  = *(uint8_t *)(buf + 0);
  desc->bDescriptorType          = *(uint8_t *)(buf + 1);
  desc->bcdHID                   =  LE16(buf + 2);
  desc->bCountryCode             = *(uint8_t *)(buf + 4);
  desc->bNumDescriptors          = *(uint8_t *)(buf + 5);
  desc->bReportDescriptorType    = *(uint8_t *)(buf + 6);
  desc->wItemLength              =  LE16(buf + 7);
}

/**
  * @brief  USBH_HID_GetDeviceType
  *         Return Device function.
  * @param  phost: Host handle
  * @param  iface: Which interface on the device
  * @retval HID function: HID_MOUSE / HID_KEYBOARD
  */
HID_TypeTypeDef USBH_HID_GetDeviceType(USBH_HandleTypeDef *phost, uint16_t iface)
{
  HID_TypeTypeDef   type = HID_UNKNOWN;
  uint8_t InterfaceProtocol;

  if (phost->gState == HOST_CLASS)
  {
    InterfaceProtocol = phost->device.CfgDesc.Itf_Desc[iface].bInterfaceProtocol;
    if (InterfaceProtocol == HID_KEYBRD_BOOT_CODE) {
      type = HID_KEYBOARD;
    } else if (InterfaceProtocol == HID_MOUSE_BOOT_CODE) {
      type = HID_MOUSE;
    }
  }
  return type;
}


#if 0
/**
  * @brief  USBH_HID_GetPollInterval
  *         Return HID device poll time
  * @param  phost: Host handle
  * @retval poll time (ms)
  */
uint8_t USBH_HID_GetPollInterval(USBH_HandleTypeDef *phost)
{
  uint8_t interval = 0U;
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  if ((phost->gState == HOST_CLASS_REQUEST) ||
      (phost->gState == HOST_INPUT) ||
      (phost->gState == HOST_SET_CONFIGURATION) ||
      (phost->gState == HOST_CHECK_CLASS) ||
      ((phost->gState == HOST_CLASS)))
  {
    while (HID_Handle != NULL) {
      if ((interval == 0) || (interval > HID_Handle->poll))
        interval = HID_Handle->poll;
      HID_Handle = HID_Handle->next;
    }
  }
  return (interval);
}
#endif
/**
  * @brief  USBH_HID_FifoInit
  *         Initialize FIFO.
  * @param  f: Fifo address
  * @param  buf: Fifo buffer
  * @param  size: Fifo Size
  * @retval none
  */
void USBH_HID_FifoInit(FIFO_TypeDef *f, uint8_t *buf, uint16_t size)
{
  f->head = 0U;
  f->tail = 0U;
  f->lock = 0U;
  f->size = size;
  f->buf = buf;
}

/**
  * @brief  USBH_HID_FifoRead
  *         Read from FIFO.
  * @param  f: Fifo address
  * @param  buf: read buffer
  * @param  nbytes: number of item to read
  * @retval number of read items
  */
uint16_t USBH_HID_FifoRead(FIFO_TypeDef *f, void *buf, uint16_t nbytes)
{
  uint16_t i;
  uint8_t *p;

  p = (uint8_t *) buf;

  if (f->lock == 0U)
  {
    f->lock = 1U;

    for (i = 0U; i < nbytes; i++)
    {
      if (f->tail != f->head)
      {
        *p++ = f->buf[f->tail];
        f->tail++;

        if (f->tail == f->size)
        {
          f->tail = 0U;
        }
      }
      else
      {
        f->lock = 0U;
        return i;
      }
    }
  }

  f->lock = 0U;

  return nbytes;
}

/**
  * @brief  USBH_HID_FifoWrite
  *         Write To FIFO.
  * @param  f: Fifo address
  * @param  buf: read buffer
  * @param  nbytes: number of item to write
  * @retval number of written items
  */
uint16_t USBH_HID_FifoWrite(FIFO_TypeDef *f, void *buf, uint16_t  nbytes)
{
  uint16_t i;
  uint8_t *p;

  p = (uint8_t *) buf;

  if (f->lock == 0U)
  {
    f->lock = 1U;

    for (i = 0U; i < nbytes; i++)
    {
      if ((f->head + 1U == f->tail) ||
          ((f->head + 1U == f->size) && (f->tail == 0U)))
      {
        f->lock = 0U;
        return i;
      }
      else
      {
        f->buf[f->head] = *p++;
        f->head++;

        if (f->head == f->size)
        {
          f->head = 0U;
        }
      }
    }
  }

  f->lock = 0U;

  return nbytes;
}

/**
* @brief  The function is a callback about HID Data events
*  @param  phost: Selected device
* @retval None
*/
__weak void USBH_HID_EventCallback(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(phost);
  UNUSED(HID_Handle);
}
/**
* @}
*/

/**
* @}
*/

/**
* @}
*/


/**
* @}
*/


/**
* @}
*/

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

#include "config.h"
#include "usbh_hid_mouse.h"

static int
readbits(void *ptr, uint startbit, uint bits)
{
    uint byte = startbit / 8;
    uint bitoff = startbit % 8;
    uint mask = BIT(bits) - 1;
    uint val = ((*(uint *) ((uintptr_t) ptr + byte)) >> bitoff) & mask;

    if (val & BIT(bits - 1))
        val |= (0 - BIT(bits));  // Sign-extend negative

    return (val);
}

/*
 * readbits_joy() converts joystick values from the retronicdesign.com
 *                Atari C64 Amiga Joystick v3.2       ID e501.0810
 */
static int
readbits_joy(void *ptr, uint startbit, uint bits)
{
    int val = readbits(ptr, startbit, bits);
    switch (val) {
        case 0:
            val = -1;
            break;
        case -128:
            val = 0;
            break;
        case -1:
            val = 1;
            break;
    }
    return (val);
}

static uint32_t
readbits_buttons(void *ptr, HID_RDescTypeDef *rd)
{
    uint button;
    uint32_t buttons = 0;
    uint num_buttons = rd->num_buttons;

    if (num_buttons > ARRAY_SIZE(rd->pos_button))
        num_buttons = ARRAY_SIZE(rd->pos_button);

    for (button = 0; button < num_buttons; button++) {
        if ((button < ARRAY_SIZE(rd->pos_button)) &&
            readbits(ptr, rd->pos_button[button], 1))
            buttons |= BIT(button);
    }
    return (buttons);
}

/**
  * @brief  USBH_HID_DecodeReport
  *         The function gets and decodes mouse and generic data.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_DecodeReport(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle, HID_TypeTypeDef devtype, HID_MISC_Info_TypeDef *report_info)
{
    HID_RDescTypeDef *rd = &HID_Handle->HID_RDesc;
    uint32_t          report_data[4];
    uint button;

    if (HID_Handle == NULL)
        return USBH_FAIL;

    if (HID_Handle->length == 0U)
        return USBH_FAIL;

    memset(&report_data, 0, sizeof (report_data));

    /* Fill report */
    if (USBH_HID_FifoRead(&HID_Handle->fifo, &report_data,
                          HID_Handle->length) ==  HID_Handle->length) {
        uint cur;
        uint8_t id = report_data[0];
        memset(report_info, 0, sizeof (*report_info));
        report_info->usage = rd->usage;

        if (((rd->id_mouse != 0) && (rd->id_mouse == id)) ||
            ((rd->id_mouse == 0) && (devtype == HID_MOUSE))) {
is_mouse:
            dprintf(DF_USB_DECODE_MOUSE, "\n%08lx %08lx ",
                    report_data[0], report_data[1]);

            report_info->buttons = readbits_buttons(report_data, rd);
            report_info->x = readbits(report_data, rd->pos_x, rd->bits_x);
            report_info->y = readbits(report_data, rd->pos_y, rd->bits_y);
            if (rd->bits_wheel != 0) {
                report_info->wheel = readbits(report_data, rd->pos_wheel,
                                              rd->bits_wheel);
            }
            if (rd->bits_ac_pan != 0) {
                report_info->ac_pan = readbits(report_data, rd->pos_ac_pan,
                                               rd->bits_ac_pan);
            }
        } else if ((rd->id_consumer != 0) && (rd->id_consumer == id)) {
            dprintf(DF_USB_DECODE_MISC, "mmkey");
            for (cur = 0; cur < rd->num_keys; cur++) {
                if (rd->pos_key[cur] == 0)
                    continue;
                report_info->mm_key[cur] =
                        readbits(report_data, rd->pos_key[cur], rd->bits_key);
                dprintf(DF_USB_DECODE_MISC, " %x", report_info->mm_key[cur]);
            }
        } else if ((rd->id_sysctl != 0) && (rd->id_sysctl == id)) {
            report_info->sysctl = readbits(report_data,
                                           rd->pos_sysctl, rd->bits_sysctl);
            dprintf(DF_USB_DECODE_MISC, "Sysctl %x", report_info->sysctl);
        } else if ((rd->id_mouse == 0) && (rd->usage == HID_USAGE_MOUSE)) {
            goto is_mouse;
        } else if (rd->usage == HID_USAGE_GAMEPAD) {
            dprintf(DF_USB_DECODE_JOY, "\n%08lx %08lx %08lx ",
                    report_data[0], report_data[1], report_data[2]);
            report_info->buttons = readbits_buttons(report_data, rd);
            report_info->x = readbits_joy(report_data, rd->pos_x, rd->bits_x);
            report_info->y = readbits_joy(report_data, rd->pos_y, rd->bits_y);
        } else {
            dprintf(DF_USB_DECODE_MISC, "ID %x", id);
        }
        cur = 0;
        for (button = 0; button < rd->num_mmbuttons; button++) {
            uint val;
            if (id != rd->id_mmbutton[button])
                continue;
            val = readbits(report_data, rd->pos_mmbutton[button], 1);
            if (val != 0) {
                if (cur == 0)
                    dprintf(DF_USB_DECODE_MISC, "MMKEY");
                dprintf(DF_USB_DECODE_MISC, " %x", rd->val_mmbutton[button]);
                report_info->mm_key[cur++] = rd->val_mmbutton[button];
                if (cur == ARRAY_SIZE(report_info->mm_key))
                    break;
            }
        }

        return USBH_OK;
    }
    return   USBH_FAIL;
}
