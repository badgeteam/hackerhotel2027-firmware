// TVBOn: turns the TVs around you on or off, by infrared. Press A: the
// badge sends the power-on code of each TV brand it knows (Samsung, LG,
// Hisense, Sony, Panasonic, Philips, Toshiba): the arm to A fills up
// green, faster and faster, then flashes while sparks fly off it on the
// outline, more of them the longer it sends. Press B: the power-off
// codes, the same in red to B. Brands with only a power toggle (TCL, Xiaomi, newer
// Hisense, Philips and Toshiba, Sharp, Telefunken and others by Vestel,
// Hitachi)
// get it with A and with B, first, so a TV that knows both ends with its
// own on or off. Philips's off is the standby button, which toggles on
// newer Philips TVs. A press takes about two seconds.
#include "hhapp.h"

#define ARM_A 4              // the upper left arm, with A at its tip
#define ARM_B 1              // the upper right arm, with B at its tip
#define RANKS 7              // RGB LEDs on an arm, 0 nearest the centre
#define TAP_MS 600           // A held longer is the menu (it opens at 1 s), not ON
#define FILL_MS 700          // the arm fills up in this time, then flashes until the codes are out
#define DONE_MS 600          // and this long after the last code
#define GAP_US 50000         // quiet after a NEC, Samsung or Panasonic frame
#define SONY_FRAME_US 45000  // a Sony frame starts every 45 ms
#define RC5_HALF_US 889      // half an RC5 bit
#define RC6_UNIT_US 444      // an RC6 half bit
#define XIAOMI_UNIT_US 290   // Xiaomi's timing unit
#define XIAOMI_FRAME_US 30000  // a Xiaomi frame starts every 30 ms
#define RC_GAP_US 65000      // after RC5 and RC6 (RC5 repeats every 114 ms; a space holds at most 65 ms)
#define FRAME_MAX 100        // a Panasonic frame: leader, 48 bits, stop mark, gap

enum { NEC, SAMSUNG, SONY, PANASONIC, RC5, RC6, XIAOMI };
enum { IDLE, ON, OFF };

// The brands: a protocol, how often each frame goes out, the address and
// the power-on and power-off commands, or the power toggle twice for a
// brand that has nothing else (Flipper-IRDB's most common one). NEC and
// Samsung send the address bytes a0 a1, then the command and its inverse.
// Sony sends the command's 7 bits, then a1 bits of the address a0.
// Panasonic sends its vendor code, a0 a1, the command and a check byte.
// RC5, RC6 (Philips) and Xiaomi send the address a0 and the command. A toggle has
// the same command for on and off, and goes out once: twice could count as
// two presses. The toggles go first. LG goes before Hisense: on an LG,
// Hisense's on and off are the green and red buttons, so after LG's off
// the red one finds the TV off.
typedef struct {
    uint8_t proto, repeats;
    uint8_t a0, a1;
    uint8_t on, off;
} brand_t;
static const brand_t BRANDS[] = {
    {NEC, 1, 0xEA, 0xC7, 0x17, 0x17},  // TCL and other Roku TVs
    {XIAOMI, 1, 0x3C, 0, 0xCC, 0xCC},  // Xiaomi
    {NEC, 1, 0x00, 0xBF, 0x0D, 0x0D},  // Hisense, newer
    {NEC, 1, 0x00, 0x7F, 0x0A, 0x0A},  // Sharp, Blaupunkt
    {NEC, 1, 0x02, 0x7D, 0x46, 0x46},  // Toshiba and Telefunken, newer
    {RC5, 1, 0x01, 0, 0x0C, 0x0C},     // Telefunken, Finlux, Medion, JVC (Vestel)
    {RC5, 1, 0x03, 0, 0x0C, 0x0C},     // Hitachi
    {RC6, 1, 0x00, 0, 0x0C, 0x0C},     // Philips, newer
    {SAMSUNG, 2, 0x07, 0x07, 0x99, 0x98},
    {NEC, 2, 0x04, 0xFB, 0xC4, 0xC5},  // LG
    {NEC, 2, 0x04, 0xFB, 0x71, 0x72},  // Hisense (its RS-232/IR document, 2017)
    {SONY, 3, 0x01, 5, 0x2E, 0x2F},     // a TV: device 1
    {PANASONIC, 2, 0x80, 0x00, 0x3E, 0x3F},
    {RC5, 2, 0x00, 0, 0x3F, 0x0C},     // Philips: system 0; off is standby
    {NEC, 2, 0x40, 0xBF, 0x7E, 0x7F},  // Toshiba
};
#define BRAND_COUNT (sizeof BRANDS / sizeof *BRANDS)

