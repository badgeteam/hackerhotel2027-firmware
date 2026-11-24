// Life: Conway's Game of Life on the grid, 5 generations a second. Cells
// that are born flash, those that die fade out; the arms shift colour with
// the population and flash with the births. A board that dies out or
// settles into a still life or a blinker starts again. B / D: brighter /
// dimmer. As in the previous firmware.
#include "hhapp.h"
#include "fx.h"

#define GEN_MS 200

static const hh_api_t* hh;
static uint32_t cells[HH_GRID_H], prev[HH_GRID_H], mask[HH_GRID_H];  // bit c: column c
static uint32_t seen[2];  // the last two boards' hashes: a blinker repeats every second one
static uint32_t gen_ms;

static int count(uint32_t v) {
    int n = 0;
    for (; v; v &= v - 1) n++;  // no libgcc in apps: no popcount
    return n;
}

static int population(void) {
    int n = 0;
    for (int r = 0; r < HH_GRID_H; r++) n += count(cells[r]);
    return n;
}

static void seed(void) {
    for (int r = 0; r < HH_GRID_H; r++) {
        // about 34% alive: (a & b) is 25%, (c & d & e) adds about 9% more
        uint32_t a = hh->random(), b = hh->random(), c = hh->random(), d = hh->random(), e = hh->random();
        cells[r] = prev[r] = ((a & b) | (c & d & e)) & mask[r];
    }
    seen[0] = seen[1] = 0;
}

static void step(void) {
    uint32_t next[HH_GRID_H];
    for (int r = 0; r < HH_GRID_H; r++) {
        next[r] = 0;
        for (int c = 0; c < HH_GRID_W; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = r + dr, cc = c + dc;
                    if ((dr || dc) && rr >= 0 && rr < HH_GRID_H && cc >= 0 && cc < HH_GRID_W) n += cells[rr] >> cc & 1;
                }
            if (n == 3 || ((cells[r] >> c & 1) && n == 2)) next[r] |= 1u << c;
        }
        next[r] &= mask[r];
    }
    uint32_t hash = 0x811C9DC5u;
    for (int r = 0; r < HH_GRID_H; r++) {
        prev[r] = cells[r];
        cells[r] = next[r];
        hash = (hash ^ cells[r]) * 0x01000193u;
    }
    // it died out, or settled into a still life or a blinker: start again
    if (population() < 8 || hash == seen[0] || hash == seen[1]) seed();
    else seen[0] = seen[1], seen[1] = hash;
}

static void tick(uint32_t elapsed_ms) {
    fx_brightness_keys(hh, hh->pressed());
    gen_ms += elapsed_ms;
    if (gen_ms >= GEN_MS) {
        gen_ms -= GEN_MS;
        step();
    }
    int births = 0;
    for (int r = 0; r < HH_GRID_H; r++) {
        uint32_t born = cells[r] & ~prev[r], died = prev[r] & ~cells[r];
        births += count(born);
        for (int c = 0; c < HH_GRID_W; c++) {
            uint8_t v = born >> c & 1 ? 255 : cells[r] >> c & 1 ? 200 : died >> c & 1 ? (uint8_t)(120 - 120 * gen_ms / GEN_MS) : 0;
            if (hh->grid[r][c] != HH_NONE) fx_set(hh, hh->grid[r][c], v);
        }
    }
    for (int i = 0; i < HH_EDGE_COUNT; i++) fx_set(hh, hh->edge[i], 12);
    int pop = population();
    uint32_t flash = (uint32_t)(births * 20 > 200 ? 200 : births * 20) * (GEN_MS - gen_ms) / GEN_MS;
    for (int i = 0; i < HH_RGB_COUNT; i++)
        fx_hsv(hh, i, (uint8_t)(85 + pop * 2 + hh->rgb[i].rank * 8), 255, (uint8_t)(30 + flash));
}

static void init(const hh_api_t* api) {
    hh = api;
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++)
            if (hh->grid[r][c] != HH_NONE) mask[r] |= 1u << c;
    seed();
}

HH_APP(init, tick);
