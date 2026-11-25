// One big eye: the centre grid is the eyeball, the RGB LEDs on the arms its
// lashes. It looks around, blinks, and now and then opens its pupil wide to
// search and tightens it to focus. Every 30 to 60 s it has a mood: a wink,
// an eye roll, dizzy, sleepy, suspicious, angry, surprised, hypnotised,
// crying, crazy or laughing. B: a mood now; D / C: brighter / dimmer (the
// outline shows the level). A Samsung TV remote: Vol+ / Vol- the same, and
// a mood:
// 3 wink, 4 eye roll, 5 dizzy, 6 sleepy, 7 suspicious, 8 angry, 9 surprised,
// 0 hypnotised, red crazy, blue crying, yellow laughing, green one at random;
// and 1 or 2: it falls in love, which it never does by itself. A banana: a
// shot surprises it, a second one soon after makes it angry, and the lashes
// flash the shooter's team colour; a heal: love; a team: a wink.
#include "hhapp.h"

// The eye thinks in frames: 30 a second, stepped from the 5 ms ticks
#define FPS 30

// Grid positions in 1/16 cell (2 mm / 16) from the grid's centre
#define CELL 16
// Iris radius: 4.5 cells
#define IRIS 72
// The lids are arcs, flatter towards the sides with this radius (13 cells).
// Fully open they clear the whole grid.
#define LID_R2 (208 * 208)
// How far the lids open from where they meet: the upper lid travels
// further, so the eye closes below its middle
#define LID_UP 192
#define LID_DOWN 112
// Pupil radius searching (2.6 .. 3 cells) and focused (1.1 .. 1.5 cells)
#define PUPIL_WIDE 42
#define PUPIL_FOCUS 21
// Blink: 3 frames closing, 2 shut, 5 opening (1/3 s)
#define BLINK 10
// Moods fade in and out over this many frames
#define MOOD_RAMP 12
// Sleepy: the startle, in frames into the mood
#define WAKE (10 * FPS)
// Crying: tears at once at most
#define TEARS 3
// A held remote key repeats its frame every 108 ms: a key counts after
// this long without one
#define IR_QUIET_MS 250
// Samsung TV remote: volume up and down set the brightness
#define SAMSUNG_VOL_UP 0x07
#define SAMSUNG_VOL_DOWN 0x0B
// A banana shot within this many frames of the one before makes it angry;
// the lashes flash the shooter's team colour for the first second of them
#define SHOT_AGAIN (5 * FPS)

typedef enum {
    MOOD_NONE,
    MOOD_WINK,        // a slow, cheeky blink with a lash flash, looking at you
    MOOD_ROLL,        // left, up over the top, right: "oh please", lid half closed
    MOOD_DIZZY,       // the iris spirals, the lids wobble, the lashes spin a rainbow
    MOOD_SLEEPY,      // the lids get heavy, it nods off, sleeps, and startles awake
    MOOD_SUSPICIOUS,  // a squint, the gaze slides slowly side to side, amber lashes
    MOOD_ANGRY,       // the upper lid frowns into a V, pinpoint pupil, trembling, red lashes
    MOOD_SURPRISED,   // frozen on you, wide-eyed, white lashes; then a double blink
    MOOD_HYPNO,       // a spiral turning in a big iris, dazed lids, purple waves out the lashes
    MOOD_CRYING,      // sad lids, looking down, tears falling, blue tears running down the lower arms
    MOOD_CRAZY,       // the pupil twitches and jumps, the lids out of step, the lashes flicker
    MOOD_LAUGHING,    // the eye squeezed into a bobbing ^, golden lashes bouncing
    MOOD_LOVE,        // a beating heart for a pupil, pink lashes: only from a Samsung remote or a banana
    MOOD_COUNT
} mood_t;

// The moods it picks by itself: all but love
#define MOOD_PICKS (MOOD_LOVE - 1)

