#pragma once
#include <stdint.h>

void buttons_init(void);
uint8_t buttons_poll(void);
// Bitmask of the buttons down now, debounced (same bits as buttons_poll())
uint8_t buttons_down(void);
// Bitmask of the buttons down now, straight from the pins (no debouncing)
uint8_t buttons_raw(void);
void buttons_ignore_held(void);
