/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * STM32 TinyUSB stack support
 */

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "clock.h"
#include "usb.h"
#include "tinyusb.h"
#include "tinyusb/src/tusb.h"
#include <libopencm3/stm32/f2/rcc.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scb.h>

//  HCD_EVENT_DEVICE_ATTACH = 0
//  HCD_EVENT_DEVICE_REMOVE = 1
//  HCD_EVENT_XFER_COMPLETE = 2

static void open_hid_interface(uint8_t daddr,
                               const tusb_desc_interface_t *desc_itf,
                               uint16_t max_len);
static void free_hid_buf(uint8_t daddr);
static void tinyusb_show_dev(uint daddr);

tusb_rhport_init_t host_init = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_AUTO   // or TUSB_SPEED_AUTO
};

uint32_t SystemCoreClock;

struct {
    uint otg_fs_ints;
    uint otg_hs_ints;
    uint otg_hs_ep1_in_ints;
    uint otg_hs_ep1_out_ints;
    uint otg_hs_wkup_ints;
} usb_stats;

uint32_t
tusb_time_millis_api(void)
{
    uint64_t tick = timer_tick_get();
    return ((uint32_t) (timer_tick_to_usec(tick) / 1000));
}

void
tusb_time_delay_ms_api(unsigned long ms)
{
    timer_delay_msec(ms);
}

void
otg_fs_isr(void)
{
    if (usb_debug_mask & 1)
        printf("Ifs");
    usb_stats.otg_fs_ints++;
    tuh_int_handler(0, true);
}

void
otg_hs_ep1_in_isr(void)
{
    usb_stats.otg_hs_ep1_in_ints++;
    printf("[Ihsepi]");
}

void
otg_hs_ep1_out_isr(void)
{
    usb_stats.otg_hs_ep1_out_ints++;
    printf("[Ihsepo]");
}

void
otg_hs_wkup_isr(void)
{
    usb_stats.otg_hs_wkup_ints++;
    printf("[Ihsw]");
}

void
otg_hs_isr(void)
{
    if (usb_debug_mask & 2)
        printf("Ihs");
    usb_stats.otg_hs_ints++;
    tuh_int_handler(1, true);
}

// Callbacks available
// bool hcd_deinit(uint8_t rhport)
// bool hcd_configure(uint8_t rhport, uint32_t cfg_id, const void* cfg_param)
// void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
// bool hcd_dcache_clean(const void* addr, uint32_t data_size)
// bool hcd_dcache_invalidate(const void* addr, uint32_t data_size)
// bool hcd_dcache_clean_invalidate(const void* addr, uint32_t data_size)
bool hcd_deinit(uint8_t rhport);
bool
hcd_deinit(uint8_t rhport)
{
    printf("USB%u hcd_deinit\n", rhport);
    return (false);
}

uint8_t usbh_get_rhport(uint8_t dev_addr);

#include <class/hid/hid_host.h>
void
usb_show_stats(void)
{
    uint dev;
    printf("FS ints         %u\n"
           "HS ints         %u\n"
           "HS EP1 IN ints  %u\n"
           "HS EP1 OUT ints %u\n"
           "HS WKUP ints    %u\n",
           usb_stats.otg_fs_ints, usb_stats.otg_hs_ints,
           usb_stats.otg_hs_ep1_in_ints, usb_stats.otg_hs_ep1_out_ints,
           usb_stats.otg_hs_wkup_ints);
    for (dev = 0; dev < 16; dev++) {
        uint mounted = tuh_mounted(dev);
        if (mounted) {
            uint16_t vid;
            uint16_t pid;
            uint     idx;
            if (tuh_vid_pid_get(dev, &vid, &pid) == false) {
                vid = 0;
                pid = 0;
            }
// tuh_descriptor_get
// tuh_descriptor_get_device
            printf("Dev%u            USB%u %04x.%04x sp=%x %s\n",
                   dev, usbh_get_rhport(dev),
                   vid, pid, tuh_speed_get(dev),
                   tuh_rhport_is_active(dev) ? "Active" : "Inactive");

#if 0
const usbh_device_t *udev = get_device(dev);
bool hub_port_get_status(uint8_t hub_addr, uint8_t hub_port, void* resp,
                         tuh_xfer_cb_t complete_cb, uintptr_t user_data)
#endif


            for (idx = 0; idx < CFG_TUH_HID; idx++) {
                if (tuh_hid_mounted(dev, idx))
                    printf("Dev%u.%u          HID\n", dev, idx);
            }
        }
    }
}

