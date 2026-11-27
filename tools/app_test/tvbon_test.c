// apps/tvbon on the PC (mock_hh.h): TV, ON and OFF on the grid; the arms
// to A and B from white to green and to red; a tap of A sends the
// power-on code of each brand, B the power-off codes, decoded here back to
// the codes as remote control lists write them; a long press of A (the
// menu) sends nothing; B during ON switches to OFF; and a frame stays as
// it is while it goes out.
#include <stdlib.h>

#include "mock_hh.h"
#include "../../apps/tvbon/tvbon.c"

// The frames that went out, decoded: a brand's code and its carrier
typedef struct {
    char kind;  // 'N' NEC (9 ms leader), 'S' Samsung (4.5 ms), 'Y' Sony, 'P' Panasonic, 'R' RC5, 'C' RC6, '?' none
    uint64_t code;  // the bits in the order they went out, the first the highest
    uint32_t carrier;
    uint32_t us;  // the whole frame, with its gap
} sent_t;
static sent_t sent[256];  // every frame of the test
static int nsent;
static uint32_t air_end;  // the frame going out ends then (mock_ms)
static uint16_t air[FRAME_MAX];
static int bad_buffer;  // ticks where the frame going out changed under the firmware

static bool near(uint16_t us, uint16_t want) { return us > want - want / 5 && us < want + want / 5; }

// Pulse distance coding: a leader, bits of a mark of t and a space of t or
// one, a stop mark, a gap of at least 40 ms
static bool distance_frame(const uint16_t* f, int n, uint16_t lm, uint16_t ls, uint16_t t, uint16_t one, int bits,
                           uint64_t* code) {
    if (n != 2 + 2 * bits + 2 || !near(f[0], lm) || !near(f[1], ls) || !near(f[n - 2], t) || f[n - 1] < 40000)
        return false;
    *code = 0;
    for (int i = 0; i < bits; i++) {
        uint16_t m = f[2 + 2 * i], s = f[3 + 2 * i];
        if (!near(m, t) || !(near(s, t) || near(s, one))) return false;
        *code = *code << 1 | near(s, one);
    }
    return true;
}

// Sony: a 2.4 ms leader, 12 marks of 1.2 ms (1) or 0.6 ms (0), each with a
// space of 0.6 ms, the last until the next frame
static bool sony_frame(const uint16_t* f, int n, uint64_t* code) {
    if (n != 26 || !near(f[0], 2400) || !near(f[1], 600)) return false;
    *code = 0;
    for (int i = 0; i < 12; i++) {
        uint16_t m = f[2 + 2 * i], s = f[3 + 2 * i];
        if (!(near(m, 600) || near(m, 1200)) || (i < 11 && !near(s, 600))) return false;
        *code = *code << 1 | near(m, 1200);
    }
    return true;
}

// RC5: halves of 0.889 ms, each bit a space and a mark (1) or a mark and a
// space (0), 14 bits; the first half, a space, does not go out
static bool rc5_frame(const uint16_t* f, int n, uint64_t* code) {
    uint8_t half[28] = {0};  // 1: mark
    int h = 1;
    if (n < 2 || f[n - 1] < 40000) return false;
    for (int i = 0; i < n - 1; i++) {
        int halves = (f[i] + 444) / 889;
        if (halves < 1 || halves > 2 || !near(f[i], halves * 889)) return false;
        while (halves--) {
            if (h == 28) return false;
            half[h++] = !(i & 1);
        }
    }
    *code = 0;
    for (int b = 0; b < 14; b++) {
        if (half[2 * b] == half[2 * b + 1]) return false;
        *code = *code << 1 | half[2 * b + 1];
    }
    return true;
}

// RC6 mode 0: a leader of 2666 us mark and 889 us space, then units of 444
// us: the start bit, 3 mode bits, the toggle bit at twice the length and
// 16 bits, each a mark and a space (1) or a space and a mark (0)
static bool rc6_frame(const uint16_t* f, int n, uint64_t* code) {
    uint8_t unit[48] = {0};  // 1: mark
    int u = 0;
    if (n < 4 || !near(f[0], 2666) || !near(f[1], 889) || f[n - 1] < 40000) return false;
    for (int i = 2; i < n - 1; i++) {
        int units = (f[i] + 222) / 444;
        if (units < 1 || units > 4 || !near(f[i], units * 444)) return false;
        while (units--) {
            if (u == 48) return false;
            unit[u++] = !(i & 1);
        }
    }
    *code = 0;
    for (int b = 0, p = 0; b < 21; b++) {
        int w = b == 4 ? 2 : 1;
        if (unit[p] == unit[p + w]) return false;
        *code = *code << 1 | unit[p];
        p += 2 * w;
    }
    return true;
}

