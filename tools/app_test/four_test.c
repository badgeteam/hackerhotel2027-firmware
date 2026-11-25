// apps/four on the PC (mock_hh.h): the seven columns sit side by side, each
// two LEDs wide, all inside the octagon with the cursor and the floor; a
// game against the badge; and the infrared packets keep their format,
// [game, move, column, flags], so badges with an older build still pair up.
// The rules are in tools/four_test.
#include "mock_hh.h"
#include "../../apps/four/four.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
}

static void receive_packet(uint8_t g, uint8_t m, uint8_t c, uint8_t flags) {
    uint8_t d[4] = {g, m, c, flags};
    mock_packet('F', d, sizeof d);
    run(5, 0);
}

// The last packet the app sent
static const hh_ir_packet_t* sent(void) { return &mock_sent[mock_sent_n && mock_sent_n <= 64 ? mock_sent_n - 1 : 0]; }

static uint8_t px(int y, int x) { return mock_leds[layout_grid[y][x]]; }

int main(void) {
    (void)mock_rgb;
    init(&mock_api);

    // The layout: two LEDs a column, no gap between columns, all on the badge
    int missing = 0;
    for (int c = 0; c < W; c++)
        for (int k = 0; k < 2; k++) {
            int x = X0 + 2 * c + k;
            missing += layout_grid[CURSOR_Y][x] == HH_NONE || layout_grid[Y0 + H][x] == HH_NONE;
            for (int r = 0; r < H; r++) missing += layout_grid[Y0 + r][x] == HH_NONE;
        }
    CHECK(missing == 0, "every column's LEDs, cursor and floor are on the badge (%d missing)", missing);
    CHECK(X0 >= 0 && X0 + 2 * W <= HH_GRID_W, "the board fits the grid's width");
    CHECK(CURSOR_Y < Y0, "the cursor is above the board");

    // A full bottom row: 14 LEDs in a row, without a gap
    for (int c = 0; c < W; c++) cell[H - 1][c] = 1;
    run(5, 0);
    int lit = 0;
    for (int x = X0; x < X0 + 2 * W; x++) lit += px(Y0 + H - 1, x) == 255;
    CHECK(lit == 2 * W, "a full row lights %d of %d LEDs side by side", lit, 2 * W);
    new_game();

    // The cursor: two LEDs over its column; C and D move it
    run(5, HH_BTN_C);
    CHECK(cursor == 2, "C moves the cursor left (%d)", cursor);
    mock_ms = 200;  // the cursor is on then
    run(5, 0);
    CHECK(px(CURSOR_Y, X0 + 4) == 255 && px(CURSOR_Y, X0 + 5) == 255 && !px(CURSOR_Y, X0 + 3) && !px(CURSOR_Y, X0 + 6),
          "the cursor lights both LEDs of column 3");
    mock_ms = 0;  // and off then: it blinks, unlike a disc
    run(5, 0);
    CHECK(!px(CURSOR_Y, X0 + 4) && !px(CURSOR_Y, X0 + 5), "the cursor blinks fully off");
    run(5, HH_BTN_D);
    CHECK(cursor == 3, "D moves it right (%d)", cursor);

    // Against the badge: B drops, the badge answers
    run(5, HH_BTN_B);
    CHECK(mode == LOCAL && n == 1 && cell[H - 1][3] == 1, "B drops a disc and plays against the badge");
    run(THINK_MS, 0);
    run(5, 0);
    CHECK(n == 2, "the badge answers (%d moves)", n);
    run(5, 0);
    CHECK(px(Y0 + H - 1, X0 + 6) == 255 && px(Y0 + H - 1, X0 + 7) == 255, "your disc lights both its LEDs");

    // The infrared: looking sends [game, 0, 0, FL_ANSWER]
    new_game();
    mock_sent_n = 0;
    run(5, 0);
    CHECK(mock_sent_n == 1 && sent()->type == 'F' && sent()->length == 4, "it looks for a partner with an 'F' packet of 4 bytes");
    CHECK(sent()->data[0] == game && sent()->data[1] == 0 && sent()->data[3] == FL_ANSWER, "game %d, move 0, please answer", game);
    // Another badge's invitation with a higher number: join, they move first
    game = 1;
    receive_packet(200, 0, 0, FL_ANSWER);
    CHECK(mode == LINK && me == 1 && game == 200, "an invitation with a higher number joins, as the second");
    receive_packet(200, 1, 5, FL_ANSWER);
    CHECK(n == 1 && cell[H - 1][5] == 1, "their move 1 lands in column 6");
    cursor = 2;
    run(5, HH_BTN_B);
    run(5, 0);  // it goes out on the next tick
    CHECK(n == 2 && sent()->data[0] == 200 && sent()->data[1] == 2 && sent()->data[2] == 2 &&
              (sent()->data[3] & FL_SECOND),
          "our move goes out as [game, 2, column 2, FL_SECOND]");
    return mock_report("four");
}
