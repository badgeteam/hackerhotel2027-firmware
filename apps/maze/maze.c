// Maze: you stay in the middle of the grid and the maze moves around you;
// only the walls close by are lit. The buttons are a diamond round the
// badge's corners: A up, B right, D down, C left (B, C, D repeat when held;
// A steps once a tap, since holding A opens the menu). The arm that points
// most towards the exit, in the far corner, lights blue, and the outline
// shows the time left. Five mazes, from 5 x 5 to 13 x 13 cells, with 15 s
// for the first and 8 s more for each next one; an arm's sides turn green
// for each maze solved. After the end, any button starts again; the most
// mazes solved are kept on the badge. As in the previous firmware's maze.
#include "hhapp.h"

#define FRAME_MS 33  // the game steps 30 times a second
#define MAZES 5
#define CELLS 13     // the biggest maze, cells a side
#define PX (2 * CELLS + 1)  // the same in pixels: walls between the cells
#define ME_R 7       // where you stand on the grid
#define ME_C 8
#define REPEAT_AFTER 8
#define REPEAT_EVERY 4
#define SOLVED_FRAMES 30
#define OVER_MS 1000
#define TEXT_Y 4

static const int8_t DR[4] = {0, 1, 0, -1};  // right, down, left, up
static const int8_t DC[4] = {1, 0, -1, 0};
// The direction of each button bit (A, C, B, D): A up, C left, B right, D down
static const uint8_t KEY_DIR[4] = {3, 2, 0, 1};
// Each arm's direction (x right, y down), times 100: arm a is a fifth of a
// turn clockwise from the top per a
static const int8_t ARM_DIR[5][2] = {{0, -100}, {95, -31}, {59, 81}, {-59, 81}, {-95, -31}};
static const uint8_t LINE[5] = {0, 3, 4, 5, 6};  // an arm's centre line; ranks 1, 2 are its sides

static const hh_api_t* hh;
static uint32_t wall[PX];  // bit c of row r: pixel (r, c) is a wall
static uint8_t size;       // this maze's pixels a side
static uint8_t pr, pc;     // you, in pixels (odd)
static uint8_t solved, best;
static uint8_t rep;        // steps a direction has been held
static uint32_t time_left, limit, fx;  // in steps
static uint32_t t, frame_ms, over_ms;
static bool over;

static bool is_wall(int r, int c) { return r >= 0 && c >= 0 && r < size && c < size && (wall[r] >> c & 1); }
static void open_px(int r, int c) { wall[r] &= ~(1u << c); }

// A perfect maze (one way between any two cells): depth first, carving
static void make(void) {
    static uint8_t stack[CELLS * CELLS];
    int n = 5 + 2 * solved;
    size = (uint8_t)(2 * n + 1);
    for (int r = 0; r < PX; r++) wall[r] = r < size ? (1u << size) - 1 : 0;
    int top = 1;
    stack[0] = 0;
    open_px(1, 1);
    while (top > 0) {
        int cell = stack[top - 1], cr = cell / n, cc = cell % n, options[4], k = 0;
        for (int d = 0; d < 4; d++) {
            int nr = cr + DR[d], nc = cc + DC[d];
            if (nr >= 0 && nc >= 0 && nr < n && nc < n && is_wall(2 * nr + 1, 2 * nc + 1)) options[k++] = d;
        }
        if (!k) {
            top--;
            continue;
        }
        int d = options[hh->random() % (uint32_t)k];
        open_px(2 * cr + 1 + DR[d], 2 * cc + 1 + DC[d]);
        open_px(2 * (cr + DR[d]) + 1, 2 * (cc + DC[d]) + 1);
        stack[top++] = (uint8_t)((cr + DR[d]) * n + cc + DC[d]);
    }
    pr = pc = 1;
    limit = (15u + 8u * solved) * 30u;
    time_left = limit;
}

static bool move(int d) {
    if (is_wall(pr + DR[d], pc + DC[d])) return false;
    pr = (uint8_t)(pr + 2 * DR[d]);
    pc = (uint8_t)(pc + 2 * DC[d]);
    return true;
}

