// apps/breakout on the PC (mock_hh.h): the bricks, the serve, a paddle that
// follows the ball clears waves and the game speeds up, the ball stays on
// the grid, three misses end it, the best is saved, the paddle's moves.
#include "mock_hh.h"
#include "../../apps/breakout/breakout.c"

static void run(uint32_t ms, uint8_t pressed, uint8_t held) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    (void)held;
    tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
}

static int brick_count(void) {
    (void)mock_rgb;
    int n = 0;
    for (int r = 0; r < BRICK_ROWS; r++) n += __builtin_popcount(bricks[r]);
    return n;
}

int main(void) {
    init(&mock_api);

    // The demo: it clears two waves by itself, then lets the ball go,
    // saves nothing, and starts over
    CHECK(demo, "it opens with the demo");
    for (int i = 0; i < 2000000 && !over; i++) run(33, 0, 0);
    CHECK(over && waves == DEMO_WAVES, "the demo clears %d waves, then loses its balls", waves);
    CHECK(best == 0 && mock_saves == 0, "a demo game saves nothing");
    run(DEMO_OVER_MS, 0, 0);
    CHECK(demo && !over && lives == 3 && score == 0, "and starts over");
    run(10, HH_BTN_A, 0);
    CHECK(demo, "A does not end the demo (it is the menu's)");
    run(10, HH_BTN_C, 0);
    CHECK(!demo && !over && score == 0, "C starts a real game");
    int full = brick_count(), cells = 0;
    for (int r = 1; r <= BRICK_ROWS; r++)
        for (int c = 0; c < HH_GRID_W; c++) cells += layout_grid[r][c] != HH_NONE;
    CHECK(full == cells && full == 65, "the bricks fill grid rows 1 .. 5 (%d of %d)", full, cells);

    // The serve: the ball sits on the paddle and goes with it
    CHECK(serve == SERVE_FRAMES, "the ball waits on the paddle");
    step(HH_BTN_C, 0);
    CHECK(paddle == 7 && cell(x) == 7 && cell(y) == PADDLE_ROW - 1, "C moves the paddle, and the ball with it");
    for (int i = 0; i < 10; i++) step(0, HH_BTN_D);
    CHECK(paddle == 10, "holding D moves it every third step (%d)", (int)paddle);
    for (int i = 0; i < 30; i++) step(0, HH_BTN_D);
    CHECK(paddle == 11, "and it stops at the edge of its row");

    // A paddle that follows the ball, a cell to one side or the other now
    // and then, as a player steers: never a miss, waves cleared, faster
    // each time, and the ball never leaves the grid. (Right under the ball
    // every time, it can bounce in the same loop for ever.)
    int32_t first_speed = speed;
    int off_grid = 0, waves = 0, aim = 0;
    for (int i = 0; i < 200000 && waves < 3 && !over; i++) {
        if (vy < 0) aim = (int)(m_random() % 3) - 1;  // pick the next aim while the ball goes up
        paddle = clamp(cell(x) + aim, 5, 11);
        int before = brick_count();
        step(0, 0);
        if (!serve && cell(y) < PADDLE_ROW && !on_grid(cell(y), cell(x))) off_grid++;
        if (brick_count() > before) waves++;
    }
    CHECK(waves == 3 && lives == 3 && !over, "three waves cleared without a miss (%d waves, %d lives)", waves, lives);
    CHECK(score == 3 * full, "a point a brick (%d)", score);
    CHECK(speed == first_speed + 3 * (ONE / 16), "each wave a little faster");
    CHECK(off_grid == 0, "the ball stays on the grid (%d steps off it)", off_grid);

    // Three misses end it; the best is saved
    for (int miss = 0; miss < 3; miss++) {
        for (int i = 0; i < 5000 && lives == 3 - miss && !over; i++) {
            paddle = cell(x) < 8 ? 11 : 5;  // as far from the ball as it gets
            step(0, 0);
        }
    }
    CHECK(over && lives == 0, "after three misses, the end");
    CHECK(best == score && mock_saves == 1, "the best (%d) is saved", best);
    run(10, HH_BTN_B, 0);
    CHECK(over, "a button right after the end does nothing");
    run(OVER_MS, 0, 0);
    run(10, HH_BTN_B, 0);
    CHECK(!over && lives == 3 && score == 0 && brick_count() == full, "a second later, a button starts again");

    // A press between two game steps is not lost
    int p = paddle;
    run(10, HH_BTN_C, 0);
    run(30, 0, 0);
    CHECK(paddle == p - 1, "a short press moves the paddle (%d -> %d)", p, (int)paddle);

    over = true, over_ms = 0;  // a game that just ended
    run(15000, 0, 0);
    CHECK(demo && !over, "15 s after a game, the demo comes back");
    demo = false;

    uint16_t saved = best;
    best = 0;
    init(&mock_api);
    CHECK(best == saved, "the best is loaded again (%d)", best);

    return mock_report("breakout");
}
