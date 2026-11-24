// Target: shoot the badge with a Hacker Hotel 2025 banana or a Fri3d Camp
// 2022 Time Blaster. A hit sends a ring out from the middle in the
// shooter's team colour, flashes the arms and counts; a heal takes one
// hit off. Text typed into a banana's serial port scrolls by. The badge
// shoots back too: C picks its team, B shoots, D gives a banana that team.
// Hold B for a second to count from 0 again.
#include "hhapp.h"

#define HIT_MS 900        // the ring and the flash after a hit
#define COUNT_MS 2500     // the count shows this long after a hit
#define TEXT_MAX 48
#define SCROLL_MS 70      // per column
#define TEXT_Y 4
#define LABEL_MS 1200     // the team's name after C or D
#define RESET_MS 1000     // hold B this long: the count goes back to 0

// The banana's teams: 0 red, 1 green, 2 blue, 3 yellow, 4 magenta, 5 light
// blue; 6 and 7 are not the banana's
static const uint8_t TEAM[8][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 60, 255}, {255, 200, 0}, {255, 0, 200}, {0, 200, 255}, {255, 255, 255}, {255, 90, 0},
};

static const char NAME[8][4] = {"RED", "GRN", "BLU", "YEL", "MAG", "CYA", "WHT", "ORA"};

static const hh_api_t* hh;
static uint8_t my_team = 2;    // the badge's team, for its own shots (blue: it can hit both a red and a green banana)
static uint8_t channel;        // the last shot's channel, for the badge's own
static uint32_t since_shot = 1000000, since_label = 1000000, held_b;
static hh_ir_shot_t pending;   // a shot to send once the IR LED is free
static bool sending;
static uint16_t hits;
static uint8_t team;           // the last shooter's
static bool healed;            // the last one was a heal
static uint32_t since_hit = 1000000;
static char text[TEXT_MAX + 1];
static int text_len;
static uint32_t text_ms;       // scrolling since
static bool text_done;         // a line ended: scroll it once more, then clear

static void got(const hh_ir_shot_t* s) {
    char line[40] = "target: team ", *p = line + 13;
    *p++ = (char)('0' + s->team);
    *p++ = ' ', *p++ = 'c', *p++ = 'm', *p++ = 'd', *p++ = ' ';
    if (s->command >= 10) *p++ = '1';
    *p++ = (char)('0' + s->command % 10);
    *p++ = '\n', *p = 0;
    hh->print(line);

    switch (s->command) {
        case HH_SHOT_SHOOT:
        case HH_SHOT_TEAM:
            channel = s->parameter;
            hits++;
            team = s->team;
            healed = false;
            since_hit = 0;
            break;
        case HH_SHOT_HEAL:
            if (hits) hits--;
            team = s->team;
            healed = true;
            since_hit = 0;
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

static void send(uint8_t command) {
    pending = (hh_ir_shot_t){my_team, 1, command, channel};
    sending = true;
}

static void init(const hh_api_t* api) { hh = api; }

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed(), held = hh->held();
    if (pressed & HH_BTN_B) send(HH_SHOT_SHOOT), since_shot = 0;
    if (pressed & HH_BTN_C) my_team = (uint8_t)((my_team + 1) % 6), since_label = 0;
    if (pressed & HH_BTN_D) send(HH_SHOT_TEAM), since_label = 0;
    held_b = held & HH_BTN_B ? held_b + elapsed_ms : 0;
    if (held_b >= RESET_MS && held_b - elapsed_ms < RESET_MS) hits = 0, since_hit = 1000000;
    if (sending && hh->ir_send_shot(&pending)) sending = false;
    hh_ir_shot_t s;
    while (hh->ir_receive_shot(&s)) got(&s);
    since_hit += elapsed_ms;
    since_shot += elapsed_ms;
    since_label += elapsed_ms;
    text_ms += elapsed_ms;

    hh->clear();
    const uint8_t* c = healed ? TEAM[1] : TEAM[team];
    uint32_t t = hh->millis();
    if (text_len) {
        // the text scrolls in from the right, over and over until the line
        // ends, then once more
        int w = hh->text_width(text), x = HH_GRID_W - (int)(text_ms / SCROLL_MS);
        if (x + w < 0) {
            if (text_done) text_len = 0, text_done = false;
            text_ms = 0;
        }
        if (text_len) hh->draw_text(x, TEXT_Y, text, 255);
    } else if (since_label < LABEL_MS) {
        hh->draw_text(HH_CENTER, TEXT_Y, NAME[my_team], 255);
    } else if (since_hit < COUNT_MS) {
        hh->draw_number(HH_CENTER, TEXT_Y, hits, 255);
    } else {
        // the target: rings round the middle, breathing
        uint8_t breathe = (uint8_t)(60 + (hh->sin8((uint8_t)(t / 8)) >> 2));
        for (int r = 0; r < HH_GRID_H; r++)
            for (int col = 0; col < HH_GRID_W; col++) {
                uint16_t slot = hh->grid[r][col];
                if (slot == HH_NONE) continue;
                unsigned d = hh->polar[slot][0];  // 1/4 mm
                if (d < 6 || (d >= 28 && d < 34) || (d >= 54 && d < 60)) hh->set(slot, breathe);
            }
    }

    // a hit: a ring runs out over every LED, in the team's colour
    if (since_hit < HIT_MS) {
        unsigned front = since_hit * 200 / HIT_MS;  // 1/4 mm from the middle
        for (uint16_t slot = 0; slot < HH_SLOTS; slot++) {
            unsigned d = hh->polar[slot][0];
            if (d + 24 > front && d <= front) hh->set(slot, (uint8_t)(255 - (front - d) * 10));
        }
    }
    // the badge's own shot: a ring running in, in its team's colour
    if (since_shot < HIT_MS / 2) {
        unsigned front = 200 - since_shot * 400 / HIT_MS;
        for (int r = 0; r < HH_GRID_H; r++)
            for (int col = 0; col < HH_GRID_W; col++) {
                uint16_t slot = hh->grid[r][col];
                if (slot == HH_NONE) continue;
                unsigned d = hh->polar[slot][0];
                if (d >= front && d < front + 24) hh->set(slot, 200);
            }
    }
    // the outline: the hits so far, one LED each
    for (int i = 0; i < HH_EDGE_COUNT && i < hits; i++) hh->set(hh->edge[i], 50);

    // the arms: the last team's colour, bright after a hit; the badge's own
    // team after C, D or B
    uint32_t v = since_hit < HIT_MS ? 255 - since_hit * 200 / HIT_MS : hits ? 40 : 0;
    uint32_t own = since_label < LABEL_MS || since_shot < HIT_MS / 2;
    if (own && since_hit >= HIT_MS) c = TEAM[my_team], v = 200;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint32_t k = since_hit < HIT_MS && (t / 60 + hh->rgb[i].rank) % 2 ? v / 3 : v;
        hh->set_rgb(i, (uint8_t)(c[0] * k / 255), (uint8_t)(c[1] * k / 255), (uint8_t)(c[2] * k / 255));
    }
}

HH_APP(init, tick);
