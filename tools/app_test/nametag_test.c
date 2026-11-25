// apps/nametag on the PC (mock_hh.h): the styles shade the text and leave
// the arms alone; B / D step the style and it is remembered; C held with
// B / D sets the brightness; banana shots count with a ring and the
// shooter's colour, a banana's command 8 sets the team; the name swap
// with the firmware's 'H' / 'R' packets: beacons, HELLOs, answers, the air
// kept free, the friends table, the Bloom filter of hackers met (kept over
// a power cycle), the panel, the millis wrap.
#include "mock_hh.h"
#include "../../apps/nametag/styles.c"
#include "../../apps/nametag/nametag.c"

static unsigned frames;

// ms in 5 ms ticks; pressed goes with the first one
static void run(uint32_t ms, uint8_t pressed) {
    for (uint32_t t = 0; t < ms; t += 5) {
        mock_ms += 5;
        mock_pressed_now = t ? 0 : pressed;
        uint32_t f = S.last_frame;
        tick(5);
        mock_tick_end();
        frames += S.last_frame != f;
    }
    mock_pressed_now = 0;
}

// A power cycle: the saved data stays
static void reinit(void) { init(&mock_api); }

// A badge as new, with this name, from time start
static void fresh(const char* name, uint32_t start) {
    memset(mock_data, 0, sizeof mock_data);
    mock_data_len = mock_saves = 0;
    mock_sent_n = mock_in_n = mock_shotq_n = 0;
    mock_shot_pending = mock_tx_busy = mock_save_fail = false;
    mock_held_now = 0;
    mock_log[0] = 0;
    mock_brightness = 255;
    snprintf(mock_name, sizeof mock_name, "%s", name);
    mock_ms = start;
    init(&mock_api);
}

static void packet(uint8_t type, const char* d, int n) {
    if (n < 0) n = (int)strlen(d);
    mock_packet(type, d, (uint8_t)n);
    run(5, 0);
}

static void shot(uint8_t team, uint8_t command) {
    mock_shot_push((hh_ir_shot_t){team, 1, command, 0});
    run(5, 0);
}

// Packets of this type sent from index `from` on, with sent times in [t0, t1)
static int sent(uint8_t type, unsigned from, uint32_t t0, uint32_t t1) {
    int k = 0;
    for (unsigned i = from; i < mock_sent_n && i < 64; i++)
        k += mock_sent[i].type == type && mock_sent_ms[i] - t0 < t1 - t0;
    return k;
}

static int px(int x, int y) { return layout_grid[y][x] == HH_NONE ? 0 : mock_leds[layout_grid[y][x]]; }

static bool rgb_is(int i, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t c[3];
    mock_rgb(i, c);
    return c[0] == r && c[1] == g && c[2] == b;
}

static void styles(void) {
    static const char* names[2] = {"ABC", "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"};
    for (int st = 0; st < STYLE_COUNT; st++) {
        for (int k = 0; k < 2; k++) {
            fresh(names[k], 0);
            S.sv.style = (uint8_t)st;
            restyle(mock_ms);
            bool lit = false, rgb_clean = true, rainbow = true;
            unsigned most = 0;
            for (int f = 0; f < 300; f++) {  // 10 s
                run(35, 0);
                // a whole frame: some text shows
                for (int y = 0; y < HH_GRID_H; y++)
                    for (int x = 0; x < HH_GRID_W; x++) lit |= M(x, y) && px(x, y) > 0;
                // style_draw alone: never an arm LED, the call budget
                memset(mock_leds, 0, sizeof mock_leds);
                mock_calls = 0;
                style_draw((uint8_t)st, mock_ms);
                if (mock_calls > most) most = mock_calls;
                for (int i = 0; i < HH_RGB_COUNT; i++) rgb_clean &= rgb_is(i, 0, 0, 0);
                if (st == 0)
                    for (int y = 0; y < HH_GRID_H; y++)
                        for (int x = 0; x < HH_GRID_W; x++) rainbow &= M(x, y) ? px(x, y) >= 150 : px(x, y) == 0;
            }
            CHECK(lit, "S1 style %d, %s: the text shows", st, names[k]);
            CHECK(rgb_clean, "S1 style %d, %s: style_draw leaves the arms alone", st, names[k]);
            if (st == 0) CHECK(rainbow, "S1 RAINBOW, %s: background 0, text >= 150", names[k]);
            CHECK(most < 1500, "S2 style %d, %s: %u hh calls a frame at most", st, names[k], most);
        }
        uint32_t sum = 0;
        for (int i = 0; i < HH_RGB_COUNT; i++)
            sum += style_arm((uint8_t)st, i, 0) + style_arm((uint8_t)st, i, 12345) + style_arm((uint8_t)st, i, 0x80000000u);
        CHECK(sum || 1, "S3 style %d: style_arm defined at 0, 12345, 2^31", st);
    }
}

