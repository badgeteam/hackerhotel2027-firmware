// Spin the bottle: who fetches the beer? Press C once for each player:
// each press lights one more place on the star, spread round it, so up to
// 10 players: the five arms and the five dips of the outline between
// them. B takes the last player away again. Then press D: the bottle in
// the middle spins, slows down and stops pointing at a place. Is it a
// player's place, that lights up from the centre outward: that one
// fetches the beer. Is it an empty place, it blinks dimly: press D again.
// Every place has the same chance. The badge keeps the number of players.
#include "hhapp.h"

#define PLACES 10
#define SECTOR 6554     // one place, in 1/65536 turn
#define CALM_R 106      // a taken place lights its LEDs this near the centre (polar, 1/4 mm)
#define SPIN_TURNS 5    // whole turns before the last one
#define SPIN_SPEED 80   // the spin takes 1 ms for every 80/65536 turn: about 4 s
#define WIN_GROW_MS 700 // the winner's place lights up outward in this time
#define CHEER_MS 1500   // and rings on the grid this long
#define MISS_MS 700     // an empty place blinks this long
#define COUNT_MS 800    // after C or B the grid shows the number of players

enum { IDLE, SPIN, WIN, MISS };

// The order the places fill in: the top arm, the dip opposite, then the
// gaps, never next to the one before. Place p is at p/10 turn clockwise
// from the top: an even p is arm p/2, an odd p the dip after it.
static const uint8_t ORDER[PLACES] = {0, 5, 3, 8, 1, 6, 4, 9, 2, 7};

static const hh_api_t* hh;
static uint8_t players, saved, state, place;
static uint16_t angle, from;  // the bottle's direction, 1/65536 turn clockwise from up
static uint32_t dist, dur, t_ms, count_ms;

static bool taken(uint8_t p) {
    for (int i = 0; i < players; i++)
        if (ORDER[i] == p) return true;
    return false;
}

static uint8_t place_of(uint16_t a) { return (uint8_t)(((uint32_t)a * PLACES + 32768) >> 16) % PLACES; }

// Lights place p up to r from the centre (polar, 1/4 mm): an arm's RGB
// LEDs in red, green, blue; a dip's outline LEDs at v
static void light(uint8_t p, uint8_t r, uint8_t red, uint8_t green, uint8_t blue, uint8_t v) {
    for (int i = 0; i < HH_RGB_COUNT; i++)
        if (!(p & 1) && hh->rgb[i].arm == p / 2 && hh->polar[hh->rgb[i].r][0] <= r) hh->set_rgb(i, red, green, blue);
    uint8_t a = (uint8_t)(p * 128 / 5);
    for (int j = 0; j < HH_EDGE_COUNT; j++) {
        uint16_t s = hh->edge[j];
        int8_t off = (int8_t)(hh->polar[s][1] - a);
        if ((p & 1) && off > -26 && off < 26 && hh->polar[s][0] <= r) hh->set(s, v);
    }
}

// The bottle on the grid: a body and a neck, the neck pointing at angle
static void bottle(void) {
    uint8_t a = (uint8_t)(angle >> 8);
    int s = hh->sin8(a) - 128, c = hh->sin8((uint8_t)(a + 64)) - 128;  // the neck points at (s, -c)
    for (int y = 0; y < HH_GRID_H; y++)
        for (int x = 0; x < HH_GRID_W; x++) {
            int dx = 2 * x - 16, dy = 2 * y - 15;  // from the centre, in half pixels
            int u = dx * s - dy * c;               // along the bottle, 254 to a pixel
            int w = dx * c + dy * s;               // across it
            if (w < 0) w = -w;
            int in = (u > 254 ? 150 : 380) - w;  // the neck's half width, the body's
            if (u + 1524 < in) in = u + 1524;    // the bottom, 6 pixels back
            if (1651 - u < in) in = 1651 - u;    // the neck's end
            in += 127;                           // soft edges, a pixel wide
            hh->set(hh->grid[y][x], in <= 0 ? 0 : in >= 254 ? 255 : (uint8_t)(in + 1));
        }
}

