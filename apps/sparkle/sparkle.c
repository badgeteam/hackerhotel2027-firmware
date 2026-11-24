// Sparkles on the grid and the outline, a rainbow turning round the arms.
// B: more sparkles, D: fewer.
#include "hhapp.h"

static const hh_api_t* hh;
static uint8_t level[HH_SLOTS];  // the sparkles fading out, per slot
static uint8_t rate = 6;          // new sparkles per 256 ticks per LED, roughly
static uint32_t hue_t;

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
        uint32_t c = hh->hsv((uint8_t)(hue_t / 8 + led->arm * 51 + led->rank * 12), 255, 170);  // two thirds
        hh->set_rgb(i, HH_RED(c), HH_GREEN(c), HH_BLUE(c));
    }
}

HH_APP(init, tick);
