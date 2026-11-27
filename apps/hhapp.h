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
#define HH_API_VERSION 7  // 2: draw_text, text_width; 3: polar; 4: the helpers; 5: IR packets; 6: shots; 7: raw IR

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

// draw_text / draw_number with this as x: the text centred on the grid
#define HH_CENTER (-32768)

// step_brightness(): the levels B and D step through outside apps (8, 16,
// 32, 64, 128, 255)
#define HH_BRIGHTNESS_LEVELS 6

// The parts of a colour from hsv(): 0xRRGGBB
#define HH_RED(c) ((uint8_t)((c) >> 16))
#define HH_GREEN(c) ((uint8_t)((c) >> 8))
#define HH_BLUE(c) ((uint8_t)(c))

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

// An infrared packet from another badge (ir_send_packet, HH_API_VERSION 5).
// Types 'H' and 'R' are the name swap: outside apps the firmware answers
// them; while an app runs, the app gets them like any other.
#define HH_IR_PACKET_MAX 32
typedef struct {
    uint8_t type;
    uint8_t length;  // 0 .. HH_IR_PACKET_MAX
    uint8_t data[HH_IR_PACKET_MAX];
} hh_ir_packet_t;

// A shot from a Fri3d Camp 2022 Time Blaster or a Hacker Hotel 2025 banana
// (ir_receive_shot, HH_API_VERSION 6), CRC checked. The banana's team is
// 0 red, 1 green, 2 blue, 3 yellow, 4 magenta, 5 light blue; the Time
// Blaster's team is one bit per colour (1 red, 2 green, 4 blue).
#define HH_SHOT_SHOOT 1  // parameter: the channel (0 .. 15)
#define HH_SHOT_HEAL 2   // parameter: the channel
#define HH_SHOT_TEAM 8   // the banana: take this team
#define HH_SHOT_BYTE 10  // the banana: one byte of text, see HH_SHOT_BYTE_OF()
typedef struct {
    uint8_t team;       // 0 .. 7
    uint8_t trigger;    // 1: from a trigger pull
    uint8_t command;    // HH_SHOT_*
    uint8_t parameter;  // 0 .. 15
} hh_ir_shot_t;
#define HH_SHOT_BYTE_OF(s) ((uint8_t)((s).team << 5 | (s).trigger << 4 | (s).parameter))

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
    // the previous one is still going out. Samsung TV remotes come in too,
    // as 16-bit addresses with both bytes the same (0x0707 for a TV).
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
    // From HH_API_VERSION 4, x = HH_CENTER centres the text.
    int (*draw_text)(int x, int y, const char *text, uint8_t value);
    int (*text_width)(const char *text);

    // Where each LED is (HH_API_VERSION 3), for round effects (ripples,
    // spirals): polar[slot][0] is the distance from the grid's centre in
    // 1/4 mm (at most 255; the outline is 21 to 45 mm out), polar[slot][1]
    // the angle in 1/256 turn, clockwise from the top. An RGB LED's three
    // slots have the same place.
    const uint8_t (*polar)[2];

    // Helpers (HH_API_VERSION 4): what many apps need, kept once in the
    // firmware so each app is smaller. See docs/APP_API.md.
    void (*set_rgb)(int index, uint8_t r, uint8_t g, uint8_t b);  // RGB LED rgb[index]; ignores a bad index
    void (*set_linear)(uint16_t slot, uint8_t value);  // value in linear light (PWM duty), as the old firmware
    uint8_t (*sin8)(uint8_t angle);  // 128 + 127 sin(angle / 256 turn); sin8(angle + 64) is the cosine
    uint32_t (*hsv)(uint8_t hue, uint8_t saturation, uint8_t value);  // 0xRRGGBB, see HH_RED()
    uint32_t (*isqrt)(uint32_t n);   // the square root, rounded down
    uint8_t (*atan8)(int x, int y);  // the angle of (x, y), y down, in 1/256 turn clockwise from up, as polar
    int (*draw_number)(int x, int y, int32_t n, uint8_t value);  // as draw_text, n in decimal
    int (*step_brightness)(int dir);  // one level up (dir > 0) or down (< 0); the level now, 0 .. 5

    // Infrared packets, badge to badge (HH_API_VERSION 5): a type and up to
    // HH_IR_PACKET_MAX bytes, with a CRC on air, so what arrives is what
    // was sent. A packet takes 6.75 ms + 1.1 to 2.2 ms per bit, 8 * (length
    // + 3) bits: 0.2 s for 10 bytes. send returns false while the previous
    // packet or NEC frame still goes out. The firmware keeps up to 3
    // packets that came in; receive returns false when there is none.
    // Nothing confirms that a packet arrived: answer it, and send again
    // if no answer comes.
    bool (*ir_send_packet)(uint8_t type, const void *data, uint8_t length);
    bool (*ir_receive_packet)(hh_ir_packet_t *packet);

    // Shots (HH_API_VERSION 6): the 16-bit frames of the Fri3d Camp 2022
    // Time Blaster and the Hacker Hotel 2025 banana. False when none came
    // in; the firmware keeps up to 7. The banana sends a NEC frame (address
    // 0) before each shot, for the bar's LED strips: ir_receive() gets it.
    bool (*ir_receive_shot)(hh_ir_shot_t *shot);
    // Sends a shot (with its CRC) in JVC timing; false while something still
    // goes out (a shot takes about 40 ms). A banana takes shots only in its
    // mode 0, and a shoot only from another team; its 5th hit is game over
    // until its owner holds btn1, so send only when the player asks.
    bool (*ir_send_shot)(const hh_ir_shot_t *shot);

    // Raw infrared (HH_API_VERSION 7), for remote controls that ir_send()
    // does not speak (Sony, RC5, RC6, ...): count marks (carrier on) and
    // spaces, alternating and starting with a mark, us[i] microseconds
    // each, on a carrier of carrier_hz (20000 .. 60000; most remotes use
    // 36000 to 40000). End with a space to keep quiet that long before the
    // next frame. The firmware reads us[] while it sends, so leave it as
    // it is until then, and keep it in the app's own data (a const table
    // or a global), not on the stack: an array in a function is refused.
    // False while something still goes out (this or another ir_send*), or
    // for count 0 or a carrier out of range. When the app stops, a frame
    // still going out stops too.
    bool (*ir_send_raw)(const uint16_t *us, uint16_t count, uint32_t carrier_hz);
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
