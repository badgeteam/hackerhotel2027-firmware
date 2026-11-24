#ifndef _PATTERN_PLAYER_H
#define _PATTERN_PLAYER_H

// Plays a .ledanim animation (LA04, see la04.h and docs/ANIMATION_FORMAT.md)
// from flash on repeat, decoding one frame at a time into the live matrix.

#include <stdint.h>

// Starts playing the .ledanim at data (in flash, length bytes) from frame 0.
// Call once when switching into APP_MODE_FLASH_PATTERN. Returns 0 if data
// is not an LA04 file (for example one stored by older firmware): then
// playback stays inactive and PatternPlayer_Tick() does nothing.
int PatternPlayer_Start( const uint8_t *data, uint32_t length );

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
