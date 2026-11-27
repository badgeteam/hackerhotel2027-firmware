// The looks of the name tag app (see styles.h): RAINBOW, FIRE, MATRIX,
// WAVE, STARFIELD, GLITCH, TYPEWRITER. Each style is one case in grid_px
// (a grid cell), edge_px (an outline LED) and style_arm (an arm LED's base
// colour), plus one step() per frame for its state. The grid and the
// outline are mono, so "rainbow" and "fire" are brightness patterns there;
// colour lives on the arms. The text stays bright (200 and up, RAINBOW 150)
// and the background in the text rows dim (110 at most), so names read.
// Effects are fixed to the screen: a scrolling name moves through them.
#include "styles.h"

#define S(a) h->sin8((uint8_t)(a))
#define RND() h->random()
#define W HH_GRID_W
#define H HH_GRID_H
#define EDGES HH_EDGE_COUNT
#define STARS 20

// If the app outgrows its room in the store: STYLE_CUT 1 drops STARFIELD's
// flash and arm streak when a star leaves the grid (about 130 bytes), 2 also
// WAVE's reflection (about 120). STYLE_COUNT drops whole styles from the end.
#ifndef STYLE_CUT
#define STYLE_CUT 0
#endif

// All style state in one place, so style_start clears it at once. The
// styles run one at a time, so they share their RAM.
static struct {
    uint32_t t0;              // when the style started
    uint8_t glow[EDGES + 2];  // outline glow, shared, LED i at i + 1; it fades every frame
    union {
        uint8_t heat[H + 2][W];  // FIRE; rows 16 and 17 are hidden embers
        struct {
            int16_t drop[W];  // the head's row in 1/16 rows
            uint8_t spd[W];   // 1/16 rows per frame
        } mx;  // MATRIX
        struct {
            uint32_t streak[5];          // when a star left over each arm
            uint8_t a[STARS], r[STARS];  // angle (1/256 turn), distance (1/16 cell)
            uint8_t sky[H][W];
        } sf;  // STARFIELD
        struct {
            uint32_t roll;            // t / 40 of the last reroll
            int8_t shift[4], mx, my;  // per band of 2 rows; the whole text (1 burst in 4)
            uint8_t inv, burst, swap, arm, seg[4];  // seg: 2 x start, length
        } gl;  // GLITCH
        struct {
            uint32_t strike_t;
            int c;                    // columns typed; -999 while blank
            uint8_t phase, key, arm;  // phase 0 typing, 1 hold, 2 blank
        } tw;  // TYPEWRITER
    };
} z;

void style_start(uint32_t t) {
    __builtin_memset(&z, 0, sizeof z);
    z.t0 = t;
}

#if STYLE_COUNT > 4
static void star(int dx, int dy, int r, int v) {
    int x = 8 + dx * r / 2032, y = 7 + dy * r / 2032;
    if ((unsigned)x < W && (unsigned)y < H && z.sf.sky[y][x] < v) z.sf.sky[y][x] = (uint8_t)v;
}
#endif

