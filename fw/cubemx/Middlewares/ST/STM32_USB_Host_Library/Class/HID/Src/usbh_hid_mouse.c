/**
  ******************************************************************************
  * @file    usbh_hid_mouse.c
  * @author  MCD Application Team
  * @brief   This file is the application layer for USB Host HID Mouse Handling.
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
#include "usbh_hid_mouse.h"
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

/** @defgroup USBH_HID_MOUSE
  * @brief    This file includes HID Layer Handlers for USB Host HID class.
  * @{
  */

/** @defgroup USBH_HID_MOUSE_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup USBH_HID_MOUSE_Private_FunctionPrototypes
  * @{
  */
static USBH_StatusTypeDef USBH_HID_MouseDecode(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle);

/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Variables
  * @{
  */
HID_MOUSE_Info_TypeDef    mouse_info;
uint32_t                  mouse_report_data[2];
uint32_t                  mouse_rx_report_buf[2];

#if 0
/* Structures defining how to access items in a HID mouse report */
/* Access button 1 state. */
static const HID_Report_ItemTypedef prop_b1 =
{
  (uint8_t *)(void *)mouse_report_data + 0, /*data*/
  1,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min value device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access button 2 state. */
static const HID_Report_ItemTypedef prop_b2 =
{
  (uint8_t *)(void *)mouse_report_data + 0, /*data*/
  1,     /*size*/
  1,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min value device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access button 3 state. */
static const HID_Report_ItemTypedef prop_b3 =
{
  (uint8_t *)(void *)mouse_report_data + 0, /*data*/
  1,     /*size*/
  2,     /*shift*/
  0,     /*count (only for array items)*/
  0,     /*signed?*/
  0,     /*min value read can return*/
  1,     /*max value read can return*/
  0,     /*min vale device can report*/
  1,     /*max value device can report*/
  1      /*resolution*/
};

/* Access x coordinate change. */
static const HID_Report_ItemTypedef prop_x =
{
  (uint8_t *)(void *)mouse_report_data + 1, /*data*/
  8,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  1,     /*signed?*/
  0,     /*min value read can return*/
  0xFFFF,/*max value read can return*/
  0,     /*min vale device can report*/
  0xFFFF,/*max value device can report*/
  1      /*resolution*/
};

/* Access y coordinate change. */
static const HID_Report_ItemTypedef prop_y =
{
  (uint8_t *)(void *)mouse_report_data + 2, /*data*/
  8,     /*size*/
  0,     /*shift*/
  0,     /*count (only for array items)*/
  1,     /*signed?*/
  0,     /*min value read can return*/
  0xFFFF,/*max value read can return*/
  0,     /*min vale device can report*/
  0xFFFF,/*max value device can report*/
  1      /*resolution*/
};
#endif


/**
  * @}
  */

#include "config.h"
#define DEBUG_HIDREPORT_DESCRIPTOR
#ifdef DEBUG_HIDREPORT_DESCRIPTOR
#define DPRINTF(...) dprintf(DF_USB_MOUSE_RPT, __VA_ARGS__)
#else
#define DPRINTF(...)
#endif
#define ARRAY_SIZE(x) (int)((sizeof (x) / sizeof ((x)[0])))
#define BIT(x) (1U << (x))

/** @defgroup USBH_HID_MOUSE_Private_Functions
  * @{
  */
static void USBH_HID_Process_HIDReportDescriptor_Mouse(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
    uint pos;
    uint desc_len = HID_Handle->HID_Desc.wItemLength;
    uint coll_depth = 0;
    uint8_t report_size = 0;  // in bits
    uint8_t report_count = 0;
    uint8_t usage_page = 0;
    uint16_t usage = 0;
    uint    bitpos = 0;  // First byte is record
    uint    button = 0;
    uint    temp;
    uint    value;
    HID_RDescTypeDef *rd = &HID_Handle->HID_RDesc;
    uint16_t usage_array[16];
    uint8_t  usage_count = 0;
    const uint8_t *desc = (const uint8_t *) phost->device.Data;
#ifdef DEBUG_HIDREPORT_DESCRIPTOR
    const char *spaces = "                 ";
#endif
    DPRINTF("Mouse: rlen=%x ", desc_len);

    /* Defaults (hopefully these are BOOT MODE mouse settings) */
    rd->pos_button[0] = 8 + 0;
    rd->pos_button[1] = 8 + 1;
    rd->pos_button[2] = 8 + 2;
    rd->pos_x         = 8 + 8;
    rd->pos_y         = 8 + 16;
    rd->pos_wheel     = 8 + 24;
    rd->num_buttons   = 3;
    rd->bits_x = 8;
    rd->bits_y = 8;
    rd->bits_wheel = 8;

    if (desc_len > 255)
        desc_len = 255;  // Let's not go crazy here
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
                        for (temp = 0; temp < report_count; temp++) {
                            if (value & (BIT(0) | BIT(6))) {
                                /* Constant or NULL data */
                            } else if (usage_page == HID_USAGE_PAGE_BUTTON) {
                                DPRINTF(" Button%u=%u", button, bitpos);
                                if (button < ARRAY_SIZE(rd->pos_button))
                                    rd->pos_button[button] = bitpos;
                                button++;
                                rd->num_buttons = button;
                            } else if (temp <= usage_count) {
                                switch (usage_array[temp]) {
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
                                    case HID_USAGE_AC_PAN:
                                        DPRINTF(" AC_PAN=%u", bitpos);
                                        rd->pos_ac_pan = bitpos;
                                        rd->bits_ac_pan = report_size;
                                        break;
                                }
                            }
                            bitpos += report_size;
                        }
#ifdef DEBUG_HIDREPORT_DESCRIPTOR
                        DPRINTF("\n");
#endif
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
                        bitpos += 8;
                        break;
                    case HID_GLOBAL_ITEM_TAG_REPORT_COUNT:
                        report_count = desc[pos];  // bits per report
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
                            case HID_USAGE_AC_PAN:
                                DPRINTF(" AC_PAN");
                                break;
                        }
                        if (usage_count < ARRAY_SIZE(usage_array))
                            usage_array[usage_count++] = usage;
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

/**
  * @brief  USBH_HID_MouseInit
  *         The function init the HID mouse.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_MouseInit(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
  uint32_t i;
  int      timeout;
  dprintf(DF_USB_MOUSE, "USB%u mouseinit class=%lu\n",
          phost->id, phost->ClassNumber);

  /* Request HID Report Descriptor (some mice require 20ms) */
  for (timeout = 100; timeout > 0; timeout--) {
    if (USBH_HID_GetHIDReportDescriptor(phost, HID_Handle->interface, HID_Handle->HID_Desc.wItemLength) == USBH_OK) {
      USBH_HID_Process_HIDReportDescriptor_Mouse(phost, HID_Handle);
      break;
    }
    timer_delay_msec(1);
  }
  if (timeout == 0)
    printf("Mouse get report descriptor failed\n");

  mouse_info.x = 0U;
  mouse_info.y = 0U;
  mouse_info.buttons = 0U;

  for (i = 0U; i < (sizeof(mouse_report_data) / sizeof(uint32_t)); i++)
  {
    mouse_report_data[i] = 0U;
    mouse_rx_report_buf[i] = 0U;
  }

  if (HID_Handle->length > sizeof(mouse_report_data))
  {
    HID_Handle->length = sizeof(mouse_report_data);
  }
  HID_Handle->pData = (uint8_t *)(void *)mouse_rx_report_buf;
  USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, HID_QUEUE_SIZE * sizeof(mouse_report_data));

  return USBH_OK;
}

/**
  * @brief  USBH_HID_GetMouseInfo
  *         The function return mouse information.
  * @param  phost: Host handle
  * @retval mouse information
  */
HID_MOUSE_Info_TypeDef *USBH_HID_GetMouseInfo(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
  if (USBH_HID_MouseDecode(phost, HID_Handle) == USBH_OK)
  {
    return &mouse_info;
  }
  else
  {
    return NULL;
  }
}

/**
  * @brief  USBH_HID_MouseDecode
  *         The function decode mouse data.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_MouseDecode(USBH_HandleTypeDef *phost, HID_HandleTypeDef *HID_Handle)
{
  HID_RDescTypeDef *rd = &HID_Handle->HID_RDesc;
  if (HID_Handle == NULL)
  {
    return USBH_FAIL;
  }

  if (HID_Handle->length == 0U)
  {
    return USBH_FAIL;
  }
  /*Fill report */
  if (USBH_HID_FifoRead(&HID_Handle->fifo, &mouse_report_data, HID_Handle->length) ==  HID_Handle->length)
  {
    uint button;
    uint32_t buttons = 0;
    dprintf(DF_USB_MOUSE, "\n%08lx %08lx ",
            mouse_report_data[0], mouse_report_data[1]);
    for (button = 0; button < rd->num_buttons; button++) {
        if ((button < ARRAY_SIZE(rd->pos_button)) &&
            readbits(mouse_report_data, rd->pos_button[button], 1))
            buttons |= BIT(button);
    }

    mouse_info.buttons = buttons;
    mouse_info.x = readbits(mouse_report_data, rd->pos_x, rd->bits_x);
    mouse_info.y = readbits(mouse_report_data, rd->pos_y, rd->bits_y);
    mouse_info.wheel = readbits(mouse_report_data, rd->pos_wheel,
                                rd->bits_wheel);
    mouse_info.ac_pan = readbits(mouse_report_data, rd->pos_ac_pan,
                                 rd->bits_ac_pan);
    return USBH_OK;
  }
  return   USBH_FAIL;
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
