#pragma once
// LED layout tables, generated into layout.c by tools/gen_layout.py from
// webui/board_data.js. Apps get them through hh_api_t (apps/hhapp.h).
#include <stdint.h>
#include "apps/hhapp.h"

extern const uint16_t layout_grid[HH_GRID_H][HH_GRID_W];
extern const uint16_t layout_edge[HH_EDGE_COUNT];
extern const hh_rgb_t layout_rgb[HH_RGB_COUNT];
// Every slot once, in LA03's spatial order (grid, outline, RGB)
extern const uint16_t layout_order[HH_SLOTS];
// Brightness (as a screen shows it) to PWM duty: gamma 2.2
extern const uint8_t layout_gamma[256];
