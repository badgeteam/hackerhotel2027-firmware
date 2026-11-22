#include <string.h>
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "pattern_player.h"

#define FRAME_PIXELS ( LED_MATRIX_ROWS * LED_MATRIX_COLS )

// Bits needed to index any single cell (0..FRAME_PIXELS-1) or store a run
// length (biased by -1) -- must match webui/compression.js's INDEX_BITS
// (Math.ceil(Math.log2(LEDANIM_PIXELS))), computed there from the matrix
// size instead of hardcoded for the same reason this is hardcoded here
// instead of computed: this is a compile-time constant, that's a
// runtime one, so a mismatch would only be caught by testing, not the
// compiler. Both currently resolve to 9 (384 pixels = 16*24).
#define INDEX_BITS 9

// Last decoded frame, kept in *quantized* level space (not dequantized
// 0-255 brightness) -- deltas are encoded/applied against the quantized
// values, matching webui/compression.js's decompressPattern() exactly
// (it keeps `quant` separate from the dequantized values it pushes into
// `frames`).
static uint8_t s_quant[FRAME_PIXELS];
static const uint8_t *s_data; // the .ledanim being played, in flash
static uint32_t s_bitpos;      // bit offset into the flash blob of the next frame to decode
static uint16_t s_frame_index;
static uint16_t s_frame_count;
static uint8_t s_fps;
static uint8_t s_quant_bits;
static uint32_t s_ms_accum;
static int s_active;
static int s_paused;

static uint8_t dequantize( uint8_t level, uint8_t bits )
{
	if( bits >= 8 )
		return level;
	unsigned levels = ( 1u << bits ) - 1;
	// Rounded integer version of level/levels*255 (matches
	// Math.round(level/levels*255) in the JS reference).
	return (uint8_t)( ( (unsigned)level * 255u + levels / 2u ) / levels );
}

static uint16_t rd_u16( const uint8_t *p )
{
	return (uint16_t)p[0] | ( (uint16_t)p[1] << 8 );
}

// LSB-first bit reader, matching webui/compression.js's BitReader exactly
// (first bit written/read is bit 0 of the first byte). Up to 16 bits at a
// time -- nothing in this format ever needs more (quant_bits maxes out at
// 8, INDEX_BITS at 9).
static unsigned read_bits( const uint8_t *d, uint32_t *bitpos, unsigned n )
{
	unsigned v = 0, got = 0;
	while( got < n )
	{
		uint32_t byte_idx = *bitpos >> 3;
		unsigned bit_idx = *bitpos & 7u;
		unsigned avail = 8u - bit_idx;
		unsigned take = ( avail < ( n - got ) ) ? avail : ( n - got );
		unsigned chunk = ( d[byte_idx] >> bit_idx ) & ( ( 1u << take ) - 1u );
		v |= chunk << got;
		got += take;
		*bitpos += take;
	}
	return v;
}

static void apply_quant_to_display( void )
{
	for( unsigned i = 0; i < FRAME_PIXELS; i++ )
	{
		unsigned row = i / LED_MATRIX_COLS;
		unsigned col = i % LED_MATRIX_COLS;
		LEDMatrix_SetPixel( row, col, dequantize( s_quant[i], s_quant_bits ) );
	}
}

void PatternPlayer_Start( const uint8_t *data )
{
	s_active = 0;
	s_paused = 0;
	if( memcmp( data, "LA02", 4 ) != 0 )
		return;
	const uint8_t *d = data;
	s_data = data;
	s_frame_count = rd_u16( d + 4 );
	s_fps = d[6];
	if( s_fps < 1 )
		s_fps = 1;
	s_quant_bits = d[7];
	if( s_frame_count == 0 )
		return;
	s_bitpos = 8u * 8u; // past the 8-byte header
	s_frame_index = 0;
	s_ms_accum = 0;
	memset( s_quant, 0, sizeof( s_quant ) );
	s_active = 1;
}

// Decodes exactly one frame starting at s_bitpos, advances s_bitpos past
// it, and updates the live display. Mirrors webui/compression.js's
// decompressPattern() per-frame logic (2-bit type field, then
// keyframe-RLE / index-delta / bitmap-delta / raw, all bit-packed at
// exactly quant_bits/INDEX_BITS width, no padding) -- see
// docs/ANIMATION_FORMAT.md.
static void decode_one_frame( void )
{
	const uint8_t *d = s_data;
	uint32_t bp = s_bitpos;
	unsigned type = read_bits( d, &bp, 2 );

	if( type == 0 ) // keyframe: run/value pairs until FRAME_PIXELS cells are filled
	{
		unsigned vi = 0;
		while( vi < FRAME_PIXELS )
		{
			unsigned run = read_bits( d, &bp, INDEX_BITS ) + 1;
			unsigned val = read_bits( d, &bp, s_quant_bits );
			for( unsigned k = 0; k < run && vi < FRAME_PIXELS; k++ )
				s_quant[vi++] = (uint8_t)val;
		}
	}
	else if( type == 3 ) // raw: FRAME_PIXELS values, quant_bits each
	{
		for( unsigned i = 0; i < FRAME_PIXELS; i++ )
			s_quant[i] = (uint8_t)read_bits( d, &bp, s_quant_bits );
	}
	else if( type == 1 ) // index-delta: count (INDEX_BITS), then (index, value) pairs
	{
		unsigned count = read_bits( d, &bp, INDEX_BITS );
		for( unsigned c = 0; c < count; c++ )
		{
			unsigned idx = read_bits( d, &bp, INDEX_BITS );
			unsigned val = read_bits( d, &bp, s_quant_bits );
			if( idx < FRAME_PIXELS )
				s_quant[idx] = (uint8_t)val;
		}
	}
	else // type == 2, bitmap-delta: FRAME_PIXELS 1-bit changed flags, then a
	     // value per set bit. The changed-mask has to be read in full
	     // before any values (they come after all FRAME_PIXELS flag bits
	     // in the stream), so this needs a scratch bitset rather than a
	     // single pass.
	{
		uint8_t changed[ ( FRAME_PIXELS + 7 ) / 8 ];
		memset( changed, 0, sizeof( changed ) );
		for( unsigned i = 0; i < FRAME_PIXELS; i++ )
		{
			if( read_bits( d, &bp, 1 ) )
				changed[i >> 3] |= (uint8_t)( 1u << ( i & 7 ) );
		}
		for( unsigned i = 0; i < FRAME_PIXELS; i++ )
		{
			if( changed[i >> 3] & ( 1u << ( i & 7 ) ) )
				s_quant[i] = (uint8_t)read_bits( d, &bp, s_quant_bits );
		}
	}

	s_bitpos = bp;
	apply_quant_to_display();
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
	// A while loop (not if) so a long stall (e.g. a slow WebUSB command)
	// catches up by frame-skipping rather than drifting the pacing.
	while( s_ms_accum >= frame_ms )
	{
		s_ms_accum -= frame_ms;
		decode_one_frame();
		s_frame_index++;
		if( s_frame_index >= s_frame_count )
		{
			// Loop back to the first frame. The format requires frame 0
			// to be a keyframe or raw (never a delta), so restarting
			// here doesn't need the "previous frame" state to be
			// anything in particular.
			s_bitpos = 8u * 8u;
			s_frame_index = 0;
		}
	}
}