static const uint16_t mood_frames[MOOD_COUNT] = {
    0,           FPS,     5 * FPS / 2, 4 * FPS, 12 * FPS, 7 * FPS, 5 * FPS,
    5 * FPS / 2, 6 * FPS, 7 * FPS,     4 * FPS, 4 * FPS,  6 * FPS,
};

typedef struct {
    uint8_t r, g, b;
} rgb_t;

// The lids this frame, each 0 ..= 256: how far open (a blink), the upper lid
// lowered, the lower lid raised; the upper lid slanted (its right end this
// much lower, in 1/16 cell), and frowning (its middle this much lower;
// negative: sad, the middle higher)
typedef struct {
    int open, droop, raise, tilt, vee;
} lids_t;

static const hh_api_t* hh;

static int gx, gy;          // the iris centre
static int tx, ty;          // where it is jumping to
static uint32_t hold;       // frames until the next jump
static int pupil, pupil_to; // pupil radius
static uint32_t focus;      // frames until the pupil focuses, 0 = focused
static uint32_t next_wide;  // frames until a jump opens the pupil wide
static uint32_t blink;      // frames into a blink, 0 = open
static uint32_t next_blink; // frames until the next blink
static bool again;          // a second blink follows this one
static mood_t mood, last;   // last: the one before, not to have it twice in a row
static uint32_t mt;         // frames into the mood
static uint32_t next_mood;  // frames until the next mood
static uint32_t t;          // frames since the start
static uint32_t acc;        // ms * FPS towards the next frame
static int lid_low[HH_GRID_W];  // the lower lid's edge per column, as last drawn
static uint8_t tear_c[TEARS];   // a tear's column + 1; 0 = no tear
static int tear_y[TEARS], tear_v[TEARS];  // its height, and its speed in 1/4
static uint32_t ir_ms;          // when the last IR frame came in
static uint8_t level_shown;     // frames the outline still shows the brightness
static uint8_t shot_left;       // frames left of SHOT_AGAIN after a banana shot
static uint8_t shot_team;       // the shooter's team

// Samsung TV remote (Samsung32) keys, by their command; MOOD_NONE: at random
static const uint8_t samsung_keys[][2] = {
    {0x04, MOOD_LOVE},     {0x05, MOOD_LOVE},       {0x06, MOOD_WINK},      {0x08, MOOD_ROLL},
    {0x09, MOOD_DIZZY},    {0x0A, MOOD_SLEEPY},     {0x0C, MOOD_SUSPICIOUS}, {0x0D, MOOD_ANGRY},
    {0x0E, MOOD_SURPRISED}, {0x11, MOOD_HYPNO},     {0x6C, MOOD_CRAZY},     {0x16, MOOD_CRYING},
    {0x15, MOOD_LAUGHING}, {0x14, MOOD_NONE},
};

// The banana's team colours, as hues: red, green, blue, yellow, magenta,
// light blue, then white (no colour) and orange
static const uint8_t team_hue[8] = {0, 86, 165, 36, 222, 140, 0, 15};

// ---------------------------------------------------------------- helpers

static int sgn(int v) { return (v > 0) - (v < 0); }
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t sat_sub(uint32_t a, uint32_t b) { return a > b ? a - b : 0; }

static uint8_t sin8(uint8_t x) { return hh->sin8(x); }
static uint8_t rnd8(void) { return (uint8_t)(hh->random() >> 8); }

// a * b / 255
static uint8_t scale(uint8_t a, uint8_t b) { return (uint8_t)((a * b + 255) >> 8); }

// A phase that turns per_s / 256 turns a second, at frame f
static uint8_t phase(uint32_t f, uint32_t per_s) { return (uint8_t)(f * per_s / FPS); }

static rgb_t scaled(rgb_t c, uint8_t v) { return (rgb_t){scale(c.r, v), scale(c.g, v), scale(c.b, v)}; }

static rgb_t hsv(uint8_t h, uint8_t s, uint8_t v) {
    uint32_t c = hh->hsv(h, s, v);
    return (rgb_t){HH_RED(c), HH_GREEN(c), HH_BLUE(c)};
}