void
usb_ls(uint verbose)
{
    uint dev;
    for (dev = 0; dev < 16; dev++) {
        if (tuh_mounted(dev)) {
            uint16_t vid;
            uint16_t pid;
            uint     idx;
            if (tuh_vid_pid_get(dev, &vid, &pid) == false) {
                vid = 0;
                pid = 0;
            }
            printf("Dev%u            USB%u %04x.%04x sp=%x %s\n",
                   dev, usbh_get_rhport(dev),
                   vid, pid, tuh_speed_get(dev),
                   tuh_rhport_is_active(dev) ? "Active" : "Inactive");
            for (idx = 0; idx < CFG_TUH_HID; idx++) {
                if (tuh_hid_mounted(dev, idx))
                    printf("Dev%u.%u         HID\n", idx, dev);
            }
            tinyusb_show_dev(dev);
        }
    }
}

void
tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                 uint8_t const *desc_report, uint16_t desc_len)
{
    printf("HID device address = %d, instance = %d is mounted\r\n",
            dev_addr, instance);
}

#if 0
#include <host/usbh_pvt.h>
usbh_class_driver_t const *
usb_app_cb(uint8_t *driver_count)
{
}

usbh_class_driver_t const *usbh_app_driver_get_cb = usb_cb;
#endif

uint64_t timer_hub_poll;
void
tinyusb_poll(void)
{
    tuh_task();
    if (timer_tick_has_elapsed(timer_hub_poll)) {
        timer_hub_poll = timer_tick_plus_msec(1000);
// poll any attached hubs for port changes
    }
}

void
tinyusb_init(void)
{
    SystemCoreClock = clock_get_hclk();

    /*
     * TinyUSB code needs work to support multiple hosts. There
     * are globals which are shared among the two ports.
     * Need check:
     *   usbh.c
     *      _app_driver
     *      _app_driver_count
     *      _usbh_controller
     *      _dev0
     *      _usbh_devices[]
     *            Maybe get_new_address() could be modified to start
     *            alloc from end of device list if it's a hub
     *      _usbh_epbuf
     *      _usbh_q
     *      _usbh_qdef
     *      process_enumeration().failed_count
     *   hub.c
     *      hub_data[]
     *      _hub_buffer[]
     */
    tusb_init(0, &host_init);
//  tusb_init(1, &host_init);
}

void
tinyusb_shutdown(void)
{
    tuh_deinit(0);
    tuh_deinit(1);
}

#define LANGUAGE_ID 0x0409  // English

// Declare for buffer for usb transfer, may need to be in USB/DMA section and
// multiple of dcache line size if dcache is enabled (for some ports).
CFG_TUH_MEM_SECTION struct {
    TUH_EPBUF_TYPE_DEF(tusb_desc_device_t, device);
    TUH_EPBUF_DEF(serial, 64 * sizeof (uint16_t));
    TUH_EPBUF_DEF(buf, 128 * sizeof (uint16_t));
} desc;

