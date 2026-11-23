// Sparkles on the grid and the outline, a rainbow turning round the arms.
// B: more sparkles, D: fewer.
#include "hhapp.h"

static const hh_api_t* hh;
static uint8_t level[HH_SLOTS];  // the sparkles fading out, per slot
static uint8_t rate = 6;          // new sparkles per 256 ticks per LED, roughly
static uint32_t hue_t;

static void hsv(uint8_t h, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t region = h / 43, rem = (uint8_t)((h - region * 43) * 6);
    uint8_t q = (uint8_t)(255 - rem), t = rem;
    switch (region) {
        case 0: *r = 255, *g = t, *b = 0; break;
        case 1: *r = q, *g = 255, *b = 0; break;
        case 2: *r = 0, *g = 255, *b = t; break;
        case 3: *r = 0, *g = q, *b = 255; break;
        case 4: *r = t, *g = 0, *b = 255; break;
        default: *r = 255, *g = 0, *b = q; break;
    }
}

static void spark(uint16_t slot) {
    if (slot == HH_NONE) return;
    if ((hh->random() & 0xFFFF) < rate * 4u) level[slot] = 255;
    hh->set(slot, level[slot]);
    level[slot] = (uint8_t)(level[slot] * 7 / 8);
}

static void init(const hh_api_t* api) { hh = api; }

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if ((pressed & HH_BTN_B) && rate < 64) rate *= 2;
    if ((pressed & HH_BTN_D) && rate > 1) rate /= 2;

    for (int y = 0; y < HH_GRID_H; y++)
        for (int x = 0; x < HH_GRID_W; x++) spark(hh->grid[y][x]);
    for (int i = 0; i < HH_EDGE_COUNT; i++) spark(hh->edge[i]);

    hue_t += elapsed_ms;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &hh->rgb[i];
        uint8_t r, g, b;
        hsv((uint8_t)(hue_t / 8 + led->arm * 51 + led->rank * 12), &r, &g, &b);
        hh->set(led->r, r * 2 / 3);
        hh->set(led->g, g * 2 / 3);
        hh->set(led->b, b * 2 / 3);
    }
}

HH_APP(init, tick);
