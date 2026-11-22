// The interpreter test on the PC: the expected lines.
#include <stdint.h>
#include <stdio.h>

uint32_t work(uint32_t seed);

int main(void) {
    static const uint32_t seeds[] = {1, 2, 12345, 0xDEADBEEF, 0x80000000u, 0xFFFFFFFFu};
    for (unsigned i = 0; i < sizeof seeds / sizeof seeds[0]; i++) printf("RV32TEST %08x %08x\n", seeds[i], work(seeds[i]));
    return 0;
}