static uint8_t mix1(uint8_t x, uint8_t y, uint8_t amt) { return (uint8_t)((x * (256 - amt) + y * amt) >> 8); }

static rgb_t mix(rgb_t a, rgb_t b, uint8_t amt) {
    return (rgb_t){mix1(a.r, b.r, amt), mix1(a.g, b.g, amt), mix1(a.b, b.b, amt)};
}

// A point on a circle of radius r, at angle a (1/256 turn, 64 = down)
static void circle(uint8_t a, int r, int* x, int* y) {
    *x = (sin8((uint8_t)(a + 64)) - 128) * r / 128;
    *y = (sin8(a) - 128) * r / 128;
}

// One brightness level up (dir 1) or down (-1), as the firmware's B / D
// outside apps; the outline shows it
static void brighter(int dir) {
    hh->step_brightness(dir);
    level_shown = FPS;
}

// ------------------------------------------------------------------ moods

static void start(mood_t m) {
    mood = m;
    last = m;
    mt = 0;
    switch (m) {
        case MOOD_DIZZY: pupil_to = PUPIL_WIDE - 6; break;
        case MOOD_SUSPICIOUS: pupil_to = PUPIL_FOCUS - 5; break;
        case MOOD_ANGRY: pupil_to = PUPIL_FOCUS - 8; break;
        case MOOD_SURPRISED: pupil_to = PUPIL_FOCUS - 9; break;
        case MOOD_HYPNO: pupil_to = PUPIL_FOCUS - 4; break;
        case MOOD_CRYING: pupil_to = PUPIL_WIDE - 8; break;  // big and watery
        default: pupil_to = PUPIL_FOCUS; break;
    }
}

// A mood at random, never the one before, never love
static mood_t pick(void) {
    mood_t m = (mood_t)(1 + rnd8() % MOOD_PICKS);
    if (m == last) m = (mood_t)(m % MOOD_PICKS + 1);
    return m;
}

// How much of the mood shows, 0 ..= 256: it fades in and out
static int amount(void) {
    uint32_t left = sat_sub(mood_frames[mood], mt);
    uint32_t a = (mt < left ? mt : left) * 256 / MOOD_RAMP;
    return a > 256 ? 256 : (int)a;
}

// Love: a heartbeat, lub-dub, 0 ..= 256, a little over once a second
static int beat(void) {
    int p = (int)(mt % 27);
    return p < 4 ? 256 - p * 64 : p >= 7 && p < 11 ? 192 - (p - 7) * 48 : 0;
}

// No mood: jumps, blinks, now and then a wide pupil, and the next mood
static void wander(void) {
    if (hold == 0) {
        // a quarter of the jumps go back to the middle
        if (rnd8() < 64) {
            tx = ty = 0;
        } else {
            uint8_t a = rnd8();
            circle(a, 24 + rnd8() % 41, &tx, &ty);  // 1.5 .. 4 cells out
        }
        if (next_wide == 0) {
            // searching: wide open, then it focuses; it stays long enough on
            // this spot to see both
            pupil_to = PUPIL_WIDE + rnd8() % 7;
            focus = FPS / 4 + hh->random() % (FPS / 4);
            hold = focus + FPS + hh->random() % FPS;
            next_wide = 90 * FPS + hh->random() % (60 * FPS);  // 1.5 .. 2.5 min
        } else {
            hold = FPS / 3 + hh->random() % (2 * FPS);
        }
        if (rnd8() < 40 && blink == 0) next_blink = 0;  // blink with the jump
    } else {
        hold--;
    }
    next_wide = sat_sub(next_wide, 1);
    if (focus > 0 && --focus == 0) pupil_to = PUPIL_FOCUS + rnd8() % 7 - 3;

    if (blink > 0) {
        if (++blink > BLINK) {
            blink = 0;
            next_blink = again ? 3 : 2 * FPS + hh->random() % (5 * FPS);
            again = false;
        }
    } else if (next_blink == 0) {
        blink = 1;
        again = rnd8() < 45;
    } else {
        next_blink--;
    }

    // a mood, between blinks and with a focused pupil
    next_mood = sat_sub(next_mood, 1);
    if (next_mood == 0 && blink == 0 && focus == 0) start(pick());
}

