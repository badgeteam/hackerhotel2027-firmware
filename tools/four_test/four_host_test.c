// 4 in a row's rules on the PC: apps/four/four.c included as it is, with a
// stand-in for the firmware's API. Wins across, up and on both diagonals,
// the winning line marked, a draw, the badge taking a win and blocking one.
//
//   cc -Iapps tools/four_test/four_host_test.c -o four_test && ./four_test
#include <stdio.h>
#include <string.h>

char __hh_load_end[1], __hh_bss_end[1];  // the app's linker symbols (HH_APP)

#include "../../apps/four/four.c"

static int checks, failures;
#define CHECK(cond, ...)                      \
    do {                                      \
        checks++;                             \
        if (!(cond)) {                        \
            failures++;                       \
            printf("FAIL  " __VA_ARGS__);     \
            printf("  (line %d)\n", __LINE__); \
        }                                     \
    } while (0)

static uint32_t s_rand = 1;
static uint32_t mock_random(void) { return s_rand = s_rand * 1103515245u + 12345u; }
static void mock_print(const char* text) { (void)text; }
static bool mock_send(uint8_t type, const void* data, uint8_t length) { (void)type, (void)data, (void)length; return true; }

static hh_api_t mock = {.random = mock_random, .print = mock_print, .ir_send_packet = mock_send};

static void play(const int* cols, int count) {
    new_game();
    for (int i = 0; i < count; i++) drop(cols[i]);
}

static int win_cells(void) {
    int k = 0;
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++) k += (cell[r][c] & WIN_BIT) != 0;
    return k;
}

int main(void) {
    hh = &mock;

    static const int across[] = {0, 0, 1, 1, 2, 2, 3};
    static const int up[] = {0, 1, 0, 1, 0, 1, 0};
    static const int diag[] = {0, 1, 1, 2, 2, 3, 2, 3, 3, 6, 3};
    static const int anti[] = {6, 5, 5, 4, 4, 3, 4, 3, 3, 0, 3};
    const int* games[] = {across, up, diag, anti};
    const int lens[] = {7, 7, 11, 11};
    const char* names[] = {"across", "up", "diagonal", "the other diagonal"};
    for (int g = 0; g < 4; g++) {
        play(games[g], lens[g] - 1);
        CHECK(winner == 0, "%s: no winner before the last disc", names[g]);
        drop(games[g][lens[g] - 1]);
        CHECK(winner == 1, "%s: the first player wins (%d)", names[g], winner);
        CHECK(win_cells() == 4, "%s: four discs marked as the line (%d)", names[g], win_cells());
    }

    // Five in a line: all five are the winning line
    static const int five[] = {0, 0, 1, 1, 3, 3, 4, 4, 2};
    play(five, 9);
    CHECK(winner == 1 && win_cells() == 5, "a disc that makes five: %d marked", win_cells());

    // A full column refuses more discs
    static const int full[] = {0, 0, 0, 0, 0, 0};
    play(full, 6);
    CHECK(row_free(0) < 0 && n == 6, "a full column");
    drop(0);
    CHECK(n == 6, "a disc in a full column is not a move");

    // A full board without four is a draw
    static const uint8_t board[H][W] = {
        {1, 1, 2, 1, 2, 1, 0}, {2, 2, 1, 2, 2, 1, 1}, {2, 2, 2, 1, 1, 2, 1},
        {2, 1, 2, 1, 2, 1, 1}, {1, 2, 2, 1, 1, 2, 2}, {1, 2, 1, 2, 1, 1, 2},
    };
    new_game();
    memcpy(cell, board, sizeof cell);
    n = W * H - 1;
    drop(6);
    CHECK(winner == 3, "a full board without four is a draw (%d)", winner);

    // The badge blocks three in a row, and takes four
    for (int t = 0; t < 50; t++) {
        static const int setup[] = {0, 6, 1, 6, 2};
        play(setup, 5);
        CHECK(ai(2) == 3, "the badge blocks 0 1 2 (round %d)", t);
        drop(5);
        CHECK(ai(1) == 3, "and the first player's move takes 0 1 2 3 (round %d)", t);
    }

    // It does not drop where the other side then wins on top of it
    for (int t = 0; t < 50; t++) {
        new_game();
        // player 1 has 3 4 5 on row 4 (second from the bottom); the bottom
        // row is full under 3 4 5 but empty under 2 and 6, and nobody wins
        // at once anywhere
        static const uint8_t b2[H][W] = {
            {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 1, 1, 1, 0}, {2, 1, 0, 2, 2, 1, 0},
        };
        memcpy(cell, b2, sizeof cell);
        n = 9;
        int c = ai(2);
        CHECK(c != 2 && c != 6, "the badge does not set up a win above (it picked %d)", c);
    }

    printf("%s  four rules: %d of %d checks\n", failures ? "FAIL" : "ok  ", checks - failures, checks);
    return failures != 0;
}
