#include "nametag.h"
#include "ch643_ledpwm.h"
#include "font.h"
#include "layout.h"
#include "persist.h"

#define FRAME_MS 33   // about 30 frames per second
#define TEXT_Y 4      // the text's top row: centred on the 16-row grid
#define FALLBACK "HACKER HOTEL 2027"

static uint32_t s_ms;     // since Nametag_Start
static uint32_t s_accum;  // towards the next frame
static uint32_t s_rand = 0x20275EA5u;

static uint8_t random8(void) {
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return (uint8_t)(s_rand >> 24);
}

static void set_slot(uint16_t slot, uint8_t v) { LEDMatrix_SetPixel(slot / HH_COLS, slot % HH_COLS, v); }
static uint8_t get_slot(uint16_t slot) { return LEDMatrix_GetPixel(slot / HH_COLS, slot % HH_COLS); }

// 0 .. 255 .. 0 over one turn of x
static uint8_t triangle(uint8_t x) { return (uint8_t)(x < 128 ? 2 * x : 2 * (255 - x)); }

// Hue h (0..255 is one turn), full value v, saturation 200 of 255
static void hue(uint8_t h, uint8_t v, uint8_t rgb[3]) {
    uint8_t region = h / 43, rem = (uint8_t)((h - region * 43) * 6);
    uint8_t lo = (uint8_t)(v * 55 / 255);  // v * (255 - saturation)
    uint8_t down = (uint8_t)(v - (v - lo) * rem / 255), up = (uint8_t)(lo + (v - lo) * rem / 255);
    static const uint8_t order[6][3] = {{0, 1, 2}, {1, 0, 2}, {1, 2, 0}, {2, 1, 0}, {2, 0, 1}, {0, 2, 1}};
    uint8_t parts[3] = {v, region & 1 ? down : up, lo};  // the strong, the moving and the weak channel
    for (int c = 0; c < 3; c++) rgb[order[region][c]] = parts[c];
}

static void draw(uint32_t t_ms) {
    LEDMatrix_Clear();

    // The name: still if it fits, else scrolling
    Text_DrawLabel(Persist_Name()[0] ? Persist_Name() : FALLBACK, TEXT_Y, t_ms, 170);

    // A sheen: a diagonal band of full brightness moves over the letters
    int band = (int)(t_ms * 60 / 1000 % 256);
    for (int y = TEXT_Y; y < TEXT_Y + FONT_H; y++)
        for (int x = 0; x < HH_GRID_W; x++) {
            uint16_t slot = layout_grid[y][x];
            if (slot == HH_NONE || !get_slot(slot)) continue;
            int d = ((x + y) * 12 - band) & 255;
            if (d < 40) set_slot(slot, 255);
        }

    // The arms cycle through the colours, each arm a fifth apart, and
    // breathe from the centre out
    uint8_t turn = (uint8_t)(t_ms * 20 / 1000);
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &layout_rgb[i];
        uint8_t rgb[3];
        hue((uint8_t)(turn + led->arm * 51), (uint8_t)(90 + triangle((uint8_t)(turn * 3 + led->rank * 30)) / 3), rgb);
        set_slot(led->r, rgb[0]);
        set_slot(led->g, rgb[1]);
        set_slot(led->b, rgb[2]);
    }

    // The outline sparkles
    for (int j = 0; j < HH_EDGE_COUNT; j++)
        if (random8() < 6) set_slot(layout_edge[j], 200);
}

void Nametag_Start(void) {
    s_ms = 0;
    s_accum = FRAME_MS;  // draw at once
}

void Nametag_Tick(uint32_t elapsed_ms) {
    s_ms += elapsed_ms;
    s_accum += elapsed_ms;
    if (s_accum < FRAME_MS) return;
    s_accum %= FRAME_MS;
    draw(s_ms);
}
