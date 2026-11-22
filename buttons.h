#pragma once
#include <stdint.h>

void buttons_init(void);
uint8_t buttons_poll(void);
void buttons_ignore_held(void);
