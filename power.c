#include "power.h"
#include "ch32fun.h"

#define CHRG_PIN 22  // PB22
#define VBUS_PIN 3   // PC3: a floating input, as it comes out of reset (100k to GND on the board)

void Power_Init(void) {
    RCC->APB2PCENR |= RCC_IOPBEN | RCC_IOPCEN;
    // PB22: input with pull-up (CFGXR nibble 6 = 0b1000, the pull-up chosen by OUTDR = 1)
    GPIOB->CFGXR = (GPIOB->CFGXR & ~(0xFu << ((CHRG_PIN - 16) * 4))) | (0x8u << ((CHRG_PIN - 16) * 4));
    GPIOB->BSXR = 1u << (CHRG_PIN - 16);
    // PC3: floating input (CFGLR nibble 3 = 0b0100)
    GPIOC->CFGLR = (GPIOC->CFGLR & ~(0xFu << (VBUS_PIN * 4))) | (0x4u << (VBUS_PIN * 4));
}

bool Power_Usb(void) { return GPIOC->INDR & (1u << VBUS_PIN); }

bool Power_Charging(void) { return !(GPIOB->INDR & (1u << CHRG_PIN)); }