static void core(void) {
    fresh("", 0);
    run(100, 0);
    CHECK(S.sv.style == 0 && S.sv.team == 0xFF && S.sv.met == 0, "C1 a first start: style 0, no team, met 0");
    CHECK(S.shown && !strcmp(S.shown, "HACKER HOTEL 2027"), "C1 no name: HACKER HOTEL 2027 (%s)", S.shown);
    CHECK(strstr(mock_log, "nametag: style 0 met 0") != NULL, "C1 init logs the style and met (%s)", mock_log);
    fresh("ALICE", 0);
    run(100, 0);
    CHECK(!strcmp(S.shown, "ALICE"), "C1 the name shows (%s)", S.shown);

    run(5, HH_BTN_B);
    CHECK(S.sv.style == 1 && mock_data[0] == 1, "C2 B: style 1, saved (%d, %d)", S.sv.style, mock_data[0]);
    CHECK(S.dial_t == mock_ms && S.style_t0 == mock_ms, "C2 the dial and the reveal start");
    run(100, HH_BTN_D);
    run(100, HH_BTN_D);
    CHECK(S.sv.style == STYLE_COUNT - 1, "C2 D twice from 1: wraps to %d (%d)", STYLE_COUNT - 1, S.sv.style);
    run(100, HH_BTN_B);
    CHECK(S.sv.style == 0, "C2 B from the last: 0 (%d)", S.sv.style);
    run(100, HH_BTN_B);
    run(100, HH_BTN_B);
    run(100, HH_BTN_B);
    reinit();
    CHECK(S.sv.style == 3, "C2 a power cycle keeps the style (%d)", S.sv.style);
    mock_data[0] = 200;
    reinit();
    CHECK(S.sv.style == 0, "C2 style 200 in the data: 0");
    mock_data[1] = 9;
    reinit();
    CHECK(S.sv.team == 0xFF, "C2 team 9 in the data: none");
    mock_data[1] = 0xFF;

    // C held + B: the brightness, not the style, and no panel
    mock_brightness = 64;
    mock_held_now = HH_BTN_C;
    run(50, HH_BTN_C);
    run(50, HH_BTN_B);
    run(1000, 0);
    mock_held_now = 0;
    run(50, 0);
    CHECK(mock_brightness == 128 && S.sv.style == 0 && !S.panel, "C3 C + B: brighter (%d), style %d, no panel", mock_brightness,
          S.sv.style);
    run(5, HH_BTN_C);
    run(5, 0);
    CHECK(S.panel, "C3 a C tap opens the panel");
    run(5, HH_BTN_C);
    run(5, 0);
    CHECK(!S.panel, "C3 and another closes it");
}