static void
_convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len,
                         uint8_t *utf8, size_t utf8_len)
{
    // TODO: Check for runover.
    (void) utf8_len;
    // Get the UTF-16 length out of the data itself.

    for (size_t i = 0; i < utf16_len; i++) {
        uint16_t chr = utf16[i];
        if (chr < 0x80) {
            *utf8++ = chr & 0xffu;
        } else if (chr < 0x800) {
            *utf8++ = (uint8_t) (0xC0 | (chr >> 6 & 0x1F));
            *utf8++ = (uint8_t) (0x80 | (chr >> 0 & 0x3F));
        } else {
            // TODO: Verify surrogate.
            *utf8++ = (uint8_t) (0xE0 | (chr >> 12 & 0x0F));
            *utf8++ = (uint8_t) (0x80 | (chr >> 6 & 0x3F));
            *utf8++ = (uint8_t) (0x80 | (chr >> 0 & 0x3F));
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int
_count_utf8_bytes(const uint16_t *buf, size_t len)
{
    size_t total_bytes = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t chr = buf[i];
        if (chr < 0x80) {
            total_bytes += 1;
        } else if (chr < 0x800) {
            total_bytes += 2;
        } else {
            total_bytes += 3;
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
    return ((int) total_bytes);
}

static void
print_utf16(uint16_t *temp_buf, size_t buf_len)
{
    if ((temp_buf[0] & 0xff) == 0)
        return;  // empty
    size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof (uint16_t);
    size_t utf8_len = (size_t) _count_utf8_bytes(temp_buf + 1, utf16_len);
    _convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t *) temp_buf,
                             sizeof (uint16_t) * buf_len);
    ((uint8_t *) temp_buf)[utf8_len] = '\0';

    printf("%s", (char *) temp_buf);
}

static uint16_t
count_interface_total_len(tusb_desc_interface_t const *desc_itf,
                          uint8_t itf_count, uint16_t max_len)
{
    uint8_t const *p_desc = (uint8_t const *) desc_itf;
    uint16_t len = 0;

    while (itf_count--) {
        // Next on interface desc
        len += tu_desc_len(desc_itf);
        p_desc = tu_desc_next(p_desc);

        while (len < max_len) {
            // return on IAD regardless of itf count
            if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION)
                return (len);

            if ((tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) &&
                ((const tusb_desc_interface_t *)
                 p_desc)->bAlternateSetting == 0) {
                break;
            }

            len += tu_desc_len(p_desc);
            p_desc = tu_desc_next(p_desc);
        }
    }

    return (len);
}

// simple configuration parser and open HID
static void
parse_config_descriptor(uint8_t dev_addr,
                        tusb_desc_configuration_t const * desc_cfg)
{
    uint8_t const *desc_end = ((uint8_t const *) desc_cfg) +
                              tu_le16toh(desc_cfg->wTotalLength);
    uint8_t const *p_desc   = tu_desc_next(desc_cfg);

    // parse each interfaces
    while (p_desc < desc_end) {
        uint8_t assoc_itf_count = 1;

        /*
         * Class will always start with Interface Association (if any)
         * and then Interface descriptor.
         */
        if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION) {
            tusb_desc_interface_assoc_t const *desc_iad =
                            (tusb_desc_interface_assoc_t const *) p_desc;
            assoc_itf_count = desc_iad->bInterfaceCount;

            p_desc = tu_desc_next(p_desc); // next to Interface
        }

        // must be interface from now
        if (tu_desc_type(p_desc) != TUSB_DESC_INTERFACE)
            return;
        tusb_desc_interface_t const *desc_itf;
        desc_itf = (tusb_desc_interface_t const*) p_desc;

        uint16_t const drv_len = count_interface_total_len(desc_itf,
                    assoc_itf_count, (uint16_t) (desc_end-p_desc));

        if (drv_len < sizeof (tusb_desc_interface_t))
            return;  // probably corrupted descriptor

        // only open and listen to HID endpoint IN
        if (desc_itf->bInterfaceClass == TUSB_CLASS_HID) {
            open_hid_interface(dev_addr, desc_itf, drv_len);
        }

        // next Interface or IAD descriptor
        p_desc += drv_len;
    }
}

static void
tinyusb_show_dev(uint daddr)
{
    uint8_t xfer_result;
    printf("tuh_mount_cb %x\n", daddr);

    /* Get Device Descriptor */
    xfer_result = tuh_descriptor_get_device_sync(daddr, &desc.device, 18);
    if (XFER_RESULT_SUCCESS != xfer_result) {
        printf("Failed to get device descriptor\n");
        return;
    }

    printf("Device %u: ID %04x:%04x SN ",
           daddr, desc.device.idVendor, desc.device.idProduct);
    xfer_result = tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID,
                                            desc.serial, sizeof (desc.serial));
    if (XFER_RESULT_SUCCESS != xfer_result) {
        uint16_t *serial = (uint16_t *)(uintptr_t) desc.serial;
        serial[0] = 'n';
        serial[1] = '/';
        serial[2] = 'a';
        serial[3] = 0;
    }
    print_utf16((uint16_t *)(uintptr_t) desc.serial, sizeof (desc.serial) / 2);
    printf("\n");

    printf("Device Descriptor:\n");
    printf("  bLength             %u\n", desc.device.bLength);
    printf("  bDescriptorType     %u\n", desc.device.bDescriptorType);
    printf("  bcdUSB              %04x\n", desc.device.bcdUSB);
    printf("  bDeviceClass        %u\n", desc.device.bDeviceClass);
    printf("  bDeviceSubClass     %u\n", desc.device.bDeviceSubClass);
    printf("  bDeviceProtocol     %u\n", desc.device.bDeviceProtocol);
    printf("  bMaxPacketSize0     %u\n", desc.device.bMaxPacketSize0);
    printf("  idVendor            0x%04x\n", desc.device.idVendor);
    printf("  idProduct           0x%04x\n", desc.device.idProduct);
    printf("  bcdDevice           %04x\n", desc.device.bcdDevice);

    // Get String descriptor using Sync API
    uint16_t temp_buf[128];

    printf("  iManufacturer       %u     ", desc.device.iManufacturer);

    if (tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID,
                        temp_buf, sizeof (temp_buf)) == XFER_RESULT_SUCCESS) {
        print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
    }
    printf("\n");

    printf("  iProduct            %u     ", desc.device.iProduct);
    if (tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, temp_buf,
                                  sizeof (temp_buf)) == XFER_RESULT_SUCCESS) {
        print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
    }
    printf("\n");

    printf("  iSerialNumber       %u     ", desc.device.iSerialNumber);
    if (tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, temp_buf,
                                  sizeof (temp_buf)) == XFER_RESULT_SUCCESS) {
        print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
    }
    printf("\n");

    printf("  bNumConfigurations  %u\r\n", desc.device.bNumConfigurations);

    // Get configuration descriptor with sync API
    if (tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf,
                                sizeof (temp_buf)) == XFER_RESULT_SUCCESS) {
        parse_config_descriptor(daddr, (tusb_desc_configuration_t *) temp_buf);
    }
}

