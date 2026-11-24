// Higher or lower: a number from 1 to 99. Will the next one be higher (D)
// or lower (C)? The arm next to D blinks green for higher, the arm next to
// C orange for lower. A right guess adds one to the streak, which fills the
// outline; a wrong one ends the game. After the end, any button starts
// again; the longest streak is kept on the badge. As in the previous
// firmware's Higher Lower.
#include "hhapp.h"

#define SHOW_MS 800  // the next number shows this long, with the verdict
#define OVER_MS 1000
#define TEXT_Y 4

static const uint8_t LINE[5] = {0, 3, 4, 5, 6};  // an arm's centre line; ranks 1, 2 are its sides

static const hh_api_t* hh;
static uint8_t now, next;
static bool showing, right, over;
static uint16_t streak, best;
static uint32_t t_ms;

static uint8_t pick(uint8_t not_this) {
    for (;;) {
        uint8_t n = (uint8_t)(1 + hh->random() % 99);
        if (n != not_this) return n;
    }
}

static void start(void) {
    now = pick(0);
    next = pick(now);
    showing = over = false;
    streak = 0;
    t_ms = 0;
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    t_ms += elapsed_ms;
    if (over) {
        if (t_ms >= OVER_MS && pressed) start();
    } else if (showing) {
        if (t_ms >= SHOW_MS) {
            if (!right) {
                over = true;
                if (streak > best) {
                    best = streak;
                    hh->save(&best, sizeof best);
                }
            } else {
                now = next;
                next = pick(now);
                showing = false;
            }
            t_ms = 0;
        }
    } else if (pressed & (HH_BTN_C | HH_BTN_D)) {
        bool higher = pressed & HH_BTN_D;
        right = (next > now) == higher;
        if (right) streak++;
        showing = true;
        t_ms = 0;
    }

    // Draw
    hh->clear();
    uint8_t n = showing || over ? next : now;
    char text[3] = {(char)('0' + n / 10), (char)('0' + n % 10), 0};
    const char* s = n >= 10 ? text : text + 1;
    hh->draw_text((HH_GRID_W - hh->text_width(s)) / 2, TEXT_Y, s, 255);
    uint16_t lit = over ? best : streak;  // after the end: the best
    for (int j = 0; j < HH_EDGE_COUNT; j++) hh->set(hh->edge[j], j < lit * 5 ? (over ? 40 : 120) : 0);
    uint32_t blink = hh->millis() % 1000 < 670 ? 160 : 60;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        uint8_t r = 0, g = 0, b = 0;
        bool line = false;
        for (int k = 0; k < 5; k++) line = line || LINE[k] == l->rank;
        if (over) {
            uint32_t ph = t_ms / 4 % 256;
            r = (uint8_t)(ph < 128 ? ph * 2 : (255 - ph) * 2);
        } else if (showing) {
            if (right) g = 255, b = 60;
            else r = 255, g = 20;
        } else if (line && l->arm == 2) {  // next to D: higher
            g = (uint8_t)(200 * blink / 255), b = (uint8_t)(80 * blink / 255);
        } else if (line && l->arm == 3) {  // next to C: lower
            r = (uint8_t)blink, g = (uint8_t)(60 * blink / 255);
        }
        hh->set(l->r, r);
        hh->set(l->g, g);
        hh->set(l->b, b);
    }
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&best, sizeof best)) best = 0;
    start();
}

HH_APP(init, tick);