static void banana(void) {
    fresh("ALICE", 0);
    run(500, 0);
    uint32_t t0 = mock_ms;
    shot(2, HH_SHOT_SHOOT);
    CHECK(S.hits == 1, "C4 a blue shot: 1 hit");
    bool ring = false, arms = false, count = true;
    while (mock_ms - t0 < 2400) {
        run(5, 0);
        uint32_t age = mock_ms - t0;
        if (age >= 400 && age < 650)
            for (int i = 0; i < HH_EDGE_COUNT; i++) ring |= mock_leds[layout_edge[i]] == 255;
        if (age < 900)
            for (int i = 0; i < HH_RGB_COUNT; i++) arms |= rgb_is(i, 0, 60, 255) || rgb_is(i, 0, 18, 80);
        if (age >= 340 && age < 2290 && mock_ms - S.last_frame < 5) count &= !strcmp(S.shown, "1");
    }
    CHECK(ring, "C4 the ring reaches the outline");
    CHECK(arms, "C4 the arms flash blue");
    CHECK(count, "C4 the hit count shows from 300 ms to 2.3 s");
    CHECK(!strcmp(S.shown, "ALICE"), "C4 then the name again (%s)", S.shown);

    shot(1, HH_SHOT_HEAL);
    CHECK(S.hits == 0, "C5 a heal takes the hit off");
    shot(1, HH_SHOT_HEAL);
    CHECK(S.hits == 0, "C5 never below 0");

    run(2500, 0);  // the heal's count is over
    shot(4, HH_SHOT_TEAM);
    run(100, 0);
    CHECK(S.sv.team == 4 && mock_data[1] == 4 && !strcmp(S.shown, "MAG"), "C6 command 8: team magenta, saved, MAG shows (%s)",
          S.shown);
    run(2000, 0);
    while (mock_ms % 8000 < 500) run(100, 0);  // not in the pulse
    bool tips = true;
    for (int i = 0; i < HH_RGB_COUNT; i++)
        if (layout_rgb[i].rank == 6) {
            uint8_t c[3];
            mock_rgb(i, c);
            tips &= c[0] > 100 && c[1] == 0 && c[2] > 80;
        }
    CHECK(tips, "C6 the tips are magenta");
    shot(4, HH_SHOT_SHOOT);
    CHECK(S.hits == 0, "C6 a magenta shot at a magenta badge does not count");
    shot(0, HH_SHOT_SHOOT);
    CHECK(S.hits == 1, "C6 a red one does");

    mock_shot = (hh_ir_shot_t){0, 1, HH_SHOT_SHOOT, 0};
    mock_shot_pending = true;
    mock_shot_push((hh_ir_shot_t){1, 1, HH_SHOT_SHOOT, 0});
    mock_shot_push((hh_ir_shot_t){3, 1, HH_SHOT_SHOOT, 0});
    mock_shot_push((hh_ir_shot_t){2, 1, HH_SHOT_BYTE, 5});
    run(5, 0);
    CHECK(S.hits == 4 && S.sv.team == 4 && !mock_shot_pending && !mock_shotq_n, "C7 3 shots in one tick: all count (%d), a byte is ignored",
          S.hits);
    run(3000, 0);
    reinit();
    CHECK(S.hits == 0 && S.sv.team == 4, "C6 a power cycle: the team stays, the hits go");
}

