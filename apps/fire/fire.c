// Fire: heat rises from two rows below the grid and cools as it goes up;
// the lower half of the outline and the arms glow like embers. B / D:
// brighter / dimmer. As in the previous firmware's fire.
#include "hhapp.h"
#include "fx.h"

#define FRAME_MS 33  // 30 frames a second
#define ROWS (HH_GRID_H + 2)

static const hh_api_t* hh;
static uint8_t heat[ROWS][HH_GRID_W];
static uint32_t frame_ms;

static uint8_t byte(void) { return (uint8_t)(hh->random() >> 8); }

static void burn(void) {
    for (int x = 0; x < HH_GRID_W; x++) heat[ROWS - 1][x] = (uint8_t)(128 + byte() / 2);
    for (int y = 0; y < ROWS - 1; y++)
        for (int x = 0; x < HH_GRID_W; x++) {
            int shift = byte() % 3;  // the heat drifts -1, 0 or +1 column as it rises
            int src = heat[y + 1][(x + HH_GRID_W + shift - 1) % HH_GRID_W], cool = 5 + byte() % 34;
            heat[y][x] = (uint8_t)(src > cool ? src - cool : 0);
        }
}

static void tick(uint32_t elapsed_ms) {
    fx_brightness_keys(hh, hh->pressed());
    frame_ms += elapsed_ms;
    if (frame_ms < FRAME_MS) return;  // the LEDs keep the last frame
    frame_ms %= FRAME_MS;
    burn();
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++)
            if (hh->grid[r][c] != HH_NONE) fx_set(hh, hh->grid[r][c], scale8(heat[r][c], heat[r][c]));
    for (int i = 0; i < HH_EDGE_COUNT; i++) {  // the lower half: the further down, the hotter
        uint8_t down = (uint8_t)(255 - sin8((uint8_t)(hh->polar[hh->edge[i]][1] + 64)));
        fx_set(hh, hh->edge[i], scale8(down > 90 ? (uint8_t)(down - 90) : 0, (uint8_t)(byte() / 2 + 64)));
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t down = (uint8_t)(255 - sin8((uint8_t)(hh->polar[hh->rgb[i].r][1] + 64)));
        uint8_t v = scale8(down, (uint8_t)(100 + byte() / 2));
        fx_rgb(hh, i, scale8(255, v), scale8((uint8_t)(40 + v / 4), v), scale8(4, v));
    }
}

static void init(const hh_api_t* api) { hh = api; }

HH_APP(init, tick);
