#pragma once
#include <stdint.h>

#define LED_MATRIX_COLS 24  // 8 groups x R/G/B, PA[7:0]=R PA[15:8]=G PA[23:16]=B
#define LED_MATRIX_ROWS 16  // COM0-15, PB0-PB15
#define LED_MATRIX_SLOTS (LED_MATRIX_ROWS * LED_MATRIX_COLS)  // slot = row * LED_MATRIX_COLS + col

void LEDMatrix_Init(void);
void LEDMatrix_Deinit(void);

// Drawing goes into a buffer of its own; LEDMatrix_Show() puts it on the
// LEDs. The row scan reads the shown buffer only, so it never shows a frame
// half drawn (cleared, and only partly drawn again: that flickered).
void LEDMatrix_SetPixel(uint8_t row, uint8_t col, uint8_t brightness);
// The same by slot; a slot past the LEDs (HH_NONE) is skipped, and reads 0.
// GetSlot reads what is drawn.
void LEDMatrix_SetSlot(unsigned slot, uint8_t brightness);
uint8_t LEDMatrix_GetSlot(unsigned slot);
// The whole drawing at once: LED_MATRIX_SLOTS values, by slot
void LEDMatrix_SetFrame(const uint8_t* frame);
void LEDMatrix_GetFrame(uint8_t* frame);
void LEDMatrix_Clear(void);
// Call when a frame is drawn (the main loop does, once a pass)
void LEDMatrix_Show(void);

// Global brightness, applied on top of every pixel value (255 = unscaled)
void LEDMatrix_SetBrightness(uint8_t brightness);
uint8_t LEDMatrix_GetBrightness(void);
