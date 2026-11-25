#ifndef _USB_DESCRIPTORS_H
#define _USB_DESCRIPTORS_H
// The badge's USB descriptors. usb_config.h includes this file for the USB
// stack (fsusb.c). It needs nothing from the chip, so the host test
// (tools/usb_test) includes it too and checks every length.

#include <stdint.h>

// The vendor request that asks for the Microsoft OS 2.0 descriptor set
// (wIndex 7) and for the WebUSB landing page (wIndex 2). The badge's own
// protocol (webusb_protocol.c) uses the bulk endpoints only, so no other
// control request has this number.
#define USB_VENDOR_CODE 0x01
#define MS_OS_20_INDEX  7  // wIndex: the Microsoft OS 2.0 descriptor set
#define WEBUSB_GET_URL  2  // wIndex: a WebUSB URL descriptor

static const uint8_t device_descriptor[] = {
    18,          // Length
    1,           // Type (Device)
    0x10, 0x02,  // Spec: USB 2.1, so that the host asks for the BOS descriptor
    0x0,         // Device Class
    0x0,         // Device Subclass
    0x0,         // Device Protocol (0 = use config descriptor)
    64,          // Max packet size for EP0
    0x09, 0x12,  // ID Vendor
    0x43, 0xd6,  // ID Product
    // ID Rev. Windows keeps what it learned about a device under its vendor
    // ID, product ID and this number. 2 is new, so a PC that saw the badge
    // without the BOS descriptor asks again.
    0x02, 0x00,
    1,           // Manufacturer string
    2,           // Product string
    3,           // Serial string
    1,           // Max number of configurations
};

/* Configuration Descriptor Set */
static const uint8_t config_descriptor[] = {
    /* Configuration Descriptor */
    0x09,  // bLength
    0x02,  // bDescriptorType
    0x20,
    0x00,  // wTotalLength
    0x01,  // bNumInterfaces (1)
    0x01,  // bConfigurationValue
    0x00,  // iConfiguration
    0xA0,  // bmAttributes: Bus Powered; Remote Wakeup
    0x32,  // MaxPower: 100mA

    /* Interface Descriptor (Vendor) */
    0x09,  // bLength
    0x04,  // bDescriptorType
    0x00,  // bInterfaceNumber
    0x00,  // bAlternateSetting
    0x02,  // bNumEndpoints
    0xff,  // bInterfaceClass: Vendor-specific
    0xff,  // bInterfaceSubClass
    0xff,  // bInterfaceProtocol
    0x00,  // iInterface

    /* Endpoint Descriptor (Bulk IN) */
    0x07,  // bLength
    0x05,  // bDescriptorType
    0x81,  // bEndpointAddress: IN Endpoint 1 (BULK)
    0x02,  // bmAttributes
    0x40,
    0x00,  // wMaxPacketSize
    0x01,  // bInterval

    /* Endpoint Descriptor (Bulk OUT) */
    0x07,  // bLength
    0x05,  // bDescriptorType
    0x02,  // bEndpointAddress: OUT Endpoint 2 (BULK)
    0x02,  // bmAttributes
    0x40,
    0x00,  // wMaxPacketSize
    0x01,  // bInterval
};