void
tuh_mount_cb(uint8_t daddr)
{
    tinyusb_show_dev(daddr);
}

void
tuh_umount_cb(uint8_t daddr)
{
    printf("tuh_umount_cb %x\n", daddr);
#if 1
    free_hid_buf(daddr);
#endif
}

#define BUF_COUNT   8

uint8_t buf_pool[BUF_COUNT][64];
uint8_t buf_owner[BUF_COUNT] = { 0 }; // device address that owns buffer

// get a HID buffer from pool
static uint8_t *
get_hid_buf(uint8_t daddr)
{
    for (size_t i = 0; i < BUF_COUNT; i++) {
        if (buf_owner[i] == 0) {
            buf_owner[i] = daddr;
            return (buf_pool[i]);
        }
    }

    // out of memory, increase BUF_COUNT
    return (NULL);
}

#if 1
// free all HID buffers owned by device
static void
free_hid_buf(uint8_t daddr)
{
    for (size_t i = 0; i < BUF_COUNT; i++) {
        if (buf_owner[i] == daddr)
            buf_owner[i] = 0;
    }
}
#endif

static void
hid_report_received(tuh_xfer_t *xfer)
{
    /*
     * Note: not all field in xfer is available for use (i.e filled by
     *       tinyusb stack) in callback to save sram.
     *       For instance, xfer->buffer is NULL. We have used user_data
     *       to store buffer when submitted callback
     */
    uint8_t *buf = (uint8_t *) xfer->user_data;

    if (xfer->result == XFER_RESULT_SUCCESS) {
        printf("[dev %u: ep %02x] HID Report:", xfer->daddr, xfer->ep_addr);
        for (uint32_t i = 0; i < xfer->actual_len; i++) {
            if (i % 16 == 0)
                printf("\r\n  ");
            printf("%02X ", buf[i]);
        }
        printf("\r\n");
    }

    // continue to submit transfer, with updated buffer
    // other field remain the same
    xfer->buflen = 64;
    xfer->buffer = buf;

    tuh_edpt_xfer(xfer);
}

static void
open_hid_interface(uint8_t daddr, tusb_desc_interface_t const *desc_itf,
                   uint16_t max_len)
{
    // len = interface + hid + n*endpoints
    const uint16_t drv_len = (uint16_t) (sizeof (tusb_desc_interface_t) +
                                         sizeof (tusb_hid_descriptor_hid_t) +
                                         desc_itf->bNumEndpoints *
                                         sizeof (tusb_desc_endpoint_t));

    // corrupted descriptor
    if (max_len < drv_len)
        return;

    uint8_t const *p_desc = (uint8_t const *) desc_itf;

    // HID descriptor
    p_desc = tu_desc_next(p_desc);
    tusb_hid_descriptor_hid_t const *desc_hid =
        (tusb_hid_descriptor_hid_t const *) p_desc;
    if (HID_DESC_TYPE_HID != desc_hid->bDescriptorType)
        return;

    // Endpoint descriptor
    p_desc = tu_desc_next(p_desc);
    tusb_desc_endpoint_t const * desc_ep =
        (tusb_desc_endpoint_t const *) p_desc;

    for (int i = 0; i < desc_itf->bNumEndpoints; i++) {
        if (TUSB_DESC_ENDPOINT != desc_ep->bDescriptorType)
            return;

        if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) {
            // skip if failed to open endpoint
            if (! tuh_edpt_open(daddr, desc_ep))
                return;

            uint8_t *buf = get_hid_buf(daddr);
            if (!buf)
                return; // out of memory

            tuh_xfer_t xfer =
            {
                .daddr       = daddr,
                .ep_addr     = desc_ep->bEndpointAddress,
                .buflen      = 64,
                .buffer      = buf,
                .complete_cb = hid_report_received,
                .user_data   = (uintptr_t) buf,
                /*
                 * Since buffer is not available in callback, use user data
                 * to store the buffer
                 */
            };

            // submit transfer for this EP
            tuh_edpt_xfer(&xfer);

            printf("Listen to [dev %u: ep %02x]\r\n",
                   daddr, desc_ep->bEndpointAddress);
        }

        p_desc = tu_desc_next(p_desc);
        desc_ep = (tusb_desc_endpoint_t const *) p_desc;
    }
}
