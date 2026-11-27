#include "pattern_player.h"
#include "ch643_ledpwm.h"
#include "la04.h"

#define FRAME_PIXELS ( LED_MATRIX_ROWS * LED_MATRIX_COLS )

static uint8_t s_frame[FRAME_PIXELS]; // the decoded frame, per slot
static uint8_t s_fps;
static uint32_t s_ms_accum;
static int s_active;
static int s_paused;

int PatternPlayer_Start( const uint8_t *data, uint32_t length )
{
	s_paused = 0;
	s_active = LA04_Open( data, length );
	s_fps = LA04_Fps();
	if( s_fps < 1 )
		s_fps = 1;
	s_ms_accum = 0;
	return s_active;
}

int PatternPlayer_Pause( void )
{
	if( !s_active || s_paused )
		return 0;
	s_paused = 1;
	return 1;
}

void PatternPlayer_Resume( void )
{
	s_paused = 0;
	s_ms_accum = 0; // continue from the current frame, no catching up on the paused time
}

void PatternPlayer_Tick( uint32_t elapsed_ms )
{
	if( !s_active || s_paused )
		return;
	s_ms_accum += elapsed_ms;
	uint32_t frame_ms = 1000u / s_fps;
	if( frame_ms == 0 )
		frame_ms = 1;
	if( s_ms_accum < frame_ms )
		return;
	// After a long stall (e.g. a slow WebUSB command) decode the frames that
	// were missed, so the pacing stays right, but show only the last one.
	// LA04_Next() goes back to frame 0 after the last frame by itself.
	while( s_ms_accum >= frame_ms )
	{
		s_ms_accum -= frame_ms;
		LA04_Next( s_frame );
	}
	LEDMatrix_SetFrame( s_frame );
}
