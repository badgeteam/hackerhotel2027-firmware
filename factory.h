#pragma once
// The factory test: what a freshly flashed badge does first, for the
// volunteers at the flashing table (docs/FACTORY_TEST.md). Short, pass or
// fail, no terminal needed. It runs at every power-on until it has passed
// once; the persistent record (persist.h) keeps that, so a firmware
// update does not bring it back.
//
// Only KEY needs a hand; the rest runs on its own, and the volunteer looks:
//   LED  every LED on: white, then the arms red, green, blue (4.5 s). All
//        lit, the right colours? If not: the repair pile
//   ROW  one LED row at a time top to bottom, then one column left to right
//        (a COM line, then a PWM channel), once (5 s): exactly one moving
//        line or set of dots, never two?
//   KEY  press A, B, C and D once each (20 s). make FACTORY_KEYS=0 leaves
//        it out: then the whole test runs by itself
//   USB  USB power for a second (10 s)
//   IR   sends test packets until it hears one: its own reflection, or
//        another badge's. After 10 s: IR? with amber arms, and it keeps
//        listening (a hand in front helps); after 30 s it fails
//
// A failed step blinks red with its name; B tries that step again. Hold
// A + B for 3 s to leave the test without passing it (it comes back at the
// next power-on). Every step is logged (debug_log, minichlink -T).
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FACTORY_BUSY,
    FACTORY_PASSED,  // once, when it passes: keep that in flash
    FACTORY_DONE,    // the pass screen is over, or the test was left: back to the badge
} factory_status_t;

void Factory_Start(void);
// One pass of the main loop: draws the test (LEDMatrix) and returns what
// happened. pressed / held: HH_BTN_* (buttons_poll, buttons_down).
factory_status_t Factory_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held);
