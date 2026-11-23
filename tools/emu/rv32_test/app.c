// The interpreter test as an app: prints "RV32TEST <seed> <result>" for a
// few seeds, then exits.
#include "hhapp.h"

uint32_t work(uint32_t seed);

static void hex(char* out, uint32_t v) {
    for (int i = 7; i >= 0; i--, v >>= 4) out[i] = "0123456789abcdef"[v & 15];
}

static void init(const hh_api_t* api) {
    static const uint32_t seeds[] = {1, 2, 12345, 0xDEADBEEF, 0x80000000u, 0xFFFFFFFFu};
    char line[] = "RV32TEST ........ ........\n";
    for (unsigned i = 0; i < sizeof seeds / sizeof seeds[0]; i++) {
        hex(line + 9, seeds[i]);
        hex(line + 18, work(seeds[i]));
        api->print(line);
    }
    api->exit();
}

static void tick(uint32_t elapsed_ms) { (void)elapsed_ms; }

HH_APP(init, tick);
