// Snake on the centre grid. C turns left, D turns right; eat the blinking
// dot. The outline counts the score, dimly up to the best score so far
// (kept on the badge). After a crash the score shows as a number; A or B
// starts again. Until the first button the badge plays a demo, and it goes
// back to that 15 s after a crash.
#include "hhapp.h"

#define W HH_GRID_W
#define H HH_GRID_H
#define MAX_LEN (W * H)

static const hh_api_t* hh;
static uint8_t body_x[MAX_LEN], body_y[MAX_LEN];  // ring buffer, head at `head`
static uint16_t head, len;
static int8_t dx, dy;
static int8_t turn;  // -1 left, 1 right, 0 straight: at the next step
static uint8_t food_x, food_y;
static uint32_t step_ms, wait_ms;
static uint16_t score;
static uint16_t best;  // saved with hh->save()
static bool crashed;
static bool demo;          // the badge plays by itself
static uint32_t crashed_ms;  // since the crash

static bool on_grid(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H && hh->grid[y][x] != HH_NONE; }

static bool on_snake(int x, int y, uint16_t skip_tail) {
    for (uint16_t i = 0; i + skip_tail < len; i++) {
        uint16_t k = (uint16_t)((head + MAX_LEN - i) % MAX_LEN);
        if (body_x[k] == x && body_y[k] == y) return true;
    }
    return false;
}

static void place_food(void) {
    do {
        food_x = (uint8_t)(hh->random() % W);
        food_y = (uint8_t)(hh->random() % H);
    } while (!on_grid(food_x, food_y) || on_snake(food_x, food_y, 0));
}

static void start(void) {
    len = 3;
    head = 2;
    for (uint16_t i = 0; i < len; i++) {
        body_x[i] = W / 2;
        body_y[i] = (uint8_t)(H / 2 + 2 - i);  // heading up
    }
    dx = 0;
    dy = -1;
    turn = 0;
    crashed_ms = 0;
    step_ms = 220;
    wait_ms = 0;
    score = 0;
    crashed = false;
    place_food();
}

static void step(void) {
    if (turn < 0) {  // left: up becomes left
        int8_t t = dx;
        dx = dy;
        dy = (int8_t)-t;
    } else if (turn > 0) {  // right: up becomes right
        int8_t t = dx;
        dx = (int8_t)-dy;
        dy = t;
    }
    turn = 0;
    int x = body_x[head] + dx, y = body_y[head] + dy;
    bool eats = x == food_x && y == food_y;
    // the tail moves on in the same step, unless the snake grows
    if (!on_grid(x, y) || on_snake(x, y, eats ? 0 : 1)) {
        crashed = true;
        if (!demo && score > best) {
            best = score;
            hh->save(&best, sizeof best);
        }
        return;
    }
    head = (uint16_t)((head + 1) % MAX_LEN);
    body_x[head] = (uint8_t)x;
    body_y[head] = (uint8_t)y;
    if (eats) {
        len++;
        score++;
        if (step_ms > 90) step_ms -= 8;
        if (len < MAX_LEN) place_food();
    }
}

// The score as text, centred on the grid, over a dimmed snake
static void draw_score(void) {
    hh->draw_number(HH_CENTER, (H - HH_FONT_H) / 2, score, 255);
}

static void draw(void) {
    uint32_t t = hh->millis();
    hh->clear();
    for (uint16_t i = 0; i < len; i++) {
        uint16_t k = (uint16_t)((head + MAX_LEN - i) % MAX_LEN);
        hh->set(hh->grid[body_y[k]][body_x[k]], crashed ? 40 : i == 0 ? 255 : 150);
    }
    if (crashed) draw_score();
    if (!crashed && (t / 150) % 2) hh->set(hh->grid[food_y][food_x], 255);
    for (uint16_t i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < score ? 140 : i < best ? 50 : 0);
    // the arms: green while playing, a red pulse after a crash
    uint8_t pulse = (uint8_t)((t / 4) % 256 < 128 ? (t / 4) % 128 * 2 : 255 - (t / 4) % 128 * 2);
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &hh->rgb[i];
        if (crashed) {
            hh->set(led->r, pulse);
        } else if (led->rank == 6) {
            hh->set(led->g, 140);
        }
    }
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&best, sizeof best)) best = 0;
    demo = true;
    start();
}

// The demo's turn: of straight on, left and right, a move that does not
// crash and comes closest to the food
static int8_t demo_turn(void) {
    int8_t best_turn = 0;
    int best_d = 1 << 20;
    static const int8_t turns[3] = {0, -1, 1};
    for (int k = 0; k < 3; k++) {
        int ndx = turns[k] < 0 ? dy : turns[k] > 0 ? -dy : dx, ndy = turns[k] < 0 ? -dx : turns[k] > 0 ? dx : dy;
        int x = body_x[head] + ndx, y = body_y[head] + ndy;
        if (!on_grid(x, y) || on_snake(x, y, 1)) continue;
        int d = (x > food_x ? x - food_x : food_x - x) + (y > food_y ? y - food_y : food_y - y);
        if (d < best_d) best_d = d, best_turn = turns[k];
    }
    return best_turn;
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (demo && (pressed & (HH_BTN_B | HH_BTN_C | HH_BTN_D))) {  // the first button: a real game
        demo = false;
        start();
        pressed = 0;
    }
    if (crashed) {
        crashed_ms += elapsed_ms;
        if (demo ? crashed_ms >= 2000 : (pressed & (HH_BTN_A | HH_BTN_B)) != 0) {
            start();
        } else if (!demo && crashed_ms >= 15000) {  // no button for 15 s: the demo again
            demo = true;
            start();
        }
    } else {
        if (pressed & HH_BTN_C) turn = -1;
        if (pressed & HH_BTN_D) turn = 1;
        if (demo && wait_ms + elapsed_ms >= step_ms) turn = demo_turn();
        wait_ms += elapsed_ms;
        if (wait_ms >= step_ms) {
            wait_ms -= step_ms;
            step();
        }
    }
    draw();
}

HH_APP(init, tick);
