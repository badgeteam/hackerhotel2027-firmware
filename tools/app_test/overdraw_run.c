// One app on the PC (mock_hh.h) for a long time, for the overdraw report:
// left alone (the demo modes, their games and scores), then random button
// taps and holds, with pauses that let a game end and go back to its demo,
// and a restart now and then (init again, what it saved kept). Run with
// MOCK_OVERDRAW=1 (or =all, see mock_hh.h): it fails when the app drew over
// a text in the same tick, and prints the report at exit. run.sh runs it
// for every app; the argument is the minutes of buttons per seed.
//   cc -DAPP_FILE='"../../apps/startris/startris.c"' -DMOCK_APP='"startris"' overdraw_run.c ../../layout.c
#include "mock_hh.h"
#include APP_FILE

#define TICK_MS 5  // as the firmware's main loop

static uint32_t run_rnd = 12345;
static uint32_t next_run_rnd(void) { return (run_rnd = run_rnd * 1664525u + 1013904223u) >> 8; }

static void run(uint32_t ticks, uint8_t pressed_first, uint8_t held) {
    for (uint32_t i = 0; i < ticks; i++) {
        mock_ms += TICK_MS;
        mock_pressed_now = i == 0 ? pressed_first : 0;
        mock_held_now = held;
        tick(TICK_MS);
        mock_tick_end();
    }
    mock_pressed_now = mock_held_now = 0;
}

int main(int argc, char** argv) {
    unsigned minutes = argc > 1 ? (unsigned)atoi(argv[1]) : 20;
    static const uint8_t buttons[4] = {HH_BTN_A, HH_BTN_B, HH_BTN_C, HH_BTN_D};
    for (unsigned seed = 1; seed <= 3; seed++) {
        run_rnd = seed * 7919u, mock_seed = seed;
        init(&mock_api);
        run(10 * 60000 / TICK_MS, 0, 0);  // 10 minutes alone: the demo
        uint32_t end = mock_ms + minutes * 60000u;
        while (mock_ms < end) {
            uint32_t r = next_run_rnd() % 100;
            if (r < 3) {
                run((20000 + next_run_rnd() % 10000) / TICK_MS, 0, 0);  // alone 20 to 30 s: game over, the demo
            } else if (r < 4) {
                init(&mock_api);  // a restart
            } else {
                uint8_t b = buttons[next_run_rnd() % 4];
                if (next_run_rnd() % 4 == 0) b |= buttons[next_run_rnd() % 4];
                uint32_t hold = (b & HH_BTN_A ? 20 + next_run_rnd() % 400 : 20 + next_run_rnd() % 1500) / TICK_MS;  // A under 1 s: no menu
                run(1, b, b);
                run(hold, 0, b);
                run((30 + next_run_rnd() % 600) / TICK_MS, 0, 0);
            }
        }
    }
    return od_overwritten() != 0;
}