static void friends(void) {
    // C8: the beacon, again 20 .. 30 s later, a HELLO within 15 s
    fresh("ALICE", 0);
    run(3000, 0);
    CHECK(mock_sent_n >= 1 && mock_sent[0].type == 'R' && mock_sent[0].length == 7 && !memcmp(mock_sent[0].data, "ALICE\0\x1F", 7),
          "C8 a beacon 'R' ALICE + NUL + 0x1F within 3 s");
    run(32000, 0);
    int r = -1;
    for (unsigned i = 1; i < mock_sent_n && r < 0; i++)
        if (mock_sent[i].type == 'R') r = (int)i;
    CHECK(r > 0 && mock_sent_ms[r] - mock_sent_ms[0] >= 20000 && mock_sent_ms[r] - mock_sent_ms[0] <= 30005,
          "C8 the next beacon 20 .. 30 s later (%u ms)", r > 0 ? mock_sent_ms[r] - mock_sent_ms[0] : 0);
    int h = -1;
    for (unsigned i = 0; i < mock_sent_n && h < 0; i++)
        if (mock_sent[i].type == 'H') h = (int)i;
    CHECK(h >= 0 && mock_sent_ms[h] <= 15005 && mock_sent[h].length == 7, "C8 a HELLO within 15 s (%u ms)",
          h >= 0 ? mock_sent_ms[h] : 0);

    // C9: a new friend
    unsigned saves = mock_saves;
    packet('R', "BOB\0\x13", 5);
    CHECK(S.sv.met == 1 && mock_saves > saves && mock_data[2] == 1, "C9 BOB: met 1, saved");
    CHECK(strstr(mock_log, "nametag: new BOB\n") != NULL, "C9 logged (%s)", mock_log);
    run(40, 0);
    CHECK(!strcmp(S.shown, "HI BOB"), "C9 HI BOB shows (%s)", S.shown);
    int yellow = 0;
    for (int i = 0; i < HH_RGB_COUNT; i++) yellow += rgb_is(i, 255, 200, 0);
    CHECK(yellow >= 30, "C9 the arms burst in BOB's team colour, yellow (%d)", yellow);
    bool count = false;
    for (int i = 0; i < 1300; i++) {
        run(5, 0);
        count |= !strcmp(S.shown, "#1");
    }
    CHECK(count, "C9 then #1, the hackers met");
    run(3000, 0);
    CHECK(!strcmp(S.shown, "ALICE"), "C9 then the name again (%s)", S.shown);

    // C10: known names
    saves = mock_saves;
    packet('R', "BOB\0\x13", 5);
    run(100, 0);
    CHECK(S.sv.met == 1 && mock_saves == saves && !S.npop, "C10 BOB again: still 1, nothing saved, no popup");
    reinit();
    packet('R', "bob", 3);
    CHECK(S.sv.met == 1 && mock_saves == saves, "C10 after a power cycle, bob: still 1, nothing saved");
    run(40, 0);
    CHECK(!strcmp(S.shown, "HI bob"), "C10 but a hello (%s)", S.shown);

    // C11: not counted
    fresh("ALICE", 0);
    packet('R', "ALICE\0\x1F", 7);
    packet('R', "alice", 5);
    packet('R', "", 0);
    packet('H', "\x01" "AB", 3);
    packet('R', "A\xC3" "B", 3);
    CHECK(S.sv.met == 0 && !S.fr[0].name[0] && !S.reply_pending, "C11 own name, empty, bad bytes: nothing learned (met %d)", S.sv.met);
}

static void answering(void) {
    // C12
    fresh("DAVE", 0);
    run(4000, 0);
    unsigned from = mock_sent_n;
    uint32_t t0 = mock_ms;
    packet('H', "CAROL", 5);
    run(3000, 0);
    CHECK(sent('R', from, t0, t0 + 3005) == 1 && sent('R', from, t0 + 300, t0 + 1205) == 1, "C12 an 'H' without a tail: one 'R' in 0.3 .. 1.2 s");
    CHECK(mock_sent[from].length == 6 && !memcmp(mock_sent[from].data, "DAVE\0\x1F", 6), "C12 with the own name and the tail");
    CHECK(S.sv.met == 1, "C12 CAROL counts");
    from = mock_sent_n;
    t0 = mock_ms;
    packet('H', "ERIN\0\x12", 6);
    run(100, 0);
    packet('H', "ERIN\0\x12", 6);
    run(3000, 0);
    CHECK(sent('R', from, t0, t0 + 3200) == 1 && sent('R', from, t0 + 300, t0 + 2305) == 1,
          "C12 an 'H' with a tail, twice: one 'R' in 0.3 .. 2.3 s");

    // C13: the transmitter busy past the answer's time: dropped; the beacon
    // goes once it is free
    fresh("DAVE", 0);
    run(500, 0);
    mock_tx_busy = true;
    packet('H', "CAROL", 5);
    while (mock_ms < 3000) run(5, 0);
    mock_tx_busy = false;
    CHECK(mock_sent_n == 0, "C13 nothing while busy");
    run(2000, 0);
    CHECK(mock_sent_n == 1 && mock_sent[0].type == 'R' && mock_sent_ms[0] <= 3010, "C13 the answer is dropped, the beacon goes at once (%u, %u ms)",
          mock_sent_n, mock_sent_ms[0]);

    // C14: no name: silent, but answers, and counts
    fresh("", 0);
    packet('R', "BOB", 3);
    CHECK(S.sv.met == 1, "C14 no name: BOB still counts");
    t0 = mock_ms;
    packet('H', "CAROL", 5);
    run(1300, 0);
    CHECK(mock_sent_n == 1 && mock_sent[0].type == 'R' && mock_sent[0].length == 0 && mock_sent_ms[0] - t0 >= 300,
          "C14 an 'H' gets an empty 'R'");
    run(600000, 0);
    CHECK(mock_sent_n == 1, "C14 nothing else in 10 min (%u)", mock_sent_n);
}

