// Hacker Hotel 2027: "HH" over "2027" for 4 s, then the whole name scrolls
// by. The outline runs marquee lights like a hotel sign; the arms glow
// warm gold. B / D: brighter / dimmer. As in the previous firmware.
#include "hhapp.h"
#include "fx.h"

#define LOGO_MS 4000
#define SCROLL_CPS 10  // columns a second
#define HOTEL_NAME "HACKER HOTEL 2027"

static const hh_api_t* hh;

// Digits 3 wide and 5 high, a row per byte, bit 2 = the left column
static const uint8_t DIGITS[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static void digit(int d, int row, int col) {
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (DIGITS[d][r] >> (2 - c) & 1) fx_set(hh, hh->grid[row + r][col + c], 200);
}

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    fx_brightness_keys(hh, hh->pressed());
    uint32_t ms = hh->millis();
    hh->clear();
    uint32_t scroll_ms = (uint32_t)(hh->text_width(HOTEL_NAME) + HH_GRID_W) * 1000 / SCROLL_CPS, k = ms % (LOGO_MS + scroll_ms);
    if (k < LOGO_MS) {
        hh->draw_text(3, 1, "HH", 255);
        static const int year[4] = {2, 0, 2, 7};
        for (int i = 0; i < 4; i++) digit(year[i], 9, 1 + i * 4);
    } else {
        hh->draw_text(HH_GRID_W - (int)((k - LOGO_MS) * SCROLL_CPS / 1000), 4, HOTEL_NAME, 255);
    }
    uint32_t step = ms * 8 / 1000;  // the marquee: 8 steps a second
    for (int j = 0; j < HH_EDGE_COUNT; j++) fx_set(hh, hh->edge[j], (j + step) % 3 == 0 ? 230 : 25);
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        uint8_t flicker = sin8((uint8_t)(fx_phase(ms, 90) + l->rank * 37 + l->arm * 71));
        uint8_t v = (uint8_t)(120 + flicker / 3);
        fx_rgb(hh, i, scale8(255, v), scale8(140, v), scale8(20, v));
    }
}

static void init(const hh_api_t* api) { hh = api; }

HH_APP(init, tick);