// In a mood: where it looks, and the mood's end
static void act(void) {
    int m = (int)mt;
    switch (mood) {
        case MOOD_ANGRY:  // trembles
            tx = (m * 7 % 5 - 2) * 2;
            ty = (m * 3 % 5 - 2) * 2;
            break;
        case MOOD_ROLL:  // left, then over the top (angle 192 is straight up) to the right
            if (m <= 5) tx = -50, ty = 8;
            else if (m <= 35) circle((uint8_t)(128 + (m - 6) * 128 / 30), 56, &tx, &ty);
            else if (m <= 54) tx = 54, ty = -6;
            else tx = ty = 0;
            break;
        case MOOD_DIZZY:  // a spiral inwards, 1.3 turns a second
            circle((uint8_t)(m * 11), 60 - 44 * m / mood_frames[MOOD_DIZZY], &tx, &ty);
            break;
        case MOOD_SLEEPY:
            if (m < WAKE) tx = 0, ty = 28;  // looks down as the lids get heavy
            else if (m < WAKE + 15) tx = -44, ty = -12;  // startled: where? there?
            else if (m < WAKE + 35) tx = 46, ty = -18;
            else tx = ty = 0;
            if (m == WAKE) pupil_to = PUPIL_FOCUS - 7;
            break;
        case MOOD_SUSPICIOUS:  // slides one way, then the other
            tx = (mt / (2 * FPS)) % 2 == 0 ? -52 : 52;
            ty = 6;
            break;
        case MOOD_CRYING:  // looks down; the tears well up at the lower lid
            tx = 0, ty = 24;
            if (m > MOOD_RAMP && mt + FPS < mood_frames[MOOD_CRYING] && rnd8() < 16) {
                for (int i = 0; i < TEARS; i++) {
                    if (tear_c[i]) continue;
                    int c = 5 + rnd8() % 7;
                    tear_c[i] = (uint8_t)(c + 1);
                    tear_y[i] = lid_low[c];
                    tear_v[i] = 0;
                    break;
                }
            }
            break;
        case MOOD_CRAZY:  // twitches: jumps, and the pupil pumps
            if (rnd8() < 90) circle(rnd8(), rnd8() % 64, &tx, &ty);
            if (rnd8() < 50) pupil_to = 10 + rnd8() % 40;
            break;
        default: tx = ty = 0; break;  // at you
    }
    if (++mt >= mood_frames[mood]) {
        if (mood == MOOD_DIZZY || mood == MOOD_CRYING) {
            next_blink = 0;  // shakes it off; blinks the tears away
        } else if (mood == MOOD_SURPRISED || mood == MOOD_CRAZY) {
            next_blink = 0;  // can't believe it; snaps out of it
            again = true;
        } else {
            next_blink = FPS + hh->random() % (2 * FPS);
        }
        mood = MOOD_NONE;
        pupil_to = PUPIL_FOCUS;
        hold = FPS / 2;
        next_mood = 30 * FPS + hh->random() % (30 * FPS);
    }
}

static int ease(int p, int to, bool slow) {
    int d = to - p;
    return p + (slow ? d / 10 + sgn(d) : d * 2 / 3);
}

static void step(void) {
    if (mood == MOOD_NONE) wander();
    else act();
    // a saccade, two thirds of the way each frame; slow for some moods
    bool slow = mood == MOOD_SUSPICIOUS || (mood == MOOD_SLEEPY && mt < WAKE);
    gx = ease(gx, tx, slow);
    gy = ease(gy, ty, slow);
    // the pupil: a quarter of the way each frame, at least one step
    int d = pupil_to - pupil;
    pupil += iabs(d) < 4 ? sgn(d) : d / 4;
    // tears fall on, also when the crying is over
    for (int i = 0; i < TEARS; i++) {
        if (!tear_c[i]) continue;
        tear_y[i] += tear_v[i]++ / 4;
        if (tear_y[i] > 9 * CELL) tear_c[i] = 0;
    }
}

