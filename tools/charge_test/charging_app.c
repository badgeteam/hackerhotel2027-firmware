// A test app for tools/charge_test: it prints "CHARGING 1" or "CHARGING 0"
// when charging() changes (and once at the start), and lights the centre of
// the grid while the battery charges.
#include "hhapp.h"

static const hh_api_t* hh;
static int last = -1;

static void init(const hh_api_t* api) { hh = api; }

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    int now = hh->charging();
    if (now != last) {
        hh->print(now ? "CHARGING 1\n" : "CHARGING 0\n");
        last = now;
    }
    hh->set(hh->grid[HH_GRID_H / 2][HH_GRID_W / 2], now ? 255 : 0);
}

HH_APP(init, tick);
