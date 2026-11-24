// Flappy: one button. B, C or D flaps; fly through the gaps in the pipes.
// The bird waits until the first flap. Hitting a pipe or the edge of the
// grid ends the game; the arms fill gold with the pipes you passed, and
// after the end the grid shows the score, the outline the best. As in the
// previous firmware's flappy.
#include "hhapp.h"

#define FRAME_MS 33   // the game steps 30 times a second
#define ONE 256       // fixed point: 1/256 of a cell
#define BIRD_COL 4
#define GAP 4         // rows in a gap
#define PIPES 4
#define GRAVITY 6
#define FLAP (-72)
#define MAX_FALL 100
#define SPEED 57      // columns per step, 1/256: about 6.7 per second
#define EVERY 36      // steps between pipes
#define OVER_MS 1000  // after the end, buttons wait this long
#define TEXT_Y 4
#define FLAP_KEYS (HH_BTN_B | HH_BTN_C | HH_BTN_D)

typedef struct {
    int32_t x;  // column, fixed point; < 0: not in use
    int32_t gap;
    bool passed;
} pipe_t;

enum { READY, FLYING, OVER };

static const hh_api_t* hh;
static int32_t y, vy;
static pipe_t pipes[PIPES];
static uint16_t score, best;
static uint32_t t, frame_ms, over_ms;
static uint8_t state;

static int32_t cell(int32_t v) { return (v + ONE / 2) >> 8; }

static bool on_grid(int32_t r, int32_t c) {
    return r >= 0 && r < HH_GRID_H && c >= 0 && c < HH_GRID_W && hh->grid[r][c] != HH_NONE;
}

static void start(void) {
    y = 7 * ONE;
    vy = 0;
    for (int i = 0; i < PIPES; i++) pipes[i].x = -1;
    score = 0;
    t = 0;
    frame_ms = 0;
    state = READY;
}

static void game_over(void) {
    state = OVER;
    over_ms = 0;
    if (score > best) {
        best = score;
        hh->save(&best, sizeof best);
    }
}

static void step(bool flap) {
    t++;
    if (t % EVERY == 1)
        for (int i = 0; i < PIPES; i++)
            if (pipes[i].x < 0) {
                pipes[i] = (pipe_t){(HH_GRID_W + 1) * ONE, 3 + (int32_t)(hh->random() % 6), false};
                break;
            }
    for (int i = 0; i < PIPES; i++) {
        pipe_t* p = &pipes[i];
        if (p->x < 0) continue;
        p->x -= SPEED;
        if (p->x < 0) p->x = -1;
        if (!p->passed && p->x < BIRD_COL * ONE - ONE / 2) {
            p->passed = true;
            score++;
        }
    }

    if (flap) vy = FLAP;
    vy += GRAVITY;
    if (vy > MAX_FALL) vy = MAX_FALL;
    y += vy;

    int32_t r = cell(y);
    bool hit = !on_grid(r, BIRD_COL);  // the top or bottom of the grid
    for (int i = 0; i < PIPES; i++) {
        pipe_t* p = &pipes[i];
        int32_t dx = p->x - BIRD_COL * ONE;
        if (p->x >= 0 && dx < ONE * 3 / 5 && dx > -ONE * 3 / 5 && (r < p->gap || r >= p->gap + GAP)) hit = true;
    }
    if (hit) game_over();
}

static void draw(void) {
    hh->clear();
    for (int i = 0; i < PIPES; i++) {
        if (pipes[i].x < 0) continue;
        int32_t c = cell(pipes[i].x);
        if (c >= HH_GRID_W) continue;
        for (int r = 0; r < HH_GRID_H; r++)
            if ((r < pipes[i].gap || r >= pipes[i].gap + GAP) && hh->grid[r][c] != HH_NONE) hh->set(hh->grid[r][c], state == OVER ? 30 : 100);
    }
    int32_t r = cell(y);
    if (state != OVER && on_grid(r, BIRD_COL)) {
        hh->set(hh->grid[r][BIRD_COL], 255);
        int32_t tail = r + (vy < 0 ? 1 : -1);  // the tail trails behind the flap
        if (on_grid(tail, BIRD_COL - 1)) hh->set(hh->grid[tail][BIRD_COL - 1], 80);
    }
    if (state == OVER) {
        char text[6], *p = text + sizeof text - 1;
        uint16_t n = score;
        *p = 0;
        do *--p = (char)('0' + n % 10);
        while ((n /= 10) && p > text);
        hh->draw_text((HH_GRID_W - hh->text_width(p)) / 2, TEXT_Y, p, 255);
        for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < best ? 40 : 0);
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {  // the score fills the arms gold, arm after arm
        const hh_rgb_t* l = &hh->rgb[i];
        bool on = l->rank < (int)score - l->arm * 7;
        hh->set(l->r, on ? 220 : 0);
        hh->set(l->g, on ? 160 : 0);
        hh->set(l->b, on ? 17 : 0);
    }
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (state == OVER) {
        over_ms += elapsed_ms;
        if (over_ms >= OVER_MS && (pressed & FLAP_KEYS)) start();
    } else if (state == READY) {
        y = 7 * ONE + (hh->millis() / 300 % 2) * ONE / 2;  // it bobs until the first flap
        if (pressed & FLAP_KEYS) {
            state = FLYING;
            step(true);
        }
    } else {
        // Whole game steps; a flap between two of them is not lost
        static bool flap;
        flap = flap || (pressed & FLAP_KEYS);
        frame_ms += elapsed_ms;
        while (frame_ms >= FRAME_MS && state == FLYING) {
            frame_ms -= FRAME_MS;
            step(flap);
            flap = false;
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