static void step(const hh_api_t* h, uint8_t st, uint32_t t) {
    (void)h, (void)t;
    switch (st) {
#if STYLE_COUNT > 1
        case 1:  // FIRE: hot embers below, heat rises and cools, the text burns; above it, tongues
            for (uint8_t* p = z.heat[H]; p < z.heat[H] + 2 * W; p++) *p = (uint8_t)(RND() | 128);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    uint32_t r = RND();
                    int c = y < 4 ? 15 + (r & 63) : (y < 12 ? 8 : 6) + r % 30;
                    int v = z.heat[y + 1][(x + 16 + (r >> 8) % 3) % W] - c;  // row y + 1 is still last frame's
                    z.heat[y][x] = M(x, y) ? (r >> 16 | 160) & 255 : v < 0 ? 0 : (uint8_t)v;
                }
            break;
#endif
#if STYLE_COUNT > 2
        case 2:  // MATRIX: one drop per column
            for (int x = 0; x < W; x++) {
                if (!z.mx.spd[x] || z.mx.drop[x] > 21 * 16) {  // spd 0: not started
                    uint32_t r = RND();
                    z.mx.drop[x] = (int16_t)-(int)(r % 160), z.mx.spd[x] = 3 + (r >> 8) % 7;
                }
                z.mx.drop[x] += z.mx.spd[x];
            }
            break;
#endif
#if STYLE_COUNT > 4
        case 4: {  // STARFIELD: lightspeed for the first 0.7 s
            int n = t - z.t0 < 700 ? 3 : 1;
            __builtin_memset(z.sf.sky, 0, sizeof z.sf.sky);
            for (int k = 0; k < STARS; k++) {
                int a = z.sf.a[k], r = z.sf.r[k];
                if (!r || r > 150) {  // r 0: not started
#if STYLE_CUT < 1
                    if (r) {  // it left the grid: a flash on the outline, a streak up its arm
                        uint8_t* g = z.glow + (a * EDGES >> 8);
                        int q = a * 5 + 50;  // arm m is at 51.2 m
                        g[0] = g[2] = 110, g[1] = 230;
                        if ((q & 255) <= 100) z.sf.streak[(q >> 8) % 5] = t;  // within 10 of an arm
                    }
#endif
                    r = RND(), a = r >> 8 & 255, r %= 40;
                }
                r += (2 + r / 10) * n;
                z.sf.a[k] = (uint8_t)a, z.sf.r[k] = (uint8_t)r;
                int dx = S(a) - 128, dy = 128 - S(a + 64), v = r < 145 ? 110 + r : 255;  // bright enough through the gamma
                star(dx, dy, r, v);
                if (r > 60) star(dx, dy, r * 3 / 4, 75);  // its tail
            }
            break;
        }
#endif
#if STYLE_COUNT > 5
        case 5: {  // GLITCH: one burst of 120..250 ms somewhere in every 0.7 s, by a hash of the period
            uint32_t c = t / 700 * 2654435761u;
            z.gl.burst = t % 700 - (c >> 24) < 120 + (c >> 12 & 127);
            if (z.gl.burst && t / 40 != z.gl.roll) {  // reroll every 40 ms
                z.gl.roll = t / 40;
                for (int b = 0; b < 4; b++) z.gl.shift[b] = (int8_t)(RND() % 7) - 3;
                uint32_t r = RND();
                z.gl.inv = r & 15;
                z.gl.mx = z.gl.my = 0;
                if (!(c >> 8 & 3)) z.gl.mx = (int8_t)(r >> 4 & 7) - 4, z.gl.my = (int8_t)((r >> 8) % 3) - 1;  // 1 in 4
                for (int k = 0; k < 2; k++) r = RND(), z.gl.seg[k] = r % EDGES, z.gl.seg[k + 2] = 4 + (r >> 8 & 7);
                z.gl.swap ^= 1;
            }
            uint32_t r = RND();
            z.gl.arm = r & 7 ? 5 : (r >> 3) % 5;  // 5: none
            break;
        }
#endif
#if STYLE_COUNT > 6
        case 6: {  // TYPEWRITER: 50 columns a second, a short text holds 3 s, blanks, retypes
            unsigned w = nt_text_w, type = w * 20, ms = nt_text_ms;
            int c = w, ph = 1;
            if (w <= W) ms %= type + 3400;
            if (ms < type)
                c = ms / 20, ph = 0;
            else if (w <= W && ms >= type + 3000)
                c = -999, ph = 2;
            if (ph)
                z.tw.key = 0;
            else if (c / 6 + 1 != z.tw.key) {  // a keystroke
                uint32_t r = RND();
                z.tw.key = c / 6 + 1;
                z.tw.strike_t = t;
                z.tw.arm = r % 5;
                for (int k = 0; k < 3; k++) z.glow[(r >> 3) % EDGES + k] = 120;
            }
            if (ph == 1 && !z.tw.phase) __builtin_memset(z.glow, 255, sizeof z.glow);  // ding
            z.tw.phase = (uint8_t)ph, z.tw.c = c;
            break;
        }
#endif
        default:
            break;
    }
}

#if STYLE_COUNT > 3
// WAVE: every letter bobs as a whole, so it reads, by a whole row (a
// pixel split over two rows loses its strokes to the gamma); a wave runs
// through the letters
static int wave(const hh_api_t* h, int x, int y, uint32_t t) {
    int k = (x - nt_text_x) / HH_FONT_ADVANCE;
    return M(x, y - ((S(k * 48 - t / 4) - 64) >> 7)) ? 255 : 0;
}
#endif

