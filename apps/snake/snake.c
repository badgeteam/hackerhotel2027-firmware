// Snake on the centre grid. C turns left, D turns right; eat the blinking
// dot. The outline counts the score. After a crash, A or B starts again.
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
static bool crashed;

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

static void draw(void) {
    uint32_t t = hh->millis();
    hh->clear();
    for (uint16_t i = 0; i < len; i++) {
        uint16_t k = (uint16_t)((head + MAX_LEN - i) % MAX_LEN);
        hh->set(hh->grid[body_y[k]][body_x[k]], i == 0 ? 255 : 70);
    }
    if (!crashed && (t / 150) % 2) hh->set(hh->grid[food_y][food_x], 255);
    for (uint16_t i = 0; i < score && i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], 60);
    // the arms: green while playing, a red pulse after a crash
    uint8_t pulse = (uint8_t)((t / 4) % 256 < 128 ? (t / 4) % 128 * 2 : 255 - (t / 4) % 128 * 2);
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &hh->rgb[i];
        if (crashed) {
            hh->set(led->r, pulse);
        } else if (led->rank == 6) {
            hh->set(led->g, 80);
        }
    }
}

static void init(const hh_api_t* api) {
    hh = api;
    start();
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (crashed) {
        if (pressed & (HH_BTN_A | HH_BTN_B)) start();
    } else {
        if (pressed & HH_BTN_C) turn = -1;
        if (pressed & HH_BTN_D) turn = 1;
        wait_ms += elapsed_ms;
        if (wait_ms >= step_ms) {
            wait_ms -= step_ms;
            step();
        }
    }
    draw();
}

HH_APP(init, tick);