static void decode(const uint16_t* f, int n, uint32_t hz) {
    sent_t* s = &sent[nsent++ % 256];
    s->carrier = hz, s->us = 0, s->kind = '?';
    for (int i = 0; i < n; i++) s->us += f[i];
    if (distance_frame(f, n, 9000, 4500, 560, 1690, 32, &s->code)) s->kind = 'N';
    else if (distance_frame(f, n, 4500, 4500, 560, 1690, 32, &s->code)) s->kind = 'S';
    else if (distance_frame(f, n, 3456, 1728, 432, 1296, 48, &s->code)) s->kind = 'P';
    else if (sony_frame(f, n, &s->code)) s->kind = 'Y';
    else if (rc5_frame(f, n, &s->code)) s->kind = 'R';
    else if (rc6_frame(f, n, &s->code)) s->kind = 'C';
}

// One tick, with the transmitter busy while a frame goes out
static void run(uint32_t ms, uint8_t pressed, uint8_t held) {
    mock_ms += ms;
    mock_tx_busy = (int32_t)(mock_ms - air_end) < 0;
    if (mock_tx_busy && memcmp(mock_raw_us, air, mock_raw_count * 2)) bad_buffer++;
    mock_pressed_now = pressed, mock_held_now = held;
    unsigned before = mock_raws_sent;
    tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
    if (mock_raws_sent != before) {
        decode(mock_raw, mock_raw_count, mock_raw_carrier);
        memcpy(air, mock_raw_us, mock_raw_count * 2);
        air_end = mock_ms + sent[(nsent - 1) % 256].us / 1000;
    }
}

static void idle(uint32_t total_ms) {
    for (uint32_t t = 0; t < total_ms; t += 5) run(5, 0, 0);
}

static void tap(uint8_t b) {
    run(5, b, b);
    for (int i = 0; i < 16; i++) run(5, 0, b);
    run(5, 0, 0);
}

// NEC as some lists write it: the bytes in the order they go out, each
// sent LSB first (Hisense's 04FB718E)
#define REV8(b) ((((b) * 0x0802u & 0x22110u) | ((b) * 0x8020u & 0x88440u)) * 0x10101u >> 16 & 0xFF)
#define NEC_BYTES(x)                                                                          \
    ((uint64_t)REV8((x) >> 24 & 0xFF) << 24 | REV8((x) >> 16 & 0xFF) << 16 | REV8((x) >> 8 & 0xFF) << 8 | \
     REV8((x) & 0xFF))
#define RC5_TOGGLE (1 << 11)
#define RC6_TOGGLE (1 << 16)
static uint64_t toggle_bit(const sent_t* s) { return s->kind == 'R' ? RC5_TOGGLE : s->kind == 'C' ? RC6_TOGGLE : 0; }

// The power codes, as remote control lists give them. The toggles first,
// as Flipper-IRDB has them (NEC: address and command bytes in send order)
typedef struct {
    char kind;
    uint64_t on, off;
    uint32_t carrier;
    int repeats;
} want_t;
static const want_t WANT[] = {
    {'N', NEC_BYTES(0xEAC717E8u), NEC_BYTES(0xEAC717E8u), 38000, 1},  // TCL: EA C7, 17 E8
    {'N', NEC_BYTES(0x00BF0DF2u), NEC_BYTES(0x00BF0DF2u), 38000, 1},  // Hisense, newer
    {'N', NEC_BYTES(0x007F0AF5u), NEC_BYTES(0x007F0AF5u), 38000, 1},  // Sharp
    {'N', NEC_BYTES(0x027D46B9u), NEC_BYTES(0x027D46B9u), 38000, 1},  // Toshiba, newer
    {'R', 0x304C, 0x304C, 36000, 1},                                    // Vestel: RC5 1:12
    {'R', 0x30CC, 0x30CC, 36000, 1},                                    // Hitachi: RC5 3:12
    {'C', 0x10000C, 0x10000C, 36000, 1},                                // Philips, newer: RC6 0:12
    {'S', 0xE0E09966, 0xE0E019E6, 38000, 2},                            // Samsung
    {'N', 0x20DF23DC, 0x20DFA35C, 38000, 2},                            // LG
    {'N', NEC_BYTES(0x04FB718Eu), NEC_BYTES(0x04FB728Du), 38000, 2},    // Hisense
    {'Y', 0x750, 0xF50, 40000, 3},                                      // Sony TV: command 46 and 47, device 1
    {'P', 0x400401007C7D, 0x40040100FCFD, 36700, 2},                    // Panasonic
    {'R', 0x303F, 0x300C, 36000, 2},                                    // Philips: RC5 0:63 and 0:12 (standby)
    {'N', NEC_BYTES(0x40BF7E81u), NEC_BYTES(0x40BF7F80u), 38000, 2},    // Toshiba: NEC 64:126 and 64:127
};

