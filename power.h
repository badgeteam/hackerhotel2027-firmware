#pragma once
// USB power and the battery charger. The XT4054 charger's CHRG output
// (PB22, open-drain) is pulled low while the battery charges; PC3 sees
// VBUS (through R37 to GND), so it is high while USB powers the badge.
#include <stdbool.h>
#include <stdint.h>

void Power_Init(void);
bool Power_Usb(void);       // USB power present
// Call every main loop pass. The charging state changes only once CHRG has
// kept its new level for 500 ms (and at least 25 passes): with no battery
// it toggles, and a line that toggles faster keeps the last steady state.
// Returns true on the pass where charging starts (also at power-on with USB in).
bool Power_Tick(uint32_t elapsed_ms);
bool Power_Charging(void);  // the battery charges (debounced)
