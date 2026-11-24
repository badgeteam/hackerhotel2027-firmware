// apps/whack on the PC (mock_hh.h): points by how soon you stop the mole,
// faster every 3 hits, the mole lit from the tip of its arm in, the game
// ending when it reaches the centre or when you press too early, the best
// saved.
#include "mock_hh.h"
#include "../../apps/whack/whack.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

// Ticks of 5 ms until `count` LEDs of the mole are lit
static void until_lit(int count) {
    for (int i = 0; i < 2000 && lit < count && !over; i++) run(5, 0);
}

// The lit LEDs of the arm's line (ranks 0 3 4 5 6), as a bit mask, bit 0 = centre
static int line_lit(int a) {
    static const int rank_place[7] = {0, -1, -1, 1, 2, 3, 4};
    int mask = 0;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t c[3];
        mock_rgb(i, c);
        int k = rank_place[layout_rgb[i].rank];
        if (layout_rgb[i].arm == a && k >= 0 && c[0] > 100) mask |= 1 << k;
    }
    return mask;
}

int main(void) {
    init(&mock_api);

    // Stopped with one LED lit: 5 points; with four: 2
    until_lit(1);
    CHECK(lit == 1 && line_lit(arm) == 0x10, "the mole comes up at the tip of arm %d (%02x)", arm, line_lit(arm));
    uint8_t first_arm = arm;
    run(5, HH_BTN_C);
    CHECK(score == 5 && !over, "stopped at once: 5 points (%d)", score);
    CHECK(arm != first_arm, "the next mole is on another arm");
    run(5, HH_BTN_C);
    CHECK(!over && score == 5, "a second press on the hit (the green flash) does not count");
    until_lit(4);
    CHECK(line_lit(arm) == 0x1e, "with four lit, the line is lit from the tip in (%02x)", line_lit(arm));
    run(5, HH_BTN_D);
    CHECK(score == 7, "stopped with four lit: 2 points (%d)", score);

    // Faster every 3 hits
    uint32_t step = step_ms;
    until_lit(1);
    run(5, HH_BTN_A);
    CHECK(step_ms == step - STEP_FASTER_MS, "after 3 hits, %u ms per LED", (unsigned)step_ms);
    for (int i = 0; i < 40; i++) {
        until_lit(1);
        run(5, HH_BTN_B);
    }
    CHECK(step_ms == LAST_STEP_MS, "it stops getting faster at %u ms", (unsigned)step_ms);
    CHECK(!over && hits == 43, "43 hits, still going");

    // Reaching the centre ends it; the best is saved
    until_lit(5);
    CHECK(over && score == 7 + 5 + 40 * 5, "the mole reached the centre: the end, %d points", score);
    CHECK(best == score && mock_saves == 1, "the best is saved");
    run(5, HH_BTN_B);
    CHECK(over, "a button right after the end does nothing");
    run(OVER_MS, 0);
    run(5, HH_BTN_B);
    CHECK(!over && score == 0 && step_ms == FIRST_STEP_MS, "a second later, a button starts again");

    // Pressing while no mole is up ends it
    run(5, 0);
    CHECK(!lit, "waiting for the next mole");
    run(5, HH_BTN_D);
    CHECK(over, "a press while nothing is lit ends it");
    CHECK(mock_saves == 1, "a lower score does not save");

    best = 0;
    init(&mock_api);
    CHECK(best == 212, "the best is loaded again (%d)", best);

    return mock_report("whack");
}
