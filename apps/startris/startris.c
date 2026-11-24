// Startris: a 9-wide, 14-deep well in the middle of the octagon. C / D move
// the piece (hold to repeat), tap A to rotate, B drops it. Full rows clear;
// the more lines, the faster the pieces fall. The arms show the next
// piece's colour and fill with the lines you cleared. When a new piece has
// no room, the game is over: the grid shows the lines, the outline the
// best (kept on the badge), and any button starts again. As in the
// previous firmware's Startris.
#include "hhapp.h"

#define FRAME_MS 33  // the game steps 30 times a second
#define W 9
#define H 14
#define R0 1         // the well's top row on the grid
#define C0 4         // its left column
#define FULL ((1u << W) - 1)
#define CLEAR_FRAMES 9
#define OVER_MS 1000
#define TEXT_Y 4

enum { I, O };
// Each piece's cells in its 3 x 3 box (I: 4 x 4); rotating turns the box
static const int8_t SHAPES[7][4][2] = {
    {{1, 0}, {1, 1}, {1, 2}, {1, 3}},  // I
    {{0, 0}, {0, 1}, {1, 0}, {1, 1}},  // O
    {{0, 1}, {1, 0}, {1, 1}, {1, 2}},  // T
    {{0, 1}, {0, 2}, {1, 0}, {1, 1}},  // S
    {{0, 0}, {0, 1}, {1, 1}, {1, 2}},  // Z
    {{0, 0}, {1, 0}, {1, 1}, {1, 2}},  // J
    {{0, 2}, {1, 0}, {1, 1}, {1, 2}},  // L
};
static const uint8_t HUE[7] = {128, 40, 200, 85, 0, 165, 20};

static const hh_api_t* hh;
static uint16_t well[H];  // bit c: column c is filled
static uint8_t kind, rot, next, bag;
static int32_t pr, pc;    // the piece's box: top row, left column
static uint32_t fall, frame_ms, over_ms, t;
static uint16_t lines, best;
static uint16_t clearing;  // the rows being cleared, a bit each
static uint8_t clear_t;
static uint8_t rep[2];     // steps C / D have been held, for the repeat
static bool over;

// The piece's cells (row, column) in its box, turned rot times
static void cells(uint8_t k, uint8_t r, int32_t out[4][2]) {
    int n = k == I ? 4 : 3;
    for (int i = 0; i < 4; i++) {
        int32_t a = SHAPES[k][i][0], b = SHAPES[k][i][1];
        if (k != O)
            for (int j = 0; j < r % 4; j++) {
                int32_t tmp = a;
                a = b;
                b = n - 1 - tmp;
            }
        out[i][0] = a;
        out[i][1] = b;
    }
}

static bool fits_in(const uint16_t* w, uint8_t k, uint8_t r, int32_t row, int32_t col) {
    int32_t c[4][2];
    cells(k, r, c);
    for (int i = 0; i < 4; i++) {
        int32_t rr = row + c[i][0], cc = col + c[i][1];
        if (rr < 0 || rr >= H || cc < 0 || cc >= W || (w[rr] >> cc & 1)) return false;
    }
    return true;
}

static bool fits(uint8_t r, int32_t row, int32_t col) { return fits_in(well, kind, r, row, col); }

static uint16_t full_rows(const uint16_t* w) {
    uint16_t m = 0;
    for (int i = 0; i < H; i++)
        if (w[i] == FULL) m |= (uint16_t)(1u << i);
    return m;
}

// Removes the rows in mask; the rows above fall
static void compact(uint16_t* w, uint16_t mask) {
    int j = H;
    for (int i = H - 1; i >= 0; i--)
        if (!(mask >> i & 1)) w[--j] = w[i];
    while (j > 0) w[--j] = 0;
}

// The next piece from a bag of all seven, so none stays away for long
static uint8_t deal(void) {
    if (bag == 0x7F) bag = 0;
    for (;;) {
        uint8_t k = (uint8_t)(hh->random() % 7);
        if (!(bag >> k & 1)) {
            bag |= (uint8_t)(1u << k);
            return k;
        }
    }
}

static void game_over(void) {
    over = true;
    over_ms = 0;
    if (lines > best) {
        best = lines;
        hh->save(&best, sizeof best);
    }
}

static void spawn(void) {
    kind = next;
    next = deal();
    rot = 0;
    pr = 0;
    pc = 3;
    fall = 0;
    if (!fits(0, 0, 3)) game_over();
}

static void start(void) {
    for (int i = 0; i < H; i++) well[i] = 0;
    bag = 0;
    lines = 0;
    clearing = 0;
    clear_t = 0;
    rep[0] = rep[1] = 0;
    over = false;
    next = deal();
    spawn();
}

static void rotate(void) {
    uint8_t r = (uint8_t)((rot + 1) % 4);
    static const int8_t kick[3] = {0, -1, 1};  // against a wall, it moves aside
    for (int k = 0; k < 3; k++)
        if (fits(r, pr, pc + kick[k])) {
            rot = r;
            pc += kick[k];
            return;
        }
}

