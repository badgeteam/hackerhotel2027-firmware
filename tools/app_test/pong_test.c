// apps/pong on the PC (mock_hh.h): B / D move your paddle within its
// rows, a player who always gets to the ball beats the badge 5 to
// something and the win is saved, a player who stays put loses 0 to 5,
// the ball stays on the grid, a button starts the next game.
#include "mock_hh.h"
#include "../../apps/pong/pong.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

// Plays a game with this player: true = follow the ball, false = stay put.
// Returns the steps it took; counts the ball off the grid
static int off_grid;
static int play(bool follow) {
    int steps = 0;
    while (!over && steps < 100000) {
        uint8_t press = 0;
        if (follow && vx < 0) {
            int32_t want = clamp(cell(y) - 1, TOP, BOTTOM);  // as the badge aims, but never wrong
            if (want < paddle[0]) press = HH_BTN_B;
            else if (want > paddle[0]) press = HH_BTN_D;
        }
        step(press, 0);
        int32_t r = cell(y), c = cell(x);
        if (!serve && c >= 1 && c <= RIGHT - 1 && !on_grid(r, c)) off_grid++;  // behind a paddle it is on its way out
        steps++;
    }
    return steps;
}

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    // B up, D down, within rows 5 .. 10
    step(HH_BTN_B, 0);
    CHECK(paddle[0] == 6, "B moves your paddle up");
    for (int i = 0; i < 20; i++) step(HH_BTN_B, 0);
    CHECK(paddle[0] == TOP, "up to row %d (%d)", TOP, (int)paddle[0]);
    for (int i = 0; i < 30; i++) step(0, HH_BTN_D);
    CHECK(paddle[0] == BOTTOM, "holding D moves it down to row %d", BOTTOM);
    step(HH_BTN_C, 0);
    CHECK(paddle[0] == BOTTOM, "C does nothing");

    // A player who always gets to the ball wins; the win is saved
    start();
    int steps = play(true);
    CHECK(over && points[0] == WIN && points[1] < WIN, "a player at the ball every time wins %d:%d (%d steps)", points[0], points[1], steps);
    CHECK(wins == 1 && mock_saves == 1, "the win is saved (%d)", wins);
    draw();
    CHECK(mock_text[0] == '5' && mock_text[1] == ':', "the grid shows the score: %s", mock_text);
    CHECK(off_grid == 0, "the ball stays on the grid (%d steps off it)", off_grid);

    // The next game: a button a second after the end
    run(10, HH_BTN_B);
    CHECK(over, "a button right after the end does nothing");
    run(OVER_MS, 0);
    run(10, HH_BTN_D);
    CHECK(!over && points[0] == 0 && points[1] == 0, "a second later, a button starts the next game");

    // A player who stays put loses 0 to 5, and nothing is saved
    play(false);
    CHECK(over && points[0] == 0 && points[1] == WIN, "staying put loses %d:%d", points[0], points[1]);
    CHECK(wins == 1 && mock_saves == 1, "a loss does not count");

    // Ten games of the following player: it wins them all, in reasonable time
    int won = 0, longest = 0;
    for (int g = 0; g < 10; g++) {
        start();
        int s = play(true);
        won += points[0] == WIN;
        if (s > longest) longest = s;
    }
    CHECK(won == 10 && off_grid == 0, "ten more games won (%d), the ball always on the grid", won);
    CHECK(longest < 30 * 60 * 5, "no game takes over 5 minutes (the longest %d s)", longest / 30);

    wins = 0;
    init(&mock_api);
    CHECK(wins == 11, "the wins are loaded again (%d)", wins);
    return mock_report("pong");
}