static const hh_api_t* hh;
static uint8_t mode, last;          // what goes out now (ON, OFF or IDLE), and what went out last
static uint8_t frame, frames;       // the next frame of a run, and how many a run has
static uint16_t buf[2][FRAME_MAX];  // two frames: the one going out, and the next
static uint16_t len[2];
static uint32_t carrier[2];
static uint8_t next;  // the buffer for the next frame
static bool built;    // buf[next] holds frame `frame`
static uint32_t ms, a_down, done_ms;
static bool a_tap;
static uint8_t toggle;  // RC5's and RC6's toggle bit, new for each press
static uint32_t run_ms;  // since the press
static uint8_t blast_from;  // the frame the blast started at, frames while it fills up
static uint8_t spark[HH_EDGE_COUNT];  // the sparks on the outline, fading
static uint8_t tip_angle[2];          // where the arms to A and B point (polar)

// A frame of pulse distance coding (NEC, Samsung, Panasonic): a leader,
// then n bytes LSB first, each bit a mark of t and a space of t (0) or
// one (1), then a stop mark and the gap
static uint16_t distance(uint16_t* f, uint16_t lead_mark, uint16_t lead_space, uint16_t t, uint16_t one,
                         const uint8_t* bytes, int n) {
    uint16_t k = 0;
    f[k++] = lead_mark;
    f[k++] = lead_space;
    for (int i = 0; i < n * 8; i++) {
        f[k++] = t;
        f[k++] = bytes[i / 8] >> (i % 8) & 1 ? one : t;
    }
    f[k++] = t;
    f[k++] = GAP_US;
    return k;
}

// A Sony frame: a 2.4 ms leader, then 7 command bits and the address bits,
// LSB first, each a mark of 1.2 ms (1) or 0.6 ms (0) and a space of 0.6 ms.
// The last space lasts until the next frame is due.
static uint16_t sony(uint16_t* f, uint8_t command, uint8_t address, uint8_t address_bits) {
    uint16_t k = 0;
    uint32_t total = 2400 + 600;
    f[k++] = 2400;
    f[k++] = 600;
    uint16_t bits = (uint16_t)(command | address << 7);
    for (int i = 0; i < 7 + address_bits; i++) {
        f[k] = bits >> i & 1 ? 1200 : 600;
        total += f[k++] + 600;
        f[k++] = 600;
    }
    f[k - 1] = (uint16_t)(SONY_FRAME_US - total + 600);
    return k;
}

// Adds a stretch of mark or space to a frame of k entries: to the last one
// if that is the same (marks are the even entries). A frame starts with a
// mark, so a space at the start is left out.
static uint16_t level(uint16_t* f, uint16_t k, bool mark, uint16_t us) {
    if (k && (k % 2 == 1) == mark) f[k - 1] += us;
    else if (k || mark) f[k++] = us;
    return k;
}

// Ends a frame with the gap after RC5 and RC6
static uint16_t rc_gap(uint16_t* f, uint16_t k) {
    if (k % 2) f[k++] = RC_GAP_US;
    else f[k - 1] = RC_GAP_US;
    return k;
}

// A Philips RC5 frame at 36 kHz: 14 bits of 2 x 889 us, MSB first, each a
// space and a mark (1) or a mark and a space (0): two start bits (both 1
// for commands below 64), the toggle bit, 5 address bits, 6 command bits
static uint16_t rc5(uint16_t* f, uint8_t address, uint8_t command) {
    uint16_t bits = (uint16_t)(3 << 12 | toggle << 11 | (address & 31) << 6 | (command & 63));
    uint16_t k = 0;
    for (int i = 13; i >= 0; i--) {
        bool one = bits >> i & 1;
        k = level(f, k, !one, RC5_HALF_US);
        k = level(f, k, one, RC5_HALF_US);
    }
    return rc_gap(f, k);
}

// A Philips RC6 (mode 0) frame at 36 kHz: a leader of 2666 us mark and 889
// us space, then bits of 2 x 444 us, MSB first, each a mark and a space
// (1) or a space and a mark (0): the start bit (1), 3 mode bits (0), the
// toggle bit at twice the length, 8 address bits, 8 command bits
static uint16_t rc6(uint16_t* f, uint8_t address, uint8_t command) {
    uint32_t bits = 1u << 20 | (uint32_t)toggle << 16 | (uint32_t)address << 8 | command;
    uint16_t k = level(f, 0, true, 6 * RC6_UNIT_US);
    k = level(f, k, false, 2 * RC6_UNIT_US);
    for (int i = 20; i >= 0; i--) {
        bool one = bits >> i & 1;
        uint16_t us = i == 16 ? 2 * RC6_UNIT_US : RC6_UNIT_US;
        k = level(f, k, one, us);
        k = level(f, k, !one, us);
    }
    return rc_gap(f, k);
}

