#ifndef _WEBUSB_PROTOCOL_H
#define _WEBUSB_PROTOCOL_H

// WebUSB control protocol for the LED matrix. See docs/WEBUSB_PROTOCOL.md
// for the on-the-wire format this implements.

#include <stdint.h>

#define APP_MODE_BUILTIN      0 // one of the four button-selectable test patterns is running
#define APP_MODE_CUSTOM       1 // the matrix shows whatever the host last uploaded
#define APP_MODE_FLASH_PATTERN 2 // playing the pattern stored in flash on repeat (see pattern_player.h)

// Owned by led_matrix_test.c (the main loop reads g_app_mode/g_app_pattern
// every tick to decide whether to keep animating a built-in pattern; the
// buttons write them directly). webusb_protocol.c also writes them, since
// host commands are just another way to pick what's on screen.
extern volatile uint8_t g_app_mode;
extern volatile uint8_t g_app_pattern;

// Bring-up debugging (docs/POWER_MANAGEMENT.md): set by CMD_DEBUG_ENTER_STANDBY,
// checked once per main-loop tick in led_matrix_test.c to trigger
// enter_standby() without needing a physical BTN1 press.
extern volatile int g_debug_force_standby;
extern volatile uint32_t g_debug_exti_count;
extern volatile uint32_t g_debug_boot_rstsckr;
extern volatile int g_debug_boot_woke;
extern volatile int g_debug_boot_armed;

// Bring-up debugging: writes a live power_poll() state snapshot to out
// (led_matrix_test.c), returns the number of bytes written. See
// CMD_DEBUG_POWER_STATE.
int Power_DebugState( uint8_t *out );

// Thin wrapper around FSUSBSetup(). Callers outside this file should not
// #include "fsusb.h" directly -- it #includes fsusb.c itself (see
// docs/WEBUSB_PROTOCOL.md), so it must only ever appear in one translation
// unit, and this one already claims that spot.
void WebUSB_Init( void );

// Call once per main loop tick to flush any reply queued by the last
// WebUSBBulkDataOut() call.
void WebUSB_SendPendingReply( void );

#endif
