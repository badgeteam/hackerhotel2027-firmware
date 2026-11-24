// apps/startris on the PC (mock_hh.h): the pieces turn and come in bags of
// seven, the keys move, turn and drop, full rows clear and speed the game
// up, a player that places each piece where the old demo liked it best
// clears lines for a long time, dropping every piece in the middle ends
// the game, the best is saved.
#include "mock_hh.h"
#include "../../apps/startris/startris.c"

static void run(uint32_t ms, uint8_t pressed, uint8_t held) {
    (void)held;
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

static int sorted_key(int32_t c[4][2]) {  // the cell set as a number, order-free
    int k = 0;
    for (int i = 0; i < 4; i++) k |= 1 << (c[i][0] * 4 + c[i][1]);
    return k;
}

// rate() is the app's own: the demo places pieces with it

// Plays one piece with the keys: A to the wanted turn, C / D to the column, B
static void play_piece(void) {
    int best_v = -1000000;
    uint8_t want_rot = 0;
    int32_t want_col = 3;
    for (uint8_t r = 0; r < (kind == O ? 1 : 4); r++)
        for (int32_t col = -2; col < W; col++)
            if (fits(r, 0, col)) {
                int v = rate(r, col);
                if (v > best_v) best_v = v, want_rot = r, want_col = col;
            }
    for (int i = 0; i < 4 && rot != want_rot; i++) step(HH_BTN_A, 0);
    for (int i = 0; i < 12 && pc != want_col; i++) step(pc < want_col ? HH_BTN_D : HH_BTN_C, 0);
    step(HH_BTN_B, 0);
    while (clear_t && !over) step(0, 0);  // the rows blink, then clear
}

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    // The demo: it clears lines by itself, stops trying after 10 .. 20,
    // saves nothing, and starts over
    CHECK(demo, "it opens with the demo");
    for (int i = 0; i < 2000000 && !over; i++) run(33, 0, 0);
    CHECK(over && lines >= 10 && lines <= 22, "the demo clears %d lines, then fills up", lines);
    CHECK(best == 0 && mock_saves == 0, "a demo game saves nothing");
    run(DEMO_OVER_MS, 0, 0);
    CHECK(demo && !over && lines == 0, "and starts over");
    run(10, HH_BTN_A, 0);
    CHECK(demo, "A does not end the demo");
    run(10, HH_BTN_C, 0);
    CHECK(!demo && !over && lines == 0, "C starts a real game");

    // Four turns bring every piece back; O does not turn
    for (uint8_t k = 0; k < 7; k++) {
        int32_t a[4][2], b[4][2];
        cells(k, 0, a);
        cells(k, 4, b);
        CHECK(sorted_key(a) == sorted_key(b), "piece %d: four turns are none", k);
        cells(k, 1, b);
        CHECK((k == O) == (sorted_key(a) == sorted_key(b)), "piece %d: one turn %s", k, k == O ? "changes nothing" : "changes it");
    }

    // Pieces come in bags of all seven
    for (int b = 0; b < 20; b++) {
        bag = 0;
        uint8_t seen = 0;
        for (int i = 0; i < 7; i++) seen |= (uint8_t)(1u << deal());
        CHECK(seen == 0x7F, "bag %d holds all seven (%02x)", b, seen);
    }

    // The keys: C / D move, A turns (aside from a wall), B drops
    start();
    int32_t c0 = pc;
    step(HH_BTN_D, 0);
    CHECK(pc == c0 + 1, "D moves right");
    step(HH_BTN_C, 0);
    step(HH_BTN_C, 0);
    CHECK(pc == c0 - 1, "C moves left");
    for (int i = 0; i < 20; i++) step(0, HH_BTN_C);
    int32_t moved = c0 - 1 - pc;
    CHECK(moved >= 1, "holding C keeps moving after a moment (%d more)", (int)moved);
    uint16_t before[H];
    memcpy(before, well, sizeof before);
    step(HH_BTN_B, 0);
    int filled = 0;
    for (int i = 0; i < H; i++) filled += __builtin_popcount(well[i]) - __builtin_popcount(before[i]);
    CHECK(filled == 4 && (well[H - 1] || well[H - 2]), "B drops the piece to the bottom (%d cells)", filled);

    // Full rows clear; the rest falls; the lines count, and speed it up
    start();
    well[H - 1] = FULL & ~1u;  // the bottom row lacks only column 0
    well[H - 2] = 0x0F0;
    kind = I, rot = 1, pr = 0, pc = -2;  // an I upright in column 0
    while (!clear_t) step(HH_BTN_B, 0);
    CHECK(clearing == 1u << (H - 1), "the full bottom row blinks (%04x)", clearing);
    while (clear_t) step(0, 0);
    CHECK(lines == 1 && well[H - 1] == (0x0F0 | 1u) && well[H - 2] == 1, "it clears, and the rows above fall (%03x %03x)", well[H - 1], well[H - 2]);

    // A player that places each piece as the old demo liked best clears
    // lines for a long time (until the speed beats its slow keys)
    // (one game can end early: over 12 seeds it cleared 15 to 345 lines)
    int total = 0;
    for (int seed = 1; seed <= 5; seed++) {
        mock_seed = (uint32_t)seed;
        start();
        int pieces = 0;
        while (!over && pieces < 1000) play_piece(), pieces++;
        total += lines;
    }
    CHECK(total >= 250, "a good player clears %d lines in 5 games", total);
    best = 0, mock_saves = 0, mock_data_len = 0;  // forget those games

    // Dropping every piece in the middle: the well fills up, the game ends
    start();
    for (int i = 0; i < 100 && !over; i++) step(HH_BTN_B, 0);
    CHECK(over, "dropping every piece in the middle fills the well: the end");
    CHECK(best == 0 && mock_saves == 0, "no lines, nothing saved");
    start();
    lines = 7;
    for (int i = 0; i < 100 && !over; i++) step(HH_BTN_B, 0);
    CHECK(best == 7 && mock_saves == 1, "a game with 7 lines saves 7");
    run(10, HH_BTN_B, 0);
    CHECK(over, "a button right after the end does nothing");
    run(OVER_MS, 0, 0);
    run(10, HH_BTN_D, 0);
    CHECK(!over && lines == 0, "a second later, a button starts again");

    over = true, over_ms = 0;  // a game that just ended
    run(15000, 0, 0);
    CHECK(demo, "15 s after a game, the demo comes back");
    demo = false;

    best = 0;
    init(&mock_api);
    CHECK(best == 7, "the best is loaded again (%d)", best);
    return mock_report("startris");
}
