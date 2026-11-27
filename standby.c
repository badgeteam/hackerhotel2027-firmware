#include "standby.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "infrared.h"
#include "pattern_player.h"
#include "webusb_protocol.h"

// Button EXTI lines: BTN1=PB20, BTN2=PC4, BTN3=PC5, BTN4=PB18
#define STANDBY_WAKE_LINES ((1u << 20) | (1u << 4) | (1u << 5) | (1u << 18))

void standby(void) {
    bool pattern_was_playing = PatternPlayer_Pause();
    WebUSB_Deinit();
    LEDMatrix_Deinit();
    bool infrared_was_on = infrared_deinit();  // IR interrupts would wake us, and parks the IR LED pin

    RCC->APB2PCENR |= RCC_AFIOEN;
    RCC->APB1PCENR |= RCC_PWREN;

    // Route the button pins to their EXTI lines (CH643RM 8.3.2.2/8.3.2.3)
    AFIO->EXTICR1 =
        (AFIO->EXTICR1 & ~(AFIO_EXTICR1_EXTI4 | AFIO_EXTICR1_EXTI5)) | AFIO_EXTICR1_EXTI4_PC | AFIO_EXTICR1_EXTI5_PC;
    AFIO->EXTICR2 = (AFIO->EXTICR2 & ~(AFIO_EXTICR2_EXTI18 | AFIO_EXTICR2_EXTI20)) | AFIO_EXTICR2_EXTI18_PB |
                    AFIO_EXTICR2_EXTI20_PB;

    // Buttons are active low: wake on falling edge, as an event (no ISR needed)
    EXTI->INTENR &= ~STANDBY_WAKE_LINES;
    EXTI->RTENR &= ~STANDBY_WAKE_LINES;
    EXTI->FTENR |= STANDBY_WAKE_LINES;
    EXTI->INTFR = STANDBY_WAKE_LINES;  // clear stale flags
    EXTI->EVENR |= STANDBY_WAKE_LINES;

    // Standby (CH643RM 2.3.4): SLEEPDEEP=1, PDDS=1, then WFE. With the default
    // STANDYRST option byte the chip does not reset; execution resumes here.
    PWR->CTLR |= PWR_CTLR_PDDS;
    NVIC->SCTLR |= (1 << 2);  // SLEEPDEEP
    __WFE();

    // Back to normal so later WFI/WFE calls are plain sleep again
    NVIC->SCTLR &= ~(1 << 2);
    PWR->CTLR &= ~PWR_CTLR_PDDS;
    EXTI->EVENR &= ~STANDBY_WAKE_LINES;
    EXTI->FTENR &= ~STANDBY_WAKE_LINES;
    EXTI->INTFR = STANDBY_WAKE_LINES;
#if FUNCONF_ENABLE_HPE
    // The interrupt handlers save no registers themselves (IRQ_HANDLER in
    // funconfig.h): they need HPE on. CH643RM 2.3 says the system just
    // continues after standby, so INTSYSCR should still hold what the
    // startup wrote. Write it again anyway (the same 0x0b: HPE and nesting
    // on) before any interrupt is enabled again, so that a wake can never
    // leave HPE off.
    __set_INTSYSCR(0x0b);
#endif

    LEDMatrix_Init();
    WebUSB_Init();
    if (infrared_was_on) infrared_init();
    if (pattern_was_playing) PatternPlayer_Resume();
}
