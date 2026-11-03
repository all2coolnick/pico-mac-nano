/* USB device descriptors for USB_PC mode: a single CDC serial port.
 */

#include "tusb.h"

#define USB_VID                 0x2e8a  /* Raspberry Pi */
#define USB_PID                 0x000a  /* Raspberry Pi Pico SDK CDC */

#define EP_CDC_NOTIF            0x81
#define EP_CDC_OUT              0x02
#define EP_CDC_IN               0x82

enum { STR_LANGID, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_CDC };

static const tusb_desc_device_t desc_device = {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = TUSB_CLASS_MISC,
        .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol    = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor           = USB_VID,
        .idProduct          = USB_PID,
        .bcdDevice          = 0x0100,
        .iManufacturer      = STR_MANUFACTURER,
        .iProduct           = STR_PRODUCT,
        .iSerialNumber      = STR_SERIAL,
        .bNumConfigurations = 1,
};

static const uint8_t desc_config[] = {
        TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 250),
        TUD_CDC_DESCRIPTOR(0, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
};

static const char *const desc_strings[] = {
        [STR_MANUFACTURER] = "pico-mac-nano",
        [STR_PRODUCT]      = "pico-mac-nano",
        [STR_SERIAL]       = "0001",
        [STR_CDC]          = "pico-mac-nano serial",
};

uint8_t const   *tud_descriptor_device_cb(void)
{
        return (uint8_t const *)&desc_device;
}

uint8_t const   *tud_descriptor_configuration_cb(uint8_t index)
{
        (void)index;
        return desc_config;
}

uint16_t const  *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
        static uint16_t buf[32];
        unsigned int len;

        (void)langid;
        if (index == STR_LANGID) {
                buf[1] = 0x0409;
                len = 1;
        } else {
                if (index >= TU_ARRAY_SIZE(desc_strings))
                        return NULL;
                const char *s = desc_strings[index];
                for (len = 0; s[len] && (len < TU_ARRAY_SIZE(buf) - 1); len++)
                        buf[1 + len] = s[len];
        }
        buf[0] = (TUSB_DESC_STRING << 8) | (2 * len + 2);
        return buf;
}