// Do sent[from..] hold the codes of a whole run, ON or OFF
static bool run_was(int from, bool on, int* frames) {
    int k = from;
    for (unsigned b = 0; b < sizeof WANT / sizeof *WANT; b++)
        for (int r = 0; r < WANT[b].repeats; r++, k++) {
            const sent_t* s = &sent[k];
            uint64_t code = s->code & ~toggle_bit(s);
            if (k >= nsent || s->kind != WANT[b].kind || code != (on ? WANT[b].on : WANT[b].off) ||
                s->carrier != WANT[b].carrier) {
                printf("      frame %d: %c %llx at %u Hz\n", k - from, k < nsent ? s->kind : '-',
                       (unsigned long long)s->code, s->carrier);
                return false;
            }
        }
    *frames = k - from;
    return true;
}

// The RGB LEDs of an arm, by rank
static void arm_leds(int which, uint8_t c[RANKS][3]) {
    for (int i = 0; i < HH_RGB_COUNT; i++)
        if (layout_rgb[i].arm == which) mock_rgb(i, c[layout_rgb[i].rank]);
}

static int bright_ranks(int which) {
    uint8_t c[RANKS][3];
    arm_leds(which, c);
    int n = 0;
    for (int r = 0; r < RANKS; r++) n += c[r][0] == 255 || c[r][1] == 255;
    return n;
}

// The grid, as the app should draw it (the O of OFF has no LED at its
// bottom left corner)
static const char* const GRID[HH_GRID_H] = {
    "      .....      ", "    .###.#.#.    ", "   ...#..#.#..   ", "  ....#..#.#...  ",
    " .....#...#..... ", ".................", ".....###.##......", ".....#.#.#.#.....",
    ".....#.#.#.#.....", ".....###.#.#.....", ".................", " ..###.###.###.. ",
    "  .#.#.#...#...  ", "   #.#.##..##.   ", "    ##.#...#.    ", "      .....      ",
};

