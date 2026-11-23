#pragma once
// Text on the centre grid: a 5 x 7 pixel font (from the previous firmware,
// with the missing punctuation filled in). Apps get it through hh_api_t
// (draw_text, text_width), and the name tag uses it.
#include <stdint.h>

#define FONT_W 5        // pixels per glyph, across
#define FONT_H 7        // and down
#define FONT_ADVANCE 6  // a glyph and one blank column

// Width in grid columns of text: FONT_ADVANCE per character, without the
// blank column after the last one; 0 for "".
int Text_Width(const char* text);

// Draws text on the grid (HH_GRID_W x HH_GRID_H) with its top left corner
// at column x, row y; x and y may be negative or past the edge (scrolling):
// what falls outside the grid, or on a corner without an LED, is skipped.
// Lit pixels get `value`; the others are left alone. Lower case shows as
// upper case; characters the font does not have show as a blank.
// Returns Text_Width(text).
int Text_Draw(int x, int y, const char* text, uint8_t value);
