// apps/maze on the PC (mock_hh.h): every maze is perfect (each cell reached
// one way), the buttons move as a diamond (A up, B right, D down, C left),
// A does not repeat when held, a player keeping a hand on the right-hand
// wall solves all five in time, the first two mazes have no timer (a
// minute without a button ends those), the time runs out from the third
// on, the best is saved.
#include "mock_hh.h"
#include "../../apps/maze/maze.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
}

// Cells reachable from (1, 1), and the openings between cells
static int reachable(int* openings) {
    static uint8_t seen[PX][PX];
    static uint8_t queue[PX * PX][2];
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0, count = 0;
    queue[tail][0] = 1, queue[tail][1] = 1, tail++;
    seen[1][1] = 1;
    while (head < tail) {
        int r = queue[head][0], c = queue[head][1];
        head++, count++;
        for (int d = 0; d < 4; d++)
            if (!is_wall(r + DR[d], c + DC[d]) && !seen[r + 2 * DR[d]][c + 2 * DC[d]]) {
                seen[r + 2 * DR[d]][c + 2 * DC[d]] = 1;
                queue[tail][0] = (uint8_t)(r + 2 * DR[d]), queue[tail][1] = (uint8_t)(c + 2 * DC[d]), tail++;
            }
    }
    *openings = 0;
    for (int r = 1; r < size - 1; r++)
        for (int c = 1; c < size - 1; c++)
            if ((r + c) % 2 && !is_wall(r, c)) (*openings)++;
    return count;
}

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    // The demo walks all five mazes by itself, saves nothing, starts over
    CHECK(demo, "it opens with the demo");
    for (int i = 0; i < 2000000 && !over; i++) run(33, 0);
    CHECK(over && solved == MAZES, "the demo solves %d of %d mazes", solved, MAZES);
    CHECK(best == 0 && mock_saves == 0, "a demo game saves nothing");
    run(DEMO_OVER_MS, 0);
    CHECK(demo && !over && solved == 0, "and starts over");
    run(10, HH_BTN_A);
    CHECK(demo, "A does not end the demo (holding it opens the menu)");
    run(10, HH_BTN_C);
    CHECK(!demo && !over && solved == 0 && pr == 1 && pc == 1, "C starts a real game, at the start");

    // Perfect mazes of 5 .. 13 cells: all cells reachable, and n*n - 1
    // openings (a tree: one way between any two cells)
    for (int s = 0; s < MAZES; s++) {
        for (int k = 0; k < 10; k++) {
            solved = (uint8_t)s;
            make();
            int n = 5 + 2 * s, open, cells = reachable(&open);
            CHECK(cells == n * n && open == n * n - 1, "maze %d: %d of %d cells reached, %d openings", s, cells, n * n, open);
        }
    }

    // The buttons: a diamond round the badge's corners
    start();
    static const uint8_t btn[4] = {HH_BTN_B, HH_BTN_D, HH_BTN_C, HH_BTN_A};  // right, down, left, up
    for (int d = 0; d < 4; d++) {
        // find a cell with an opening that way, and stand there
        bool done = false;
        for (int r = 1; r < size - 1 && !done; r += 2)
            for (int c = 1; c < size - 1 && !done; c += 2)
                if (!is_wall(r + DR[d], c + DC[d]) && !(r + 2 * DR[d] == size - 2 && c + 2 * DC[d] == size - 2)) {
                    pr = (uint8_t)r, pc = (uint8_t)c, done = true;
                }
        uint8_t r0 = pr, c0 = pc;
        step(btn[d], 0);
        CHECK(pr == r0 + 2 * DR[d] && pc == c0 + 2 * DC[d], "button %d moves %s", d, d == 0 ? "right" : d == 1 ? "down" : d == 2 ? "left" : "up");
    }
    // Holding: B repeats, A does not (holding A opens the menu)
    for (int held_key = 0; held_key < 2; held_key++) {
        start();
        uint8_t key = held_key ? HH_BTN_A : HH_BTN_B;
        int d = held_key ? 3 : 0;
        pr = 1, pc = 1;
        for (int r = 1; r < size - 1; r += 2)
            for (int c = 1; c < size - 1; c += 2)
                if (!is_wall(r + DR[d], c + DC[d]) && !is_wall(r + 3 * DR[d], c + 3 * DC[d]) && r + 4 * DR[d] > 0 && c + 4 * DC[d] > 0)
                    pr = (uint8_t)r, pc = (uint8_t)c;  // two steps free that way
        uint8_t r0 = pr, c0 = pc;
        step(key, key);
        for (int i = 0; i < 20; i++) step(0, key);
        int moved = (pr - r0) * DR[d] / 2 + (pc - c0) * DC[d] / 2;
        CHECK(held_key ? moved == 1 : moved >= 2, "holding %s moves %d", held_key ? "A" : "B", moved);
    }

    // A player with a hand on the right-hand wall solves all five in time
    start();
    int dir = 0, steps = 0;
    while (!over && steps < 100000) {
        if (fx) {
            step(0, 0);
            dir = 0;
        } else {
            uint8_t press = 0;
            if (steps % 3 == 0)
                for (int k = 0; k < 4; k++) {
                    int d = (dir + 1 + 3 * k) % 4;  // right, straight, left, back
                    if (!is_wall(pr + DR[d], pc + DC[d])) {
                        dir = d;
                        press = btn[d];
                        break;
                    }
                }
            step(press, 0);
        }
        steps++;
    }
    CHECK(over && solved == MAZES, "the wall follower solves %d of %d mazes", solved, MAZES);
    CHECK(best == MAZES && mock_saves == 1, "the best (%d) is saved", best);

    // The first two mazes have no timer; the next ones about twice the
    // previous firmware's (31, 39, 47 s)
    static const uint32_t secs[MAZES] = {0, 0, 60, 75, 90};
    for (int s = 0; s < MAZES; s++) {
        solved = (uint8_t)s;
        make();
        CHECK(limit == secs[s] * 30, "maze %d: %u s (%u s wanted)", s + 1, limit / 30, secs[s]);
    }

    // Without a timer the outline glows, all of it alike and dim
    run(OVER_MS, 0);
    run(10, HH_BTN_B);
    CHECK(!over && solved == 0, "a button starts again");
    run(FRAME_MS, 0);
    int unlike = 0;
    for (int j = 0; j < HH_EDGE_COUNT; j++) unlike += mock_leds[layout_edge[j]] != mock_leds[layout_edge[0]];
    CHECK(unlike == 0 && mock_leds[layout_edge[0]] >= 25 && mock_leds[layout_edge[0]] <= 60, "no timer: the outline glows (%d, %d unlike)",
          mock_leds[layout_edge[0]], unlike);
    // ... and fades: after gamma it passes many levels, not two or three
    uint8_t levels[256] = {0};
    int nlevels = 0;
    for (int i = 0; i < 128; i++) {
        run(FRAME_MS, 0);
        uint8_t g = layout_gamma[mock_leds[layout_edge[0]]];
        if (!levels[g]++) nlevels++;
    }
    CHECK(nlevels >= 8, "the glow fades through %d levels", nlevels);
    idle = 0;  // that was not the player standing still

    // Standing still in the first maze: no time is up, but a minute
    // without a button ends the game
    for (int i = 0; i < 59 * 30; i++) step(0, 0);
    CHECK(!over, "59 s without moving in the first maze: still on");
    step(0, HH_BTN_D);  // holding a button counts
    for (int i = 0; i < 59 * 30; i++) step(0, 0);
    CHECK(!over, "a held button starts the minute again");
    for (int i = 0; i < 2 * 30 && !over; i++) step(0, 0);
    CHECK(over && solved == 0, "a minute without a button ends it");

    // Standing still in the third maze, the time runs out
    run(OVER_MS, 0);
    run(10, HH_BTN_B);
    solved = 2;
    make();
    for (int i = 0; i < 59 * 30; i++) step(0, HH_BTN_D);
    CHECK(!over, "59 s in the third maze: time left");
    for (int i = 0; i < 2 * 30 && !over; i++) step(0, HH_BTN_D);
    CHECK(over && solved == 2, "60 s: time is up, even while moving");
    CHECK(mock_saves == 1, "a lower score does not save");

    over = true, over_ms = 0;  // a game that just ended
    run(15000, 0);
    CHECK(demo, "15 s after a game, the demo comes back");

    best = 0;
    init(&mock_api);
    CHECK(best == MAZES, "the best is loaded again (%d)", best);
    return mock_report("maze");
}
