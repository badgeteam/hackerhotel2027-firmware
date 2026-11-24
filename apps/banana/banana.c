// Banana: the badge joins the Hacker Hotel 2025 banana game (thanks,
// Anesidora team!), by the banana's rules. Shoot it with a banana or a
// Fri3d Camp 2022 Time Blaster: a hit sends a ring out in the shooter's
// colour, and 5 hits is GAME OVER (hold B to play again). You can't shoot
// your own team. It shoots back: B shoots (5 shots, the arms show them),
// C reloads, D picks the badge's team, hold D to give a banana that team.
// Text typed into a banana's serial port scrolls by.
#include "hhapp.h"

#define SHOTS 5           // as the banana: 5 shots, then reload
#define LIVES 5           // and 5 hits is game over
#define HIT_MS 900        // the ring and the flash after a hit
#define COUNT_MS 2000     // the hits show this long after one
#define LABEL_MS 1200     // the team's name after D
#define EMPTY_MS 300      // B with no shots left: the arms flash red
#define HOLD_MS 1000      // hold D: give the team; hold B after game over: play again
#define CREDIT_EVERY_MS 60000
#define TEXT_MAX 48
#define SCROLL_MS 70      // per column
#define TEXT_Y 4

// The banana's teams: 0 red, 1 green, 2 blue, 3 yellow, 4 magenta, 5 light
// blue; 6 and 7 are not the banana's
static const uint8_t TEAM[8][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 60, 255}, {255, 200, 0}, {255, 0, 200}, {0, 200, 255}, {255, 255, 255}, {255, 90, 0},
};
static const char NAME[8][4] = {"RED", "GRN", "BLU", "YEL", "MAG", "CYA", "WHT", "ORA"};

// A banana on the grid, one row per word, bit x = column x
static const uint32_t BANANA[HH_GRID_H] = {
    0, 0, 0, 0, 0x00003, 0x10003, 0x10002, 0x18006, 0x0E01C, 0x0FFFC, 0x07FF8, 0x03FF0, 0x00FC0, 0, 0, 0,
};

static const hh_api_t* hh;
static uint8_t my_team = 2;  // blue: it can hit both a red and a green banana
static uint8_t shots = SHOTS, hits, team, channel;
static bool over, healed;
static uint32_t since_hit = 1000000, since_shot = 1000000, since_label = 1000000, since_empty = 1000000;
static uint32_t held_b, held_d, credit_ms = CREDIT_EVERY_MS - 3000;
static hh_ir_shot_t pending;  // a shot to send once the IR LED is free
static bool sending;
static char text[TEXT_MAX + 1];
static int text_len;
static uint32_t text_ms;
static bool text_done;        // a line ended: scroll it once more, then clear

static void send(uint8_t command) {
    pending = (hh_ir_shot_t){my_team, 1, command, channel};
    sending = true;
}

static void got(const hh_ir_shot_t* s) {
    switch (s->command) {
        case HH_SHOT_SHOOT:
            if (over || s->team == my_team) break;  // you can't shoot your own team
            channel = s->parameter;
            team = s->team;
            healed = false;
            since_hit = 0;
            if (++hits >= LIVES) over = true, hh->print("banana: game over\n");
            break;
        case HH_SHOT_HEAL:
            if (over) break;
            if (hits) hits--;
            team = s->team;
            healed = true;
            since_hit = 0;
            break;
        case HH_SHOT_TEAM:  // as the banana: take the sender's team
            my_team = s->team;
            since_label = 0;
            break;
        case HH_SHOT_BYTE: {
            char c = (char)HH_SHOT_BYTE_OF(*s);
            if (text_done) text_len = 0, text_done = false, text_ms = 0;
            if (c == '\n' || c == '\r') {
                if (text_len) text_done = true;
            } else if (text_len < TEXT_MAX) {
                if (!text_len) text_ms = 0;
                text[text_len++] = c;
                text[text_len] = 0;
            }
            break;
        }
        default: break;
    }
}

// Scrolls t in from the right; true once it has gone off the left
static bool scroll(const char* t, uint32_t ms) {
    int x = HH_GRID_W - (int)(ms / SCROLL_MS);
    hh->draw_text(x, TEXT_Y, t, 255);
    return x + hh->text_width(t) < 0;
}

