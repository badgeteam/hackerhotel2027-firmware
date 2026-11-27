// The name tag, with all the extras. Your name in 7 styles: rainbow, fire,
// matrix, wave, starfield, glitch, typewriter (B / D, remembered). The
// Hacker Hotel 2025 banana game built in: a hit rings out in the shooter's
// colour and counts, a banana's team colours the arms. Badge friends:
// badges near each other swap names by infrared by themselves, count the
// hackers you met (kept over power-off; an estimate from about 50 on, it
// tops out near 400), and the dots going round show who is nearby. C: who
// is here? (and says hello at once). Hold C and press B / D for
// brightness. Badges with older firmware (it had SAY HI in the menu) and
// no app nearby show "HI <your name>" when this one says hello, at most
// every 10 min; badges with this firmware and no app do not answer.
// When the battery starts to charge, a battery fills up on the grid, over
// and over; tap any button to hide it until charging starts again.

// This file: what shows (the name, a hit count, a team, "HI <name>", the
// panel), the buttons, the saved data, the banana's shots, the name swap
// and the charging screen. styles.c gives every text its look, and draws
// the battery (STYLE_BATTERY).
//
// Infrared: the packets of older firmware's SAY HI (greet.c, gone from the
// firmware since), 'H' HELLO and 'R' REPLY, with the owner's name and,
// when it fits, a NUL and an info byte (0x10 | team, 15 for none) that an
// older badge does not show. Every 20 to 60 s the app sends an 'N' with
// the same contents, a beacon: other NAMETAG badges learn the name, the
// firmware and other apps ignore the type. (Not 'R': an older badge doing
// SAY HI takes any 'R' as its answer.) About every 12 min one badge of a
// group sends 'H' (an 'H' heard puts ours off), which older badges
// answer, and show "HI <name>" for 5 s. An 'H' gets an 'R' after 0.3 s or
// more, after older firmware's own answers (0.3 to 2.3 s from an app), and
// from an older badge early enough to end before its next HELLO, 1.5 s
// after the last. Beacons and HELLOs wait while the air is
// busy, and after our HELLO for its answers.
//
// The hackers met are counted with a 192-bit Bloom filter of name hashes
// in the saved data, so the count survives power-off. Past about 50 the
// count shown is estimated from how full the filter is; it tops out near
// 400, and the more names it holds, the more often a new name looks known.
#include "styles.h"

#define FRAME_MS 33
#define GAP 8                // columns before a scrolling text comes round, as the firmware
#define HIT_MSG_MS 2000      // the hit count shows this long, from 300 ms after the hit
#define LABEL_MS 1500        // the team's name after a TEAM shot
#define REVEAL_MS 350
#define DIAL_MS 1200
#define BAR_MS 1000
#define BURST_MS 1500        // a new friend: the arms and confetti
#define POP_MIN_MS 3000      // "HI <name>" shows at least this long
#define COUNT_MS 1500        // then "#<met>" for a new friend
#define TAP_MS 400           // C shorter than this opens the panel
#define HELLO_GAP_MS 5000    // C sends no HELLO if one was sent or heard this recently
#define NEAR_MS 150000       // heard this recently: nearby
#define REGREET_MS 600000    // not heard this long: "HI" again
#define BACK_GAP_MS 60000    // at most one "HI" a minute for a friend back
#define HELLO_EVERY_MS 600000  // our own HELLO: 10 .. 15 min after one sent or heard
#define REPLY_LATE_MS 1000   // an answer the transmitter never frees for is dropped
#define OLD 0x40000000u      // a start time long ago: nothing shows
#define FALLBACK "HACKER HOTEL 2027"
#define FRIENDS 12
#define POPUPS 4
#define HELLO 'H'
#define REPLY 'R'
#define BEACON 'N'
#ifndef CUT_EXTRAS
#define CUT_EXTRAS 0  // 1: no blink for a friend back, no confetti (the size ladder's step 3, about 120 bytes)
#endif

// The banana's teams, as apps/banana
static const uint8_t TEAM[8][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 60, 255}, {255, 200, 0}, {255, 0, 200}, {0, 200, 255}, {255, 255, 255}, {255, 90, 0},
};
static const char NAME[8][4] = {"RED", "GRN", "BLU", "YEL", "MAG", "CYA", "WHT", "ORA"};