static void spin(void) {
    uint8_t p = (uint8_t)(hh->random() % PLACES);  // every place the same chance
    // anywhere in the middle 2/3 of the place, so the bottle clearly points at one
    int32_t off = (int32_t)(hh->random() % (SECTOR * 2 / 3)) - SECTOR / 3;
    uint16_t end = (uint16_t)((uint32_t)p * 65536 / PLACES + off);
    from = angle;
    dist = SPIN_TURNS * 65536u + (uint16_t)(end - from);
    dur = dist / SPIN_SPEED;
    state = SPIN;
    t_ms = 0;
    if (players != saved) hh->save(&players, 1), saved = players;
}

static void tick(uint32_t elapsed_ms) {
    uint8_t b = hh->pressed();
    t_ms += elapsed_ms;
    count_ms = count_ms > elapsed_ms ? count_ms - elapsed_ms : 0;
    if (state != SPIN) {  // no changes while it spins
        if (b & (HH_BTN_B | HH_BTN_C)) {
            if (b & HH_BTN_C && players < PLACES) players++;
            if (b & HH_BTN_B && players) players--;
            state = IDLE, t_ms = 0, count_ms = COUNT_MS;
        }
        if (b & HH_BTN_D) spin(), count_ms = 0;
    }
    if (state == SPIN) {
        if (t_ms > dur) t_ms = dur;
        uint32_t x = 32768 - t_ms * 32768 / dur;  // the time left, 32768 at the start
        uint64_t x3 = (uint64_t)(x * x) * x >> 15;  // slows down as (time left)^3, 1 << 30 at the start
        angle = (uint16_t)(from + dist - (uint32_t)(dist * x3 >> 30));
        if (t_ms == dur) {
            place = place_of(angle);
            state = players < 2 ? IDLE : taken(place) ? WIN : MISS;
            t_ms = 0;
        }
    } else if (state == MISS && t_ms >= MISS_MS) {
        state = IDLE;
    }

    // The places
    hh->clear();
    for (uint8_t p = 0; p < PLACES; p++)
        if (taken(p)) light(p, CALM_R, 0, 40, 60, 50);
    if (state == SPIN) {
        uint8_t p = place_of(angle);  // where the bottle points now
        if (taken(p)) light(p, CALM_R, 255, 140, 0, 255);
        else light(p, CALM_R, 20, 20, 20, 20);
    } else if (state == MISS) {
        if (t_ms / 175 % 2 == 0) light(place, CALM_R, 30, 30, 30, 30);
    } else if (state == WIN) {
        uint32_t r = 80 + t_ms * 100 / WIN_GROW_MS;  // outward from the centre
        uint8_t v = r < 180 ? 255 : (uint8_t)(128 + hh->sin8((uint8_t)(t_ms / 4)) / 2);  // then a pulse
        light(place, r > 255 ? 255 : (uint8_t)r, v, (uint8_t)(v / 2), 0, v);
    }

    // The grid: the number of players just after C or B, rings for the
    // winner, "C+" asking for players, or else the bottle
    if (count_ms) {
        hh->draw_number(HH_CENTER, 4, players, 255);
    } else if (state == WIN && t_ms < CHEER_MS) {
        for (int y = 0; y < HH_GRID_H; y++)
            for (int x = 0; x < HH_GRID_W; x++) {
                uint16_t s = hh->grid[y][x];
                if (s == HH_NONE) continue;
                uint8_t v = hh->sin8((uint8_t)(hh->polar[s][0] * 8 - t_ms / 2));
                hh->set(s, (uint8_t)(v * (CHEER_MS - t_ms) / CHEER_MS));
            }
    } else if (state == IDLE && players < 2 && t_ms % 2500 >= 1500) {
        hh->draw_text(HH_CENTER, 4, "C+", 255);
    } else {
        bottle();
    }
}

static void init(const hh_api_t* api) {
    hh = api;
    if (!hh->load(&players, 1) || players > PLACES) players = 0;
    saved = players;
    state = IDLE;
    t_ms = count_ms = 0;
}

HH_APP(init, tick);