static void ring(uint32_t ms, bool in, uint8_t v) {
    unsigned front = in ? 200 - ms * 400 / HIT_MS : ms * 200 / HIT_MS;  // 1/4 mm from the middle
    for (uint16_t slot = 0; slot < HH_SLOTS; slot++) {
        unsigned d = hh->polar[slot][0];
        if (in ? d >= front && d < front + 24 : d + 24 > front && d <= front) hh->set(slot, v);
    }
}

static void init(const hh_api_t* api) { hh = api; }

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed(), held = hh->held();
    held_b = held & HH_BTN_B ? held_b + elapsed_ms : 0;
    uint32_t was_d = held_d;
    held_d = held & HH_BTN_D ? held_d + elapsed_ms : 0;
    if (over) {
        if (held_b >= HOLD_MS) over = false, hits = 0, shots = SHOTS, held_b = 0, since_hit = 1000000;
    } else {
        if (pressed & HH_BTN_B) {
            if (shots) shots--, send(HH_SHOT_SHOOT), since_shot = 0;
            else since_empty = 0;
        }
        if (pressed & HH_BTN_C) shots = SHOTS;  // reload
        if (!held_d && was_d && was_d < HOLD_MS) my_team = (uint8_t)((my_team + 1) % 6), since_label = 0;  // D tapped
        if (held_d >= HOLD_MS && was_d < HOLD_MS) send(HH_SHOT_TEAM), since_label = 0;  // D held
    }
    if (sending && hh->ir_send_shot(&pending)) sending = false;
    hh_ir_shot_t s;
    while (hh->ir_receive_shot(&s)) got(&s);
    since_hit += elapsed_ms;
    since_shot += elapsed_ms;
    since_label += elapsed_ms;
    since_empty += elapsed_ms;
    text_ms += elapsed_ms;
    credit_ms += elapsed_ms;
    uint32_t t = hh->millis();
    uint8_t pulse = hh->sin8((uint8_t)(t / 4));

    // The grid: a line of text, the team's name, GAME OVER, the hits, the
    // credits, or the banana
    hh->clear();
    if (text_len) {
        if (scroll(text, text_ms)) {
            if (text_done) text_len = 0, text_done = false;
            text_ms = 0;
        }
    } else if (since_label < LABEL_MS) {
        hh->draw_text(HH_CENTER, TEXT_Y, NAME[my_team], 255);
    } else if (over) {
        if (scroll("GAME OVER", credit_ms)) credit_ms = 0;
    } else if (since_hit < COUNT_MS) {
        hh->draw_number(HH_CENTER, TEXT_Y, hits, 255);
    } else if (credit_ms >= CREDIT_EVERY_MS) {
        if (scroll("THANKS ANESIDORA!", credit_ms - CREDIT_EVERY_MS)) credit_ms = 0;
    } else {
        uint8_t v = (uint8_t)(90 + (pulse >> 2));
        for (int r = 0; r < HH_GRID_H; r++)
            for (int col = 0; col < HH_GRID_W; col++)
                if (BANANA[r] >> col & 1) hh->set(hh->grid[r][col], v);
    }
    if (since_hit < HIT_MS) ring(since_hit, false, 255);
    if (since_shot < HIT_MS / 2) ring(since_shot, true, 200);

    // The outline: the hits, a fifth of it each; after GAME OVER, all of it pulses
    unsigned lit = over ? HH_EDGE_COUNT : (unsigned)hits * HH_EDGE_COUNT / LIVES;
    for (unsigned i = 0; i < lit; i++) hh->set(hh->edge[i], over ? (uint8_t)(pulse / 3) : 60);

    // The arms: the shots left, one arm each, in the badge's team colour; a
    // hit flashes the shooter's colour, an empty B red
    const uint8_t* own = TEAM[my_team];
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        const uint8_t* c = own;
        uint32_t v;
        if (since_hit < HIT_MS) {
            c = healed ? TEAM[1] : TEAM[team];
            v = (t / 60 + l->rank) % 2 ? 80 : 255;
        } else if (over) {
            c = TEAM[0], v = pulse;
        } else if (since_empty < EMPTY_MS) {
            c = TEAM[0], v = 200;
        } else {
            // a shot left: a gradient turning slowly round the star (once in 4 s)
            uint8_t w = hh->sin8((uint8_t)(hh->polar[l->r][1] - t * 64 / 1000));
            v = l->arm < shots ? 40 + w * 180u / 255 : 6;
        }
        hh->set_rgb(i, (uint8_t)(c[0] * v / 255), (uint8_t)(c[1] * v / 255), (uint8_t)(c[2] * v / 255));
    }
}

HH_APP(init, tick);