typedef struct {
    uint8_t style;     // 0 .. STYLE_COUNT-1
    uint8_t team;      // 0..7, 0xFF = none
    uint16_t met;      // hackers met, saturates at 65535
    uint8_t seen[24];  // 192-bit Bloom filter of name hashes, k = 2
} save_t;
_Static_assert(sizeof(save_t) == HH_APP_DATA_MAX, "save_t must be 28 bytes");

typedef struct {
    uint32_t hash, last;  // last: when heard
    char name[HH_NAME_MAX + 1];  // "": an empty entry
} friend_t;

typedef struct {
    char text[3 + HH_NAME_MAX + 1];  // "HI <name>"
    uint8_t isnew;                   // then "#<met>"
} popup_t;

// Text sources, for S.src
enum { SRC_NAME, SRC_HIT, SRC_TEAM, SRC_PANEL, SRC_POP, SRC_COUNT, SRC_NONE = 0xFF };

void* memset(void* d, int c, __SIZE_TYPE__ n);

const hh_api_t* hh;
uint32_t nt_mask[HH_GRID_H];
int nt_text_x, nt_text_w;
uint32_t nt_text_ms, nt_text_on;

// All the state in one place: init() clears it in one go, and the code
// reaches every field from one base address (smaller code). Words, not
// bytes, where the code reads them often: the short instructions load
// only words.
static struct {
    uint32_t now;  // this tick's millis()
    uint32_t hit_t, team_t, style_t0, dial_t, bright_t, last_frame, new_t, ret_t, src_t, c_down;
    uint32_t beacon_at, query_at, quiet_until, last_hello, reply_due, own_hash;
    const char* shown;  // this frame's text
    char* put_at;       // put()
    uint32_t hits;      // this session's, not saved
    uint32_t shot_team, heal, new_team, bright_lvl, src, npop, pop_head, pop_count;
    uint32_t reply_pending, hello_now, panel, c_on, c_used;
    uint32_t chg, chg_on;  // charging(); the battery shows
    char num[12];       // dec()
    save_t sv;
    friend_t fr[FRIENDS];  // the most recently heard first
    popup_t pop[POPUPS];   // a ring: pop[pop_head] shows first
    char panel_text[96];
} S;

static uint32_t rnd(uint32_t n) { return hh->random() % n; }
static bool due(uint32_t t) { return (int32_t)(S.now - t) >= 0; }
static void save(void) { hh->save(&S.sv, sizeof S.sv); }  // false (no room): goes on in RAM

static void quiet(uint32_t ms) {
    if ((int32_t)(S.now + ms - S.quiet_until) > 0) S.quiet_until = S.now + ms;
}

// FNV-1a, a..z as A..Z
static uint32_t fnv(const uint8_t* d, int n) {
    uint32_t x = 2166136261u;
    while (n--) {
        uint8_t c = *d++;
        if ((uint8_t)(c - 'a') < 26) c -= 32;
        x = (x ^ c) * 16777619u;
    }
    return x;
}

// The owner's name into b (at most 32 bytes, no NUL); its length
static int own(uint8_t* b) {
    const char* nm = hh->name();
    int n = 0;
    while (n < HH_NAME_MAX && nm[n]) b[n] = (uint8_t)nm[n], n++;
    return n;
}

// v in decimal, in S.num (room for one more character in front)
static char* dec(unsigned v) {
    char* p = S.num + 7;
    *p = 0;
    do *--p = (char)('0' + v % 10);
    while (v /= 10);
    return p;
}

// Appends to the panel text, bounded
static void put(const char* s) {
    char* p = S.put_at;
    while (*s && p < &S.panel_text[sizeof S.panel_text - 1]) *p++ = *s++;
    *p = 0;
    S.put_at = p;
}

// The friends heard in the last NEAR_MS: the first ones of the table
static int nearby(void) {
    int k = 0;
    while (k < FRIENDS && S.fr[k].name[0] && S.now - S.fr[k].last < NEAR_MS) k++;
    return k;
}

// Name + tail, as HELLO or REPLY
static bool say(uint8_t type) {
    uint8_t b[HH_IR_PACKET_MAX];
    int n = own(b);
    if (n && n + 2 <= HH_IR_PACKET_MAX) b[n] = 0, b[n + 1] = (uint8_t)(0x10 | (S.sv.team & 15)), n += 2;
    return hh->ir_send_packet(type, b, (uint8_t)n);
}

