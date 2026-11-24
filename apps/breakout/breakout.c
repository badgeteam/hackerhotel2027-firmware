// Breakout: five rows of bricks on top, a 3-wide paddle near the bottom,
// and the octagon's angled walls to bounce off. C / D move the paddle (hold
// to keep moving); where the ball hits the paddle steers it. Clear all the
// bricks for a new wave, a little faster. 3 lives, shown blue on the top
// arms; the score fills the arms orange. After the last life, any button
// starts again. As in the previous firmware's breakout.
#include "hhapp.h"

#define FRAME_MS 33      // the game steps 30 times a second
#define ONE 256          // fixed point: 1/256 of a cell
#define BRICK_ROWS 5     // grid rows 1 .. 5
#define PADDLE_ROW 14
#define SERVE_FRAMES 20  // the ball sits on the paddle this long
#define OVER_MS 1000     // after the end, buttons wait this long

static const hh_api_t* hh;
static uint32_t bricks[BRICK_ROWS];  // bit c: a brick in column c
static int32_t paddle;               // its centre column
static int32_t x, y, vx, vy;         // the ball, in 1/256 cells
static int32_t speed;
static uint8_t serve;                // frames the ball still sits on the paddle
static uint8_t lives, flash;
static uint16_t score, best;
static uint32_t t, frame_ms, over_ms;
static uint32_t idle;                // steps since the ball last hit a brick or the paddle
static bool over;

static int32_t cell(int32_t v) { return (v + ONE / 2) >> 8; }

static bool on_grid(int32_t r, int32_t c) {
    return r >= 0 && r < HH_GRID_H && c >= 0 && c < HH_GRID_W && hh->grid[r][c] != HH_NONE;
}

// Where the ball may be: a grid cell, or below the paddle's row (it falls out)
static bool open(int32_t r, int32_t c) { return r > PADDLE_ROW || on_grid(r, c); }

static bool brick(int32_t r, int32_t c) { return r >= 1 && r <= BRICK_ROWS && c >= 0 && c < HH_GRID_W && (bricks[r - 1] >> c & 1); }

static void fill(void) {
    for (int r = 0; r < BRICK_ROWS; r++) {
        bricks[r] = 0;
        for (int c = 0; c < HH_GRID_W; c++)
            if (on_grid(r + 1, c)) bricks[r] |= 1u << c;
    }
}

static void serve_ball(void) {
    serve = SERVE_FRAMES;
    idle = 0;
    vy = -speed;
    vx = speed / 3;
}

static void start(void) {
    fill();
    paddle = 8;
    lives = 3;
    score = 0;
    speed = ONE / 2;  // 15 cells per second
    flash = 0;
    over = false;
    serve_ball();
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

static void step(uint8_t pressed, uint8_t held) {
    t++;
    if (flash) flash--;

    // The paddle: a press moves it one, holding keeps it moving
    int32_t target = paddle;
    if ((pressed & HH_BTN_C) || ((held & HH_BTN_C) && t % 3 == 0)) target = paddle - 1;
    else if ((pressed & HH_BTN_D) || ((held & HH_BTN_D) && t % 3 == 0)) target = paddle + 1;
    paddle = clamp(target, 5, 11);

    if (serve) {
        serve--;
        y = (PADDLE_ROW - 1) * ONE;
        x = paddle * ONE;
        return;
    }

    int32_t ny = y + vy, nx = x + vx, r = cell(ny), c = cell(nx);
    idle++;
    if (brick(r, c)) {
        bricks[r - 1] &= ~(1u << c);
        score++;
        flash = 6;
        vy = -vy;
        idle = 0;
    } else if (r <= PADDLE_ROW) {
        // The octagon's walls, down to the paddle's row: only the paddle's
        // reach (columns 4 .. 12) is open at the bottom
        if (!open(r, cell(x))) vy = -vy;
        if (!open(cell(y), c)) vx = -vx;
        // A ball that has bounced around for 10 s without touching a brick
        // or the paddle may be in a loop: a wall nudges it sideways
        if (idle > 300 && (!open(r, cell(x)) || !open(cell(y), c))) {
            vx = clamp(vx + ((hh->random() & 1) ? speed : -speed) / 4, -speed * 3 / 5, speed * 3 / 5);
            idle = 0;
        }
    }
    // Where that leads must be open too (a corner without an LED, or a
    // wall behind a brick): else the first mirror image of the move that is
    static const int8_t flip[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (int k = 0; k < 4; k++) {
        int32_t fx = vx * flip[k][0], fy = vy * flip[k][1];
        if (open(cell(y + fy), cell(x + fx))) {
            vx = fx, vy = fy;
            break;
        }
    }
    // The paddle: bounce, and steer by where the ball hits it
    int32_t edge = PADDLE_ROW * ONE - ONE / 2;
    if (vy > 0 && y < edge && y + vy >= edge && abs32(x - paddle * ONE) <= ONE * 8 / 5) {
        vy = -abs32(vy);
        vx = clamp(vx + (x - paddle * ONE) / 10, -speed * 3 / 5, speed * 3 / 5);
        idle = 0;
    }
    y += vy;
    x += vx;

    if (y > (PADDLE_ROW + 1) * ONE + ONE / 2) {  // missed
        if (--lives == 0) {
            over = true;
            over_ms = 0;
            if (score > best) {
                best = score;
                hh->save(&best, sizeof best);
            }
            return;
        }
        serve_ball();
    }
    bool empty = true;
    for (int i = 0; i < BRICK_ROWS; i++) empty = empty && !bricks[i];
    if (empty) {  // the next wave, a bit faster
        fill();
        speed += ONE / 16;
        serve_ball();
    }
}

static void set_rgb(int i, uint8_t r, uint8_t g, uint8_t b) {
    hh->set(hh->rgb[i].r, r);
    hh->set(hh->rgb[i].g, g);
    hh->set(hh->rgb[i].b, b);
}

static void draw(void) {
    hh->clear();
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < HH_GRID_W; c++)
            if (bricks[r] >> c & 1) hh->set(hh->grid[r + 1][c], 130);
    for (int d = -1; d <= 1; d++) hh->set(hh->grid[PADDLE_ROW][paddle + d], 210);
    if (!over && (!serve || t % 6 < 3) && on_grid(cell(y), cell(x))) hh->set(hh->grid[cell(y)][cell(x)], 255);

    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        int filled = (int)score / 3 - l->arm * 7;  // the score fills arm after arm
        if (over) {
            uint32_t ph = over_ms / 4 % 256;
            set_rgb(i, (uint8_t)(ph < 128 ? ph * 2 : (255 - ph) * 2), 0, 0);
        } else if (l->rank < filled) {
            set_rgb(i, 220, 130, 17);
        } else if (l->arm < lives) {
            set_rgb(i, 5, 19, 70);
        }
    }
    for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], over ? (i < best / 3 ? 40 : 0) : (uint8_t)(flash * 30));
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed(), held = hh->held();
    if (over) {
        over_ms += elapsed_ms;
        if (over_ms >= OVER_MS && pressed) start();
    } else {
        // Whole game steps; a press is not lost between two of them
        static uint8_t pending;
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
