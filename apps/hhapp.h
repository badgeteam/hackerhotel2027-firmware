// Native apps for the starfish badge: the interface between the firmware
// and an app. The firmware and every app include this file, so it is the
// whole contract between the two.
//
// An app is a small C program, linked by apps/app.ld to run from a fixed
// block of RAM (HH_APP_RAM_BASE, HH_APP_RAM_SIZE). The web interface
// stores the .hhapp file (the linked image, see apps/Makefile) in the
// badge's flash. To start it, the firmware copies the image to that RAM
// block, zeroes its .bss, calls init() once, and then calls tick() from
// its main loop, about every 5 ms. tick() must return quickly: USB,
// standby and leaving the app all happen between two ticks.
//
// The app gets all four buttons. Holding A for a second opens the
// badge's menu: the app gets no ticks while it is open, and goes on when
// the menu closes, or stops when another entry is chosen. The web
// interface can also stop it. Hold any button while the badge powers on, and it does not start
// the boot item, so an app that hangs cannot lock the badge up.
//
// The app runs on the firmware's stack (about 5 KB): keep big arrays
// static, not on the stack.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HH_APP_MAGIC 0x50414848u  // "HHAP"
#define HH_API_VERSION 2  // 2: draw_text, text_width

#define HH_APP_RAM_BASE 0x20002000u
#define HH_APP_RAM_SIZE 0x3000u  // 12 KB: the image and its .bss

// The LED matrix: slot = row * HH_COLS + col. Row is the COM line
// (PB0..PB15), col the PWM channel (PA0..PA23).
#define HH_ROWS 16
#define HH_COLS 24
#define HH_SLOTS (HH_ROWS * HH_COLS)
#define HH_NONE 0xFFFFu  // no LED at this place in a layout table

// The layout tables (tools/gen_layout.py)
#define HH_GRID_W 17  // the centre grid, 2 mm pitch; the octagon's corners are HH_NONE
#define HH_GRID_H 16
#define HH_EDGE_COUNT 71  // the outline, clockwise from the top
#define HH_RGB_COUNT 35   // 7 RGB LEDs on each of the 5 arms

// Text (draw_text): a 5 x 8 pixel font (7 rows, and 1 for the descenders of
// g j p q y), HH_FONT_ADVANCE columns per character
#define HH_FONT_W 5
#define HH_FONT_H 8
#define HH_FONT_ADVANCE 6

// Persistent data (survives power-off)
#define HH_NAME_MAX 32      // the owner's name, characters
#define HH_APP_DATA_MAX 28  // what one app can save, bytes

// Buttons, by their silkscreen letter
#define HH_BTN_A (1u << 0)  // SW1, PB20
#define HH_BTN_C (1u << 1)  // SW3, PC4
#define HH_BTN_B (1u << 2)  // SW2, PC5
#define HH_BTN_D (1u << 3)  // SW4, PB18

typedef struct {
    uint16_t r, g, b;  // slots of the red, green and blue LED
    uint8_t arm;       // 0 = top arm, then clockwise
    uint8_t rank;      // 0 = nearest the centre .. 6 = the tip
} hh_rgb_t;

typedef struct {
    uint16_t address;  // 8-bit for standard NEC, 16-bit for extended NEC
    uint8_t command;
    uint8_t repeat;  // NEC repeat code: the sender holds its button
} hh_ir_t;

// What the firmware offers an app. New members only ever go at the end:
// check `version` (or `size`) before using one that came after version 1.
// Firmware older than the app's HH_API_VERSION does not start the app.
typedef struct {
    uint16_t version;  // HH_API_VERSION of the firmware
    uint16_t size;     // sizeof(hh_api_t) in the firmware

    // LEDs, 0 = off .. 255 = full, as a screen shows brightness: the
    // firmware applies gamma 2.2 (and then the global brightness), so 128
    // looks half as bright, not nearly full

    void (*set)(uint16_t slot, uint8_t value);  // ignores HH_NONE and slots out of range
    uint8_t (*get)(uint16_t slot);
    void (*clear)(void);
    void (*set_brightness)(uint8_t brightness);  // the global brightness, 255 = unscaled; kept over power-off
    uint8_t (*brightness)(void);
    const uint16_t (*grid)[HH_GRID_W];  // grid[y][x], y = 0 at the top
    const uint16_t *edge;
    const hh_rgb_t *rgb;

    // Buttons (HH_BTN_*), debounced
    uint8_t (*pressed)(void);  // went down since the previous tick
    uint8_t (*held)(void);     // down now

    // Time and chance
    uint32_t (*millis)(void);  // since the app started, as of this tick
    uint32_t (*random)(void);

    // Infrared (NEC). A frame takes about 68 ms; send() returns false while
    // the previous one is still going out.
    bool (*ir_send)(uint16_t address, uint8_t command);
    bool (*ir_receive)(hh_ir_t *message);  // false when nothing came in

    // System
    void (*standby)(void);  // sleep until a button wakes the badge; returns after
    void (*exit)(void);     // leave the app once this tick returns
    void (*print)(const char *text);  // debug output over SWIO (minichlink -T)

    // Persistent data. It lives in flash, so save only when something
    // changed (a new high score, not every tick): the firmware writes it
    // after the tick, and each write wears the flash a little.
    const char *(*name)(void);  // the owner's name, NUL-terminated; "" if not set
    void (*set_name)(const char *name);
    bool (*load)(void *data, unsigned len);        // false if this app saved nothing yet
    bool (*save)(const void *data, unsigned len);  // len <= HH_APP_DATA_MAX; false if no room

    // Text on the centre grid (HH_API_VERSION 2). draw_text puts text with
    // its top left corner at column x, row y of grid[][]; x and y may be
    // negative or past the edge, for scrolling: what falls outside, or on a
    // corner without an LED, is skipped. Lit pixels get value, the rest is
    // left alone (clear() first for a clean line). All of printable ASCII;
    // other bytes show as a blank. Returns text_width(text): HH_FONT_ADVANCE
    // columns per character, without the blank column after the last one.
    int (*draw_text)(int x, int y, const char *text, uint8_t value);
    int (*text_width)(const char *text);
} hh_api_t;

// The start of every app image, at HH_APP_RAM_BASE. HH_APP() fills it in.
#ifndef HH_EMU
typedef struct {
    uint32_t magic;  // HH_APP_MAGIC
    uint16_t api;    // the HH_API_VERSION the app was built against
    uint16_t flags;  // 0
    void *load_end;  // end of the image: code, read-only data, .data
    void *bss_end;   // end of .bss, which starts at load_end
    void (*init)(const hh_api_t *api);
    void (*tick)(uint32_t elapsed_ms);
} hh_app_header_t;
#else
// The emulator (tools/emu) reads the same 32-bit image on a 64-bit PC:
// the pointers are 32-bit numbers there.
typedef struct {
    uint32_t magic;
    uint16_t api;
    uint16_t flags;
    uint32_t load_end;
    uint32_t bss_end;
    uint32_t init;
    uint32_t tick;
} hh_app_header_t;
#endif

// Declares the app: HH_APP(my_init, my_tick); once, in one of its files.
#define HH_APP(init_fn, tick_fn)                                                     \
    extern char __hh_load_end[], __hh_bss_end[];                                     \
    __attribute__((section(".hh_header"), used)) const hh_app_header_t hh_header = { \
        HH_APP_MAGIC, HH_API_VERSION, 0, __hh_load_end, __hh_bss_end, init_fn, tick_fn}
