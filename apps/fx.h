// Helpers for animation apps (apps/ripple, galaxy, heart, ...): include
// after hhapp.h. Everything is static, so an app keeps only what it uses.
//
// The animations come from the previous firmware, which worked in linear
// light (the value was the PWM duty). hh->set() takes brightness as a
// screen shows it and applies gamma 2.2 itself, so fx_set() goes through
// hh->set_linear(): the animations look as they did. The sine and the
// colours are the firmware's (hh->sin8(), hh->hsv()).
#pragma once
#include "hhapp.h"

// a * b / 255, rounded up a little, as the previous firmware
static inline uint8_t scale8(uint8_t a, uint8_t b) { return (uint8_t)(((unsigned)a * b + 255) >> 8); }

// Slot in linear light (the firmware converts)
static inline void fx_set(const hh_api_t* hh, uint16_t slot, uint8_t linear) { hh->set_linear(slot, linear); }

// RGB LED i of hh->rgb, in linear light
static inline void fx_rgb(const hh_api_t* hh, int i, uint8_t r, uint8_t g, uint8_t b) {
    fx_set(hh, hh->rgb[i].r, r);
    fx_set(hh, hh->rgb[i].g, g);
    fx_set(hh, hh->rgb[i].b, b);
}

// RGB LED i, hue h (1/256 turn), saturation s, value v, in linear light
static inline void fx_hsv(const hh_api_t* hh, int i, uint8_t h, uint8_t s, uint8_t v) {
    uint32_t c = hh->hsv(h, s, v);
    fx_rgb(hh, i, HH_RED(c), HH_GREEN(c), HH_BLUE(c));
}

// Phase in 1/256 turn after ms milliseconds, at per_s 256ths a second.
// Every 256 s it is a whole number of turns again, so ms wraps there and
// the multiplication never overflows (an animation can run all day).
static inline uint8_t fx_phase(uint32_t ms, uint32_t per_s) { return (uint8_t)(ms % 256000u * per_s / 1000u); }

// B brighter, D dimmer, as outside apps: the menu's levels
static inline void fx_brightness_keys(const hh_api_t* hh, uint8_t pressed) {
    if (pressed & HH_BTN_B) hh->step_brightness(1);
    if (pressed & HH_BTN_D) hh->step_brightness(-1);
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
