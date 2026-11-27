// TVBOn: turns the TVs around you on or off, by infrared. Press A: the
// badge sends the power-on code of each TV brand it knows (Samsung, LG,
// Hisense, Sony, Panasonic, Philips, Toshiba): the arm to A fills up
// green, faster and faster, then flashes; in its second half a laser
// flickers at the arm's tip, wilder and wilder, until the arm all but
// bursts at the end. Press B: the power-off codes, the same in red to B.
// Brands with only a power toggle (TCL, Xiaomi, newer Hisense, Philips and
// Toshiba, Sharp, Telefunken and others by Vestel, Hitachi) get it with A
// and with B, first, so a TV that knows both ends with its own on or off.
// Philips's off is the standby button, which toggles on newer Philips TVs.
// A press takes about two seconds.
#include "hhapp.h"

#define ARM_A 4              // the upper left arm, with A at its tip
#define ARM_B 1              // the upper right arm, with B at its tip
#define RANKS 7              // RGB LEDs on an arm, 0 nearest the centre
#define TAP_MS 600           // A held longer is the menu (it opens at 1 s), not ON
#define FILL_MS 700          // the arm fills up in this time, then flashes until the codes are out
#define DONE_MS 600          // and this long after the last code
#define ARM_HALF 26          // an arm's outline, either side of its tip's angle (1/256 turn)
#define BEAMS 4              // laser beams at once
#define BEAM_SPEED 2         // 1/4 mm per ms: a beam runs to the tip in about 30 ms
#define BEAM_LEN 24          // its length, 6 mm
#define GAP_US 50000         // quiet after a NEC, Samsung or Panasonic frame
#define SONY_FRAME_US 45000  // a Sony frame starts every 45 ms
#define RC5_HALF_US 889      // half an RC5 bit
#define RC6_UNIT_US 444      // an RC6 half bit
#define XIAOMI_UNIT_US 290   // Xiaomi's timing unit
#define XIAOMI_FRAME_US 30000  // a Xiaomi frame starts every 30 ms
#define RC_GAP_US 65000      // after RC5 and RC6 (RC5 repeats every 114 ms; a space holds at most 65 ms)
#define FRAME_MAX 100        // a Panasonic frame: leader, 48 bits, stop mark, gap

enum { NEC, SAMSUNG, PANASONIC, SONY, RC5, RC6, XIAOMI };
enum { IDLE, ON, OFF };

// The carrier of each protocol, Hz. Xiaomi: the JP1 forum's capture says
// 36 kHz, Xiaomi's own Mi Remote database 38.
static const uint16_t CARRIER[] = {38000, 38000, 36700, 40000, 36000, 36000, 38000};

// Pulse distance coding (NEC, Samsung, Panasonic): a leader, then bytes
// LSB first, each bit a mark of t and a space of t (0) or one (1), then a
// stop mark and GAP_US
typedef struct {
    uint16_t lead_mark, lead_space, t, one;
} distance_t;
static const distance_t DISTANCE[] = {
    [NEC] = {9000, 4500, 560, 1690},
    [SAMSUNG] = {4500, 4500, 560, 1690},
    [PANASONIC] = {3456, 1728, 432, 1296},
};

// The brands: a protocol, how often each frame goes out, the address and
// the power-on and power-off commands, or the power toggle twice for a
// brand that has nothing else (Flipper-IRDB's most common one). NEC and
// Samsung send the address bytes a0 a1, then the command and its inverse.
// Sony sends the command's 7 bits, then a1 bits of the address a0.
// Panasonic sends its vendor code, a0 a1, the command and a check byte.
// RC5, RC6 (Philips) and Xiaomi send the address a0 and the command. A
// toggle has the same command for on and off, and goes out once: twice
// could count as two presses. The toggles go first. LG goes before
// Hisense: on an LG, Hisense's on and off are the green and red buttons,
// so after LG's off the red one finds the TV off.
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

