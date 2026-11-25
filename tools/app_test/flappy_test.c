// apps/flappy on the PC (mock_hh.h): it waits for the first flap, a pilot
// that flaps when it sinks below the next gap passes pipe after pipe,
// without flaps it falls off the grid, a pipe ends it, the best is saved.
#include "mock_hh.h"
#include "../../apps/flappy/flappy.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
}

// pilot() is the app's own: the demo flies with it

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    // The demo flies by itself, stops flapping after 15 .. 30 pipes, saves
    // nothing, and starts over
    CHECK(demo, "it opens with the demo");
    for (int i = 0; i < 200000 && state != OVER; i++) run(33, 0);
    CHECK(state == OVER && score >= 15 && score <= 30, "the demo passes %d pipes, then falls", score);
    CHECK(best == 0 && mock_saves == 0, "a demo game saves nothing");
    run(DEMO_OVER_MS, 0);
    run(10, 0);
    CHECK(demo && state != OVER && score == 0, "and starts over");
    run(10, HH_BTN_A);
    CHECK(demo, "A does not end the demo (it is the menu's)");
    run(10, HH_BTN_D);
    CHECK(!demo && state == READY, "D starts a real game, waiting for its first flap");

    for (int i = 0; i < 100; i++) run(10, 0);
    CHECK(state == READY && t == 0, "it waits for the first flap");
    run(10, HH_BTN_A);
    CHECK(state == READY, "A does not flap (it is the menu's)");
    run(10, HH_BTN_C);
    CHECK(state == FLYING && vy < 0, "C flaps and starts it");

    // The pilot passes 40 pipes (it passes 100 with any of 20 seeds); each
    // passed pipe is a point
    for (int i = 0; i < 100000 && state == FLYING && score < 40; i++) step(pilot());
    CHECK(state == FLYING && score == 40, "a good pilot passes 40 pipes (%d, %s)", score, state == OVER ? "crashed" : "flying");
    uint8_t lit = 0;
    draw();
    for (int i = 0; i < HH_RGB_COUNT; i++) lit += mock_leds[layout_rgb[i].r] != 0;
    CHECK(lit == 35, "40 points fill all five arms (%d LEDs)", lit);

    // Without flaps it falls off the bottom of the grid
    int steps = 0;
    while (state == FLYING && steps < 200) step(false), steps++;
    CHECK(state == OVER && cell(y) >= 13, "without flaps it falls off the grid (row %d, %d steps)", (int)cell(y), steps);
    CHECK(best == 40 && mock_saves == 1, "the best (%d) is saved", best);
    run(10, HH_BTN_B);
    CHECK(state == OVER, "a button right after the end does nothing");
    run(OVER_MS, 0);
    run(10, HH_BTN_D);
    CHECK(state == READY && score == 0, "a second later, a button starts again");

    // Into a pipe: flying straight across the middle, the first pipe with
    // its gap away from the middle ends it
    run(10, HH_BTN_B);
    for (int i = 0; i < 2000 && state == FLYING; i++) {
        bool flap = vy >= 0 && y > 7 * ONE;  // hold row 7
        step(flap);
        for (int k = 0; k < PIPES; k++)
            if (pipes[k].x > BIRD_COL * ONE + ONE) pipes[k].gap = 10;  // the gaps are low, far from row 7
    }
    CHECK(state == OVER && score == 0, "a pipe ends it (score %d)", score);
    CHECK(mock_saves == 1, "a lower score does not save");

    // Flapping at the top: the top of the grid ends it too
    start();
    run(10, HH_BTN_B);
    for (int i = 0; i < 200 && state == FLYING; i++) step(true);
    CHECK(state == OVER && cell(y) <= 1, "flapping all the time hits the top (row %d)", (int)cell(y));

    state = OVER, over_ms = 0;  // a game that just ended
    run(15000, 0);
    CHECK(demo, "15 s after a game, the demo comes back");

    best = 0;
    init(&mock_api);
    CHECK(best == 40, "the best is loaded again (%d)", best);
    return mock_report("flappy");
}
