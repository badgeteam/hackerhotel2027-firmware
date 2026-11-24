#pragma once
#include <stdint.h>

#define LED_MATRIX_COLS 24  // 8 groups x R/G/B, PA[7:0]=R PA[15:8]=G PA[23:16]=B
#define LED_MATRIX_ROWS 16  // COM0-15, PB0-PB15

void LEDMatrix_Init(void);
void LEDMatrix_Deinit(void);

// Drawing goes into a buffer of its own; LEDMatrix_Show() puts it on the
// LEDs. The row scan reads the shown buffer only, so it never shows a frame
// half drawn (cleared, and only partly drawn again: that flickered).
void LEDMatrix_SetPixel(uint8_t row, uint8_t col, uint8_t brightness);
uint8_t LEDMatrix_GetPixel(uint8_t row, uint8_t col);  // as drawn
void LEDMatrix_Clear(void);
// Call when a frame is drawn (the main loop does, once a pass)
void LEDMatrix_Show(void);

// Global brightness, applied on top of every pixel value (255 = unscaled)
void LEDMatrix_SetBrightness(uint8_t brightness);
uint8_t LEDMatrix_GetBrightness(void);
