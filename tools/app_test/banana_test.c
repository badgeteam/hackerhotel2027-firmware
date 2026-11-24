// apps/banana on the PC (mock_hh.h), by the HH2025 banana's rules: a shot
// from another team counts and flashes the arms in its colour, one from
// its own team does not, 5 hits is GAME OVER until B is held; the badge has
// 5 shots (B), C reloads, D picks its team, holding D gives it (command 8),
// a banana's command 8 gives the badge a team; text bytes scroll.
#include "mock_hh.h"
#include "../../apps/banana/banana.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

static void shot(uint8_t t, uint8_t command, uint8_t parameter) {
    mock_shot = (hh_ir_shot_t){t, 1, command, parameter};
    mock_shot_pending = true;
    run(10, 0);
}

static void byte(char c) {
    mock_shot = (hh_ir_shot_t){(uint8_t)((uint8_t)c >> 5), (uint8_t)(c >> 4 & 1), HH_SHOT_BYTE, (uint8_t)(c & 15)};
    mock_shot_pending = true;
    run(10, 0);
}

static void brightest(uint8_t out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t c[3];
        mock_rgb(i, c);
        if (c[0] + c[1] + c[2] > out[0] + out[1] + out[2]) out[0] = c[0], out[1] = c[1], out[2] = c[2];
    }
}

static int lit_grid(void) {
    int n = 0;
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++) n += layout_grid[r][c] != HH_NONE && mock_leds[layout_grid[r][c]];
    return n;
}

static void hold(uint8_t key, uint32_t ms) {
    run(10, key);
    mock_held_now = key;
    for (uint32_t t = 0; t < ms; t += 20) run(20, 0);
    mock_held_now = 0;
    run(10, 0);
}

int main(void) {
    init(&mock_api);
    run(10, 0);
    CHECK(hits == 0 && shots == SHOTS && lit_grid() > 40, "it opens with the banana (%d LEDs), 5 shots", lit_grid());

    shot(1, HH_SHOT_SHOOT, 1);  // a green banana
    uint8_t c[3];
    brightest(c);
    CHECK(hits == 1 && c[1] > 200 && c[0] == 0, "a green shot counts, the arms flash green (%d %d %d)", c[0], c[1], c[2]);
    run(300, 0);
    CHECK(!strcmp(mock_text, "1"), "the hits show (%s)", mock_text);
    shot(2, HH_SHOT_SHOOT, 0);
    CHECK(hits == 1, "a blue shot at a blue badge: you can't shoot your own team");
    shot(2, HH_SHOT_HEAL, 0);
    CHECK(hits == 0, "a heal takes one off");

    run(1000, 0);  // the heal's flash is over
    // shooting: 5 shots, then empty; C reloads
    for (int i = 0; i < 5; i++) run(50, HH_BTN_B);
    CHECK(mock_shots_sent == 5 && shots == 0 && mock_sent_shot.team == 2 && mock_sent_shot.command == HH_SHOT_SHOOT,
          "B shoots 5 times, as blue (%u sent)", mock_shots_sent);
    CHECK(mock_sent_shot.parameter == 1, "on the last shooter's channel (%d)", mock_sent_shot.parameter);
    run(50, HH_BTN_B);
    brightest(c);
    CHECK(mock_shots_sent == 5 && c[0] > 150 && c[1] == 0, "a 6th: nothing goes out, the arms flash red (%d %d %d)", c[0], c[1], c[2]);
    run(10, HH_BTN_C);
    CHECK(shots == SHOTS, "C reloads");

    // the team: D tapped picks the next, D held gives it
    run(10, HH_BTN_D);
    run(10, 0);
    CHECK(my_team == 3 && !strcmp(mock_text, "YEL"), "tapping D: the next team, its name shows (%s)", mock_text);
    hold(HH_BTN_D, 1100);
    CHECK(my_team == 3 && mock_shots_sent == 6 && mock_sent_shot.command == HH_SHOT_TEAM && mock_sent_shot.team == 3,
          "holding D gives yellow, without picking the next (team %d, command %d)", my_team, mock_sent_shot.command);
    shot(4, HH_SHOT_TEAM, 0);
    CHECK(my_team == 4 && hits == 0, "a banana's command 8: the badge takes magenta, no hit");

    // 5 hits: GAME OVER, until B is held
    for (int i = 0; i < 5; i++) shot(0, HH_SHOT_SHOOT, 0), run(100, 0);
    CHECK(hits == 5 && over, "5 red hits: game over");
    shot(0, HH_SHOT_SHOOT, 0);
    run(10, HH_BTN_B);
    CHECK(hits == 5 && mock_shots_sent == 6, "after it, no more hits, no shots");
    for (int i = 0; i < 300; i++) run(20, 0);
    CHECK(strstr(mock_text, "GAME OVER") != NULL, "GAME OVER scrolls (%s)", mock_text);
    hold(HH_BTN_B, 1100);
    CHECK(!over && hits == 0 && shots == SHOTS, "holding B: play again");

    // the credits, now and then
    int credits = 0;
    for (int i = 0; i < 4000; i++) {
        run(20, 0);
        credits += strstr(mock_text, "ANESIDORA") != NULL;
        mock_text[0] = 0;
    }
    CHECK(credits > 0, "THANKS ANESIDORA scrolls by within 80 s");

    // text from a banana's serial port
    const char* line = "PX 1 2 ff0000";
    for (const char* p = line; *p; p++) byte(*p);
    run(500, 0);
    CHECK(text_len == 13 && !strcmp(mock_text, line), "text bytes make a line, it scrolls (%s)", mock_text);
    byte('\n');
    for (int i = 0; i < 200 && text_len; i++) run(50, 0);
    CHECK(text_len == 0, "a newline: it scrolls once more, then goes");
    return mock_report("banana");
}
