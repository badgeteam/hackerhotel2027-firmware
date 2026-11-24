// Builds one app for anim_test.c under a prefix: APP_PREFIX_init and _tick
// instead of the static init / tick and the hh_header HH_APP() declares.
//   cc -DAPP_PREFIX=ripple -DAPP_FILE='"../../apps/ripple/ripple.c"' -c anim_wrap.c
#include "hhapp.h"
#undef HH_APP
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define HH_APP(i, t)                                         \
    void CAT(APP_PREFIX, _init)(const hh_api_t* api) { i(api); }   \
    void CAT(APP_PREFIX, _tick)(uint32_t ms) { t(ms); }
#include APP_FILE

#ifdef LIFE  // for the test: the population, and a board that died out
uint32_t life_population(void) { return (uint32_t)population(); }
void life_kill(void) {
    for (int r = 0; r < HH_GRID_H; r++) cells[r] = prev[r] = 0;
}
#endif
