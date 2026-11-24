// Helpers for animation apps (apps/ripple, galaxy, heart, ...): include
// after hhapp.h. Everything is static, so an app keeps only what it uses.
//
// The animations come from the previous firmware, which worked in linear
// light (the value was the PWM duty). hh->set() takes brightness as a
// screen shows it and applies gamma 2.2 itself, so fx_set() converts:
// the animations look as they did.
#pragma once
#include "hhapp.h"

// A quarter of a sine wave: 128 + 127 sin(2 pi i / 256), i = 0 .. 64
static const uint8_t FX_SIN_Q[65] = {
    128, 131, 134, 137, 140, 144, 147, 150, 153, 156, 159, 162, 165,
    168, 171, 174, 177, 179, 182, 185, 188, 191, 193, 196, 199, 201,
    204, 206, 209, 211, 213, 216, 218, 220, 222, 224, 226, 228, 230,
    232, 234, 235, 237, 239, 240, 241, 243, 244, 245, 246, 248, 249,
    250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255, 255,
};

// 128 + 127 sin(x / 256 of a turn); sin8(x + 64) is the cosine
static inline uint8_t sin8(uint8_t x) {
    uint8_t q = x & 63, half = x & 128;
    uint8_t v = x & 64 ? FX_SIN_Q[64 - q] : FX_SIN_Q[q];
    return half ? (uint8_t)(256 - v) : v;
}

// a * b / 255, rounded up a little, as the previous firmware
static inline uint8_t scale8(uint8_t a, uint8_t b) { return (uint8_t)(((unsigned)a * b + 255) >> 8); }

// Linear light to brightness as a screen shows it: the inverse of gamma 2.2
static const uint8_t FX_LIN[256] = {
      0,  21,  28,  34,  39,  43,  46,  50,  53,  56,  59,  61,  64,  66,  68,  70,
     72,  74,  76,  78,  80,  82,  84,  85,  87,  89,  90,  92,  93,  95,  96,  98,
     99, 101, 102, 103, 105, 106, 107, 109, 110, 111, 112, 114, 115, 116, 117, 118,
    119, 120, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
    136, 137, 138, 139, 140, 141, 142, 143, 144, 144, 145, 146, 147, 148, 149, 150,
    151, 151, 152, 153, 154, 155, 156, 156, 157, 158, 159, 160, 160, 161, 162, 163,
    164, 164, 165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173, 173, 174, 175,
    175, 176, 177, 178, 178, 179, 180, 180, 181, 182, 182, 183, 184, 184, 185, 186,
    186, 187, 188, 188, 189, 190, 190, 191, 192, 192, 193, 194, 194, 195, 195, 196,
    197, 197, 198, 199, 199, 200, 200, 201, 202, 202, 203, 203, 204, 205, 205, 206,
    206, 207, 207, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215,
    215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 223, 223, 224,
    224, 225, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232,
    232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 240,
    240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 247, 247, 248,
    248, 249, 249, 249, 250, 250, 251, 251, 252, 252, 253, 253, 254, 254, 255, 255,
};

static inline void fx_set(const hh_api_t* hh, uint16_t slot, uint8_t linear) { hh->set(slot, FX_LIN[linear]); }

// RGB LED i of hh->rgb, in linear light
static inline void fx_rgb(const hh_api_t* hh, int i, uint8_t r, uint8_t g, uint8_t b) {
    fx_set(hh, hh->rgb[i].r, r);
    fx_set(hh, hh->rgb[i].g, g);
    fx_set(hh, hh->rgb[i].b, b);
}

// Hue h (1/256 turn), saturation s, value v to RGB, as the previous firmware
static inline void hsv(uint8_t h, uint8_t s, uint8_t v, uint8_t out[3]) {
    if (!s) {
        out[0] = out[1] = out[2] = v;
        return;
    }
    uint8_t region = h / 43, rem = (uint8_t)((h - region * 43) * 6);
    uint8_t p = scale8(v, (uint8_t)(255 - s)), q = scale8(v, (uint8_t)(255 - scale8(s, rem)));
    uint8_t t = scale8(v, (uint8_t)(255 - scale8(s, (uint8_t)(255 - rem))));
    static const uint8_t pick[6][3] = {{0, 3, 1}, {2, 0, 1}, {1, 0, 3}, {1, 2, 0}, {3, 1, 0}, {0, 1, 2}};
    uint8_t vals[4] = {v, p, q, t};
    for (int c = 0; c < 3; c++) out[c] = vals[pick[region][c]];
}

static inline void fx_hsv(const hh_api_t* hh, int i, uint8_t h, uint8_t s, uint8_t v) {
    uint8_t c[3];
    hsv(h, s, v, c);
    fx_rgb(hh, i, c[0], c[1], c[2]);
}

// Phase in 1/256 turn after ms milliseconds, at per_s 256ths a second.
// Every 256 s it is a whole number of turns again, so ms wraps there and
// the multiplication never overflows (an animation can run all day).
static inline uint8_t fx_phase(uint32_t ms, uint32_t per_s) { return (uint8_t)(ms % 256000u * per_s / 1000u); }

// B brighter, D dimmer, as outside apps: the menu's levels
static inline void fx_brightness_keys(const hh_api_t* hh, uint8_t pressed) {
    static const uint8_t levels[6] = {8, 16, 32, 64, 128, 255};
    uint8_t b = hh->brightness();
    int k = 0;
    while (k < 5 && levels[k] < b) k++;  // the level at or above the brightness now
    if ((pressed & HH_BTN_B) && k < 5) hh->set_brightness(levels[levels[k] > b ? k : k + 1]);
    if ((pressed & HH_BTN_D) && k > 0) hh->set_brightness(levels[k - 1]);
}

// The mono LEDs: the grid's (row by row) and then the outline's
#define FX_MONO_COUNT (208 + HH_EDGE_COUNT)
static inline uint16_t fx_mono(const hh_api_t* hh, int i) {
    static uint16_t grid_slots[208];
    static int n;
    if (!n)
        for (int r = 0; r < HH_GRID_H; r++)
            for (int c = 0; c < HH_GRID_W; c++)
                if (hh->grid[r][c] != HH_NONE) grid_slots[n++] = hh->grid[r][c];
    return i < 208 ? grid_slots[i] : hh->edge[i - 208];
}
