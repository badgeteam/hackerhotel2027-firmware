#pragma once
// USB power and the battery charger. The XT4054 charger's CHRG output
// (PB22, open-drain) is pulled low while the battery charges; PC3 sees
// VBUS (through R37 to GND), so it is high while USB powers the badge.
#include <stdbool.h>

void Power_Init(void);
bool Power_Usb(void);       // USB power present
bool Power_Charging(void);  // the battery is charging
