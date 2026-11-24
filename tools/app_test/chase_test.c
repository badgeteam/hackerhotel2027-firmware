// apps/chase on the PC (mock_hh.h): a press beside the green arm is a hit
// and speeds the light up, never so far that it jumps a zone; a press
// elsewhere is a miss; three misses end it; the best is saved.
#include "mock_hh.h"
#include "../../apps/chase/chase.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

static void settle(void) {
    while (fx && !over) step(0);
}

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    // The zones: each arm's lies where the outline passes its tip, in order
    // round the badge, and none overlaps the next
    for (int a = 0; a < 5; a++) {
        int n = ZONE[a][0] <= ZONE[a][1] ? ZONE[a][1] - ZONE[a][0] + 1 : HH_EDGE_COUNT - ZONE[a][0] + ZONE[a][1] + 1;
        CHECK(n >= 6 && n <= 8, "arm %d's zone is %d LEDs", a, n);
        int mid = (ZONE[a][0] + n / 2) % HH_EDGE_COUNT, expect = a * HH_EDGE_COUNT / 5;
        int off = (mid - expect + HH_EDGE_COUNT) % HH_EDGE_COUNT;
        if (off > HH_EDGE_COUNT / 2) off -= HH_EDGE_COUNT;
        CHECK(off >= -4 && off <= 4, "arm %d's zone is about a fifth of the way round per arm (%d off)", a, off);
    }

    // A player who presses as soon as the light is beside the green arm
    for (int i = 0; i < 200000 && hits < 30 && !over; i++) {
        settle();
        step(in_zone(head()) ? HH_BTN_C : 0);
    }
    CHECK(hits == 30 && lives == LIVES, "30 hits without a miss (%d hits, %d lives)", hits, lives);
    CHECK(speed == ONE * 2, "the light is at its top speed (%d)", (int)speed);

    // At top speed it never jumps over a zone: every lap passes every arm
    settle();
    for (uint8_t a = 0; a < 5; a++) {
        arm = a;
        int seen = 0;
        for (int i = 0; i < HH_EDGE_COUNT; i++) {  // a lap and a bit at 2 LEDs a step
            pos = (pos + speed) % (HH_EDGE_COUNT * ONE);
            seen += in_zone(head());
        }
        CHECK(seen >= 2, "at top speed the light is beside arm %d on %d steps a lap", a, seen);
    }

    // A press away from the green arm is a miss; three end it
    for (int m = 0; m < 3; m++) {
        settle();
        while (in_zone(head())) step(0);
        step(HH_BTN_D);
    }
    CHECK(lives == 0, "three misses (%d lives)", lives);
    settle();
    CHECK(over, "and the game is over");
    CHECK(best == 30 && mock_saves == 1, "the best (%d) is saved", best);
    run(10, HH_BTN_B);
    CHECK(over, "a button right after the end does nothing");
    run(OVER_MS, 0);
    run(10, HH_BTN_B);
    CHECK(!over && hits == 0 && lives == LIVES, "a second later, a button starts again");
    run(40, HH_BTN_A);
    CHECK(lives == LIVES && hits == 0, "A does nothing (it is the menu's)");

    best = 0;
    init(&mock_api);
    CHECK(best == 30, "the best is loaded again (%d)", best);
    return mock_report("chase");
}
