#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

#include "funconfig.h"
#include "ch32fun.h"

#define FUSB_CONFIG_EPS       3 // EP0 + EP1 (bulk IN) + EP2 (bulk OUT)
#define FUSB_SUPPORTS_SLEEP   0
#define FUSB_HID_INTERFACES   0
#define FUSB_HID_USER_REPORTS 0
#define FUSB_IO_PROFILE       0
#define FUSB_USE_HPE          FUNCONF_ENABLE_HPE
#define FUSB_5V_OPERATION     FUNCONF_USE_5V_VDD
#define FUSB_CURSED_TURBO_DMA 0

#include "usb_defines.h"

#define USB_EP_TX 1 // bulk IN
#define USB_EP_RX 2 // bulk OUT

static const uint8_t device_descriptor[] = {
	18, //Length
	1,  //Type (Device)
	0x00, 0x02, //Spec
	0x0, //Device Class
	0x0, //Device Subclass
	0x0, //Device Protocol (0 = use config descriptor)
	64, //Max packet size for EP0
	0x09, 0x12, //ID Vendor
	0x43, 0xd6, //ID Product
	0x01, 0x00, //ID Rev
	1, //Manufacturer string
	2, //Product string
	3, //Serial string
	1, //Max number of configurations
};

/* Configuration Descriptor Set */
static const uint8_t config_descriptor[ ] =
{
	/* Configuration Descriptor */
	0x09,                                                   // bLength
	0x02,                                                   // bDescriptorType
	0x20, 0x00,                                             // wTotalLength
	0x01,                                                   // bNumInterfaces (1)
	0x01,                                                   // bConfigurationValue
	0x00,                                                   // iConfiguration
	0xA0,                                                   // bmAttributes: Bus Powered; Remote Wakeup
	0x32,                                                   // MaxPower: 100mA

	/* Interface Descriptor (Vendor) */
	0x09,                                                   // bLength
	0x04,                                                   // bDescriptorType
	0x00,                                                   // bInterfaceNumber
	0x00,                                                   // bAlternateSetting
	0x02,                                                   // bNumEndpoints
	0xff,                                                   // bInterfaceClass: Vendor-specific
	0xff,                                                   // bInterfaceSubClass
	0xff,                                                   // bInterfaceProtocol
	0x00,                                                   // iInterface

	/* Endpoint Descriptor (Bulk IN) */
	0x07,                                                   // bLength
	0x05,                                                   // bDescriptorType
	0x81,                                                   // bEndpointAddress: IN Endpoint 1 (BULK)
	0x02,                                                   // bmAttributes
	0x40, 0x00,                                             // wMaxPacketSize
	0x01,                                                   // bInterval

	/* Endpoint Descriptor (Bulk OUT) */
	0x07,                                                   // bLength
	0x05,                                                   // bDescriptorType
	0x02,                                                   // bEndpointAddress: OUT Endpoint 2 (BULK)
	0x02,                                                   // bmAttributes
	0x40, 0x00,                                             // wMaxPacketSize
	0x01,                                                   // bInterval
};

#define STR_MANUFACTURER u"ch32fun"
#define STR_PRODUCT      u"hackerhotel2027 LED matrix control"
#define STR_SERIAL       u"001"

struct usb_string_descriptor_struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wString[];
};
const static struct usb_string_descriptor_struct string0 __attribute__((section(".rodata"))) = {
	4,
	3,
	{0x0409}
};
const static struct usb_string_descriptor_struct string1 __attribute__((section(".rodata")))  = {
	sizeof(STR_MANUFACTURER),
	3,
	STR_MANUFACTURER
};
const static struct usb_string_descriptor_struct string2 __attribute__((section(".rodata")))  = {
	sizeof(STR_PRODUCT),
	3,
	STR_PRODUCT
};
const static struct usb_string_descriptor_struct string3 __attribute__((section(".rodata")))  = {
	sizeof(STR_SERIAL),
	3,
	STR_SERIAL
};

// This table defines which descriptor data is sent for each specific
// request from the host (in wValue and wIndex).
const static struct descriptor_list_struct {
	uint32_t	lIndexValue;
	const uint8_t	*addr;
	uint8_t		length;
} descriptor_list[] = {
	{0x00000100, device_descriptor, sizeof(device_descriptor)},
	{0x00000200, config_descriptor, sizeof(config_descriptor)},
	{0x00000300, (const uint8_t *)&string0, 4},
	{0x04090301, (const uint8_t *)&string1, sizeof(STR_MANUFACTURER)},
	{0x04090302, (const uint8_t *)&string2, sizeof(STR_PRODUCT)},
	{0x04090303, (const uint8_t *)&string3, sizeof(STR_SERIAL)}
};
#define DESCRIPTOR_LIST_ENTRIES ((sizeof(descriptor_list))/(sizeof(struct descriptor_list_struct)) )

#endif
