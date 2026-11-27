#include "power.h"
#include "ch32fun.h"

#define CHRG_PIN 22  // PB22
#define VBUS_PIN 3   // PC3: a floating input, as it comes out of reset (100k to GND on the board)
#define CHRG_STEADY_MS 500  // CHRG must keep its level this long (it toggles with no battery)

void Power_Init(void) {
    RCC->APB2PCENR |= RCC_IOPBEN | RCC_IOPCEN;
    // PB22: input with pull-up (CFGXR nibble 6 = 0b1000, the pull-up chosen by OUTDR = 1)
    GPIOB->CFGXR = (GPIOB->CFGXR & ~(0xFu << ((CHRG_PIN - 16) * 4))) | (0x8u << ((CHRG_PIN - 16) * 4));
    GPIOB->BSXR = 1u << (CHRG_PIN - 16);
    // PC3: floating input (CFGLR nibble 3 = 0b0100)
    GPIOC->CFGLR = (GPIOC->CFGLR & ~(0xFu << (VBUS_PIN * 4))) | (0x4u << (VBUS_PIN * 4));
}

bool Power_Usb(void) { return GPIOC->INDR & (1u << VBUS_PIN); }

static bool s_charging;      // debounced
static uint32_t s_steady_ms;  // how long CHRG has said otherwise

bool Power_Tick(uint32_t elapsed_ms) {
    bool now = !(GPIOB->INDR & (1u << CHRG_PIN));
    if (now == s_charging) {
        s_steady_ms = 0;
        return false;
    }
    // At most 20 ms a pass: a slow pass (a store erase over USB) must not
    // take one sample of a toggling line as 500 ms of it
    if (elapsed_ms > 20) elapsed_ms = 20;
    s_steady_ms += elapsed_ms;
    if (s_steady_ms < CHRG_STEADY_MS) return false;
    s_charging = now;
    s_steady_ms = 0;
    return now;
}

bool Power_Charging(void) { return s_charging; }