static uint32_t beacon_gap(void) {
    uint32_t ms = 20000 + rnd(10000) + 5000u * (uint32_t)nearby();
    return ms < 60000 ? ms : 60000;
}

static void hello_heard(void) {  // or sent: one HELLO per group every ~12 min
    S.query_at = S.now + HELLO_EVERY_MS + rnd(HELLO_EVERY_MS / 2);
    S.last_hello = S.now;
}

static void popnext(void) {
    S.npop--;
    S.pop_head = (S.pop_head + 1) % POPUPS;
    S.pop_count = 0;
    S.src = SRC_NONE;
}

// Sets a bit of the Bloom filter; whether it was set
static int bloom(unsigned a) {
    uint8_t *p = &S.sv.seen[a >> 3], m = (uint8_t)(1 << (a & 7)), was = *p & m;
    *p |= m;
    return was;
}

// The hackers met: the count, or the estimate from how many bits of the
// filter are set (linear counting: a name sets 2 bits, so going from i to
// i + 1 bits takes 96 / (192 - i) names), whichever is more. The count
// misses the new names that looked known, the estimate is rough while
// small.
static unsigned met(void) {
    unsigned b = 0, n = 128;
    for (int i = 0; i < 192; i++)
        if (S.sv.seen[i >> 3] >> (i & 7) & 1) n += 96 * 256 / (192 - b++);
    n >>= 8;
    return n > S.sv.met ? n : S.sv.met;
}

static void learn(const uint8_t* d, int n, uint8_t team) {
    uint32_t hs = fnv(d, n), now = S.now;
    if (!n || hs == S.own_hash) return;  // no name, our own echo, or a namesake
    // Find the name, else take the last entry (empty, or the oldest), and
    // move it to the front
    int i = 0;
    while (i < FRIENDS - 1 && !(S.fr[i].name[0] && S.fr[i].hash == hs)) i++;
    friend_t f = S.fr[i];
    bool miss = !f.name[0] || f.hash != hs, back = now - f.last >= REGREET_MS;
    for (; i; i--) S.fr[i] = S.fr[i - 1];
    if (miss) {
        f.hash = hs;
        for (int k = 0; k < n; k++) f.name[k] = (char)d[k];
        f.name[n] = 0;
    }
    f.last = now;
    S.fr[0] = f;

    int known = bloom(hs % 192);
    known = bloom(hs / 192 % 192) && known;
    if (!known) {
        if (S.sv.met < 0xFFFF) S.sv.met++;
        save();
        hh->print("nametag: new ");
        hh->print(f.name);
        hh->print("\n");
        S.new_t = now;
        S.new_team = team;
    } else if (back && !miss && now - S.ret_t >= BACK_GAP_MS) {
        S.ret_t = now;
    } else {
        // A friend who stays near: no hello again. Nor one who dropped out
        // of the table, in a crowd of more than FRIENDS: that would greet
        // the whole crowd over and over instead of showing the name.
        return;
    }
    if (S.npop < POPUPS) {  // else the name still counts and is in the panel
        popup_t* q = &S.pop[(S.pop_head + S.npop++) % POPUPS];
        char *t = q->text, *nm = f.name;
        *t++ = 'H', *t++ = 'I', *t++ = ' ';
        while ((*t++ = *nm++)) {}
        q->isnew = !known;
    }
}

static void heard(const hh_ir_packet_t* p) {
    uint8_t t = p->type;
    const uint8_t* d = p->data;
    int len = p->length, n = 0;
    uint32_t q = t == HELLO ? 400 : 300;
    quiet(t == HELLO ? 2500 : q + rnd(q));
    if (t != HELLO && t != REPLY && t != BEACON) return;
    while (n < len && d[n]) n++;  // any bytes, as the firmware: the font shows those it lacks as a space
    bool tail = len == n + 2 && d[n + 1] >> 4 == 1;
    if (t == HELLO) {
        hello_heard();
        // An older badge calls again 1.5 s after its HELLO began, and does
        // not hear while it sends: our answer ends before that. A packet of
        // n bytes takes 7 + 18 * (n + 3) ms at the slowest (hhapp.h); ours
        // has the name and the tail.
        uint8_t b[HH_NAME_MAX];
        int w = 1450 - 14 - (len + own(b) + 8) * 18, lo = w > 600 ? 300 : 20;
        if (tail) w = 2300, lo = 300;
        if (!S.reply_pending) S.reply_pending = 1, S.reply_due = S.now + (uint32_t)lo + rnd(w > lo ? (uint32_t)(w - lo) : 1);
    }
    learn(d, n, tail ? d[n + 1] & 15 : 15);
}

