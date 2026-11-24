// Pong against the badge: your paddle on the left edge, the badge's on the
// right. B moves yours up, D down (hold to keep moving); where the ball hits
// a paddle steers it. First to 5 wins. The left arms show your points in
// blue, the right arms the badge's in red. After a game the grid shows the
// score, the outline your wins so far (kept on the badge); any button
// starts the next game. As in the previous firmware's pong.
#include "hhapp.h"

#define FRAME_MS 33  // the game steps 30 times a second
#define ONE 256      // fixed point: 1/256 of a cell
#define RIGHT (HH_GRID_W - 1)
#define TOP 5        // the paddles move over rows 5 .. 10, 2 tall
#define BOTTOM 9
#define WIN 5
#define SERVE_FRAMES 20
#define OVER_MS 1000
#define TEXT_Y 4

static const hh_api_t* hh;
static int32_t paddle[2];  // the top row of yours (0) and the badge's (1)
static int32_t x, y, vx, vy;
static uint8_t points[2];
static uint8_t serve;
static int32_t aim_err[2];  // a computer player misjudges a ball now and then
static uint8_t flash, flash_who;
static uint32_t t, frame_ms, over_ms;
static bool over;
static uint16_t wins;  // saved

static int32_t cell(int32_t v) { return (v + ONE / 2) >> 8; }
static int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool on_grid(int32_t r, int32_t c) {
    return r >= 0 && r < HH_GRID_H && c >= 0 && c < HH_GRID_W && hh->grid[r][c] != HH_NONE;
}

static void serve_ball(int towards) {
    y = 7 * ONE + ONE / 2;
    x = 8 * ONE;
    vx = towards * ONE * 2 / 7;
    vy = (hh->random() & 1) ? ONE / 8 : -ONE / 8;
    serve = SERVE_FRAMES;
    aim_err[0] = aim_err[1] = 0;
}

static void start(void) {
    paddle[0] = paddle[1] = 7;
    points[0] = points[1] = 0;
    flash = 0;
    over = false;
    serve_ball(1);
}

// Where a computer paddle wants its top row
static int32_t aim(int side) {
    bool coming = side == 0 ? vx < 0 : vx > 0;
    return clamp(cell(coming ? y + aim_err[side] : 7 * ONE) - 1, TOP, BOTTOM);
}

static void step(uint8_t pressed, uint8_t held) {
    t++;
    if (flash) flash--;

    if ((pressed & HH_BTN_B) || ((held & HH_BTN_B) && t % 3 == 0)) paddle[0] = clamp(paddle[0] - 1, TOP, BOTTOM);
    else if ((pressed & HH_BTN_D) || ((held & HH_BTN_D) && t % 3 == 0)) paddle[0] = clamp(paddle[0] + 1, TOP, BOTTOM);
    if ((t + 1) % 3 == 0 && paddle[1] != aim(1)) paddle[1] += paddle[1] < aim(1) ? 1 : -1;  // the badge, one row a step at most

    if (serve) {
        serve--;
        return;
    }
    int32_t ny = y + vy, nx = x + vx;
    if (!on_grid(cell(ny), clamp(cell(x), 0, RIGHT))) vy = -vy;
    for (int side = 0; side < 2; side++) {
        int32_t top = paddle[side], line = (side ? RIGHT - 1 : 1) * ONE;
        bool moving = side ? vx > 0 : vx < 0;
        bool crosses = (x - line) * (nx - line) <= 0;  // in 1/256 cells: at most 17 * 256 squared, fits
        bool covered = y >= top * ONE - ONE * 3 / 5 && y <= (top + 1) * ONE + ONE * 3 / 5;
        if (moving && crosses && covered) {
            vx = -vx * 26 / 25;
            vy = clamp(vy + (y - top * ONE - ONE / 2) / 10, -ONE / 3, ONE / 3);
            aim_err[1 - side] = hh->random() % 4 == 0 ? ONE * 5 / 2 : 0;  // the other side may misjudge this one
        }
    }
    // Where that leads must be on the grid: at the octagon's angled
    // corners, the first mirror image of the move that is, up and down
    // first. Behind the paddles (columns 0 and 16) nothing stops the ball:
    // it is a point.
    static const int8_t flip[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (int k = 0; k < 4; k++) {
        int32_t fx = vx * flip[k][0], fy = vy * flip[k][1], c = cell(x + fx);
        if (c < 1 || c > RIGHT - 1 || on_grid(cell(y + fy), c)) {
            vx = fx, vy = fy;
            break;
        }
    }
    y += vy;
    x += vx;

    if (x < -ONE / 2 || x > RIGHT * ONE + ONE / 2) {
        int who = x < 0 ? 1 : 0;  // past your paddle: the badge's point
        points[who]++;
        flash = 16;
        flash_who = (uint8_t)who;
        if (points[who] == WIN) {
            over = true;
            over_ms = 0;
            if (who == 0) {
                wins++;
                hh->save(&wins, sizeof wins);
            }
            return;
        }
        serve_ball(who == 0 ? 1 : -1);
    }
}

static void draw(void) {
    hh->clear();
    if (over) {
        char text[4] = {(char)('0' + points[0]), ':', (char)('0' + points[1]), 0};
        hh->draw_text((HH_GRID_W - hh->text_width(text)) / 2, TEXT_Y, text, 255);
        for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < wins ? 60 : 0);
    } else {
        for (int r = 0; r < HH_GRID_H; r += 2) hh->set(hh->grid[r][8], 18);  // the net
        for (int d = 0; d < 2; d++) {
            hh->set(hh->grid[paddle[0] + d][0], 255);
            hh->set(hh->grid[paddle[1] + d][RIGHT], 255);
        }
        int32_t r = clamp(cell(y), 0, HH_GRID_H - 1), c = clamp(cell(x), 0, RIGHT);
        if ((!serve || t % 6 < 3) && on_grid(r, c)) hh->set(hh->grid[r][c], 255);
    }
    // The left arms (4, 3) are yours, blue; the right arms (1, 2) the
    // badge's, red; a point flashes; the top arm glows white
    static const uint8_t colour[3][3] = {{20, 90, 255}, {255, 20, 20}, {255, 255, 255}};  // you, the badge, the top
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        int who = l->arm >= 3 ? 0 : l->arm >= 1 ? 1 : 2;
        uint32_t v = 0;
        if (who == 2) v = 25;
        else if (flash && flash_who == who) v = flash * 15u;
        else if (l->rank < points[who]) v = 220;
        hh->set(l->r, (uint8_t)(colour[who][0] * v / 255));
        hh->set(l->g, (uint8_t)(colour[who][1] * v / 255));
        hh->set(l->b, (uint8_t)(colour[who][2] * v / 255));
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
    if (!hh->load(&wins, sizeof wins)) wins = 0;
    start();
}

HH_APP(init, tick);
