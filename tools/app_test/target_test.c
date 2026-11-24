// apps/target on the PC (mock_hh.h): a shot counts and lights the arms in
// the shooter's team colour, a heal takes one off, a banana's text bytes
// make a line that scrolls, then goes; the badge shoots back with B, picks
// its team with C, gives it with D, and holding B counts from 0 again.
#include "mock_hh.h"
#include "../../apps/target/target.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_pressed_now = 0;
}

static void shot(uint8_t team, uint8_t command, uint8_t parameter) {
    mock_shot = (hh_ir_shot_t){team, 1, command, parameter};
    mock_shot_pending = true;
    run(10, 0);
}

static void byte(char c) {
    mock_shot = (hh_ir_shot_t){(uint8_t)((uint8_t)c >> 5), (uint8_t)(c >> 4 & 1), HH_SHOT_BYTE, (uint8_t)(c & 15)};
    mock_shot_pending = true;
    run(10, 0);
}

// The brightest arm LED's colour
static void brightest(uint8_t out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t c[3];
        mock_rgb(i, c);
        if (c[0] + c[1] + c[2] > out[0] + out[1] + out[2]) out[0] = c[0], out[1] = c[1], out[2] = c[2];
    }
}

static int lit(void) {
    int n = 0;
    for (int i = 0; i < HH_SLOTS; i++) n += mock_leds[i] != 0;
    return n;
}

int main(void) {
    init(&mock_api);
    run(10, 0);
    CHECK(hits == 0 && lit() > 10, "it opens with the target (%d LEDs)", lit());

    shot(1, HH_SHOT_SHOOT, 1);  // the banana: green
    uint8_t c[3];
    brightest(c);
    CHECK(hits == 1 && c[1] > 200 && c[0] == 0, "a shot counts, the arms flash green (%d %d %d)", c[0], c[1], c[2]);
    run(300, 0);
    CHECK(!strcmp(mock_text, "1"), "the count shows (%s)", mock_text);
    shot(0, HH_SHOT_SHOOT, 0);
    brightest(c);
    CHECK(hits == 2 && c[0] > 200 && c[1] == 0, "a red shot: red (%d %d %d)", c[0], c[1], c[2]);
    run(HIT_MS, 0);
    CHECK(mock_leds[layout_edge[0]] && mock_leds[layout_edge[1]] && !mock_leds[layout_edge[2]], "the outline: one LED per hit");
    shot(2, HH_SHOT_HEAL, 0);
    CHECK(hits == 1, "a heal takes one off (%d)", hits);
    shot(2, 7, 0);
    CHECK(hits == 1, "another command does nothing");
    run(COUNT_MS, 0);
    CHECK(lit() > 10, "after a while, the target again");
    // shooting back
    CHECK(mock_shots_sent == 0, "nothing sent yet");
    run(10, HH_BTN_B);
    CHECK(mock_shots_sent == 1 && mock_sent_shot.command == HH_SHOT_SHOOT && mock_sent_shot.team == 2 && mock_sent_shot.trigger == 1,
          "B shoots, as blue (team %d, command %d)", mock_sent_shot.team, mock_sent_shot.command);
    CHECK(mock_sent_shot.parameter == 0, "on the last shot's channel (%d)", mock_sent_shot.parameter);
    CHECK(hits == 1, "B alone keeps the count");
    run(10, HH_BTN_C);
    CHECK(my_team == 3 && !strcmp(mock_text, "YEL"), "C: the next team, its name shows (%s)", mock_text);
    for (int i = 0; i < 3; i++) run(10, HH_BTN_C);
    CHECK(my_team == 0, "C goes round six teams (%d)", my_team);
    run(10, HH_BTN_D);
    CHECK(mock_shots_sent == 2 && mock_sent_shot.command == HH_SHOT_TEAM && mock_sent_shot.team == 0, "D gives red (team %d, command %d)",
          mock_sent_shot.team, mock_sent_shot.command);
    run(10, HH_BTN_B);
    mock_held_now = HH_BTN_B;
    for (int i = 0; i < 60; i++) run(20, 0);
    mock_held_now = 0;
    CHECK(hits == 0 && mock_shots_sent == 3, "holding B a second: from 0 again, one shot (%d hits, %u sent)", hits, mock_shots_sent);

    const char* line = "PX 1 2 ff0000";
    for (const char* p = line; *p; p++) byte(*p);
    CHECK(text_len == 13 && !strcmp(text, line), "text bytes make a line (%s)", text);
    run(500, 0);
    CHECK(!strcmp(mock_text, line), "it scrolls (%s)", mock_text);
    byte('\n');
    CHECK(text_done && text_len == 13, "a newline ends the line");
    for (int i = 0; i < 200 && text_len; i++) run(50, 0);
    CHECK(text_len == 0, "it scrolls once more, then goes");
    byte('A');
    CHECK(text_len == 1 && text[0] == 'A', "a new line starts");
    return mock_report("target");
}