int main(void) {
    init(&mock_api);
    idle(100);

    // The grid: TV, ON, OFF, every pixel where the octagon has an LED
    int wrong = 0, missing = 0;
    for (int y = 0; y < HH_GRID_H; y++)
        for (int x = 0; x < HH_GRID_W; x++) {
            uint16_t s = layout_grid[y][x];
            bool want = GRID[y][x] == '#';
            if (s == HH_NONE) missing += want;
            else wrong += want != (mock_leds[s] != 0);
        }
    CHECK(!wrong && !missing, "the grid shows TV, ON and OFF (%d pixels wrong, %d off the octagon)", wrong, missing);
    CHECK(mock_leds[layout_grid[1][5]] == 255 && mock_leds[layout_grid[6][5]] == 150 &&
              mock_leds[layout_grid[11][3]] == 150,
          "TV bright, ON and OFF less (%d, %d, %d)", mock_leds[layout_grid[1][5]], mock_leds[layout_grid[6][5]],
          mock_leds[layout_grid[11][3]]);

    // The arms: A's and B's tips are the ones nearest the buttons
    // (SW1 at -41.9, -18.1 mm, SW2 at 42.4, -15.6 mm)
    int a_angle = AppUtil_Atan8(-419, -181), b_angle = AppUtil_Atan8(424, -156);
    int near_a = -1, near_b = -1, best_a = 256, best_b = 256;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        if (layout_rgb[i].rank != RANKS - 1) continue;
        int da = abs((int8_t)(layout_polar[layout_rgb[i].r][1] - a_angle));
        int db = abs((int8_t)(layout_polar[layout_rgb[i].r][1] - b_angle));
        if (da < best_a) best_a = da, near_a = layout_rgb[i].arm;
        if (db < best_b) best_b = db, near_b = layout_rgb[i].arm;
    }
    CHECK(near_a == ARM_A && near_b == ARM_B, "arm %d leads to A and arm %d to B (%d, %d)", ARM_A, ARM_B, near_a,
          near_b);
    uint8_t a[RANKS][3], b[RANKS][3];
    arm_leds(ARM_A, a), arm_leds(ARM_B, b);
    CHECK(a[0][0] > 40 && a[0][0] == a[0][1] && a[0][1] == a[0][2] && b[0][0] == b[0][1] && b[0][1] == b[0][2],
          "both start white at the centre (%d %d %d, %d %d %d)", a[0][0], a[0][1], a[0][2], b[0][0], b[0][1], b[0][2]);
    CHECK(a[6][1] > 40 && !a[6][0] && !a[6][2], "A's ends green at the button (%d %d %d)", a[6][0], a[6][1], a[6][2]);
    CHECK(b[6][0] > 40 && !b[6][1] && !b[6][2], "B's ends red at the button (%d %d %d)", b[6][0], b[6][1], b[6][2]);
    bool fades = true;
    for (int r = 1; r < RANKS; r++)
        fades &= a[r][0] * 255 / a[r][1] < a[r - 1][0] * 255 / a[r - 1][1] &&
                 b[r][1] * 255 / b[r][0] < b[r - 1][1] * 255 / b[r - 1][0];
    CHECK(fades, "and get greener and redder rank by rank");
    int others = 0;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t c[3];
        mock_rgb(i, c);
        if (layout_rgb[i].arm != ARM_A && layout_rgb[i].arm != ARM_B) others += c[0] + c[1] + c[2];
    }
    CHECK(!others, "the other arms are dark");
    idle(400);
    uint8_t later[RANKS][3];
    arm_leds(ARM_A, later);
    CHECK(memcmp(a, later, sizeof a), "a wave runs along them");
    CHECK(!mock_raws_sent, "nothing goes out by itself");

    // A: power on, the arm to A filling up while it goes out
    int from = nsent, frames = 0, fill[3] = {0};
    tap(HH_BTN_A);
    CHECK(mock_raws_sent == 1, "a tap of A starts at its release");
    CHECK(mock_leds[layout_grid[6][5]] == 255 && mock_leds[layout_grid[11][3]] == 40, "ON lights up, OFF dims");
    for (int t = 0; t < 3; t++) idle(500), fill[t] = bright_ranks(ARM_A);
    idle(3000);
    CHECK(run_was(from, true, &frames) && nsent == from + frames, "A sends the power-on codes (%d frames)",
          nsent - from);
    CHECK(fill[0] < fill[1] && fill[1] < fill[2] && !bright_ranks(ARM_B), "the arm to A fills up (%d, %d, %d)",
          fill[0], fill[1], fill[2]);
    uint32_t total = 0;
    int sony = 0;
    for (int i = from; i < nsent; i++) total += sent[i].us, sony += sent[i].kind == 'Y' && sent[i].us == 45000;
    CHECK(total > 1800000 && total < 2600000, "in about two seconds (%u ms)", total / 1000);
    CHECK(sony == 3, "Sony frames 45 ms apart (%d)", sony);
    CHECK(mock_leds[layout_grid[6][5]] == 150 && !bright_ranks(ARM_A), "then it is calm again");

    // B: power off
    int on_from = from;
    from = nsent;
    run(5, HH_BTN_B, HH_BTN_B);
    run(5, 0, 0);
    CHECK(mock_leds[layout_grid[11][3]] == 255 && mock_leds[layout_grid[6][5]] == 40, "B: OFF lights up, ON dims");
    idle(400);
    CHECK(bright_ranks(ARM_B) && !bright_ranks(ARM_A), "the arm to B fills up");
    idle(3000);
    CHECK(run_was(from, false, &frames) && nsent == from + frames, "B sends the power-off codes (%d frames)",
          nsent - from);
    // RC5 and RC6: the toggle bit the same in a press, the other in the next
    int set[2] = {0}, clear[2] = {0}, rc = 0;
    for (unsigned b = 0; b < sizeof WANT / sizeof *WANT; b++)
        rc += WANT[b].kind == 'R' || WANT[b].kind == 'C' ? WANT[b].repeats : 0;
    for (int i = on_from; i < nsent; i++)
        if (toggle_bit(&sent[i])) {
            bool t = sent[i].code & toggle_bit(&sent[i]);
            (t ? set : clear)[i >= from]++;
        }
    CHECK((set[0] == rc && clear[1] == rc) || (clear[0] == rc && set[1] == rc),
          "RC5 and RC6: one toggle bit for a press, the other for the next (%d %d, %d %d)", set[0], clear[0], set[1],
          clear[1]);

    // Holding A opens the menu: no codes
    from = nsent;
    run(5, HH_BTN_A, HH_BTN_A);
    for (int i = 0; i < 240; i++) run(5, 0, HH_BTN_A);
    run(5, 0, 0);
    idle(500);
    CHECK(nsent == from, "a long press of A sends nothing (%d frames)", nsent - from);

    // B while ON goes out: from then on the power-off codes, from the start
    tap(HH_BTN_A);
    idle(300);
    run(5, HH_BTN_B, HH_BTN_B);
    run(5, 0, 0);
    int switched = nsent;
    idle(3000);
    CHECK(sent[from].code == WANT[0].on && run_was(switched, false, &frames) && nsent == switched + frames,
          "B during ON: the power-off codes follow (%d frames)", nsent - switched);

    CHECK(!bad_buffer, "a frame stays as it is while it goes out (%d ticks)", bad_buffer);
    return mock_report("tvbon");
}
