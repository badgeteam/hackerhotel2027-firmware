#pragma once
// The looks of the name tag app (styles.c). nametag.c decides which text
// shows and where and draws it into nt_mask; a style shades the grid and
// the outline from it and gives each arm LED its base colour.
#include "hhapp.h"

#ifndef STYLE_COUNT
#define STYLE_COUNT 7  // B / D cycle 0 .. STYLE_COUNT-1; trim from the end to fit the store
#endif
#define TEXT_Y 4       // the text's top row, as the firmware's name tag

extern const hh_api_t* hh;            // nametag.c, set in init()
extern uint32_t nt_mask[HH_GRID_H];   // this frame's text: bit x of row y
extern int nt_text_x, nt_text_w;      // first copy's left column; width in columns
extern uint32_t nt_text_ms;           // ms since this text became visible or last wrapped
extern uint32_t nt_text_on;           // ms since this text became visible

static inline int M(int x, int y) { return (unsigned)x < HH_GRID_W && (unsigned)y < HH_GRID_H && (nt_mask[y] >> x & 1); }

void style_start(uint32_t t);                      // new style or app start: reset all style state, t0 = t
void style_draw(uint8_t style, uint32_t t);        // once per frame: clear, step, draw the grid (all 17x16) and the outline (71)
uint32_t style_arm(uint8_t style, int i, uint32_t t);  // RGB LED i's base colour, 0xRRGGBB