static void shift(int d) {
    if (fits(rot, pr, pc + d)) pc += d;
}

// Puts the piece down: full rows blink, then clear; else the next piece
static void lock(void) {
    int32_t c[4][2];
    cells(kind, rot, c);
    for (int i = 0; i < 4; i++) well[pr + c[i][0]] |= (uint16_t)(1u << (pc + c[i][1]));
    clearing = full_rows(well);
    if (clearing) clear_t = CLEAR_FRAMES;
    else spawn();
}

static void drop(void) {
    while (fits(rot, pr + 1, pc)) pr++;
    lock();
}

static void step(uint8_t pressed, uint8_t held) {
    t++;
    if (clear_t) {
        if (--clear_t == 0) {
            compact(well, clearing);
            for (int i = 0; i < H; i++) lines += clearing >> i & 1;  // no libgcc in apps: no popcount
            clearing = 0;
            spawn();
        }
        return;
    }
    if (pressed & HH_BTN_A) rotate();
    static const uint8_t keys[2] = {HH_BTN_C, HH_BTN_D};
    for (int k = 0; k < 2; k++) {
        if (pressed & keys[k]) {
            shift(k ? 1 : -1);
            rep[k] = 0;
        } else if (held & keys[k]) {
            if (rep[k] < 255) rep[k]++;
            if (rep[k] > 8 && rep[k] % 3 == 0) shift(k ? 1 : -1);
        }
    }
    if (pressed & HH_BTN_B) {
        drop();
        return;
    }
    uint32_t every = lines < 16 ? 20u - lines : 4u;  // faster with each line, down to 4 steps a row
    if (++fall >= every) {
        fall = 0;
        if (fits(rot, pr + 1, pc)) pr++;
        else lock();
    }
}

// Hue h (0 .. 255 is one turn), full saturation, value v
static void hsv(uint8_t h, uint8_t v, uint8_t out[3]) {
    uint8_t region = h / 43, rem = (uint8_t)((h - region * 43) * 6);
    uint8_t q = (uint8_t)(v * (255 - rem) / 255), u = (uint8_t)(v * rem / 255);
    static const uint8_t pick[6][3] = {{0, 2, 3}, {1, 0, 3}, {3, 0, 2}, {3, 1, 0}, {2, 3, 0}, {0, 3, 1}};
    uint8_t vals[4] = {v, q, u, 0};
    for (int c = 0; c < 3; c++) out[c] = vals[pick[region][c]];
}

static void set_rgb(int i, const uint8_t c[3]) {
    hh->set(hh->rgb[i].r, c[0]);
    hh->set(hh->rgb[i].g, c[1]);
    hh->set(hh->rgb[i].b, c[2]);
}

static void draw(void) {
    hh->clear();
    if (over) {
        char text[6], *p = text + sizeof text - 1;
        uint16_t n = lines;
        *p = 0;
        do *--p = (char)('0' + n % 10);
        while ((n /= 10) && p > text);
        hh->draw_text((HH_GRID_W - hh->text_width(p)) / 2, TEXT_Y, p, 255);
        for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < best ? 40 : 0);
    }
    for (int r = 0; r < H; r++) {
        hh->set(hh->grid[R0 + r][C0 - 1], 25);  // the well's walls
        hh->set(hh->grid[R0 + r][C0 + W], 25);
        if (over) continue;
        bool blink = clearing >> r & 1;
        for (int c = 0; c < W; c++)
            if (well[r] >> c & 1) hh->set(hh->grid[R0 + r][C0 + c], !blink ? 170 : clear_t % 3 < 2 ? 255 : 40);
    }
    if (!clear_t && !over) {
        int32_t c[4][2];
        cells(kind, rot, c);
        for (int i = 0; i < 4; i++) hh->set(hh->grid[R0 + pr + c[i][0]][C0 + pc + c[i][1]], 255);
    }
    // The arms: the next piece's colour, dim; the lines fill them, arm after
    // arm; clearing rows make them spin with colour
    uint8_t dim[3], bright[3], c[3];
    hsv(HUE[next], 40, dim);
    hsv(HUE[next], 220, bright);
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        if (clear_t) {
            hsv((uint8_t)(l->arm * 51 + l->rank * 9 + t * 12), 255, c);
            set_rgb(i, c);
        } else {
            set_rgb(i, l->rank < (int)lines - l->arm * 7 ? bright : dim);
        }
    }
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed(), held = hh->held();
    if (over) {
        over_ms += elapsed_ms;
        if (over_ms >= OVER_MS && pressed) start();
    } else {
        static uint8_t pending;  // a press between two game steps is not lost
        pending |= pressed;
        frame_ms += elapsed_ms;
        while (frame_ms >= FRAME_MS && !over) {
            frame_ms -= FRAME_MS;
            step(pending, held);
            pending = 0;
        }
    }
    draw();
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&best, sizeof best)) best = 0;
    start();
}

HH_APP(init, tick);
