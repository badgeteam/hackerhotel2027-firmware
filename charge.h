#pragma once
// The charging screen (issue #9): a battery on the centre grid that fills
// up row by row, over and over, while the battery charges. The badge
// cannot tell how full the battery is, so the fill says only "charging".
//
// ledmatrix.c starts it when charging starts (power.h), only outside apps,
// with the menu closed and no factory test or greeting. Any button ends
// it (that press does nothing else), and so do the end of charging and a
// host command that changes what runs. It comes back only when charging
// stops and starts again.
#include <stdint.h>

void Charge_Start(void);
void Charge_Draw(uint32_t elapsed_ms);
