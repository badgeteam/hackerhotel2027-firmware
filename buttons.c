#include "buttons.h"
#include <stdint.h>
#include "ch32fun.h"

#define BTN2_PIN PC4
#define BTN3_PIN PC5

// Bit i set: button i is down (the buttons pull their pin low).
// BTN1: PB20, BTN2: PC4, BTN3: PC5, BTN4: PB18; one read of each port
static unsigned read_all(void) {
    uint32_t b = ~GPIOB->INDR, c = ~GPIOC->INDR;
    return (b >> 20 & 1) | (c >> 3 & 6) | (b >> 15 & 8);
}

static uint8_t s_count[4];  // ticks each button has been down, up to DEBOUNCE_TICKS
static uint8_t s_pressed;   // bit i: button i counts as pressed (debounced)

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
    unsigned raw = read_all(), edges = 0;
    for (int i = 0; i < 4; i++) {
        if (raw >> i & 1) {
            if (s_count[i] < DEBOUNCE_TICKS && ++s_count[i] == DEBOUNCE_TICKS) edges |= 1u << i;
        } else {
            s_count[i] = 0;
        }
    }
    s_pressed = (uint8_t)((s_pressed | edges) & raw);
    return (uint8_t)edges;
}

uint8_t buttons_down(void) { return s_pressed; }

uint8_t buttons_raw(void) { return (uint8_t)read_all(); }

// Treat every button as already pressed, so a press that is still held (e.g.
// the one that woke us from standby) doesn't register until it's released
void buttons_ignore_held(void) {
    for (int i = 0; i < 4; i++) s_count[i] = DEBOUNCE_TICKS;
    s_pressed = 15;
}