static void got(const hh_ir_shot_t* s) {
    uint8_t team = s->team & 7, cmd = s->command;
    quiet(500);
    if (cmd == HH_SHOT_TEAM) {
        S.sv.team = team;
        save();
        S.team_t = S.now;
    } else if (cmd == HH_SHOT_HEAL || (cmd == HH_SHOT_SHOOT && S.sv.team != team)) {  // you can't shoot your own team
        S.heal = cmd == HH_SHOT_HEAL;
        if (!S.heal) S.hits += S.hits < 0xFFFF, S.shot_team = team;
        else if (S.hits) S.hits--;
        S.hit_t = S.now;
    }
}

static void restyle(uint32_t now) {
    S.style_t0 = now;
    S.src = SRC_NONE;  // the text starts over: MATRIX decodes it, TYPEWRITER types it
    style_start(now);
}

static void toggle_panel(void) {
    if ((S.panel = !S.panel)) {
        int k = nearby();
        S.put_at = S.panel_text;
        put("MET ");
        put(dec(met()));
        put(" NEAR ");
        put(dec((unsigned)k));
        for (int i = 0; i < k; i++) put(" "), put(S.fr[i].name);  // the most recent first
        if (S.hits) put(" HITS "), put(dec(S.hits));
        if (S.sv.team < 8) put(" TEAM "), put(NAME[S.sv.team]);
        S.src = SRC_NONE;
        if (hh->name()[0] && S.now - S.last_hello >= HELLO_GAP_MS) S.hello_now = 1;
    }
}

// Team colour t at v (255: exactly t)
static uint32_t col(const uint8_t* t, unsigned v) {
    uint32_t c = 0;
    for (int k = 0; k < 3; k++) c = c << 8 | (t[k] * v + 255) >> 8;
    return c;
}

// A ring over all the LEDs: those 0 .. width - 1 inside front (1/4 mm
// from the middle) get v; clear: those outside it go dark
static void ring(int front, int width, uint8_t v, bool clear) {
    for (uint16_t s = 0; s < HH_SLOTS; s++) {
        int e = front - hh->polar[s][0];
        if (e < width && (e >= 0 || clear)) hh->set(s, e < 0 ? 0 : v);
    }
}

