#include "ch643_ledpwm.h"
#include "ch32fun.h"
#include "ch643_ledpwm_registers.h"
#include "layout.h"

static const uint8_t s_com_sel_for_row[LED_MATRIX_ROWS] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static volatile uint8_t s_brightness[LED_MATRIX_ROWS][LED_MATRIX_COLS];  // shown: the row scan reads this
static uint8_t s_draw[LED_MATRIX_ROWS][LED_MATRIX_COLS];                 // drawn: LEDMatrix_SetPixel writes this
static uint32_t s_row_dma_buf[LED_MATRIX_COLS / 3];
static volatile uint8_t s_next_row;
static volatile uint8_t s_last_loaded_row;
static volatile uint8_t s_global_brightness = 255;

// Applies the global brightness: 255 leaves the value unchanged
// A pixel value to PWM duty: gamma first (the values are brightness as a
// screen shows it, the LEDs are linear), then the global brightness
static inline uint8_t scale(uint8_t value) {
    return (uint8_t)(((unsigned)layout_gamma[value] * (s_global_brightness + 1u)) >> 8);
}

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

// Frame ISR (fires once per row, during the inter-row blanking window)
static void ledpwm_load_row(unsigned row) {
    for (unsigned g = 0; g < LED_MATRIX_COLS / 3; g++) {
        uint8_t red = scale(s_brightness[row][g]);
        uint8_t green = scale(s_brightness[row][g + LED_MATRIX_COLS / 3]);
        uint8_t blue = scale(s_brightness[row][g + 2 * (LED_MATRIX_COLS / 3)]);
        uint32_t word = 0xFFu;          // luminance = full
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

uint8_t LEDMatrix_GetPixel(uint8_t row, uint8_t col) {
    return row < LED_MATRIX_ROWS && col < LED_MATRIX_COLS ? s_draw[row][col] : 0;
}

void LEDMatrix_Clear(void) {
    for (uint8_t row = 0; row < LED_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < LED_MATRIX_COLS; col++) {
            s_draw[row][col] = 0;
        }
    }
}

// Each LED goes from its old value straight to its new one; a row the scan
// loads during the copy mixes the two frames, but is never dark
void LEDMatrix_Show(void) {
    for (uint8_t row = 0; row < LED_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < LED_MATRIX_COLS; col++) {
            s_brightness[row][col] = s_draw[row][col];
        }
    }
}

void LEDMatrix_SetBrightness(uint8_t brightness) {
    s_global_brightness = brightness;
}

uint8_t LEDMatrix_GetBrightness(void) {
    return s_global_brightness;
}
