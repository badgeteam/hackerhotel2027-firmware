#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

#include "ch32fun.h"
#include "funconfig.h"

#define FUSB_CONFIG_EPS       3  // EP0 + EP1 (bulk IN) + EP2 (bulk OUT)
#define FUSB_SUPPORTS_SLEEP   0
#define FUSB_HID_INTERFACES   0
#define FUSB_HID_USER_REPORTS 0
#define FUSB_IO_PROFILE       0
#define FUSB_USE_HPE          FUNCONF_ENABLE_HPE
#define FUSB_5V_OPERATION     FUNCONF_USE_5V_VDD
#define FUSB_CURSED_TURBO_DMA 0

#include "usb_defines.h"

#define USB_EP_TX 1  // bulk IN
#define USB_EP_RX 2  // bulk OUT

#include "usb_descriptors.h"

#endif