static void frame(uint32_t now) {
    const hh_api_t* h = hh;
    const char *s, *nm = h->name();
    uint32_t id, on, lim, per;
    int w, x;

again:  // the text: the first that is on wins; some end by themselves
    lim = ~0u;
    if (now - S.hit_t - 300 < HIT_MSG_MS) s = dec(S.hits), id = SRC_HIT;
    else if (now - S.team_t < LABEL_MS) s = NAME[S.sv.team & 7], id = SRC_TEAM;
    else if (S.panel) s = S.panel_text, id = SRC_PANEL;
    else if (S.npop && S.pop_count) s = dec(met()) - 1, *(char*)s = '#', id = SRC_COUNT, lim = COUNT_MS;
    else if (S.npop) s = S.pop[S.pop_head].text, id = SRC_POP;
    else s = nm[0] ? nm : FALLBACK, id = SRC_NAME;
    if (id != S.src) S.src = id, S.src_t = now;
    on = now - S.src_t;
    w = h->text_width(s);
    per = (uint32_t)w + GAP;
    if (id == SRC_PANEL || id == SRC_POP) {
        lim = per * 100 + 500;  // scrolled through once
        if (id == SRC_POP && lim < POP_MIN_MS) lim = POP_MIN_MS;
    }
    if (on >= lim) {
        if (id == SRC_PANEL) S.panel = 0;
        else if (id == SRC_POP && S.pop[S.pop_head].isnew) S.pop_count = 1;
        else popnext();
        goto again;
    }
    if (w <= HH_GRID_W) {
        x = (HH_GRID_W - w) / 2;
        nt_text_ms = on;
    } else {
        x = -(int)(on / 100 % per);
        nt_text_ms = on % (per * 100);
    }
    S.shown = s;
    nt_text_x = x, nt_text_w = w, nt_text_on = on;

    // The mask: the text at 255, read back. The second copy is off the
    // grid unless the text scrolls.
    h->clear();
    h->draw_text(x, TEXT_Y, s, 255);
    h->draw_text(x + (int)per, TEXT_Y, s, 255);
    for (int y = 0; y < HH_GRID_H; y++) {
        uint32_t m = 0;
        for (int c = 0; c < HH_GRID_W; c++)
            if (h->get(h->grid[y][c])) m |= 1u << c;
        nt_mask[y] = m;
    }

    unsigned st = S.sv.style, team = S.sv.team;
    style_draw((uint8_t)st, now);

    // The outline: who is nearby going round; the brightness or the
    // style's dial; confetti for a new friend
    int k = nearby();
    if (k > 8) k = 8;
    for (int j = 0; j < k; j++) {  // a bright dot with a dark LED either side, to stand out from the style
        const uint16_t* e = h->edge + (now / 40 + (uint32_t)(j * HH_EDGE_COUNT / k)) % HH_EDGE_COUNT;
        h->set(e[0], 255);
        if (e > h->edge) h->set(e[-1], 0);
        if (e < h->edge + HH_EDGE_COUNT - 1) h->set(e[1], 0);
    }
    uint32_t na = now - S.new_t, ha = now - S.hit_t, ta = now - S.team_t, ra = now - S.ret_t;
    int mode = now - S.bright_t < BAR_MS ? 1 : now - S.dial_t < DIAL_MS ? 2 : 0;
    int lit = ((int)S.bright_lvl + 1) * HH_EDGE_COUNT / 6, hi = 120 + h->sin8((uint8_t)(now / 3)) / 2;
    for (int i = 0; i < HH_EDGE_COUNT; i++) {
        int v = -1, a = i / 10 < 6 ? i / 10 : 6;
        if (mode == 1) v = i < lit ? 200 : 0;
        if (mode == 2) v = a == (int)st ? hi : 12;
        if (!CUT_EXTRAS && na < BURST_MS && !rnd(6)) v = 255;
        if (v >= 0) h->set(h->edge[i], (uint8_t)v);
    }

    // Rings: a hit out, a heal in, a team taken in
    uint32_t heal = S.heal, dur = heal ? 450 : 900;
    int hf = (int)(ha * 200 / dur);
    if (ha < dur) ring(heal ? 200 - hf : hf, 24, heal ? 200 : 255, false);
    if (ta < 900) ring(200 - (int)(ta * 200 / 900), 24, 255, false);

    // The arms: the style's colour, then the team, a friend, a hit
    const uint8_t *U = team < 8 ? TEAM[team] : 0, *NT = S.new_team < 8 ? TEAM[S.new_team] : 0, *ST = TEAM[S.shot_team];
    uint32_t pa = now % 8000, tip = 190 + h->sin8((uint8_t)(now / 8)) / 4u, took = h->sin8((uint8_t)(ta / 2));
    uint32_t blink = now / 60, healv = 255 - ha * 255 / 450;
    int f = pa < 420 ? 6 - (int)(pa / 60) : 99, g = ta < LABEL_MS ? 6 - (int)(ta / 80) : 99;
    bool newb = na < BURST_MS, back = ra < 300 && !(ra / 100 & 1), hit = !heal && ha < dur, healb = heal && ha < dur;
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &h->rgb[i];
        int r = l->rank;
        uint32_t c = style_arm((uint8_t)st, i, now), v = 255;
        const uint8_t* T = 0;
        if (U) {
            if (r == 6) T = U, v = tip;                                  // the tips,
            if (r == 5) T = U, v = 0;                                    // set off from the style's colours
            if ((unsigned)(r - f) < 2) T = U, v = r == f ? 255 : 120;  // a pulse in, every 8 s
            if (r >= g) T = U, v = took;                               // just taken
        }
        if (newb) {  // a new friend: its team, or a rainbow
            T = NT, v = 255;
            if (!T) c = h->hsv((uint8_t)(l->arm * 51 + r * 20 + now / 2), 255, 255);
        }
        if (!CUT_EXTRAS && back && r == 6) T = TEAM[6], v = 255;  // a friend back: white tips blink twice
        if (hit && h->polar[l->r][0] <= hf) T = ST, v = (blink + (uint32_t)r) & 1 ? 80 : 255;
        if (healb) T = TEAM[1], v = healv;
        if (T) c = col(T, v);
        h->set_rgb(i, HH_RED(c), HH_GREEN(c), HH_BLUE(c));
    }

    // A new style comes in from the middle
    uint32_t ts = now - S.style_t0;
    if (ts < REVEAL_MS) ring((int)(ts * 190 / REVEAL_MS), 14, 255, true);
}

