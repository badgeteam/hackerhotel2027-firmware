#ifndef _PATTERN_PLAYER_H
#define _PATTERN_PLAYER_H

// Plays the .ledanim pattern stored in flash (see pattern_flash.h) on
// repeat. A from-scratch C port of webui/compression.js's
// decompressPattern(), decoding one frame at a time into the live
// matrix instead of expanding the whole animation into a frame array
// (which wouldn't fit in 20KB of RAM) -- see docs/ANIMATION_FORMAT.md.

#include <stdint.h>

// Rewinds to frame 0 and starts playback, if PatternFlash_IsValid().
// Call once when switching into APP_MODE_FLASH_PATTERN (at boot if a
// pattern is already stored, or right after a fresh upload finishes).
// If no valid pattern is stored, playback simply stays inactive and
// PatternPlayer_Tick() does nothing.
void PatternPlayer_Reset( void );

// Call every main-loop tick with the elapsed time since the last call
// (milliseconds) -- paces itself against the pattern's stored fps
// internally, decoding (and looping) frames as needed instead of one
// per call.
void PatternPlayer_Tick( uint32_t elapsed_ms );

// Freezes playback on the current frame (PatternPlayer_Tick() does nothing
// until resumed). Returns whether playback was active and is now paused,
// i.e. whether PatternPlayer_Resume() should be called later.
int PatternPlayer_Pause( void );
void PatternPlayer_Resume( void );

#endif
