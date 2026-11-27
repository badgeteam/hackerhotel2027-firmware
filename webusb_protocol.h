#pragma once
#include <stdbool.h>
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
//
//   The item store (store.h): animations (.ledanim), apps (.hhapp) and texts
//   0x20 STORE_ERASE                           erase every item (slow, wait for the reply);
//                                              the badge shows the name tag
//   0x21 ITEM_BEGIN     type, name...          start a new item after the last one; type 1 =
//                                              animation, 2 = app, 3 = text (nametag.h);
//                                              name up to 20 bytes
//                                              (erases the free space: slow, wait for the reply)
//   0x22 ITEM_CHUNK     data...                append to the new item, in order
//   0x23 ITEM_COMMIT                           finish the item; it starts at once, and from
//                                              then on at every boot
//   0x24 ITEM_INFO      index                  the reply describes item `index`
//   0x25 ITEM_RUN       index, boot            start item `index`; boot = 1 also makes it
//                                              the item that starts at power-on
//   0x26 STOP                                  stop the running animation or app; the badge
//                                              shows the name tag
//   0x27 ITEM_DELETE    index                  remove item `index`; the items after it move
//                                              down one index (slow, wait for the reply).
//                                              If it runs, it stops (the name tag shows).
//
//   Persistent settings (persist.h)
//   0x30 NAME_GET                              the reply carries the owner's name
//   0x31 NAME_SET       name...                set it (up to 32 bytes; written to flash
//                                              from the main loop)
//   0x32 BRIGHTNESS     value                  set the global brightness, 1 .. 255 (byte 1
//                                              of the reply); kept over power-off as B / D
//                                              keep it. The menu's levels are 8, 16, 32,
//                                              64, 128 and 255; any value works. 0 is
//                                              refused (result 1: the LEDs would be dark)
//
//   The protocol before the item store, still accepted: one animation only
//   0x10 PATTERN_BEGIN                         erase every item, then ITEM_BEGIN an animation
//   0x11 PATTERN_CHUNK  .ledanim bytes...      = ITEM_CHUNK
//   0x12 PATTERN_COMMIT                        = ITEM_COMMIT
//
//   Firmware updates (from the web interface, through the boot ROM)
//   0x40 BOOT_ROM                              reply, then reboot into the USB boot ROM
//                                              (as UPDATE in the menu), to flash new firmware
//   0x41 FW_INFO                               the reply carries the firmware's identity
//
//   0xFF QUERY                                 no effect, just the status reply
// Anything else is ignored (but still gets a status reply).
//
// IN, 8 bytes (33 after ITEM_INFO):
//   [0] mode (APP_MODE_*)        [1] global brightness
//   [2] result of the command: 0 ok, else STORE_ERR_* (store.h)
//   [3] number of items          [4] running item (0xFF: none)
//   [5] boot item (0xFF: none)   [6] free 256-byte pages in the store
//   [7] HH_API_VERSION, the app interface this firmware offers
//   after ITEM_INFO, if [2] is 0:
//   [8] type  [9..12] length (little-endian)  [13..32] name (NUL-padded)
//   after NAME_GET: [8..39] the owner's name (NUL-padded) -- 40 bytes
//   after FW_INFO: [8..55] "HH27-STARFISH-FW S<store layout> <git commit>"
//   (NUL-padded) -- 56 bytes

#define APP_MODE_CUSTOM 1         // showing whatever the host (or the firmware) last wrote
#define APP_MODE_FLASH_PATTERN 2  // playing an animation or a text from the store on repeat
#define APP_MODE_APP 3            // running an app from the store
#define APP_MODE_NAMETAG 4        // the name tag (nametag.h): nothing else runs

extern volatile uint8_t g_app_mode;
extern volatile uint8_t g_running_item;  // the store item that plays or runs (FLASH_PATTERN, APP)

void WebUSB_Init(void);

// Detaches from the bus and powers the USB peripheral down (for standby);
// WebUSB_Init() brings it back.
void WebUSB_Deinit(void);

// Call once per main loop tick: runs the command the host sent, if any.
// The USB interrupt only takes the command in; flash work happens here.
void WebUSB_HandleCommand(void);

// Call once per main loop tick, after WebUSB_HandleCommand() and after
// starting a requested item: sends the reply, once the endpoint is free.
void WebUSB_Poll(void);

#define FW_INFO_LEN 48

// True once the host asked for BOOT_ROM and has its reply: the main loop
// then reboots into the boot ROM. Call every pass with the elapsed time.
bool WebUSB_BootRomDue(uint32_t elapsed_ms);

// The item the host asked to start (ITEM_COMMIT, ITEM_RUN), once;
// STORE_NONE if none. The main loop starts it.
uint8_t WebUSB_TakeRunRequest(void);
// The host asked for the factory test (once per request)
bool WebUSB_TakeFactoryRequest(void);
