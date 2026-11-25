// The animations (apps/ripple, galaxy, heart, twinkle, life, fire, hotel)
// and apps/fx.h on the PC (mock_hh.h): fx.h's sine, colours, gamma and
// phase; the layout's polar table; each animation changes over time, lights
// the grid, the outline and the arms, and takes B / D for the brightness;
// life starts again when it dies out or settles; hotel shows its name.
// Each app is built into this program under a prefix (see run.sh).
#include <math.h>
#include <stdlib.h>
#include "mock_hh.h"
#include "../../apps/fx.h"

#define APP(name)                                     \
    void name##_init(const hh_api_t* api);            \
    void name##_tick(uint32_t ms);
APP(ripple)
APP(galaxy)
APP(heart)
APP(twinkle)
APP(life)
APP(fire)
APP(hotel)
uint32_t life_population(void);
void life_kill(void);

typedef struct {
    const char* name;
    void (*init)(const hh_api_t*);
    void (*tick)(uint32_t);
} anim_t;

static const anim_t ANIMS[] = {
    {"ripple", ripple_init, ripple_tick}, {"galaxy", galaxy_init, galaxy_tick}, {"heart", heart_init, heart_tick},
    {"twinkle", twinkle_init, twinkle_tick}, {"life", life_init, life_tick},     {"fire", fire_init, fire_tick},
    {"hotel", hotel_init, hotel_tick},
};

static void run(const anim_t* a, uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    mock_app = a->name;
    a->tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
}

static int lit_grid(void) {
    int n = 0;
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++) n += layout_grid[r][c] != HH_NONE && mock_leds[layout_grid[r][c]];
    return n;
}
static int lit_edge(void) {
    int n = 0;
    for (int i = 0; i < HH_EDGE_COUNT; i++) n += mock_leds[layout_edge[i]] != 0;
    return n;
}
static int lit_rgb(void) {
    int n = 0;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t c[3];
        mock_rgb(i, c);
        n += c[0] || c[1] || c[2];
    }
    return n;
}

#define brightness_now mock_brightness  // the mock keeps it (step_brightness)

int main(void) {
    hh_api_t api = mock_api;

    // fx.h (the firmware's sin8 and hsv: util_test.c)
    int off = 0;
    for (int v = 1; v < 256; v++) {
        int back = layout_gamma[AppUtil_Linear(layout_gamma, (uint8_t)v)];  // set_linear's value through the gamma: the linear value again
        if (abs(back - v) > 1 + v / 25) off++;
    }
    CHECK(off == 0, "set_linear undoes the firmware's gamma (%d values off)", off);
    CHECK((uint8_t)(fx_phase(255999, 171) + 1) == fx_phase(256000, 171) || fx_phase(255999, 171) == fx_phase(256000, 171) ||
              (uint8_t)(fx_phase(256000, 171) - fx_phase(255999, 171)) <= 1,
          "fx_phase runs on smoothly where ms wraps");
    CHECK(fx_phase(0xFFFFFFF0u, 255) == fx_phase(0xFFFFFFF0u % 256000u, 255), "fx_phase does not overflow after days");

    // The polar table: the grid's middle near 0, the outline 20 .. 47 mm out
    int r_mid = layout_polar[layout_grid[7][8]][0], out_min = 255, out_max = 0;
    for (int i = 0; i < HH_EDGE_COUNT; i++) {
        int r = layout_polar[layout_edge[i]][0];
        if (r < out_min) out_min = r;
        if (r > out_max) out_max = r;
    }
    CHECK(r_mid < 8, "the grid's middle is near the centre (%d / 4 mm)", r_mid);
    CHECK(out_min >= 80 && out_max <= 190, "the outline is %d to %d / 4 mm out", out_min, out_max);
    int ccw = 0;
    for (int i = 1; i < HH_EDGE_COUNT; i++) ccw += layout_polar[layout_edge[i]][1] < layout_polar[layout_edge[i - 1]][1];
    CHECK(ccw <= 1, "the outline goes round clockwise by angle (%d steps back)", ccw);
    for (int a = 0; a < 5; a++) {
        int ang = layout_polar[layout_rgb[a * 7 + 6].r][1], want = a * 256 / 5, d = abs(ang - want);
        if (d > 128) d = 256 - d;
        CHECK(d <= 4, "arm %d's tip is at angle %d (want about %d)", a, ang, want);
    }

    // Each animation: it changes, it lights the grid, outline and arms; B / D set the brightness
    for (unsigned k = 0; k < sizeof ANIMS / sizeof *ANIMS; k++) {
        const anim_t* a = &ANIMS[k];
        mock_ms = 0;
        memset(mock_leds, 0, sizeof mock_leds);
        a->init(&api);
        int grid = 0, edge = 0, rgb = 0, changes = 0;
        uint8_t before[HH_SLOTS];
        for (int f = 0; f < 300; f++) {  // 10 s
            memcpy(before, mock_leds, sizeof before);
            run(a, 33, 0);
            changes += memcmp(before, mock_leds, sizeof before) != 0;
            if (lit_grid() > grid) grid = lit_grid();
            if (lit_edge() > edge) edge = lit_edge();
            if (lit_rgb() > rgb) rgb = lit_rgb();
        }
        CHECK(changes > 100, "%s moves (%d of 300 frames change)", a->name, changes);
        CHECK(grid >= 20 && edge >= 10 && rgb >= 5, "%s lights the grid (%d), the outline (%d) and the arms (%d)", a->name, grid, edge, rgb);
        brightness_now = 255;
        run(a, 10, HH_BTN_D);
        CHECK(brightness_now == 128, "%s: D dims (%d)", a->name, brightness_now);
        for (int i = 0; i < 9; i++) run(a, 10, HH_BTN_D);
        CHECK(brightness_now == 8, "%s: down to the lowest level (%d)", a->name, brightness_now);
        run(a, 10, HH_BTN_B);
        CHECK(brightness_now == 16, "%s: B brightens (%d)", a->name, brightness_now);
    }

    // Life: a board that died out starts again
    life_init(&api);
    life_kill();
    run(&ANIMS[4], 210, 0);
    CHECK(life_population() >= 30, "life starts again after dying out (%u cells)", life_population());
    // and a blinker (a still board would be caught the same way) is caught within a few generations
    int restarts = 0;
    for (int i = 0; i < 3000; i++) {
        uint32_t p = life_population();
        run(&ANIMS[4], 200, 0);
        restarts += life_population() > p + 40;
    }
    CHECK(restarts >= 1, "in 10 minutes life starts again %d times (it settles, and is caught)", restarts);

    // Hotel: HH first, then the name scrolls
    mock_ms = 0;
    hotel_init(&api);
    run(&ANIMS[6], 100, 0);
    CHECK(!strcmp(mock_text, "HH"), "hotel shows HH first (%s)", mock_text);
    run(&ANIMS[6], 4500, 0);
    CHECK(!strcmp(mock_text, "HACKER HOTEL 2027"), "then the name scrolls (%s)", mock_text);

    return mock_report("animations");
}
