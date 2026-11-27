#pragma once
// The LA04 animation decoder (docs/ANIMATION_FORMAT.md; the encoder is
// webui/compression.js). It plays a .ledanim file that sits in flash one
// frame at a time.
//
// Its tables and frame buffers (about 4 KB) live in the app RAM block
// (HH_APP_RAM_BASE): no app runs while an animation plays. Starting an app
// overwrites them, so call LA04_Open() again before the next LA04_Next().
#include <stdbool.h>
#include <stdint.h>

// Reads the header and the tables, and goes to frame 0. False if data is
// not an LA04 file this decoder can play.
bool LA04_Open(const uint8_t* data, uint32_t length);

uint16_t LA04_Frames(void);
uint8_t LA04_Fps(void);

// Decodes the next frame into out[384]: brightness 0..255 per slot (row *
// 24 + col). After the last frame it goes on with frame 0.
void LA04_Next(uint8_t* out);