// A Xiaomi frame (a kind of RC-MM): a 1 ms mark and a 2 unit space, then
// the address, the command and a check nibble (all their nibbles XORed),
// MSB first, two bits to a symbol: a 2 unit mark and a space of 2 to 5
// units; then a stop mark, and quiet until the next frame is due
static uint16_t xiaomi(uint16_t* f, uint8_t address, uint8_t command) {
    uint8_t check = (address >> 4 ^ address ^ command >> 4 ^ command) & 15;
    uint32_t bits = (uint32_t)address << 12 | (uint32_t)command << 4 | check;
    uint16_t k = 0;
    uint32_t total = 1000 + 2 * XIAOMI_UNIT_US;
    f[k++] = 1000;
    f[k++] = 2 * XIAOMI_UNIT_US;
    for (int i = 18; i >= 0; i -= 2) {
        f[k++] = 2 * XIAOMI_UNIT_US;
        f[k] = (uint16_t)((2 + (bits >> i & 3)) * XIAOMI_UNIT_US);
        total += f[k - 1] + f[k];
        k++;
    }
    f[k++] = 2 * XIAOMI_UNIT_US;
    f[k++] = (uint16_t)(XIAOMI_FRAME_US - total - 2 * XIAOMI_UNIT_US);
    return k;
}

// Frame i of a run: the brands in order, each its number of repeats
static uint16_t build(uint16_t* f, uint8_t i, uint8_t m, uint32_t* hz) {
    const brand_t* b = BRANDS;
    while (i >= b->repeats) i -= b->repeats, b++;
    uint8_t c = m == ON ? b->on : b->off;
    uint8_t bytes[6] = {b->a0, b->a1, c, (uint8_t)~c};
    switch (b->proto) {
        case SONY:
            *hz = 40000;
            return sony(f, c, b->a0, b->a1);
        case PANASONIC:
            bytes[0] = 0x02, bytes[1] = 0x20;  // the vendor code, 0x2002
            bytes[2] = b->a0, bytes[3] = b->a1, bytes[4] = c, bytes[5] = b->a0 ^ b->a1 ^ c;
            *hz = 36700;
            return distance(f, 3456, 1728, 432, 1296, bytes, 6);
        case RC5:
            *hz = 36000;
            return rc5(f, b->a0, c);
        case RC6:
            *hz = 36000;
            return rc6(f, b->a0, c);
        case XIAOMI:
            *hz = 38000;  // the JP1 forum's capture says 36 kHz, Xiaomi's own Mi Remote database 38
            return xiaomi(f, b->a0, c);
        case SAMSUNG:
            *hz = 38000;
            return distance(f, 4500, 4500, 560, 1690, bytes, 4);
        default:
            *hz = 38000;
            return distance(f, 9000, 4500, 560, 1690, bytes, 4);
    }
}

static void start(uint8_t m) {
    mode = last = m;
    frame = 0;
    toggle ^= 1;
    built = false;
    done_ms = run_ms = 0;
    blast_from = frames;
}

// Sends the next frame once the one before has gone out. The firmware reads
// a frame while it sends it, so the next one goes in the other buffer.
static void send(void) {
    if (!built) len[next] = build(buf[next], frame, mode, &carrier[next]), built = true;
    if (!hh->ir_send_raw(buf[next], len[next], carrier[next])) return;
    next ^= 1;
    built = false;
    if (++frame == frames) mode = IDLE, done_ms = DONE_MS;
}

// The words, 3 x 4 pixels a letter: a row per nibble, bit 0 the left column.
// OFF loses the O's bottom left corner, where the octagon has no LED.
static const char LETTERS[] = "TVONF";
static const uint16_t GLYPHS[] = {0x2227, 0x2555, 0x7557, 0x5553, 0x1317};

static void word(int x, int y, const char* t, uint8_t v) {
    for (; *t; t++) {
        int g = 0;
        while (LETTERS[g] != *t) g++;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++)
                if (GLYPHS[g] >> (r * 4 + c) & 1) hh->set(hh->grid[y + r][x + c], v);
        x += 4;
    }
}