static int grid_px(const hh_api_t* h, uint8_t st, int x, int y, uint32_t t) {
    int m = M(x, y), mid = y >= 4 && y < 12;
    (void)mid;
    switch (st) {
        default:  // RAINBOW: a diagonal sheen over the text, nothing else
            return m ? 150 + (S(x * 20 + y * 10 - t / 8) * 105 >> 8) : 0;
#if STYLE_COUNT > 1
        case 1:
            if (m) return 225 + RND() % 31;
            return z.heat[y][x] * (mid ? 90 : 150) >> 8;
#endif
#if STYLE_COUNT > 2
        case 2: {  // a new text decodes column by column in 0.1 .. 0.5 s, once
            static const uint8_t TRAIL[6] = {120, 70, 45, 28, 16, 8};
            int d = (z.mx.drop[x] >> 4) - y;
            if (m) return nt_text_on < 100u + (unsigned)((x - nt_text_x) * 7 % 17) * 25 ? (RND() & 1) * 120 : d ? 210 : 255;
            int v = (unsigned)d < 6 ? TRAIL[d] : 0;
            return mid ? v * 3 >> 2 : v;
        }
#endif
#if STYLE_COUNT > 3
        case 3: {
            int v = wave(h, x, y, t);
#if STYLE_CUT < 2
            if (y >= 12) {  // a rippling reflection
                int r = wave(h, x + (S(y * 80 + t / 3) > 128), 23 - y, t) * 45 >> 8;
                if (r > v) v = r;
            }
#endif
            return v;
        }
#endif
#if STYLE_COUNT > 4
        case 4:
            return m ? 240 : z.sf.sky[y][x] >> mid;  // half in the text's rows
#endif
#if STYLE_COUNT > 5
        case 5:
            if (!z.gl.burst) {
                if ((unsigned)y == t / 45 % 24) return m ? 255 : 100;  // the scanline
                return m && RND() % 40 ? 210 : 0;                    // now and then a dead pixel
            }
            if (!(RND() % 54)) return 255;  // about 5 sparks a frame
            if (mid) {
                int b = (y - 4) >> 1;
                m = M(x - z.gl.shift[b] - z.gl.mx, y - z.gl.my);
                if (z.gl.inv >> b & 1) return m ? 0 : 90;
            }
            return m ? 255 : 0;
#endif
        case STYLE_BATTERY: {  // the charging screen (nametag.c): a battery, upright; t - z.t0: ms since it began
            unsigned u = x - 4, w = y - 2;  // from its top left corner
            if (u > 8 || w > 12 || (!w && u - 3 > 2)) return 0;  // off it: x 4 .. 12, y 2 .. 14, but y 2 only x 7 .. 9
            if (!w || u - 1 > 6 || w - 2 > 9) return 120;  // the tip (y 2), the outline (x 4 and 12, y 3 and 14)
            // inside, x 6 .. 10 of y 5 .. 12 fill from the bottom, a row more every 300 ms: 0 .. 8 rows, then full for
            // 2 steps more
            return u - 2 <= 4 && w - 3 <= 7 && w + (t - z.t0) / 300 % 11 >= 11 ? 255 : 0;
        }
#if STYLE_COUNT > 6
        case 6: {
            int dx = x - nt_text_x, c = z.tw.c;
            if (!z.tw.phase && y >= 4 && y <= 10 && dx == c) return 120;                   // the cursor, typing
            if (z.tw.phase == 1 && (t / 250 & 1) && y == 12 && (unsigned)(c - 1 - dx) < 5) return 120;  // then under the last letter, blinking
            if (!m || dx >= c) return 0;
            return z.tw.phase ? 230 : dx >= c - 6 ? 255 : 190;  // typing: the newest character brightest
        }
#endif
    }
}

