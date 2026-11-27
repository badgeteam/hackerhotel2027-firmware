#include "charge.h"
#include <stdbool.h>
#include "ch643_ledpwm.h"
#include "layout.h"

#define ROW_MS 300  // one more row lit

static uint32_t s_ms;

void Charge_Start(void) { s_ms = 0; }

// Upright, the tip at the top: the outline x 4 .. 12, y 3 .. 14, the tip
// x 7 .. 9 on y 2; inside, 8 rows (y 5 .. 12) fill from the bottom. Full
// stays 2 steps, then it starts empty again.
void Charge_Draw(uint32_t elapsed_ms) {
    s_ms += elapsed_ms;
    unsigned lit = s_ms / ROW_MS % 11;  // rows lit, 0 .. 8 (and full for 2 steps more)
    LEDMatrix_Clear();
    for (unsigned y = 2; y <= 14; y++)
        for (unsigned x = 4; x <= 12; x++) {
            unsigned outline = y == 2 ? 0x038 : y == 3 || y == 14 ? 0x1FF : 0x101;  // the row's outline, bit x - 4
            bool fill = x - 6 <= 4 && y - 5 <= 7 && y + lit >= 13;  // x 6 .. 10, y 5 .. 12, the lowest `lit` rows
            LEDMatrix_SetSlot(layout_grid[y][x], outline >> (x - 4) & 1 ? 120 : fill ? 255 : 0);
        }
}
