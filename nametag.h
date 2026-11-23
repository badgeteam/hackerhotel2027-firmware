#pragma once
// The name tag: what the badge shows when nothing else runs. The owner's
// name (persist.h, set in the web interface) on the centre grid, standing
// still if it fits and scrolling if it does not, or "HACKER HOTEL 2027"
// while no name is set; the arms slowly cycle colour and the outline
// sparkles. As in the previous firmware's name tag.
#include <stdint.h>

void Nametag_Start(void);
// Call every main loop pass with the milliseconds since the previous one
void Nametag_Tick(uint32_t elapsed_ms);
