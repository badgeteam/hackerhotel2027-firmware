#pragma once
// Text on the centre grid: a 5 x 8 pixel font, all of printable ASCII
// (the previous firmware's capitals, digits and signs, completed with the
// missing punctuation and lower case). Apps get it through hh_api_t
// (draw_text, text_width), and the name tag uses it.
#include <stdint.h>

#define FONT_W 5        // pixels per glyph, across
#define FONT_H 8        // and down: 7 rows, and 1 for the descenders of g j p q y
#define FONT_ADVANCE 6  // a glyph and one blank column

// Width in grid columns of text: FONT_ADVANCE per character, without the
// blank column after the last one; 0 for "".
int Text_Width(const char* text);
int Text_WidthN(unsigned len);  // of `len` characters

// Draws text on the grid (HH_GRID_W x HH_GRID_H) with its top left corner
// at column x, row y; x and y may be negative or past the edge (scrolling):
// what falls outside the grid, or on a corner without an LED, is skipped.
// Lit pixels get `value`; the others are left alone. Characters outside
// printable ASCII (UTF-8 bytes, for one) show as a blank.
// Returns Text_Width(text).
int Text_Draw(int x, int y, const char* text, uint8_t value);
// The same for `len` characters, which need no NUL at the end (a text item
// in the store)
int Text_DrawN(int x, int y, const char* text, unsigned len, uint8_t value);

// A label at row y: centred if it fits the grid, else scrolling to the
// left, t_ms after it was first shown, with a copy following every width +
// 8 columns
void Text_DrawLabel(const char* text, int y, uint32_t t_ms, uint8_t value);
