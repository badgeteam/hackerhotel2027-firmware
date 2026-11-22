// Arithmetic for the interpreter test (run.sh): the same code is built for
// RISC-V (as an app, run by the emulator's interpreter) and for the PC;
// the two results must be equal.
#include <stdint.h>

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

uint32_t work(uint32_t seed) {
    volatile int32_t s[64];
    volatile uint16_t h16[16];
    volatile uint8_t b8[32];
    uint32_t h = 2166136261u, x = seed | 1;
    for (int i = 0; i < 16; i++) h16[i] = (uint16_t)(i * 4099);
    for (int i = 0; i < 32; i++) b8[i] = (uint8_t)(i * 37);
    for (int i = 0; i < 64; i++) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        s[i] = (int32_t)x;
    }
    for (int i = 0; i < 64; i++) {
        int32_t a = s[i], b = s[(i * 7 + 3) & 63];
        uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
        int32_t small = (int8_t)(a >> 3);
        h = mix(h, ua * ub);
        h = mix(h, (uint32_t)(((int64_t)a * b) >> 32));
        h = mix(h, (uint32_t)(((uint64_t)ua * ub) >> 32));
        h = mix(h, (uint32_t)(((int64_t)a * (int64_t)(uint64_t)ub) >> 32));
        h = mix(h, b ? (uint32_t)(a / (b | 1)) : 0);
        h = mix(h, ub ? ua / ub : 0);
        h = mix(h, (uint32_t)(a % ((b & 0xFFFF) | 1)));
        h = mix(h, ua % ((ub & 0xFFF) | 1));
        h = mix(h, (uint32_t)(a >> (i & 31)));
        h = mix(h, ua >> (i & 31));
        h = mix(h, ua << (i & 31));
        h = mix(h, (uint32_t)(a < b) + 2 * (ua < ub) + 4 * (small < 5) + 8 * (ua < 2000u));
        h = mix(h, (uint32_t)(small * 3 - 7) ^ (ua & 0xF0F0) ^ (ua | 0x101) ^ (uint32_t)(-small));
        h16[i & 15] = (uint16_t)(ua + h16[(i + 1) & 15]);
        b8[i & 31] = (uint8_t)(ub ^ b8[(i + 5) & 31]);
        h = mix(h, (uint32_t)(int16_t)h16[i & 15]);
        h = mix(h, (uint32_t)(int8_t)b8[i & 31]);
        h = mix(h, h16[(i * 3) & 15] + b8[(i * 5) & 31]);
        if ((a ^ b) & 4) h += 0x9E3779B9u;
        else h -= 12345;
        switch (i % 5) {
            case 0: h ^= 0x11; break;
            case 1: h += 0x2222; break;
            case 2: h -= 0x333333; break;
            case 3: h = (h << 7) | (h >> 25); break;
            default: h = ~h; break;
        }
    }
    return h;
}
