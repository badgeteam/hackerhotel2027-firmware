// apps/eye on the PC (mock_hh.h): 30 frames a second from the 5 ms ticks,
// every mood shows what it should (a wink shuts and flashes, sleepy sleeps
// and startles, suspicious squints, dizzy spins its lashes, angry frowns
// red, surprised flashes white without shutting) and ends, B starts a mood,
// and a long run has blinks, a wide pupil and moods by itself.
#include "mock_hh.h"
#include "../../apps/eye/eye.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

static void reset(void) {
    gx = gy = tx = ty = 0;
    focus = blink = mt = t = acc = 0;
    again = false;
    mood = last = MOOD_NONE;
    init(&mock_api);
}

static int px(int r, int c) { return layout_grid[r][c] == HH_NONE ? 0 : mock_leds[layout_grid[r][c]]; }

static int row(int r) {
    int s = 0;
    for (int c = 0; c < HH_GRID_W; c++) s += px(r, c);
    return s;
}

static const char* const names[MOOD_COUNT] = {
    "none", "wink", "roll", "dizzy", "sleepy", "suspicious", "angry", "surprised",
};

static void check_mood(mood_t m) {
    reset();
    for (int i = 0; i < 5; i++) step(), t++, draw();
    blink = focus = 0;
    start(m);
    int shut = 0, squint = 0, white = 0, frown = 0, red = 0, frames = 0;
    bool hue_seen[8] = {false};
    while (mood != MOOD_NONE && frames < 20 * FPS) {
        step(), t++, draw();
        frames++;
        int total = 0;
        for (int r = 0; r < HH_GRID_H; r++) total += row(r);
        if (total < 2000) shut++;  // only the lid line, if anything
        if (total > 2000 && row(0) + row(1) + row(14) + row(15) == 0) squint++;  // open, top and bottom covered
        bool any_white = false;
        for (int i = 0; i < HH_RGB_COUNT; i++) {
            uint8_t c[3];
            mock_rgb(i, c);
            if (c[0] > 150 && c[1] > 150 && c[2] > 150) any_white = true;
        }
        white += any_white;
        uint8_t c[3];
        mock_rgb(0, c);
        hue_seen[(c[0] > c[1]) | (c[1] > c[2]) << 1 | (c[2] > c[0]) << 2] = true;
        if (px(4, 8) == 0 && px(4, 2) > 0) frown++;  // the upper lid lower in the middle than at the side
        if (c[0] > 60 && c[1] < c[0] / 4 && c[2] < c[0] / 4) red++;
    }
    int hues = 0;
    for (int i = 0; i < 8; i++) hues += hue_seen[i];
    CHECK(mood == MOOD_NONE, "%s ends (%d frames)", names[m], frames);
    switch (m) {
        case MOOD_WINK: CHECK(shut >= 8 && white > 0, "wink: shut %d frames, flash %d", shut, white); break;
        case MOOD_SLEEPY: CHECK(shut >= 90 && white > 0, "sleepy: asleep %d frames, startle %d", shut, white); break;
        case MOOD_SUSPICIOUS: CHECK(squint >= 150 && shut == 0, "suspicious: squint %d frames", squint); break;
        case MOOD_DIZZY: CHECK(hues >= 4 && shut == 0, "dizzy: %d lash colours", hues); break;
        case MOOD_ROLL: CHECK(shut == 0, "roll: never shut (%d)", shut); break;
        case MOOD_ANGRY: CHECK(frown >= 100 && red >= 100, "angry: frown %d, red %d", frown, red); break;
        case MOOD_SURPRISED: CHECK(white >= 20 && shut == 0, "surprised: flash %d, shut %d", white, shut); break;
        default: break;
    }
}

int main(void) {
    // 30 frames a second from 5 ms ticks
    reset();
    for (int i = 0; i < 200; i++) run(5, 0);
    CHECK(t == 30, "1 s of ticks: %u frames", (unsigned)t);
    // the menu held it up: it does not run the missed frames
    run(2000, 0);
    CHECK(t == 34, "after a 2 s gap: 4 frames, not 60 (%u)", (unsigned)t);

    // the eye is open: a lit grid, a dark pupil in the middle, lit lashes
    reset();
    for (int i = 0; i < 20; i++) run(5, 0);
    CHECK(row(7) > 1000 && px(7, 8) < 40, "open, with the pupil in the middle (row %d, centre %d)", row(7), px(7, 8));
    uint8_t c[3];
    mock_rgb(0, c);
    CHECK(c[0] + c[1] + c[2] > 100, "the lashes are lit");

    for (int m = 1; m < MOOD_COUNT; m++) check_mood((mood_t)m);

    // B: a mood now, never the same one twice in a row
    reset();
    run(5, HH_BTN_B);
    mood_t first = mood;
    CHECK(first != MOOD_NONE, "B starts a mood");
    run(5, HH_BTN_B);
    CHECK(mood != MOOD_NONE && mood != first, "B again: another mood (%s, then %s)", names[first], names[mood]);

    // five minutes by itself: blinks, the wide pupil, and moods
    reset();
    int blinks = 0, wide = 0, moods = 0;
    mood_t prev = MOOD_NONE;
    for (uint32_t ms = 0; ms < 5 * 60 * 1000; ms += 5) {
        uint32_t b = blink;
        run(5, 0);
        blinks += b == 0 && blink == 1;
        wide += pupil >= PUPIL_WIDE - 2 && mood == MOOD_NONE;
        moods += mood != MOOD_NONE && prev == MOOD_NONE;
        prev = mood;
    }
    CHECK(blinks >= 40, "it blinks by itself (%d in 5 min)", blinks);
    CHECK(wide > 0, "the pupil opens wide now and then");
    CHECK(moods >= 4 && moods <= 10, "%d moods in 5 min", moods);

    return mock_report("eye");
}
