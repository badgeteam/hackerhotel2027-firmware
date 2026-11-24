// Simon says: the badge plays a sequence on the four arms next to the
// buttons, and you press it back. Every round adds one. The top arm shows
// how far you are, and lights green when you got it all. A wrong button, or
// 5 s without one, ends the game; any button then starts again. The grid
// shows the length; the outline the length and (dimly) the best so far.
// Until the first button the badge plays a demo (it slips up at some
// point), and it goes back to that 15 s after a game. As in the previous
// firmware's Simon.
#include "hhapp.h"

#define MAX 32
#define START_MS 500   // before the badge plays the sequence
#define GAP_MS 165     // between two steps of the sequence
#define PRESS_MS 270   // a button you press lights this long
#define GOOD_MS 600    // the green "got it" before the next round
#define IDLE_MS 5000   // no button for this long ends the game
#define OVER_MS 1000   // after the end, buttons wait this long
#define TEXT_Y 4
#define DEMO_PRESS_MS 400      // the demo presses after this
#define DEMO_OVER_MS 2000      // a demo game's end shows this long
#define BACK_TO_DEMO_MS 15000  // after a game, no button this long: the demo

// The buttons, by their bit in pressed() (HH_BTN_A, C, B, D), and the arm
// next to each: A upper left (4), C lower left (3), B upper right (1),
// D lower right (2). Arm 0, the top one, has no button.
static const uint8_t ARM[4] = {4, 3, 1, 2};
// Simon's colours, by arm: 1 yellow, 2 red, 3 blue, 4 green
static const uint8_t COLOUR[5][3] = {{255, 255, 255}, {255, 200, 0}, {255, 20, 0}, {0, 60, 255}, {0, 255, 40}};
// An arm's centre line, from the centre out (ranks 1 and 2 are its sides)
static const uint8_t LINE[5] = {0, 3, 4, 5, 6};

enum { SHOW, INPUT, GOOD, OVER };

static const hh_api_t* hh;
static uint8_t seq[MAX];  // buttons, 0..3
static uint8_t len, pos;  // pos: the step shown or entered
static uint8_t state;
static int8_t lit = -1;   // the button lit
static uint32_t lit_ms;   // for this long still
static uint32_t t_ms;     // in this state
static uint32_t idle_ms;
static uint16_t best;     // saved with hh->save()
static bool demo;         // the badge plays by itself
static uint8_t demo_slip; // the demo gets this length wrong

static uint32_t on_ms(void) { return len < 16 ? (14u - len / 2u) * 33u : 200u; }

static void add_step(void) {
    if (len < MAX) seq[len++] = (uint8_t)(hh->random() % 4);
    state = SHOW;
    pos = 0;
    t_ms = 0;
}

static void light(int b, uint32_t ms) {
    lit = (int8_t)b;
    lit_ms = ms;
}

static void start(void) {
    len = 0;
    demo_slip = (uint8_t)(3 + hh->random() % 8);
    add_step();
}

static void game_over(void) {
    uint16_t score = (uint16_t)(len - 1);  // the rounds done: this one failed
    state = OVER;
    t_ms = 0;
    if (!demo && score > best) {
        best = score;
        hh->save(&best, sizeof best);
    }
}

static void rgb(const hh_rgb_t* led, const uint8_t c[3], uint32_t scale) {
    hh->set(led->r, (uint8_t)(c[0] * scale / 255));
    hh->set(led->g, (uint8_t)(c[1] * scale / 255));
    hh->set(led->b, (uint8_t)(c[2] * scale / 255));
}

static void draw(void) {
    hh->clear();
    char text[4] = {0};
    uint8_t shown = state == OVER ? (uint8_t)(len - 1) : len;
    if (shown >= 10) text[0] = (char)('0' + shown / 10), text[1] = (char)('0' + shown % 10);
    else text[0] = (char)('0' + shown);
    hh->draw_text((HH_GRID_W - hh->text_width(text)) / 2, TEXT_Y, text, state == SHOW ? 80 : 255);
    for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < shown ? 140 : i < best ? 40 : 0);

    static const uint8_t white[3] = {200, 200, 200}, dim[3] = {20, 20, 20}, green[3] = {0, 255, 60},
                         red[3] = {255, 0, 0};
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &hh->rgb[i];
        bool side = led->rank == 1 || led->rank == 2;
        int line = -1;  // its place on the arm's line, 0 = centre
        for (int k = 0; k < 5; k++)
            if (LINE[k] == led->rank) line = k;
        if (state == OVER) {  // a red pulse
            uint32_t ph = t_ms / 4 % 256;
            rgb(led, red, ph < 128 ? ph * 2 : (255 - ph) * 2);
            continue;
        }
        if (led->arm == 0) {
            if (state == GOOD) rgb(led, green, 255);
            else if (state == INPUT && line >= 0) rgb(led, line < pos * 5 / len ? white : dim, 255);
            continue;
        }
        bool on = lit >= 0 && ARM[lit] == led->arm;
        if (side) rgb(led, COLOUR[led->arm], on ? 255 : 50);
        else if (on) rgb(led, COLOUR[led->arm], 255);
    }
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (demo && pressed) {  // the first button: a real game
        demo = false;
        start();
        pressed = 0;
    }
    t_ms += elapsed_ms;
    if (lit >= 0) {
        lit_ms = lit_ms > elapsed_ms ? lit_ms - elapsed_ms : 0;
        if (!lit_ms) lit = -1;
    }
    switch (state) {
        case SHOW: {
            uint32_t step = on_ms() + GAP_MS;
            if (pos < len && t_ms >= START_MS + pos * step) {
                light(seq[pos], on_ms());
                pos++;
            } else if (pos == len && lit < 0 && t_ms > START_MS) {
                state = INPUT;
                pos = 0;
                idle_ms = 0;
            }
            break;
        }
        case INPUT: {
            int b = -1;
            for (int k = 0; k < 4; k++)
                if (pressed & (1u << k)) b = k;
            if (demo && idle_ms + elapsed_ms >= DEMO_PRESS_MS)  // the right one, but at demo_slip a wrong one
                b = len == demo_slip && pos == len - 1 ? (seq[pos] + 1) % 4 : seq[pos];
            if (b < 0) {
                idle_ms += elapsed_ms;
                if (idle_ms >= IDLE_MS) game_over();
                break;
            }
            idle_ms = 0;
            light(b, PRESS_MS);
            if (b != seq[pos]) {
                game_over();
                break;
            }
            if (++pos == len) {
                state = GOOD;
                t_ms = 0;
            }
            break;
        }
        case GOOD:
            if (t_ms >= GOOD_MS) add_step();
            break;
        case OVER:
            if (demo) {
                if (t_ms >= DEMO_OVER_MS) start();
            } else if (t_ms >= OVER_MS && pressed) {
                start();
            } else if (t_ms >= BACK_TO_DEMO_MS) {
                demo = true;
                start();
            }
            break;
    }
    draw();
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&best, sizeof best)) best = 0;
    demo = true;
    start();
}

HH_APP(init, tick);