static int edge_px(const hh_api_t* h, uint8_t st, int i, uint32_t t) {
    (void)h;
    switch (st) {
        case 0: {  // two comets, half a lap apart
            int d = ((int)(t / 28 % EDGES) + EDGES - i) % EDGES;
            if (d >= 35) d -= 35;
            return d < 12 ? 230 - d * 19 : 10;
        }
#if STYLE_COUNT > 1
        case 1: {  // flames along the lower half, and embers
            uint32_t r = RND();
            if ((r & 255) < 5) z.glow[i + 1] = 120;
            int down = 255 - S(h->polar[h->edge[i]][1] + 64);
            return down > 90 ? (down - 90) * (64 + (int)(r >> 8 & 127)) >> 8 : 0;
        }
#endif
#if STYLE_COUNT > 2
        case 2: {
            uint32_t r = RND();
            if ((r & 255) < 14) z.glow[i + 1] = 90 + (r >> 8) % 100;
            return 0;
        }
#endif
#if STYLE_COUNT > 3
        case 3:
            return S(i * 7 - t / 6) * 120 >> 8;
#endif
#if STYLE_COUNT > 5
        case 5:
            if (z.gl.burst)
                for (int k = 0; k < 2; k++)
                    if ((unsigned)(i - z.gl.seg[k]) < z.gl.seg[k + 2]) return 200;
            return 0;
#endif
        default:  // the glow alone
            return 0;
    }
}

void style_draw(uint8_t st, uint32_t t) {
    const hh_api_t* h = hh;
    h->clear();
    step(h, st, t);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) h->set(h->grid[y][x], (uint8_t)grid_px(h, st, x, y, t));
    for (int i = 0; i < EDGES; i++) {
        int v = edge_px(h, st, i, t), g = z.glow[i + 1];
        h->set(h->edge[i], (uint8_t)(v > g ? v : g));
        z.glow[i + 1] = (uint8_t)(g - g / 6 - (g > 0));
    }
}

uint32_t style_arm(uint8_t st, int i, uint32_t t) {
    const hh_api_t* h = hh;
    int arm = h->rgb[i].arm, rank = h->rgb[i].rank, hu, sa = 255, va;
    uint32_t r = RND();
    (void)r;
    switch (st) {
        default:
            hu = arm * 51 + rank * 6 + t / 16, va = 110 + (S(rank * 36 - t / 6) * 145 >> 8);
            break;
#if STYLE_COUNT > 1
        case 1:
            hu = 28 - rank * 4 + r % 6, va = (255 - rank * 22) * (150 + (int)((r >> 8) % 106)) >> 8;
            break;
#endif
#if STYLE_COUNT > 2
        case 2: {  // rain falling inward
            int d = rank - 6 + (int)((t / 70 + arm * 3) % 10);
            hu = 85, va = 90;
            if (!d)
                sa = 120, va = 255;
            else if ((unsigned)(d - 1) < 3)
                va = 160 >> (d - 1);
            break;
        }
#endif
#if STYLE_COUNT > 3
        case 3:
            hu = 119 + (S(arm * 51 + t / 24) * 43 >> 8), sa = 230, va = 50 + (S(rank * 40 - t / 6) * 205 >> 8);
            break;
#endif
#if STYLE_COUNT > 4
        case 4: {  // a twinkling night blue; a star leaving over the arm streaks out to its tip
            hu = 165, va = r & 15 ? 110 : 230;
#if STYLE_CUT < 1
            uint32_t age = t - z.sf.streak[arm];
            int d = (int)(age / 25) - rank;
            if (age < 250 && (unsigned)d < 3) hu = 160, sa = d ? 150 : 40, va = d ? 240 >> d : 255;
#else
            if (!(r % 32)) sa = 0, va = 255;  // a twinkle
#endif
            break;
        }
#endif
#if STYLE_COUNT > 5
        case 5:
            if (z.gl.burst) return (rank ^ z.gl.swap) & 1 ? 0x00DCFF : 0xFF0028;
            hu = 200 + t / 64, va = 120;  // a slow drift between bursts
            if (arm == z.gl.arm) hu = 128, va = 255;
            break;
#endif
#if STYLE_COUNT > 6
        case 6: {  // a struck key flashes an arm
            uint32_t age = t - z.tw.strike_t;
            hu = 30, sa = 170, va = 130;
            if (arm == z.tw.arm && age < 150) sa = 80, va = 255 - age * 4 / 3;
            break;
        }
#endif
    }
    return h->hsv((uint8_t)hu, (uint8_t)sa, (uint8_t)va);
}
