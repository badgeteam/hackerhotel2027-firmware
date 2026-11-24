// apps/hilo on the PC (mock_hh.h): numbers 1 .. 99, never the same twice in
// a row; right guesses build the streak, D is higher and C lower, a wrong
// one ends the game after the next number shows; the best is saved.
#include "mock_hh.h"
#include "../../apps/hilo/hilo.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

static uint8_t right_key(void) { return next > now ? HH_BTN_D : HH_BTN_C; }
static uint8_t wrong_key(void) { return next > now ? HH_BTN_C : HH_BTN_D; }

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    int bad = 0;
    for (int i = 0; i < 2000; i++) {
        uint8_t a = pick(0), b = pick(a);
        bad += a < 1 || a > 99 || b == a;
    }
    CHECK(bad == 0, "numbers 1 .. 99, the next never the same (%d bad)", bad);

    // Right guesses: the streak grows
    for (int i = 0; i < 12; i++) {
        uint8_t was_next = next;
        run(10, right_key());
        CHECK(showing && right, "guess %d is right", i + 1);
        run(SHOW_MS, 0);
        CHECK(!showing && now == was_next, "then the next number is the one to guess from");
    }
    CHECK(streak == 12 && !over, "a streak of 12");
    run(10, HH_BTN_A);
    run(10, HH_BTN_B);
    CHECK(!showing, "A and B are no guess");

    // A wrong guess: the next number shows, then the end
    run(10, wrong_key());
    CHECK(showing && !right && !over, "a wrong guess shows the next number");
    run(SHOW_MS, 0);
    CHECK(over && streak == 12, "then the game is over");
    CHECK(best == 12 && mock_saves == 1, "the best (%d) is saved", best);
    run(10, HH_BTN_C);
    CHECK(over, "a button right after the end does nothing");
    run(OVER_MS, 0);
    run(10, HH_BTN_C);
    CHECK(!over && streak == 0 && !showing, "a second later, a button starts again (and is no guess)");

    best = 0;
    init(&mock_api);
    CHECK(best == 12, "the best is loaded again (%d)", best);
    return mock_report("hilo");
}
