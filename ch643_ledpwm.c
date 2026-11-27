#include "ch643_ledpwm.h"
#include <string.h>
#include "ch32fun.h"
#include "ch643_ledpwm_registers.h"
#include "layout.h"

static const uint8_t s_com_sel_for_row[LED_MATRIX_ROWS] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static volatile uint8_t s_brightness[LED_MATRIX_ROWS][LED_MATRIX_COLS];  // shown: the row scan reads this
static uint8_t s_draw[LED_MATRIX_ROWS][LED_MATRIX_COLS];                 // drawn: LEDMatrix_Set* write this
static uint32_t s_row_dma_buf[LED_MATRIX_COLS / 3];
static volatile uint8_t s_next_row;
static volatile uint8_t s_last_loaded_row;
static volatile uint8_t s_global_brightness = 255;
static volatile uint8_t s_luminance = 0xFF;  // the global brightness, as the luminance PWM's duty

// A pixel value to PWM duty: gamma (the values are brightness as a screen
// shows it, the LEDs are linear). The global brightness is not in here: it
// goes to the luminance PWM (s_luminance), which dims the whole word, so
// the duty keeps its 8 bits and dim values do not round down to 0 (#16).
static inline uint8_t scale(uint8_t value) { return layout_gamma[value]; }

static void gpio_set_mode(GPIO_TypeDef* port, unsigned pin, GPIO_CFGLR_PIN_MODE_Typedef mode) {
    __IO uint32_t* cfg;
    unsigned shift;
    if (pin < 8) {
        cfg = &port->CFGLR;
        shift = pin;
    } else if (pin < 16) {
        cfg = &port->CFGHR;
        shift = pin - 8;
    } else {
        cfg = &port->CFGXR;
        shift = pin - 16;
    }
    *cfg = (*cfg & ~(0xFu << (4 * shift))) | ((uint32_t)mode << (4 * shift));
}

static void led_matrix_gpio_init(void) {
    for (unsigned pin = 0; pin < 24; pin++) {
        gpio_set_mode(GPIOA, pin, GPIO_CFGLR_OUT_10Mhz_PP);
    }
    GPIOA->OUTDR &= ~0xFFFFFFu;
    for (unsigned pin = 0; pin < LED_MATRIX_ROWS; pin++) {
        gpio_set_mode(GPIOB, pin, GPIO_CFGLR_OUT_10Mhz_PP);
    }
    GPIOB->OUTDR |= 0xFFFFu;
}

// Drive both sides of the matrix low: 0V across every LED and no floating
// nodes (a Hi-Z matrix picks up noise and leaks through the LEDPWM pin logic)
static void led_matrix_gpio_deinit(void) {
    GPIOA->OUTDR &= ~0xFFFFFFu;
    GPIOB->OUTDR &= ~0xFFFFu;
    for (unsigned pin = 0; pin < 24; pin++) {
        gpio_set_mode(GPIOA, pin, GPIO_CFGLR_OUT_2Mhz_PP);
    }
    for (unsigned pin = 0; pin < LED_MATRIX_ROWS; pin++) {
        gpio_set_mode(GPIOB, pin, GPIO_CFGLR_OUT_2Mhz_PP);
    }
}

// Frame ISR (fires once per row, during the inter-row blanking window).
// The loop invariants out of the loop, which the Makefile turns off for
// the rest of the firmware: without that the row takes 48 instructions more
__attribute__((optimize("move-loop-invariants"))) static void ledpwm_load_row(unsigned row) {
    for (unsigned g = 0; g < LED_MATRIX_COLS / 3; g++) {
        uint8_t red = scale(s_brightness[row][g]);
        uint8_t green = scale(s_brightness[row][g + LED_MATRIX_COLS / 3]);
        uint8_t blue = scale(s_brightness[row][g + 2 * (LED_MATRIX_COLS / 3)]);
        uint32_t word = s_luminance;    // luminance: the global brightness
        word |= (uint32_t)red << 8;     // red byte
        word |= (uint32_t)green << 16;  // green byte
        word |= (uint32_t)blue << 24;   // blue byte
        s_row_dma_buf[g] = word;
    }
    LEDPWM->DMA = (uint16_t)(uint32_t)s_row_dma_buf;
    LEDPWM->SW.SWITCH = LEDPWM_SWITCH(s_com_sel_for_row[row], 1, LED_MATRIX_COLS / 3);

    s_last_loaded_row = (uint8_t)row;
    s_next_row = (row + 1 < LED_MATRIX_ROWS) ? row + 1 : 0;
}