// TV, ON and OFF on the grid: a word to 4 rows from row 1, 6 and 11, each
// row the columns 3 to 13, the left one in bit 10. OFF's O loses its
// bottom left corner, where the octagon has no LED.
static const uint16_t WORDS[3][4] = {
    {0b00111010100,  // TV
     0b00010010100,
     0b00010010100,
     0b00010001000},
    {0b00111011000,  // ON
     0b00101010100,
     0b00101010100,
     0b00111010100},
    {0b11101110111,  // OFF
     0b10101000100,
     0b10101100110,
     0b11101000100},
};

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
static uint8_t toggle;      // RC5's and RC6's toggle bit, new for each press
static uint32_t run_ms;     // since the press
static uint8_t blast_from;  // the frame the blast started at, frames while it fills up
static uint8_t beam[BEAMS];   // the front of each laser beam (polar distance), 0: none
static uint8_t tip_angle[2];  // where the arms to A and B point (polar)

// A frame of n bytes in pulse distance coding d
static uint16_t distance(uint16_t* f, const distance_t* d, const uint8_t* bytes, int n) {
    uint16_t k = 0;
    f[k++] = d->lead_mark;
    f[k++] = d->lead_space;
    for (int i = 0; i < n * 8; i++) {
        f[k++] = d->t;
        f[k++] = bytes[i / 8] >> (i % 8) & 1 ? d->one : d->t;
    }
    f[k++] = d->t;
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

// A Philips RC5 frame: 14 bits of 2 x 889 us, MSB first, each a space and
// a mark (1) or a mark and a space (0): two start bits (both 1 for
// commands below 64), the toggle bit, 5 address bits, 6 command bits
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

// A Philips RC6 (mode 0) frame: a leader of 2666 us mark and 889 us space,
// then bits of 2 x 444 us, MSB first, each a mark and a space (1) or a
// space and a mark (0): the start bit (1), 3 mode bits (0), the toggle bit
// at twice the length, 8 address bits, 8 command bits
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
    *hz = CARRIER[b->proto];
    switch (b->proto) {
        case SONY:
            return sony(f, c, b->a0, b->a1);
        case RC5:
            return rc5(f, b->a0, c);
        case RC6:
            return rc6(f, b->a0, c);
        case XIAOMI:
            return xiaomi(f, b->a0, c);
        case PANASONIC: {
            // the vendor code 0x2002, the address, the command, a check byte
            uint8_t bytes[6] = {0x02, 0x20, b->a0, b->a1, c, (uint8_t)(b->a0 ^ b->a1 ^ c)};
            return distance(f, &DISTANCE[PANASONIC], bytes, 6);
        }
        default: {
            uint8_t bytes[4] = {b->a0, b->a1, c, (uint8_t)~c};
            return distance(f, &DISTANCE[b->proto], bytes, 4);
        }
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

// An arm from white at the centre to its colour at the button (green to
// A, red to B). A wave runs out towards the button; lit ranks go to full.
// At overload (0 .. 255) the LEDs flicker each on their own.
static void arm(uint8_t which, bool red, uint8_t level, int lit, uint8_t overload) {
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        if (l->arm != which) continue;
        uint8_t white = (uint8_t)(255 - l->rank * 255 / (RANKS - 1));
        uint8_t wave = hh->sin8((uint8_t)(l->rank * 40 - ms / 3));
        uint32_t v = l->rank < lit ? 255 : level * (160u + wave * 95u / 255) / 255;
        if (overload && hh->random() % 256 < overload) v = hh->random() % 256;
        uint8_t full = (uint8_t)v, part = (uint8_t)(white * v / 255);
        hh->set_rgb(i, red ? full : part, red ? part : full, part);
    }
}

// Is outline LED s on the arm at angle a
static bool on_arm(uint8_t a, uint16_t s) {
    int off = (int8_t)(hh->polar[s][1] - a);
    return off >= -ARM_HALF && off <= ARM_HALF;
}

// The laser at the top of the blast, the outer half of the arm at angle a:
// beams shoot out along its outline to the tip, flickering at random, with
// crackle round them, most at the tip; more of both as power (0 .. 255)
// grows, none at 0
static void laser(uint8_t a, uint8_t power, uint32_t elapsed_ms) {
    uint8_t root = 255, top = 0;  // the arm's outline, nearest and farthest (polar distance)
    for (int j = 0; j < HH_EDGE_COUNT; j++)
        if (on_arm(a, hh->edge[j])) {
            uint8_t r = hh->polar[hh->edge[j]][0];
            if (r < root) root = r;
            if (r > top) top = r;
        }
    uint8_t from = (uint8_t)((root + top) / 2);  // the blast's top half starts here
    for (int b = 0; b < BEAMS; b++) {
        if (!beam[b]) continue;
        uint32_t r = beam[b] + elapsed_ms * BEAM_SPEED;
        beam[b] = r > (uint32_t)(top + BEAM_LEN) ? 0 : (uint8_t)r;
    }
    if (power && hh->random() % 2048 < power)
        for (int b = 0; b < BEAMS; b++)
            if (!beam[b]) {
                beam[b] = from;
                break;
            }
    for (int j = 0; j < HH_EDGE_COUNT; j++) {
        uint16_t s = hh->edge[j];
        uint8_t r = hh->polar[s][0], v = 0;
        if (!on_arm(a, s) || r < from) continue;
        for (int b = 0; b < BEAMS; b++)
            if (beam[b] && r <= beam[b] && r + BEAM_LEN >= beam[b]) v = (uint8_t)(96 + hh->random() % 160);
        uint32_t x = (r - from + 2u) * 64 / (top - from + 2u);  // 64 at the tip
        if (power && hh->random() % 2048 < power * x * x / 1024) v = 255;  // up to 1 in 2 ticks at the tip
        hh->set(s, v);
    }
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
    // faster and faster, until the codes are out. From halfway through the
    // blast the power builds up: a laser at the arm's tip, and at the end an
    // overload, the arm's LEDs flickering each on their own.
    bool busy = mode != IDLE || done_ms;
    if (mode != IDLE) run_ms += elapsed_ms;
    if (mode != IDLE && run_ms >= FILL_MS && blast_from == frames) blast_from = frame;
    bool blast = done_ms || (mode != IDLE && run_ms >= FILL_MS);
    uint8_t e = 0;  // how far the blast has come, by the frames sent since it started
    if (done_ms) e = 255;
    else if (blast) e = (uint8_t)((frame - blast_from) * 255 / (frames - blast_from));
    int lit = mode != IDLE ? (int)(RANKS * run_ms * run_ms / (FILL_MS * FILL_MS)) : 0;
    uint32_t half = e > 128 ? (e - 128u) * 2 : 0;
    uint8_t power = (uint8_t)(half * half / 255), overload = power > 128 ? (uint8_t)((power - 128) * 2) : 0;
    uint8_t level = 120;
    if (blast) {
        bool on = ms / (150 - e * 90 / 255) % 2 == 0;
        lit = on ? RANKS : 0, level = 40;
    }
    hh->clear();
    arm(ARM_A, false, busy ? (last == ON ? level : 40) : 90, busy && last == ON ? lit : 0, last == ON ? overload : 0);
    arm(ARM_B, true, busy ? (last == OFF ? level : 40) : 90, busy && last == OFF ? lit : 0, last == OFF ? overload : 0);
    laser(tip_angle[last == OFF], power, elapsed_ms);

    // The grid: TV, then ON and OFF; while one goes out, that one bright
    uint8_t v[3] = {255, busy ? (last == ON ? 255 : 40) : 150, busy ? (last == OFF ? 255 : 40) : 150};
    for (int w = 0; w < 3; w++)
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 11; c++)
                if (WORDS[w][r] >> (10 - c) & 1) hh->set(hh->grid[1 + 5 * w + r][3 + c], v[w]);
}

static void init(const hh_api_t* api) {
    hh = api;
    for (unsigned i = 0; i < BRAND_COUNT; i++) frames += BRANDS[i].repeats;
    for (int i = 0; i < HH_RGB_COUNT; i++)
        if (hh->rgb[i].rank == RANKS - 1 && (hh->rgb[i].arm == ARM_A || hh->rgb[i].arm == ARM_B))
            tip_angle[hh->rgb[i].arm == ARM_B] = hh->polar[hh->rgb[i].r][1];
}

HH_APP(init, tick);