// How far open the lids are for a blink, 0 (shut) ..= 256
static int blink_open(void) {
    if (blink == 0) return 256;
    if (blink <= 3) return 256 - (int)blink * 256 / 3;
    if (blink <= 5) return 0;
    return ((int)blink - 5) * 256 / 5;
}

static int wob(int speed, int n) { return (sin8((uint8_t)((int)mt * speed)) - 128) * n / 128; }

static lids_t lids(void) {
    int a = amount(), m = (int)mt;
    lids_t l = {blink_open(), 0, 0, 0, 0};
    switch (mood) {
        case MOOD_WINK: {
            // closes in 6 frames, the right side first, stays shut for 10,
            // opens in 12; the cheek pushes the lower lid up
            l.open = m <= 5 ? 256 - m * 256 / 6 : m <= 15 ? 0 : m <= 27 ? (m - 16) * 256 / 12 : 256;
            int shut = 256 - l.open;
            l.tilt = 4 * 40 * l.open / 256 * shut / 256;
            l.raise = 100 * shut / 256;
            break;
        }
        case MOOD_ROLL: l.droop = 110 * a / 256; break;
        case MOOD_DIZZY:
            l.droop = (60 + wob(7, 45)) * a / 256;
            l.tilt = wob(5, 36) * a / 256;
            break;
        case MOOD_SLEEPY:
            // the upper lid gets heavy; it nods off and catches itself, then
            // falls asleep, and wakes with a start
            l.droop = m < 90 ? m * 150 / 90 : m < WAKE ? 150 : m < WAKE + 4 ? 150 - (m - WAKE + 1) * 38 : 0;
            if (m >= 90 && m < 105) l.open = 256 - (m - 90) * 200 / 15;
            else if (m >= 105 && m < 120) l.open = 56 + (m - 105) * 200 / 15;
            else if (m >= 120 && m < 195) l.open = 256 - (m - 120) * 256 / 75;
            else if (m >= 195 && m < WAKE) l.open = 0;
            else l.open = 256;
            l.raise = l.droop / 4;
            break;
        case MOOD_SUSPICIOUS:
            l.droop = 130 * a / 256;
            l.raise = 170 * a / 256;
            l.tilt = 14 * a / 256;
            break;
        case MOOD_ANGRY:
            l.droop = 40 * a / 256;
            l.raise = 110 * a / 256;
            l.vee = 90 * a / 256;
            break;
        case MOOD_HYPNO:  // dazed
            l.droop = 70 * a / 256;
            l.raise = 30 * a / 256;
            break;
        case MOOD_CRYING:  // sad: the middle of the upper lid up, the corners down
            l.droop = 50 * a / 256;
            l.raise = 160 * a / 256;
            l.vee = -60 * a / 256;
            break;
        case MOOD_CRAZY:  // the lids out of step
            l.droop = (60 + wob(23, 60)) * a / 256;
            l.raise = (60 + wob(17, 60)) * a / 256;
            l.tilt = wob(13, 40) * a / 256;
            break;
        case MOOD_LAUGHING: l.open = l.open * (256 - a) / 256; break;  // shut, for the ^
        case MOOD_LOVE: l.droop = 50 * a / 256; break;  // dreamy
        default: break;
    }
    return l;
}

