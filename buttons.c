#include "buttons.h"
#include <stdint.h>
#include "ch32fun.h"

#define BTN2_PIN PC4
#define BTN3_PIN PC5

typedef struct {
    int (*read_raw)(void);  // returns 1 when pressed
    uint8_t debounce_count;
    uint8_t pressed;
} Button;

static int btn1_read_raw(void) {
    return !((GPIOB->INDR >> 20) & 1);
}
static int btn2_read_raw(void) {
    return !funDigitalRead(BTN2_PIN);
}
static int btn3_read_raw(void) {
    return !funDigitalRead(BTN3_PIN);
}
static int btn4_read_raw(void) {
    return !((GPIOB->INDR >> 18) & 1);
}

static Button s_buttons[4] = {
    {btn1_read_raw, 0, 0},
    {btn2_read_raw, 0, 0},
    {btn3_read_raw, 0, 0},
    {btn4_read_raw, 0, 0},
};

#define DEBOUNCE_TICKS 3  // 3 * TICK_MS = 15ms of stable level before it counts

void buttons_init(void) {
    // BTN2/BTN3: PC4/PC5, normal pin range.
    funPinMode(BTN2_PIN, GPIO_CFGLR_IN_PUPD);
    funPinMode(BTN3_PIN, GPIO_CFGLR_IN_PUPD);
    GPIOC->OUTDR |= (1 << 4) | (1 << 5);  // select pull-up

    // BTN1/BTN4: PB20/PB18, need the extended CFGXR register directly.
    GPIOB->CFGXR = (GPIOB->CFGXR & ~(0xFu << (4 * (20 - 16)))) | ((uint32_t)GPIO_CFGLR_IN_PUPD << (4 * (20 - 16)));
    GPIOB->CFGXR = (GPIOB->CFGXR & ~(0xFu << (4 * (18 - 16)))) | ((uint32_t)GPIO_CFGLR_IN_PUPD << (4 * (18 - 16)));
    GPIOB->OUTDR |= (1 << 20) | (1 << 18);  // select pull-up
}

// Returns a bitmask of buttons that were freshly pressed this tick
uint8_t buttons_poll(void) {
    uint8_t pressed_edges = 0;
    for (int i = 0; i < 4; i++) {
        Button* b = &s_buttons[i];
        if (b->read_raw()) {
            if (b->debounce_count < DEBOUNCE_TICKS) b->debounce_count++;
        } else {
            b->debounce_count = 0;
            b->pressed = 0;
        }

        if (b->debounce_count == DEBOUNCE_TICKS && !b->pressed) {
            b->pressed = 1;
            pressed_edges |= (1u << i);
        }
    }
    return pressed_edges;
}

uint8_t buttons_down(void) {
    uint8_t down = 0;
    for (int i = 0; i < 4; i++) {
        if (s_buttons[i].pressed) down |= (1u << i);
    }
    return down;
}

uint8_t buttons_raw(void) {
    uint8_t down = 0;
    for (int i = 0; i < 4; i++) {
        if (s_buttons[i].read_raw()) down |= (1u << i);
    }
    return down;
}

// Treat every button as already pressed, so a press that is still held (e.g.
// the one that woke us from standby) doesn't register until it's released
void buttons_ignore_held(void) {
    for (int i = 0; i < 4; i++) {
        s_buttons[i].debounce_count = DEBOUNCE_TICKS;
        s_buttons[i].pressed = 1;
    }
}
