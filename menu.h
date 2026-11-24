#pragma once
// The menu on the badge (hold A): the name tag, SAY HI, every stored item
// by name, and the badge's own settings; and the name editor (EDIT NAME).
//
// While it is open, what ran before is paused: an app gets no ticks, an
// animation does not advance. The main loop calls Menu_Tick() instead, and
// acts on what it returns.
#include <stdbool.h>
#include <stdint.h>

// How the menu and the editor use the buttons (Persist_Keys()): the two
// pairs that lie at 90 degrees to each other on the badge
#define MENU_KEYS_HORIZONTAL 0  // C previous, D next (the bottom pair); B back
#define MENU_KEYS_VERTICAL 1    // B previous, D next (the right pair); C back

typedef enum {
    MENU_OPEN,     // still open
    MENU_KEEP,     // closed: go on with what ran before
    MENU_RUN,      // closed: start store item *item
    MENU_NAMETAG,  // closed: show the name tag
    MENU_SAY_HI,   // closed: swap names over infrared (greet.h), then go on with what ran before
    MENU_SLEEP,    // closed: standby, then go on with what ran before
    MENU_UPDATE,   // closed: into the USB boot ROM
} menu_result_t;

// current: the store item that runs, or STORE_NONE (the cursor starts there)
void Menu_Open(uint8_t current);

// Call every main loop pass while the menu is open, with the buttons that
// went down (buttons_poll) and are down (buttons_down)
menu_result_t Menu_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held, uint8_t* item);

// One brightness level up (dir 1) or down (dir -1): the B and D shortcuts
void Menu_StepBrightness(int dir);
// The level of the brightness now: the highest of them at or below it, 0 .. 5
int Menu_BrightnessLevel(void);
