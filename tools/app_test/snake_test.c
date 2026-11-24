// apps/snake on the PC (mock_hh.h): the demo eats by itself until it
// crashes and starts over, saving nothing; the first button starts a real
// game; C / D turn; a crash saves the best; 15 s later the demo again.
#include "mock_hh.h"
#include "../../apps/snake/snake.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

int main(void) {
    (void)mock_rgb;
    init(&mock_api);
    CHECK(demo && !crashed, "it opens with the demo");
    for (int i = 0; i < 400000 && !crashed; i++) run(10, 0);
    CHECK(crashed && score >= 3, "the demo eats %d dots, then crashes", score);
    CHECK(best == 0 && mock_saves == 0, "a demo game saves nothing");
    run(2000, 0);
    CHECK(demo && !crashed && score == 0, "and starts over");
    run(10, HH_BTN_A);
    CHECK(demo, "A does not end the demo (it is the menu's)");
    run(10, HH_BTN_D);
    CHECK(!demo && !crashed && score == 0 && turn == 0, "D starts a real game, and is no turn");

    // C turns left, D right
    int8_t dx0 = dx, dy0 = dy;
    run(10, HH_BTN_C);
    for (int i = 0; i < 30 && dx == dx0 && dy == dy0; i++) run(10, 0);
    CHECK(dx == dy0 && dy == -dx0, "C turns left");

    // Straight on into the edge: a crash, the score saved if it is a best
    for (int i = 0; i < 2000 && !crashed; i++) run(10, 0);
    CHECK(crashed, "straight on, it crashes into the edge");
    run(15000, 0);
    CHECK(demo && !crashed, "15 s after a crash, the demo comes back");
    return mock_report("snake");
}
