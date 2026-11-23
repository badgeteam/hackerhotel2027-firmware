// The emulator's stand-in for the USB stack: webusb_protocol.c hands its
// replies to these, hw.c passes them to the emulator, and the emulator
// calls WebUSBBulkDataOut() with the packets from the web interface.
#pragma once
#include <stdint.h>

#define USB_EP_TX 1

void FSUSBSetup(void);
uint8_t* USBFS_GetEPBufferIfAvailable(int ep);
int USBFS_SendEndpoint(int ep, int len);
void WebUSBBulkDataOut(uint8_t* data, int len);