void LEDPWM_IRQHandler(void) __attribute__((interrupt));
void LEDPWM_IRQHandler(void) {
    ledpwm_load_row(s_next_row);
}

// Public functions

void LEDMatrix_Init(void) {
    led_matrix_gpio_init();
    LEDPWM->CE.COM_E = LEDPWM_COM_E(0xFF, 0xFF, 0x00, 0x00);
    LEDPWM->PP.PWM_PIN = LEDPWM_PWM_PIN(
        0xFF, 0x00, LEDPWM_MATRIX_MODE_PA24 | LEDPWM_PWM_RED_EN | LEDPWM_PWM_GREEN_EN | LEDPWM_PWM_BLUE_EN);
    LEDPWM->CR.CTRL = LEDPWM_CTRL(LEDPWM_LED_ENABLE | LEDPWM_IE_INHIBIT | LEDPWM_PWM_OE_CLR, 0,
                                  LEDPWM_CLK_FREQ(1) | LEDPWM_INTEN_CYC(2), LEDPWM_INHIBIT(15) | LEDPWM_PWM_REPEAT(0));
    s_next_row = 0;
    ledpwm_load_row(0);
    NVIC_EnableIRQ(LEDPWM_IRQn);
}

void LEDMatrix_Deinit(void) {
    NVIC_DisableIRQ(LEDPWM_IRQn);
    LEDPWM->CR.CTRL = LEDPWM_CTRL(LEDPWM_IE_INHIBIT | LEDPWM_PWM_OE_CLR, 0, LEDPWM_CLK_FREQ(1) | LEDPWM_INTEN_CYC(2),
                                  LEDPWM_INHIBIT(15) | LEDPWM_PWM_REPEAT(0));
    led_matrix_gpio_deinit();
    // Release the pins from the LED driver (PWM0_EN is hardwired to 1)
    LEDPWM->SW.SWITCH = 0;
    LEDPWM->CE.COM_E = 0;
    LEDPWM->PP.PWM_PIN = 0;
}

void LEDMatrix_SetPixel(uint8_t row, uint8_t col, uint8_t brightness) {
    if (row < LED_MATRIX_ROWS && col < LED_MATRIX_COLS) s_draw[row][col] = brightness;
}

void LEDMatrix_SetSlot(unsigned slot, uint8_t brightness) {
    if (slot < LED_MATRIX_SLOTS) ((uint8_t*)s_draw)[slot] = brightness;
}

uint8_t LEDMatrix_GetSlot(unsigned slot) { return slot < LED_MATRIX_SLOTS ? ((uint8_t*)s_draw)[slot] : 0; }

void LEDMatrix_SetFrame(const uint8_t* frame) { memcpy(s_draw, frame, LED_MATRIX_SLOTS); }

void LEDMatrix_GetFrame(uint8_t* frame) { memcpy(frame, s_draw, LED_MATRIX_SLOTS); }

void LEDMatrix_Clear(void) { memset(s_draw, 0, sizeof s_draw); }

// Each LED goes from its old value straight to its new one; a row the scan
// loads during the copy mixes the two frames, but is never dark
void LEDMatrix_Show(void) {
    for (unsigned i = 0; i < LED_MATRIX_SLOTS; i++) ((volatile uint8_t*)s_brightness)[i] = ((const uint8_t*)s_draw)[i];
}

// The luminance PWM has 6 bits here (LEDPWM_INTEN_CYC(2)): 64 steps, and
// 64 or more is full. Brightness b gives b / 4 steps (the levels 8 .. 128
// give 2 .. 32), at least 1 unless b is 0.
void LEDMatrix_SetBrightness(uint8_t brightness) {
    s_global_brightness = brightness;
    s_luminance = brightness >= 252 ? 0xFF : brightness ? (uint8_t)(brightness >> 2 ? brightness >> 2 : 1) : 0;
}

uint8_t LEDMatrix_GetBrightness(void) {
    return s_global_brightness;
}
