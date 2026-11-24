#pragma once
// The name tag: what the badge shows when nothing else runs. The owner's
// name (persist.h, set in the web interface) on the centre grid, standing
// still if it fits and scrolling if it does not, or "HACKER HOTEL 2027"
// while no name is set; the arms slowly cycle colour and the outline
// sparkles. As in the previous firmware's name tag.
//
// A text item from the store plays the same way, with its own text.
#include <stdbool.h>
#include <stdint.h>

// A text item (STORE_TYPE_TEXT): this header, then the text, one byte per
// character. Printable ASCII shows; other bytes show as a blank. The text
// stays in flash and plays from there.
typedef struct {
    char magic[4];   // "HHT1"
    uint8_t speed;   // columns per second when the text scrolls, 1 .. 60
    uint8_t style;   // TEXT_STYLE_*
    uint8_t reserved[2];
} text_item_header_t;

#define TEXT_ITEM_MAGIC "HHT1"
#define TEXT_STYLE_NAMETAG 0  // as the name tag: a sheen, colour arms, a sparkling outline
#define TEXT_STYLE_PLAIN 1    // only the text

// True if payload is a text item the firmware can play (the store checks
// it on upload)
bool TextItem_Check(const uint8_t* payload, uint32_t length);

// Shows the owner's name
void Nametag_Start(void);
// Plays a text item instead; false if it is not one
bool Nametag_StartText(const uint8_t* payload, uint32_t length);
// Call every main loop pass with the milliseconds since the previous one
void Nametag_Tick(uint32_t elapsed_ms);
