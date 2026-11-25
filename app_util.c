#include "app_util.h"

// A quarter of a sine wave: 128 + 127 sin(2 pi i / 256), i = 0 .. 64
static const uint8_t s_sin_q[65] = {
    128, 131, 134, 137, 140, 144, 147, 150, 153, 156, 159, 162, 165,
    168, 171, 174, 177, 179, 182, 185, 188, 191, 193, 196, 199, 201,
    204, 206, 209, 211, 213, 216, 218, 220, 222, 224, 226, 228, 230,
    232, 234, 235, 237, 239, 240, 241, 243, 244, 245, 246, 248, 249,
    250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255, 255,
};

uint8_t AppUtil_Sin8(uint8_t angle) {
    uint8_t q = angle & 63;
    uint8_t v = angle & 64 ? s_sin_q[64 - q] : s_sin_q[q];
    return angle & 128 ? (uint8_t)(256 - v) : v;
}

// a * b / 255, rounded up a little, as the previous firmware
static uint8_t util_scale8(uint8_t a, uint8_t b) { return (uint8_t)(((unsigned)a * b + 255) >> 8); }

uint32_t AppUtil_Hsv(uint8_t h, uint8_t s, uint8_t v) {
    if (!s) return (uint32_t)v << 16 | (uint32_t)v << 8 | v;
    uint8_t region = h / 43, rem = (uint8_t)((h - region * 43) * 6);
    uint8_t p = util_scale8(v, (uint8_t)(255 - s)), q = util_scale8(v, (uint8_t)(255 - util_scale8(s, rem)));
    uint8_t t = util_scale8(v, (uint8_t)(255 - util_scale8(s, (uint8_t)(255 - rem))));
    // which of v, p, q, t is red, green and blue, by the sixth of the turn
    static const uint8_t pick[6][3] = {{0, 3, 1}, {2, 0, 1}, {1, 0, 3}, {1, 2, 0}, {3, 1, 0}, {0, 1, 2}};
    const uint8_t vals[4] = {v, p, q, t};
    return (uint32_t)vals[pick[region][0]] << 16 | (uint32_t)vals[pick[region][1]] << 8 | vals[pick[region][2]];
}

uint32_t AppUtil_Isqrt(uint32_t n) {
    uint32_t r = 0;
    for (uint32_t b = 1u << 30; b; b >>= 2) {
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
    }
    return r;
}

// atan(t) / (2 pi) * 1024 for t = i / 32, i = 0 .. 32: 0 .. 128 (an eighth
// of a turn, in 1/1024 turn)
static const uint8_t s_atan_e[33] = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 49, 54, 58, 63, 67, 71, 76,
    80, 84, 87, 91, 95, 98, 102, 105, 108, 111, 114, 117, 120, 123, 125, 128,
};

// atan(small / big) in 1/256 turn, 0 .. 32, small <= big, big > 0
static unsigned eighth(unsigned small, unsigned big) {
    while (big >= 1u << 23) small >>= 1, big >>= 1;  // no overflow below
    unsigned f = small * 256 / big;  // t in 1/256
    unsigned i = f >> 3, fr = f & 7;
    unsigned v = i < 32 ? s_atan_e[i] * (8 - fr) + s_atan_e[i + 1] * fr : s_atan_e[32] * 8;
    return (v + 16) / 32;
}

uint8_t AppUtil_Atan8(int x, int y) {
    // clockwise from straight up, with y down: up (0, -1) is 0, right 64
    unsigned ax = (unsigned)(x < 0 ? -x : x), ay = (unsigned)(y < 0 ? -y : y);
    if (!ax && !ay) return 0;
    // the angle from the y axis, 0 .. 64, within the quadrant
    unsigned a = ax <= ay ? eighth(ax, ay) : 64 - eighth(ay, ax);
    if (y > 0) a = 128 - a;  // the lower half
    if (x < 0) a = 256 - a;  // the left half
    return (uint8_t)a;
}

char* AppUtil_Decimal(int32_t n, char text[12]) {
    uint32_t u = n < 0 ? 0u - (uint32_t)n : (uint32_t)n;
    char* p = text + 11;
    *p = 0;
    do *--p = (char)('0' + u % 10);
    while (u /= 10);
    if (n < 0) *--p = '-';
    // left-align: the caller gets text, not a pointer into it
    char* d = text;
    while ((*d++ = *p++)) {}
    return text;
}

uint8_t AppUtil_Linear(const uint8_t gamma[256], uint8_t linear) {
    unsigned lo = 0, hi = 255;  // the first value whose gamma is at least linear
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (gamma[mid] < linear) lo = mid + 1;
        else hi = mid;
    }
    return (uint8_t)(lo && linear - gamma[lo - 1] < gamma[lo] - linear ? lo - 1 : lo);
}