// A lash's colour in the mood, given its usual colour, its brightness v and
// how far the upper lid is open
static rgb_t lash(rgb_t usual, const hh_rgb_t* l, uint8_t v, int ou) {
    uint8_t a = (uint8_t)imin(amount(), 255);
    rgb_t white = {v, v, v};
    switch (mood) {
        case MOOD_WINK: return mix(usual, white, mt >= 16 && mt < 28 ? (uint8_t)((28 - mt) * 255 / 12) : 0);
        case MOOD_ROLL: return mix(usual, scaled(usual, 100), a);
        case MOOD_DIZZY: return mix(usual, hsv((uint8_t)(l->arm * 51 + l->rank * 12 + mt * 12), 255, v), a);
        case MOOD_SLEEPY:
            if (mt < WAKE) {
                // a slow blue glow, deeper as it falls asleep
                uint8_t glow = scale(v, 40 + scale(180, sin8((uint8_t)(mt * 2))));
                return mix(usual, hsv(160, 200, glow), (uint8_t)imin(256 - ou, 255));
            }
            return mix(usual, white, (uint8_t)(sat_sub(WAKE + 20, mt) * 255 / 20));
        case MOOD_SUSPICIOUS: return mix(usual, hsv(20, 255, v), a);
        case MOOD_ANGRY: return mix(usual, hsv(0, 255, scale(v, 110 + scale(145, sin8((uint8_t)(mt * 16))))), a);
        case MOOD_SURPRISED: return mix(usual, white, (uint8_t)(255 - imin((int)mt * 255 / 45, 255)));
        case MOOD_HYPNO: {  // purple waves running out along the arms
            uint8_t w = (uint8_t)(mt * 12 - l->rank * 36);
            return mix(usual, hsv((uint8_t)(195 + (sin8(w) >> 3)), 255, scale(v, 70 + scale(185, sin8(w)))), a);
        }
        case MOOD_CRYING: {  // blue; on the lower arms, tears run down to the tips
            rgb_t c = hsv(150, 210, scale(v, 90));
            if ((l->arm == 2 || l->arm == 3) && (mt / 3 + l->arm * 4) % 9 == l->rank)
                c = (rgb_t){scale(v, 140), scale(v, 190), v};
            return mix(usual, c, a);
        }
        case MOOD_CRAZY: return mix(usual, rnd8() < 60 ? (rgb_t){0, 0, 0} : hsv(rnd8(), 255, v), a);
        case MOOD_LAUGHING: {
            uint8_t b = scale(v, 140 + scale(115, sin8((uint8_t)(mt * 22 + l->rank * 24))));
            return mix(usual, hsv((uint8_t)(36 + l->rank * 3), 230, b), a);
        }
        case MOOD_LOVE: return mix(usual, hsv(235, 170, scale(v, (uint8_t)(110 + beat() * 145 / 256))), a);
        default: return usual;
    }
}

// ----------------------------------------------------------------- drawing

