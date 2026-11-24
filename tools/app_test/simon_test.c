// apps/simon on the PC (mock_hh.h): the badge shows the sequence on the
// right arms, pressing it back goes on round after round, a wrong button
// or 5 s without one ends it, the best is saved, any button starts again.
#include "mock_hh.h"
#include "../../apps/simon/simon.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

// Ticks of 5 ms until the input state, recording what the badge showed
static int shown[MAX], shown_n;
static void watch_show(void) {
    shown_n = 0;
    int was = -1;  // a step shows as its button lit, with a dark gap after it
    for (int i = 0; i < 4000 && state != INPUT; i++) {
        run(5, 0);
        if (lit >= 0 && lit != was && shown_n < MAX) shown[shown_n++] = lit;
        was = lit;
    }
}

int main(void) {
    init(&mock_api);
    CHECK(demo && state == SHOW && len == 1, "it opens with the demo, showing one step");

    // The demo plays by itself until it slips up, saves nothing, and starts over
    uint8_t slip = demo_slip;
    for (int i = 0; i < 200000 && state != OVER; i++) run(5, 0);
    CHECK(state == OVER && len == slip, "the demo plays to length %d, then slips (%d)", slip, len);
    CHECK(best == 0 && mock_saves == 0, "a demo game saves nothing");
    run(DEMO_OVER_MS, 0);
    CHECK(demo && state == SHOW && len == 1, "and starts over");

    // The first button: a real game, and that press is not a move
    run(5, HH_BTN_C);
    CHECK(!demo && state == SHOW && len == 1 && lit < 0, "the first button starts a real game");

    // Round after round: what it shows is the sequence, pressing it back works
    for (int round = 1; round <= 12; round++) {
        watch_show();
        CHECK(state == INPUT, "round %d: it waits for the buttons", round);
        CHECK(shown_n == len, "round %d: it showed %d steps of %d", round, shown_n, len);
        for (int k = 0; k < shown_n && k < len; k++) CHECK(shown[k] == seq[k], "round %d: step %d shown", round, k);
        for (int k = 0; k < len; k++) {
            run(5, 0);
            run(5, (uint8_t)(1u << seq[k]));
        }
        CHECK(state == GOOD, "round %d: the whole sequence is right", round);
        run(GOOD_MS, 0);
        CHECK(state == SHOW && len == round + 1, "round %d: then one step more", round);
    }

    // While it shows step b, the arm next to that button lights in its colour
    // (A: arm 4 green, C: arm 3 blue, B: arm 1 yellow, D: arm 2 red)
    static const int arm_of[4] = {4, 3, 1, 2};
    static const uint8_t colour_of[4][3] = {{0, 255, 40}, {0, 60, 255}, {255, 200, 0}, {255, 20, 0}};
    for (int b = 0; b < 4; b++) {
        light(b, 100);
        draw();
        int on = 0, wrong = 0;
        for (int i = 0; i < HH_RGB_COUNT; i++) {
            uint8_t c[3];
            mock_rgb(i, c);
            if (layout_rgb[i].rank != 6) continue;  // each arm's tip
            bool lit_here = c[0] || c[1] || c[2];
            if (layout_rgb[i].arm == arm_of[b]) on += lit_here && !memcmp(c, colour_of[b], 3);
            else if (layout_rgb[i].arm != 0) wrong += lit_here;
        }
        CHECK(on == 1 && wrong == 0, "button %d lights arm %d in its colour (on %d, others %d)", b, arm_of[b], on, wrong);
    }
    lit = -1;

    // A wrong button ends it; the rounds done are the best, and saved
    watch_show();
    uint8_t length = len;
    run(5, (uint8_t)(1u << ((seq[0] + 1) % 4)));
    CHECK(state == OVER, "a wrong button ends it");
    CHECK(best == length - 1 && mock_saves == 1, "the best (%d) is saved (%u saves)", best, mock_saves);
    run(5, HH_BTN_B);
    CHECK(state == OVER, "a button right after the end does nothing");
    run(OVER_MS, 0);
    run(5, HH_BTN_B);
    CHECK(state == SHOW && len == 1, "a second later, a button starts again");

    // 5 s without a button ends it too
    watch_show();
    for (int i = 0; i < IDLE_MS / 10 + 1; i++) run(10, 0);
    CHECK(state == OVER, "5 s without a button ends it");
    CHECK(best == length - 1 && mock_saves == 1, "a shorter game does not save");

    // 15 s after a game without a button, the demo again
    run(15000, 0);
    CHECK(demo && state == SHOW, "15 s after a game, the demo comes back");

    // The best comes back after a restart of the app
    best = 0;
    init(&mock_api);
    CHECK(best == length - 1, "the best is loaded again (%d)", best);

    return mock_report("simon");
}