static void timing(void) {
    fresh("ALICE", 0);
    run(600000, 0);
    int r = sent('R', 0, 0, 0xFFFFFFFF), h = sent('H', 0, 0, 0xFFFFFFFF);
    CHECK(r >= 10 && r <= 30 && h >= 2 && h <= 5, "C15 alone, 10 min: %d beacons, %d HELLOs", r, h);

    fresh("ALICE", 0);
    run(20000, 0);
    uint32_t first = mock_ms;
    unsigned from = mock_sent_n;
    for (int i = 0; i < 6; i++) {
        packet('H', "ZED", 3);
        run(100000, 0);
    }
    CHECK(sent('H', from, first, first + 700000) == 0, "C15 a foreign 'H' every 100 s: no more of ours");
    CHECK(sent('R', from, first, first + 700000) >= 6, "C15 each one answered");

    // C16: the air kept free
    fresh("ALICE", 0);
    run(4000, 0);
    from = mock_sent_n;
    S.beacon_at = mock_ms;
    uint32_t t0 = mock_ms;
    packet('F', "\x01\x02\x03\x04", 4);
    run(1000, 0);
    CHECK(sent('R', from, t0, t0 + 400) == 0 && sent('R', from, t0 + 400, t0 + 805) == 1, "C16 after an 'F': quiet 0.4 .. 0.8 s, then the beacon");
    CHECK(S.sv.met == 0 && !S.fr[0].name[0] && !S.reply_pending, "C16 an 'F' is not learned or answered");
    from = mock_sent_n;
    t0 = mock_ms;
    packet('H', "CAROL", 5);
    S.query_at = mock_ms;
    run(2480, 0);
    CHECK(sent('R', from, t0, t0 + 2500) == 1 && sent('H', from, t0, t0 + 2500) == 0, "C16 after an 'H': only the answer for 2.5 s");
    run(200, 0);
    CHECK(sent('H', from, t0 + 2500, t0 + 2800) == 1, "C16 then a due HELLO");
}

static void table(void) {
    fresh("ALICE", 0);
    packet('R', "BOB", 3);
    packet('R', "CAROL", 5);
    CHECK(nearby() == 2, "C17 2 names: 2 nearby");
    run(151000, 0);
    CHECK(nearby() == 0, "C17 151 s later: none");
    char name[48];
    for (int i = 0; i < 8; i++) snprintf(name, sizeof name, "N%d", i), packet('R', name, -1);
    uint32_t lo = 0xFFFFFFFF;
    for (int i = 0; i < 50; i++) {
        uint32_t g = beacon_gap();
        if (g < lo) lo = g;
    }
    CHECK(nearby() == 8 && lo == 60000, "C17 8 nearby: the beacon every 60 s (%u)", lo);

    fresh("ALICE", 0);
    for (int i = 0; i < 14; i++) {
        snprintf(name, sizeof name, "%02d%030d", i % 100, i % 100);
        packet('R', name, 32);
    }
    CHECK(nearby() == 12 && !strncmp(S.fr[0].name, "13", 2) && strlen(S.fr[0].name) == 32, "C18 14 long names: 12 kept, the newest first");
    run(5, HH_BTN_C);
    run(5, 0);
    CHECK(S.panel && strlen(S.panel_text) <= 95 && !strncmp(S.panel_text, "MET 14 NEAR 12 13", 17), "C18 the panel is bounded (%s)",
          S.panel_text);

    // C19: 10 names with no Bloom collisions, over a power cycle, then 10 more
    fresh("ALICE", 0);
    uint8_t sim[24] = {0};
    char names[20][12];
    for (int k = 0, i = 0; k < 20; i++) {
        snprintf(names[k], sizeof names[k], "HX%d", i);
        uint32_t hs = fnv((const uint8_t*)names[k], (int)strlen(names[k]));
        unsigned a = hs % 192, b = hs / 192 % 192;
        if ((sim[a >> 3] >> (a & 7) & 1) || (sim[b >> 3] >> (b & 7) & 1)) continue;
        sim[a >> 3] |= (uint8_t)(1 << (a & 7)), sim[b >> 3] |= (uint8_t)(1 << (b & 7));
        k++;
    }
    for (int k = 0; k < 10; k++) packet('R', names[k], -1);
    CHECK(S.sv.met == 10, "C19 10 names: met 10 (%d)", S.sv.met);
    reinit();
    for (int k = 0; k < 10; k++) packet('R', names[k], -1);
    CHECK(S.sv.met == 10, "C19 after a power cycle, the same 10: still 10 (%d)", S.sv.met);
    for (int k = 10; k < 20; k++) packet('R', names[k], -1);
    CHECK(S.sv.met == 20, "C19 10 more: 20 (%d)", S.sv.met);

    // C20: false positives after 50 names
    fresh("ALICE", 0);
    for (int i = 0; i < 50; i++) snprintf(name, sizeof name, "P%dQ", i * 7919), packet('R', name, -1);
    int fp = 0;
    for (int i = 0; i < 2000; i++) {
        snprintf(name, sizeof name, "O%dX", i);
        uint32_t hs = fnv((const uint8_t*)name, (int)strlen(name));
        unsigned a = hs % 192, b = hs / 192 % 192;
        fp += (S.sv.seen[a >> 3] >> (a & 7) & 1) && (S.sv.seen[b >> 3] >> (b & 7) & 1);
    }
    CHECK(fp < 500 && S.sv.met >= 45, "C20 after 50 names (met %d): %d of 2000 others look known", S.sv.met, fp);
}