static void draw(void) {
    lids_t ld = lids();
    int a = amount();
    int ou = ld.open * (256 - ld.droop) / 256;  // upper lid open
    int ol = ld.open * (256 - ld.raise) / 256;  // lower lid open
    // focused, the pupil breathes: +-1/8 cell, about once a second
    int p = focus == 0 ? pupil + (sin8(phase(t, 230)) - 128) / 64 : pupil;
    int p2 = p * p;
    // wide-eyed: a smaller iris shows more white; hypnotised: a bigger one
    int ir = mood == MOOD_SURPRISED ? IRIS - 22 * a / 256 : mood == MOOD_HYPNO ? IRIS + 26 * a / 256 : IRIS;
    int ir2 = ir * ir;
    // in love: the iris and pupil give way to a heart of this size, beating
    int heart = mood == MOOD_LOVE ? (56 + beat() * 8 / 256) * a / 256 : 0;
    int fade = 256 - (mood == MOOD_LOVE ? a : 0);
    // laughing: the ^ bobs up and down
    int bob = (sin8((uint8_t)(mt * 22)) - 128) / 12;

    // the lids depend on the column only: where they meet, and how far apart
    int up[HH_GRID_W];
    for (int c = 0; c < HH_GRID_W; c++) {
        int x = c * CELL - 8 * CELL;
        int k = 256 - x * x * 256 / LID_R2;
        int meet = CELL + CELL * 3 / 2 * k / 256;
        up[c] = meet - LID_UP * ou / 256 * k / 256 + gy / 3 * ou / 256  // follows the gaze
                + ld.tilt * x / (8 * CELL) + ld.vee * (k - 160) / 96;   // vee: 0 at the sides
        lid_low[c] = meet + LID_DOWN * ol / 256 * k / 256;
    }

    hh->clear();
    for (int r = 0; r < HH_GRID_H; r++) {
        for (int c = 0; c < HH_GRID_W; c++) {
            uint16_t slot = hh->grid[r][c];
            if (slot == HH_NONE) continue;
            int x = c * CELL - 8 * CELL, y = r * CELL - 15 * CELL / 2;
            int vis = imin(clamp(y - up[c] + 8, 0, 16), clamp(lid_low[c] - y + 8, 0, 16));

            // the ball, darker towards the rim
            int sclera = imax(210 - (x * x + y * y) * 130 / (136 * 136), 60);
            int dx = x - gx, dy = y - gy, d2 = dx * dx + dy * dy;
            // iris: a dark ring at its edge, lighter towards the pupil
            int iris = 25 + clamp(60 * (ir2 - d2) / (ir2 - p2), 0, 60);
            int in_iris = clamp(8 - (d2 - ir2) / (2 * ir), 0, 16) * fade / 256;
            int in_pupil = clamp(8 - (d2 - p2) / (2 * p), 0, 16) * fade / 256;
            if (mood == MOOD_HYPNO && in_iris) {
                // a two-armed spiral, turning
                int sp = sin8((uint8_t)((int)hh->isqrt((uint32_t)d2) * 6 + hh->atan8(dx, dy) * 2 - (int)mt * 12)) > 128 ? 230 : 15;
                iris += (sp - iris) * a / 256;
            }
            int v = (sclera * (16 - in_iris) + iris * in_iris) / 16 * (16 - in_pupil) / 16;
            // the catch light, up and left of the pupil's centre
            int hx = dx + 20, hy = dy + 20, h2 = hx * hx + hy * hy;
            if (h2 < 14 * 14) v = imax(v, 255 - h2 * 255 / (14 * 14));
            if (heart > 8) {
                // two lobes and a point, in 1/64 of the heart's half width
                int X = dx * 64 / heart, Y = dy * 64 / heart;
                int l1 = (X - 30) * (X - 30) + (Y + 16) * (Y + 16), l2 = (X + 30) * (X + 30) + (Y + 16) * (Y + 16);
                if (l1 < 34 * 34 || l2 < 34 * 34 || (Y > -16 && iabs(X) * 76 < (60 - Y) * 64)) v = 12;
            }
            v = v * vis / 16;
            // a heavy or shut lid: its edge as a faint line
            if (ou < 128 && mood != MOOD_LAUGHING)
                v = imax(v, imax(16 - iabs(y - imin(up[c], lid_low[c])), 0) * (128 - ou) * 110 / (16 * 128));
            if (mood == MOOD_LAUGHING) v = v * (256 - a) / 256;  // the shut eye gives way to the ^
            if (mood == MOOD_LAUGHING && iabs(x) < 7 * CELL) {
                // the ^: an arch, highest in the middle, thinner at its ends
                int d = iabs(y - (-20 + bob + x * x / 200));
                if (d < 14) v = imax(v, (255 - d * 255 / 14) * imin(7 * CELL - iabs(x), 16) / 16 * a / 256);
            }
            hh->set(slot, (uint8_t)v);
        }
    }

    // tears: a bright drop with a short trail
    for (int i = 0; i < TEARS; i++) {
        if (!tear_c[i]) continue;
        int c = tear_c[i] - 1;
        for (int r = 0; r < HH_GRID_H; r++) {
            int d = tear_y[i] - (r * CELL - 15 * CELL / 2);
            int v = d >= -10 && d <= 10 ? 255 - iabs(d) * 12 : d > 10 && d < 40 ? (40 - d) * 4 : 0;
            uint16_t slot = hh->grid[r][c];
            if (v && slot != HH_NONE) hh->set(slot, (uint8_t)imax(hh->get(slot), v));
        }
    }

    // the brightness level, for a second after a change: a bar on the outline
    if (level_shown) {
        level_shown--;
        int lit = HH_EDGE_COUNT * (hh->step_brightness(0) + 1) / HH_BRIGHTNESS_LEVELS;
        for (int i = 0; i < lit; i++) hh->set(hh->edge[i], 160);
    }
    if (shot_left) shot_left--;

    // lashes: upper ones on the top three arms, lower ones on the bottom two;
    // they fold in with the lids and spring out again
    uint8_t hue = phase(t, 8);
    int lu = ou, ll = ol;
    if (mood == MOOD_LAUGHING) lu = ll = imax(ou, (150 + (sin8((uint8_t)(mt * 22)) - 128) * 3 / 4) * a / 256);
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        bool upper = l->arm == 0 || l->arm == 1 || l->arm == 4;
        int len = upper ? 2 * 256 + 5 * lu : 2 * 256 + 2 * ll;  // ranks, 1/256
        if (mood == MOOD_CRYING && !upper) len = imax(len, 7 * a);  // all the way, for the tears
        uint8_t cover = (uint8_t)clamp(len - l->rank * 256, 0, 255);
        if (!cover) continue;
        uint8_t v = scale((uint8_t)(255 - l->rank * 22), cover);
        // shot: they flash the shooter's team colour
        rgb_t col = shot_left > SHOT_AGAIN - FPS && shot_left & 4
                        ? hsv(team_hue[shot_team], shot_team == 6 ? 0 : 255, v)
                        : lash(hsv((uint8_t)(hue + l->rank * 9 + (upper ? 0 : 40)), 220, v), l, v, ou);
        hh->set_rgb(i, col.r, col.g, col.b);
    }
}