static void init(const hh_api_t* api) {
    hh = api;
    memset(&S, 0, sizeof S);
    uint32_t now = S.now = api->millis();
    if (!api->load(&S.sv, sizeof S.sv)) S.sv.team = 0xFF;  // load leaves it alone when it fails
    if (S.sv.style >= STYLE_COUNT) S.sv.style = 0;
    if (S.sv.team > 7) S.sv.team = 0xFF;
    uint8_t b[HH_NAME_MAX];
    S.own_hash = fnv(b, own(b));
    S.hit_t = S.team_t = S.dial_t = S.bright_t = S.new_t = S.ret_t = S.last_hello = S.last_frame = now - OLD;
    S.src = SRC_NONE;
    S.quiet_until = now;
    S.chg = api->charging();  // the battery shows only when charging starts
    S.beacon_at = now + 1000 + rnd(2000);
    S.query_at = now + 5000 + rnd(10000);
    restyle(now);
    static char line[] = "nametag: style 0 met ";
    line[15] = (char)('0' + S.sv.style);
    api->print(line);
    char* m = dec(met());
    S.num[7] = '\n';  // S.num[8] stays 0
    api->print(m);
}

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    const hh_api_t* h = hh;
    uint32_t now = S.now = h->millis();

    // Buttons: B / D the style, C the panel, C held + B / D the brightness
    uint8_t pressed = h->pressed(), held = h->held();
    uint32_t chg = h->charging();
    if (chg != S.chg) {
        S.chg = S.chg_on = chg;     // starts: the battery shows; stops: it goes
        if (chg) style_start(now);  // no glow left on the outline; the style starts afresh after
    }
    if (held & HH_BTN_C) {
        if (!S.c_on) S.c_on = 1, S.c_down = now, S.c_used = 0;
    } else if (S.c_on) {
        S.c_on = 0;
        if (!S.c_used && now - S.c_down < TAP_MS) toggle_panel();
    }
    // While the battery shows, a press hides it and does nothing else
    // (nor does the release of C)
    if (S.chg_on) S.c_used = 1, S.chg_on = !pressed, pressed = 0;
    int dir = pressed & HH_BTN_B ? 1 : pressed & HH_BTN_D ? -1 : 0;
    if (dir && (held & HH_BTN_C)) {
        S.bright_lvl = (uint32_t)h->step_brightness(dir);
        S.bright_t = now;
        S.c_used = 1;
    } else if (dir) {
        S.sv.style = (uint8_t)((S.sv.style + STYLE_COUNT + dir) % STYLE_COUNT);
        save();
        restyle(now);
        S.dial_t = now;
        S.panel = 0;
        if (S.npop) popnext();
    }

    // Shots, the banana's NEC frames and remotes, packets
    hh_ir_shot_t shot;
    while (h->ir_receive_shot(&shot)) got(&shot);
    hh_ir_t m;
    while (h->ir_receive(&m)) {}
    hh_ir_packet_t p;
    while (h->ir_receive_packet(&p)) heard(&p);

    // Sending: an answer first, then, when the air is free, a HELLO (for
    // C, or due) or a beacon; each tried again while the transmitter is
    // busy. After our HELLO the air stays free for the answers.
    if (S.reply_pending && due(S.reply_due)) {
        if ((int32_t)(now - S.reply_due) > REPLY_LATE_MS) S.reply_pending = 0;
        else if (say(REPLY)) S.reply_pending = 0, S.beacon_at = now + beacon_gap();
    } else if (h->name()[0] && due(S.quiet_until)) {
        if (S.hello_now || due(S.query_at)) {
            if (say(HELLO)) S.hello_now = 0, hello_heard(), quiet(2500);
        } else if (due(S.beacon_at) && say(BEACON)) {
            S.beacon_at = now + beacon_gap();
        }
    }

    if (now - S.last_frame >= FRAME_MS) {
        S.last_frame = now;
        if (S.chg_on)
            style_draw(STYLE_BATTERY, now);  // the battery alone
        else
            frame(now);
    }
}

HH_APP(init, tick);
