// Whack-a-mole: a random arm lights up from its tip towards the centre,
// one LED at a time. Stop it with any button before it reaches the centre:
// the sooner, the more points (5 with one LED lit, 2 with four). Reaching
// the centre ends the game, and so does pressing while nothing is lit. It
// gets faster every 3 hits. The grid shows the points, the outline the best
// so far; after the end, any button starts again. Until the first button
// the badge plays a demo, and it goes back to that 15 s after a game. As in
// the previous firmware's whack-a-mole.
#include "hhapp.h"

#define FIRST_STEP_MS 330  // per LED at the start: 1.7 s to the centre
#define LAST_STEP_MS 100
#define STEP_FASTER_MS 33  // every 3 hits
#define HIT_MS 400         // the arms flash green after a hit
#define OVER_MS 1000       // after the end, buttons wait this long
#define TEXT_Y 4
#define DEMO_OVER_MS 2000      // a demo game's end shows this long
#define BACK_TO_DEMO_MS 15000  // after a game, no button this long: the demo

// An arm's centre line, from the centre out (ranks 1 and 2 are its sides)
static const uint8_t LINE[5] = {0, 3, 4, 5, 6};

static const hh_api_t* hh;
static uint8_t arm;
static uint8_t lit;          // 0: waiting; else how many LEDs of the line are lit, from the tip
static uint32_t wait_ms;     // until the next mole comes up
static uint32_t step_ms, t_ms;
static uint32_t hit_ms;      // the hit flash, still this long
static uint32_t over_ms;
static bool over;
static uint16_t hits, score, best;
static bool demo;            // the badge plays by itself
static uint8_t demo_at;      // it stops this mole with so many LEDs lit
static uint16_t demo_hits;   // and misses after so many hits

static void next_mole(void) {
    wait_ms = 500 + hh->random() % 1000;  // 0.5 to 1.5 s
    arm = (uint8_t)((arm + 1 + hh->random() % 4) % 5);  // never the same arm twice
    lit = 0;
    t_ms = 0;
    demo_at = (uint8_t)(1 + hh->random() % 3);
}

static void start(void) {
    demo_hits = (uint16_t)(15 + hh->random() % 16);
    step_ms = FIRST_STEP_MS;
    hits = score = 0;
    hit_ms = 0;
    over = false;
    arm = (uint8_t)(hh->random() % 5);
    next_mole();
}

static void game_over(void) {
    over = true;
    over_ms = 0;
    if (!demo && score > best) {
        best = score;
        hh->save(&best, sizeof best);
    }
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (demo && pressed) {  // the first button: a real game
        demo = false;
        start();
        pressed = 0;
    }
    if (demo) pressed = lit && lit >= demo_at && hits < demo_hits;  // until it lets one through
    hit_ms = hit_ms > elapsed_ms ? hit_ms - elapsed_ms : 0;
    if (over) {
        over_ms += elapsed_ms;
        if (demo ? over_ms >= DEMO_OVER_MS : over_ms >= OVER_MS && pressed) {
            start();
        } else if (!demo && over_ms >= BACK_TO_DEMO_MS) {
            demo = true;
            start();
        }
    } else if (!lit) {
        if (pressed && !hit_ms) {
            game_over();  // too early (a double press on a hit does not count)
        } else if (wait_ms > elapsed_ms) {
            wait_ms -= elapsed_ms;
        } else {
            lit = 1;
            t_ms = 0;
        }
    } else if (pressed) {
        score += (uint16_t)(6 - lit);
        if (++hits % 3 == 0)
            step_ms = step_ms > LAST_STEP_MS + STEP_FASTER_MS ? step_ms - STEP_FASTER_MS : LAST_STEP_MS;
        hit_ms = HIT_MS;
        next_mole();
    } else {
        t_ms += elapsed_ms;
        if (t_ms >= step_ms) {
            t_ms -= step_ms;
            if (++lit >= 5) {
                lit = 5;
                game_over();  // it reached the centre
            }
        }
    }

    // Draw
    hh->clear();
    char text[6], *p = text + sizeof text - 1;
    uint16_t n = score;
    *p = 0;
    do *--p = (char)('0' + n % 10);
    while ((n /= 10) && p > text);
    hh->draw_text((HH_GRID_W - hh->text_width(p)) / 2, TEXT_Y, p, 255);
    for (int i = 0; i < HH_EDGE_COUNT; i++) hh->set(hh->edge[i], i < best ? 40 : 0);

    uint32_t t = hh->millis();
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &hh->rgb[i];
        uint8_t r = 0, g = 0, b = 0;
        if (over) {
            uint32_t ph = t / 4 % 256;
            r = (uint8_t)(ph < 128 ? ph * 2 : (255 - ph) * 2);  // a red pulse
        } else if (hit_ms) {
            g = 255;  // caught it
            b = 60;
        } else if (led->rank == 1 || led->rank == 2) {
            r = 40, g = 8;  // the arms' sides glow
        } else if (led->arm == arm && lit) {
            int k = 0;  // its place on the line, 0 = centre
            while (LINE[k] != led->rank) k++;
            if (k >= 5 - lit) {  // lit from the tip in
                r = k == 5 - lit ? 255 : 120;
                g = k == 5 - lit ? 40 : 10;
            }
        }
        hh->set(led->r, r);
        hh->set(led->g, g);
        hh->set(led->b, b);
    }
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&best, sizeof best)) best = 0;
    demo = true;
    start();
}

HH_APP(init, tick);