// ------------------------------------------------------------------ the app

static void init(const hh_api_t* api) {
    hh = api;
    hold = FPS;
    pupil = pupil_to = PUPIL_FOCUS;
    next_wide = 20 * FPS;  // the first one soon, to show it
    next_blink = 2 * FPS;
    next_mood = 10 * FPS;  // the first one soon, to show it
    draw();
}

// A mood now, unless it is the one going on
static void now(mood_t m) {
    if (m == mood) return;
    blink = 0;
    focus = 0;
    start(m);
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    if (pressed & HH_BTN_B) now(pick());
    if (pressed & HH_BTN_D) brighter(1);
    if (pressed & HH_BTN_C) brighter(-1);
    // a Samsung TV remote sends its address byte twice (plain NEC: with its
    // inverse, so never a 16-bit address with both bytes the same)
    hh_ir_t ir;
    while (hh->ir_receive(&ir)) {
        uint32_t ms = hh->millis();
        bool fresh = ms - ir_ms >= IR_QUIET_MS || !ir_ms;
        ir_ms = ms ? ms : 1;
        if (!fresh || ir.repeat || ir.address <= 0xFF || (ir.address >> 8) != (ir.address & 0xFF)) continue;
        if (ir.command == SAMSUNG_VOL_UP) brighter(1);
        if (ir.command == SAMSUNG_VOL_DOWN) brighter(-1);
        for (unsigned i = 0; i < sizeof samsung_keys / sizeof samsung_keys[0]; i++)
            if (samsung_keys[i][0] == ir.command) now(samsung_keys[i][1] == MOOD_NONE ? pick() : (mood_t)samsung_keys[i][1]);
    }
    // a banana: shot, it flinches, and the lashes flash the shooter's team
    hh_ir_shot_t s;
    while (hh->ir_receive_shot(&s)) {
        if (s.command == HH_SHOT_SHOOT) {
            now(shot_left ? MOOD_ANGRY : MOOD_SURPRISED);  // a second shot within 5 s: angry
            shot_left = SHOT_AGAIN;
            shot_team = s.team & 7;
        }
        if (s.command == HH_SHOT_HEAL) now(MOOD_LOVE);
        if (s.command == HH_SHOT_TEAM) now(MOOD_WINK);
    }
    acc += elapsed_ms * FPS;
    if (acc < 1000) return;
    for (int n = 0; acc >= 1000; n++) {
        if (n == 4) {  // a long gap (the menu was open): skip ahead
            acc = 0;
            break;
        }
        acc -= 1000;
        step();
        t++;
    }
    draw();
}

HH_APP(init, tick);