static void finish(void) {
    over = true;
    over_ms = 0;
    if (solved > best) {
        best = solved;
        hh->save(&best, sizeof best);
    }
}

static void start(void) {
    solved = 0;
    fx = 0;
    rep = 0;
    over = false;
    make();
}

static void step(uint8_t pressed, uint8_t held) {
    t++;
    if (fx) {
        if (--fx == 0) make();
        return;
    }
    if (--time_left == 0) {
        finish();
        return;
    }
    int key = -1;
    for (int k = 0; k < 4 && key < 0; k++)
        if (pressed >> k & 1) key = k;
    if (key >= 0) {
        rep = 0;
    } else {
        for (int k = 1; k < 4 && key < 0; k++)  // not A: holding A opens the menu
            if (held >> k & 1) key = k;
        if (key >= 0 && !(++rep >= REPEAT_AFTER && rep % REPEAT_EVERY == 0)) key = -1;
    }
    if (key >= 0) move(KEY_DIR[key]);
    if (pr == size - 2 && pc == size - 2) {  // the exit
        if (++solved >= MAZES) finish();
        else fx = SOLVED_FRAMES;
    }
}

static void set_rgb(int i, uint8_t r, uint8_t g, uint8_t b) {
    hh->set(hh->rgb[i].r, r);
    hh->set(hh->rgb[i].g, g);
    hh->set(hh->rgb[i].b, b);
}

static void number(unsigned n) {
    char text[3] = {(char)('0' + n % 10), 0, 0};
    hh->draw_text((HH_GRID_W - hh->text_width(text)) / 2, TEXT_Y, text, 255);
}

static void draw(void) {
    hh->clear();
    if (over || fx) {  // between mazes and after the end: the mazes solved
        number(solved);
        for (int i = 0; i < HH_RGB_COUNT; i++) {
            const hh_rgb_t* l = &hh->rgb[i];
            if (over && solved < MAZES) {
                uint32_t ph = over_ms / 4 % 256;
                set_rgb(i, (uint8_t)(ph < 128 ? ph * 2 : (255 - ph) * 2), 0, 0);
            } else if (l->arm < solved || fx) {
                set_rgb(i, 0, 200, 60);
            }
        }
        if (over)
            for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < best * HH_EDGE_COUNT / MAZES ? 40 : 0);
        return;
    }
    int exit = size - 2;
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++) {
            int mr = pr + r - ME_R, mc = pc + c - ME_C;
            int dr = r > ME_R ? r - ME_R : ME_R - r, dc = c > ME_C ? c - ME_C : ME_C - c, d = dr > dc ? dr : dc;
            if (is_wall(mr, mc) && d <= 4) hh->set(hh->grid[r][c], d <= 2 ? 255 : d == 3 ? 110 : 35);
            else if (mr == exit && mc == exit && d <= 5) hh->set(hh->grid[r][c], t % 10 < 5 ? 255 : 60);
        }
    hh->set(hh->grid[ME_R][ME_C], t % 16 < 12 ? 255 : 90);

    // The compass: the arm that points most towards the exit
    int dx = exit - pc, dy = exit - pr, best_arm = 0, best_dot = -1000000;
    for (int a = 0; a < 5; a++) {
        int dot = dx * ARM_DIR[a][0] + dy * ARM_DIR[a][1];
        if (dot > best_dot) best_dot = dot, best_arm = a;
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        if (l->rank == 1 || l->rank == 2) {
            if (l->arm < solved) set_rgb(i, 0, 160, 40);
            else set_rgb(i, 10, 10, 30);
        } else if (l->arm == best_arm) {
            int k = 0;
            while (LINE[k] != l->rank) k++;
            uint32_t v = 80 + 35 * (uint32_t)k;
            set_rgb(i, (uint8_t)(60 * v / 255), (uint8_t)(120 * v / 255), (uint8_t)v);
        }
    }
    // The time left, round the outline; the last 5 s blink
    uint32_t left = time_left * HH_EDGE_COUNT / limit;
    for (int j = 0; j < HH_EDGE_COUNT; j++)
        hh->set(hh->edge[j], (uint32_t)j >= left ? 0 : time_left < 5 * 30 && t % 10 < 5 ? 20 : 70);
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