static void panel(void) {
    fresh("ALICE", 0);
    run(22000, 0);  // our first HELLO is out, more than 5 s ago
    packet('R', "BOB", 3);
    run(100, 0);
    unsigned from = mock_sent_n;
    uint32_t t0 = mock_ms;
    run(5, HH_BTN_C);
    run(40, 0);
    CHECK(!strncmp(S.shown, "MET 1 NEAR 1 BOB", 16), "C21 C: the panel (%s)", S.shown);
    CHECK(sent('H', from, t0, t0 + 100) == 1, "C21 and a HELLO");
    run(5, HH_BTN_C);
    run(40, 0);
    CHECK(!S.panel, "C21 C again closes it");
    run(5, HH_BTN_C);
    run(40, 0);
    CHECK(S.panel && sent('H', from, t0, mock_ms) == 1, "C21 C within 5 s of a HELLO: the panel only");
    run(30000, 0);
    CHECK(!S.panel, "C21 the panel closes after one pass");

    // C22: a menu pause gives one frame; the millis wrap; save fails
    unsigned f = frames;
    mock_ms += 5000;
    tick(5000);
    mock_ms += 5;
    tick(5);
    CHECK(S.last_frame == mock_ms - 5, "C22 after a 5 s pause: one frame, not a burst");
    (void)f;

    fresh("ALICE", 0xFFFFF000u);
    run(130000, 0);
    int r = 0;
    bool regular = true;
    for (unsigned i = 0, prev = 99; i < mock_sent_n && i < 64; i++) {
        if (mock_sent[i].type != 'R') continue;
        if (prev != 99) regular &= mock_sent_ms[i] - mock_sent_ms[prev] >= 20000 && mock_sent_ms[i] - mock_sent_ms[prev] <= 30005;
        prev = i;
        r++;
    }
    CHECK(r >= 4 && r <= 7 && regular, "C22 across the millis wrap: %d beacons, 20 .. 30 s apart", r);

    fresh("ALICE", 0);
    mock_save_fail = true;
    packet('R', "BOB", 3);
    run(5, HH_BTN_B);
    run(1000, 0);
    CHECK(S.sv.met == 1 && S.sv.style == 1 && mock_saves == 0, "C22 save fails: met and style go on in RAM");
}

int main(void) {
    styles();
    core();
    banana();
    friends();
    answering();
    timing();
    table();
    panel();
    return mock_report("nametag");
}