// An arm from white at the centre to its colour at the button (green to
// A, red to B). A wave runs out towards the button; lit ranks go to full.
static void arm(uint8_t which, bool red, uint8_t level, int lit) {
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        if (l->arm != which) continue;
        uint8_t white = (uint8_t)(255 - l->rank * 255 / (RANKS - 1));
        uint8_t wave = hh->sin8((uint8_t)(l->rank * 40 - ms / 3));
        uint32_t v = l->rank < lit ? 255 : level * (160u + wave * 95u / 255) / 255;
        uint8_t full = (uint8_t)v, part = (uint8_t)(white * v / 255);
        hh->set_rgb(i, red ? full : part, red ? part : full, part);
    }
}

// Sparks on the outline of the arm at angle a, flying off the blast: from
// its tip at first, then more of them and down the arm's sides as e (0 ..
// 255) grows; each fades in about 0.15 s
static void sparks(uint8_t a, uint8_t e, uint32_t elapsed_ms) {
    for (int j = 0; j < HH_EDGE_COUNT; j++) spark[j] = spark[j] > elapsed_ms * 2 ? spark[j] - elapsed_ms * 2 : 0;
    int width = 8 + e * 20 / 255;  // half the arc, in 1/256 turn: the arm, not the dips beside it
    for (int n = e ? 1 + e / 40 : 0, tries = 0; n > 0 && tries < 64; tries++) {
        int j = (int)(hh->random() % HH_EDGE_COUNT);
        int off = (int8_t)(hh->polar[hh->edge[j]][1] - a);
        if (off < 0) off = -off;
        if (off <= width) spark[j] = 255, n--;
    }
    for (int j = 0; j < HH_EDGE_COUNT; j++) hh->set(hh->edge[j], spark[j]);
}

static void tick(uint32_t elapsed_ms) {
    ms += elapsed_ms;
    uint8_t p = hh->pressed(), h = hh->held();
    done_ms = done_ms > elapsed_ms ? done_ms - elapsed_ms : 0;

    // A on release after a short tap; a long one opens the menu
    if (p & HH_BTN_A) a_down = ms, a_tap = true;
    if (a_tap && ms - a_down > TAP_MS) a_tap = false;
    if (a_tap && !(h & HH_BTN_A)) a_tap = false, start(ON);
    if (p & HH_BTN_B) start(OFF);
    if (mode != IDLE) send();

    // The arms: dim, with a wave running out, while nothing happens. The
    // active one fills up, slowly and then faster, then blasts: it flashes,
    // faster and faster, with sparks around it, until the codes are out.
    bool busy = mode != IDLE || done_ms;
    if (mode != IDLE) run_ms += elapsed_ms;
    if (mode != IDLE && run_ms >= FILL_MS && blast_from == frames) blast_from = frame;
    bool blast = done_ms || (mode != IDLE && run_ms >= FILL_MS);
    uint8_t e = 0;  // how far the blast has come, by the frames sent since it started
    if (done_ms) e = 255;
    else if (blast) e = (uint8_t)((frame - blast_from) * 255 / (frames - blast_from));
    int lit = mode != IDLE ? (int)(RANKS * run_ms * run_ms / (FILL_MS * FILL_MS)) : 0;
    uint8_t level = 120;
    if (blast) {
        bool on = ms / (150 - e * 90 / 255) % 2 == 0;
        lit = on ? RANKS : 0, level = 40;
    }
    hh->clear();
    arm(ARM_A, false, busy ? (last == ON ? level : 40) : 90, busy && last == ON ? lit : 0);
    arm(ARM_B, true, busy ? (last == OFF ? level : 40) : 90, busy && last == OFF ? lit : 0);
    sparks(tip_angle[last == OFF], blast ? e : 0, elapsed_ms);

    // The grid: TV, then ON and OFF; while one goes out, that one bright
    word(5, 1, "TV", 255);
    word(5, 6, "ON", busy ? (last == ON ? 255 : 40) : 150);
    word(3, 11, "OFF", busy ? (last == OFF ? 255 : 40) : 150);
}

static void init(const hh_api_t* api) {
    hh = api;
    for (unsigned i = 0; i < BRAND_COUNT; i++) frames += BRANDS[i].repeats;
    for (int i = 0; i < HH_RGB_COUNT; i++)
        if (hh->rgb[i].rank == RANKS - 1 && (hh->rgb[i].arm == ARM_A || hh->rgb[i].arm == ARM_B))
            tip_angle[hh->rgb[i].arm == ARM_B] = hh->polar[hh->rgb[i].r][1];
}

HH_APP(init, tick);
