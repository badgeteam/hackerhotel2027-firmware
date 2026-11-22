#pragma once
#include <stdint.h>

// WebUSB control protocol for the LED matrix: vendor interface, bulk OUT
// endpoint 2 (commands), bulk IN endpoint 1 (one status reply per command).
//
// OUT, byte 0 = command:
//   0x01 SET_PIXEL      row, col, brightness   set one LED, switch to custom mode
//   0x02 FRAME_CHUNK    offset_lo, offset_hi, data...  write into the staging frame
//                       (pixel index = row * LED_MATRIX_COLS + col)
//   0x03 FRAME_COMMIT                          show the staging frame, switch to custom mode
//   0x05 CLEAR                                 all LEDs off, switch to custom mode
//   0x10 PATTERN_BEGIN                         erase the pattern flash region (slow, wait for the reply)
//   0x11 PATTERN_CHUNK  .ledanim bytes...      append to the pattern flash region, in order
//   0x12 PATTERN_COMMIT                        finish the upload and play it on repeat
//                                              (it also autoplays on every boot from then on)
//   0xFF QUERY                                 no effect, just the status reply
// Anything else is ignored (but still gets a status reply).
//
// IN, 2 bytes: [mode (APP_MODE_*), global brightness]

#define APP_MODE_CUSTOM 1         // showing whatever the host (or the firmware) last wrote
#define APP_MODE_FLASH_PATTERN 2  // playing the pattern stored in flash on repeat

extern volatile uint8_t g_app_mode;

void WebUSB_Init(void);

// Detaches from the bus and powers the USB peripheral down (for standby);
// WebUSB_Init() brings it back.
void WebUSB_Deinit(void);

// Call once per main loop tick: sends the pending reply and starts playback
// of a freshly uploaded pattern.
void WebUSB_Poll(void);
