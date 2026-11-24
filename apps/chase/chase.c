// Star Chase: a light runs round the outline. Press B, C or D when it is
// beside the arm that shows green. Every hit makes the light faster, up to
// a limit; three misses and the game is over. The grid shows the level,
// the dots below it the lives left. After the end, any button starts
// again; the best level is kept on the badge. As in the previous
// firmware's Star Chase.
#include "hhapp.h"

#define FRAME_MS 33   // the game steps 30 times a second
#define ONE 256       // fixed point: 1/256 of an outline LED
#define LIVES 3
#define FX_FRAMES 16  // the flash after a hit or a miss
#define OVER_MS 1000
#define TEXT_Y 3
#define KEYS (HH_BTN_B | HH_BTN_C | HH_BTN_D)

// The outline LEDs (edge[] indices) beside each arm's tip: within 7/256 of
// a turn of the tip's angle, from webui/board_data.js (arm 0 wraps round
// the top, 68 .. 70 and 0 .. 3). Run the numbers again if the board changes.
static const uint8_t ZONE[5][2] = {{68, 3}, {11, 17}, {25, 31}, {40, 45}, {53, 58}};

static const hh_api_t* hh;
static int32_t pos;    // round the outline, 1/256 LED
static int32_t speed;
static uint8_t arm;    // the green one
static uint16_t hits, best;
static uint8_t lives;
static uint8_t fx;     // flash steps left
static bool fx_hit;
static uint32_t t, frame_ms, over_ms;
static bool over;

static bool in_zone(int j) {
    int a = ZONE[arm][0], b = ZONE[arm][1];
    return a <= b ? j >= a && j <= b : j >= a || j <= b;
}

static int head(void) { return (int)(pos / ONE % HH_EDGE_COUNT); }

static void start(void) {
    pos = 0;
    speed = ONE * 3 / 5;  // 18 LEDs a second
    arm = (uint8_t)(hh->random() % 5);
    hits = 0;
    lives = LIVES;
    fx = 0;
    over = false;
}

static void step(uint8_t pressed) {
    t++;
    if (fx) {
        if (--fx == 0 && !lives) {
            over = true;
            over_ms = 0;
            if (hits > best) {
                best = hits;
                hh->save(&best, sizeof best);
            }
        }
        return;
    }
    pos = (pos + speed) % (HH_EDGE_COUNT * ONE);
    if (!(pressed & KEYS)) return;
    if (in_zone(head())) {
        hits++;
        speed = speed * 6 / 5 < ONE * 2 ? speed * 6 / 5 : ONE * 2;  // never so fast that it jumps a zone
        arm = (uint8_t)((arm + 1 + hh->random() % 4) % 5);
        fx_hit = true;
    } else {
        lives--;
        fx_hit = false;
    }
    fx = FX_FRAMES;
}

static void set_rgb(int i, uint8_t r, uint8_t g, uint8_t b) {
    hh->set(hh->rgb[i].r, r);
    hh->set(hh->rgb[i].g, g);
    hh->set(hh->rgb[i].b, b);
}

static void draw(void) {
    hh->clear();
    uint16_t level = over ? hits : (uint16_t)(hits + 1);
    char text[6], *p = text + sizeof text - 1;
    *p = 0;
    do *--p = (char)('0' + level % 10);
    while ((level /= 10) && p > text);
    hh->draw_text((HH_GRID_W - hh->text_width(p)) / 2, TEXT_Y, p, over ? 255 : 200);
    if (over) {
        for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < best ? 40 : 0);
        for (int i = 0; i < HH_RGB_COUNT; i++) {
            uint32_t ph = over_ms / 4 % 256;
            set_rgb(i, (uint8_t)(ph < 128 ? ph * 2 : (255 - ph) * 2), 0, 0);
        }
        return;
    }
    for (int k = 0; k < lives; k++) hh->set(hh->grid[12][6 + 2 * k], 120);

    int h = head();
    for (int j = 0; j < HH_EDGE_COUNT; j++)
        if (in_zone(j)) hh->set(hh->edge[j], 50);
    for (int k = 0; k < 6; k++) hh->set(hh->edge[(h - k + HH_EDGE_COUNT) % HH_EDGE_COUNT], (uint8_t)(255 >> k));  // the light and its tail

    if (fx && fx_hit) {  // a hit: the grid flashes, the arms spin with colour
        for (int r = 0; r < HH_GRID_H; r++)
            for (int c = 0; c < HH_GRID_W; c++)
                if (!hh->get(hh->grid[r][c])) hh->set(hh->grid[r][c], (uint8_t)(fx * 6));
        for (int i = 0; i < HH_RGB_COUNT; i++) {
            uint8_t ph = (uint8_t)(hh->rgb[i].arm * 51 + t * 10), v = (uint8_t)(fx * 15);
            set_rgb(i, ph < 85 ? v : 0, ph >= 85 && ph < 170 ? v : 0, ph >= 170 ? v : 0);
        }
    } else if (fx) {  // a miss: the arms blink red
        for (int i = 0; i < HH_RGB_COUNT; i++) set_rgb(i, fx % 6 < 3 ? 230 : 0, 0, 0);
    } else {          // the green arm pulses
        uint32_t ph = t * 8 % 256, v = 128 + (ph < 128 ? ph : 255 - ph);
        for (int i = 0; i < HH_RGB_COUNT; i++)
            if (hh->rgb[i].arm == arm) set_rgb(i, (uint8_t)(20 * v / 255), (uint8_t)v, (uint8_t)(60 * v / 255));
    }
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (over) {
        over_ms += elapsed_ms;
        if (over_ms >= OVER_MS && pressed) start();
    } else {
        static uint8_t pending;  // a press between two game steps is not lost
        pending |= pressed;
        frame_ms += elapsed_ms;
        while (frame_ms >= FRAME_MS && !over) {
            frame_ms -= FRAME_MS;
            step(pending);
            pending = 0;
        }
    }
    draw();
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&best, sizeof best)) best = 0;
    start();
}

HH_APP(init, tick);