// The Microsoft OS 2.0 descriptor set. Windows 8.1 and later read it and
// bind the WinUSB driver to the badge by themselves, so WebUSB and libusb
// can open it without a driver install. The badge has one interface, so the
// set applies to the whole device and has no configuration or function
// subset headers. Windows ignores a set whose lengths are wrong.
#define MS_OS_20_SET_LEN 162
static const struct __attribute__((packed)) {
    uint8_t header[10];
    uint8_t compatible_id[20];
    uint8_t property[8];      // the registry property, up to its name
    uint16_t name[21];        // UTF-16LE, with the terminator
    uint8_t data_length[2];
    uint16_t data[40];        // REG_MULTI_SZ: the GUID, then two terminators
} ms_os_20_set = {
    // Set header: length, type 0, Windows 8.1 (0x06030000), total length
    {10, 0, 0, 0, 0x00, 0x00, 0x03, 0x06, MS_OS_20_SET_LEN, 0},
    // Compatible ID: length, type 3, "WINUSB", no sub-compatible ID
    {20, 0, 3, 0, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    // Registry property: length, type 4, REG_MULTI_SZ (7), name length
    {132, 0, 4, 0, 7, 0, 42, 0},
    u"DeviceInterfaceGUIDs",
    {80, 0},
    // A GUID of the badge's own, made once. Programs find the badge's
    // WinUSB interface by it.
    u"{C1C2396A-3C9D-466E-A29E-5396F1254DE5}\0",
};

// The WebUSB landing page: Chrome can offer it when the badge is plugged in
#define WEBUSB_URL "badge.team/hackerhotel2027-tool/"
static const struct __attribute__((packed)) {
    uint8_t length, type, scheme;
    char url[sizeof(WEBUSB_URL) - 1] __attribute__((nonstring));  // without the terminator
} webusb_url = {3 + sizeof(WEBUSB_URL) - 1, 3, 1, WEBUSB_URL};  // type 3: URL; scheme 1: https://

// The BOS descriptor: the WebUSB and the Microsoft OS 2.0 platform
// capabilities. Each names the vendor request for its descriptor.
#define BOS_LEN (5 + 24 + 28)
static const uint8_t bos_descriptor[] = {
    5, 0x0F, BOS_LEN, 0, 2,  // BOS: length, type, total length, 2 capabilities

    // WebUSB: length, device capability, platform, reserved, UUID
    // 3408B638-09A9-47A0-8BFD-A0768815B665, version 1.00, vendor code,
    // landing page 1 (webusb_url)
    24, 0x10, 0x05, 0,
    0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47, 0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65,
    0x00, 0x01, USB_VENDOR_CODE, 1,

    // Microsoft OS 2.0: length, device capability, platform, reserved, UUID
    // D8DD60DF-4589-4CC7-9CD2-659D9E648A9F, Windows 8.1 (0x06030000), the
    // length of ms_os_20_set, vendor code, no alternate enumeration
    28, 0x10, 0x05, 0,
    0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06, MS_OS_20_SET_LEN, 0, USB_VENDOR_CODE, 0,
};

_Static_assert(sizeof(ms_os_20_set) == MS_OS_20_SET_LEN, "the Microsoft OS 2.0 set's length");
_Static_assert(sizeof(bos_descriptor) == BOS_LEN, "the BOS descriptor's length");

#define STR_MANUFACTURER u"Badge.Team"
#define STR_PRODUCT      u"Hackerhotel 2027 starfish"

struct usb_string_descriptor_struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[];
};
static const struct usb_string_descriptor_struct string0 __attribute__((section(".rodata"))) = {4, 3, {0x0409}};
static const struct usb_string_descriptor_struct string1
    __attribute__((section(".rodata"))) = {sizeof(STR_MANUFACTURER), 3, {STR_MANUFACTURER}};
static const struct usb_string_descriptor_struct string2
    __attribute__((section(".rodata"))) = {sizeof(STR_PRODUCT), 3, {STR_PRODUCT}};

// The serial number: the first 8 bytes of the chip's unique ID, in hex.
// usb_serial_init() writes it at start, before the badge goes on the bus.
#define USB_SERIAL_DIGITS 16
static struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[USB_SERIAL_DIGITS];
} string3;

// uid: the unique ID's bytes, in the order they are in memory
static inline void usb_serial_init(const uint8_t* uid) {
    string3.bLength = sizeof(string3);
    string3.bDescriptorType = 3;
    for (int i = 0; i < USB_SERIAL_DIGITS; i++) {
        unsigned nibble = (uid[i / 2] >> (i & 1 ? 0 : 4)) & 0xF;
        string3.wString[i] = (uint16_t)(nibble < 10 ? '0' + nibble : 'A' - 10 + nibble);
    }
}

// This table defines which descriptor data is sent for each specific
// request from the host (in wValue and wIndex). The vendor request
// USB_VENDOR_CODE looks here too: its wIndex (7 or 2) and wValue (0 or 1)
// never match a standard request's.
static const struct descriptor_list_struct {
    uint32_t lIndexValue;
    const uint8_t* addr;
    uint8_t length;
} descriptor_list[] = {{0x00000100, device_descriptor, sizeof(device_descriptor)},
                       {0x00000200, config_descriptor, sizeof(config_descriptor)},
                       {0x00000F00, bos_descriptor, sizeof(bos_descriptor)},
                       {0x00000300, (const uint8_t*)&string0, 4},
                       {0x04090301, (const uint8_t*)&string1, sizeof(STR_MANUFACTURER)},
                       {0x04090302, (const uint8_t*)&string2, sizeof(STR_PRODUCT)},
                       {0x04090303, (const uint8_t*)&string3, sizeof(string3)},
                       {MS_OS_20_INDEX << 16, (const uint8_t*)&ms_os_20_set, sizeof(ms_os_20_set)},
                       {WEBUSB_GET_URL << 16 | 1, (const uint8_t*)&webusb_url, sizeof(webusb_url)}};
#define DESCRIPTOR_LIST_ENTRIES ((sizeof(descriptor_list)) / (sizeof(struct descriptor_list_struct)))

#endif
