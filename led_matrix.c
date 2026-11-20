#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "led_matrix.h"

// --- Pixel data -----------------------------------------------------------
// Shared between the main loop (writer) and the LEDPWM ISR (reader). Plain
// volatile bytes are enough here: at worst a pattern update straddles a row
// boundary and one frame shows a one-row seam, which isn't visible at scan
// rate.
static volatile uint8_t s_brightness[LED_MATRIX_ROWS][LED_MATRIX_COLS];

// Row-data buffer the LEDPWM DMA engine reads from. Re-packed by the ISR
// for the upcoming row just before arming the DMA, then left alone while
// hardware scans through it. One buffer is enough because the next row
// isn't touched again until it's this row's turn.
//
// 8 words, not LED_MATRIX_COLS (24): MATRIX_MODE=PA24 is an 8-column RGB
// mode (CH643RM.PDF 24.2.2, "8C*3*N") -- PA[7:0]=R, PA[15:8]=G,
// PA[23:16]=B, 8 columns each, LED_DMA_CNT=8 ("eight 32-bit data"). Each
// of our 24 addressable "columns" is one (group, channel) pair, 3
// channels per group, so one DMA word per group carries all three
// channels' brightness.
//
// LED_DMA only holds a 16-bit address (see docs/LEDPWM_MATRIX.md); CH643
// SRAM starts at 0x20000000 and is only 20KB, so any static buffer's
// address already fits in 16 bits once the base is dropped, which is all
// a (uint16_t) cast below does.
static uint32_t s_row_dma_buf[LED_MATRIX_COLS / 3];

static volatile uint8_t s_next_row;
static volatile uint8_t s_last_loaded_row; // row ledpwm_load_row() most recently packed, for LEDMatrix_DebugDump()

// When >=0, the ISR always reloads this one row instead of advancing
// through the normal 0-15 scan -- see LEDMatrix_TestPixel().
static volatile int s_frozen_row = -1;

// LED_COM_SEL codes for PB0-15 (see CH643RM.PDF 24.3.2 -- PB0-15 are
// codes 0b010000-0b011111, i.e. 0x10 + pin number, conveniently
// consecutive).
static const uint8_t s_com_sel_for_row[LED_MATRIX_ROWS] =
{
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

// --- GPIO setup -------------------------------------------------------------
// funPinMode()/funDigitalWrite() only reach pins 0-15 of a port (see
// ch32fun.h's GpioOf()/pin-encoding, which packs just one nibble-pair's
// worth of pins per port). Pins 16+ need the extra CFGXR register
// directly -- used here for PA16-23 (columns).
static void gpio_set_mode( GPIO_TypeDef *port, unsigned pin, GPIO_CFGLR_PIN_MODE_Typedef mode )
{
	__IO uint32_t *cfg;
	unsigned shift;
	if( pin < 8 )       { cfg = &port->CFGLR; shift = pin; }
	else if( pin < 16 )  { cfg = &port->CFGHR; shift = pin - 8; }
	else                 { cfg = &port->CFGXR; shift = pin - 16; }
	*cfg = ( *cfg & ~( 0xFu << ( 4 * shift ) ) ) | ( (uint32_t)mode << ( 4 * shift ) );
}

static void led_matrix_gpio_init( void )
{
	// Columns (PWM, PA0-23): LED anodes, internal-drive source current
	// (CH643RM.PDF 24.2.2) -- active-high, idle low (OUTDR=0, the
	// power-on default, cleared explicitly for clarity). All 24 PA pins
	// are driven by LEDPWM even though only 8 groups are independently
	// enabled (LED_PWM_PIN_L's 8 bits each gate one group's R+G+B
	// together) -- see ledpwm_load_row().
	for( unsigned pin = 0; pin < 24; pin++ )
		gpio_set_mode( GPIOA, pin, GPIO_CFGLR_OUT_10Mhz_PP );
	GPIOA->OUTDR &= ~0xFFFFFFu;

	// Rows (COM, PB0-15): LED cathodes, active-low, idle high (OUTDR=1)
	// per CH643RM.PDF 24.2.2 ("corresponding bit... in GPIOB_OUTDR must
	// be set to 1").
	for( unsigned pin = 0; pin < LED_MATRIX_ROWS; pin++ )
		gpio_set_mode( GPIOB, pin, GPIO_CFGLR_OUT_10Mhz_PP );
	GPIOB->OUTDR |= 0xFFFFu;
}

// --- Frame ISR --------------------------------------------------------------
// Fires once per row, during the inter-row blanking window (LED_FRAME_CFG's
// INHIBIT), per CH643RM.PDF 24.2.1. Packs the data for the row that's about
// to be displayed, hands it to the DMA engine, then selects that row's COM
// pin -- which also clears the interrupt flag and starts that row's display
// once blanking ends.
//
// Split into a plain helper (ledpwm_load_row) plus a thin ISR wrapper:
// LEDMatrix_Init() needs to run this same logic once up front to prime row
// 0, and calling an __attribute__((interrupt)) function directly is
// undefined behaviour (its generated epilogue ends in `mret`, which expects
// mepc/mstatus to have been set up by an actual hardware trap entry -- not
// by a plain call). Doing so was crashing the core during LEDMatrix_Init(),
// before WebUSB_Init() ever ran.
static void ledpwm_load_row( unsigned row )
{
	// Group g (0-7) packs all three channels of one column-group into one
	// word: g's brightness in the red byte (-> PA[g], column g), (g+8)'s
	// in the green byte (-> PA[8+g], column g+8), (g+16)'s in the blue
	// byte (-> PA[16+g], column g+16). byte0 (luminance) left fully open
	// -- see docs/LEDPWM_MATRIX.md for why.
	for( unsigned g = 0; g < LED_MATRIX_COLS / 3; g++ )
	{
		uint8_t red = s_brightness[row][g];
		uint8_t green = s_brightness[row][g + LED_MATRIX_COLS / 3];
		uint8_t blue = s_brightness[row][g + 2 * ( LED_MATRIX_COLS / 3 )];
		uint32_t word = 0xFFu; // luminance = full
		word |= (uint32_t)red << 8;    // red byte
		word |= (uint32_t)green << 16; // green byte
		word |= (uint32_t)blue << 24;  // blue byte
		s_row_dma_buf[g] = word;
	}
	LEDPWM->DMA = (uint16_t)(uint32_t)s_row_dma_buf;
	// COM_SEL/PWM_OE/DMA_CNT in one atomic 32-bit write (LED_SWITCH, see
	// ch643_ledpwm.h) -- three separate byte writes here left DMA_CNT/
	// PWM_OE not reliably latched before the COM_SEL write triggered the
	// row switch, so only the first DMA word ever took effect (see
	// docs/LEDPWM_MATRIX.md, "Hardware bring-up: only the first word
	// loads"). PWM_OE=1 re-enables output (PWM_OE_CLR blanks it on IRQ
	// entry); COM_SEL also clears IF_INHIBIT.
	//
	// PWM_PIN_L/H (column enables) are deliberately NOT touched here.
	// Confirmed on hardware: enabling a column makes it light on EVERY
	// row it's scanned on, regardless of that row's own brightness byte
	// -- i.e. the enable bit isn't a per-row gate at all, so rewriting it
	// every row (tried: full 32-bit write, 16-bit-only write, before and
	// after this SWITCH write -- all four combinations) just produces
	// "every enabled column lit on every row" the same as a static
	// all-enabled config would. PWM_PIN is instead set statically by
	// whichever test/debug function is currently active (see
	// LEDMatrix_TestPixel() below) and left alone here. See
	// "Hardware bring-up: PWM_PIN per-row rewrite" in docs/LEDPWM_MATRIX.md.
	LEDPWM->SW.SWITCH = LEDPWM_SWITCH( s_com_sel_for_row[row], 1, LED_MATRIX_COLS / 3 );

	s_last_loaded_row = (uint8_t)row;
	s_next_row = ( row + 1 < LED_MATRIX_ROWS ) ? row + 1 : 0;
}

void LEDPWM_IRQHandler( void ) __attribute__((interrupt));
void LEDPWM_IRQHandler( void )
{
	ledpwm_load_row( s_frozen_row >= 0 ? (unsigned)s_frozen_row : s_next_row );
}

// --- Public API --------------------------------------------------------------
// Leaves single-pixel test mode (see LEDMatrix_TestPixel()): resumes the
// normal 0-15 scan and restores PWM_PIN to its correct standing value --
// PWM_PIN_L=0xFF (every column-group gate on; per-pixel brightness alone
// then controls visibility -- see "PWM_PIN is a column-enable gate, not a
// mask" in docs/LEDPWM_MATRIX.md) and PWM_PIN_H=0x00 (confirmed on
// hardware: PA24 is an 8-column mode with no physical columns for
// PWM_PIN_H to gate, and leaving it nonzero was confirmed to interfere --
// 0x00 is the value that makes the matrix work correctly). This used to
// reset PWM_PIN_L to 0x00 as well, back when that was believed to be a
// safe default; it no longer is -- leaving PWM_PIN_L at 0x00 here
// silently killed every column except whichever one a subsequent
// TestPixel/TestCol call happened to re-enable, which is exactly the
// regression a stale pixel-test session used to cause.
static void exit_pixel_test( void )
{
	if( s_frozen_row < 0 )
		return;
	s_frozen_row = -1;
	LEDPWM->PP.PWM_PIN = LEDPWM_PWM_PIN( 0xFF, 0x00, LEDPWM_MATRIX_MODE_PA24 | LEDPWM_PWM_RED_EN | LEDPWM_PWM_GREEN_EN | LEDPWM_PWM_BLUE_EN );
}

// Set by LEDMatrix_GpioTest() -- see there.
static volatile int s_gpio_test_active = 0;

// Leaves raw-GPIO test mode (see LEDMatrix_GpioTest()): re-enables LEDPWM
// and resumes normal scanning from wherever it left off.
static void exit_gpio_test( void )
{
	if( !s_gpio_test_active )
		return;
	s_gpio_test_active = 0;
	GPIOA->OUTDR &= ~0xFFFFFFu; // columns (PA) idle: active-high -> idle-low
	GPIOB->OUTDR |= 0xFFFFu;    // rows (PB) idle: active-low -> idle-high
	LEDPWM->CR.FUNC_CTRL = LEDPWM_LED_ENABLE | LEDPWM_IE_INHIBIT | LEDPWM_PWM_OE_CLR;
	// Re-enabling FUNC_CTRL alone left the scan permanently stuck on
	// whatever row was active when LED_ENABLE was cleared (confirmed via
	// DebugDump: s_last_loaded_row never changed again after this
	// function ran) -- same as LEDMatrix_Init(), the peripheral needs an
	// explicit re-prime (loading real data and writing COM_SEL) to
	// actually kick the state machine back into running, simply turning
	// the clock back on isn't enough.
	ledpwm_load_row( s_next_row );
	NVIC_EnableIRQ( LEDPWM_IRQn );
}

void LEDMatrix_SetPixel( unsigned row, unsigned col, uint8_t brightness )
{
	exit_pixel_test();
	exit_gpio_test();
	if( row < LED_MATRIX_ROWS && col < LED_MATRIX_COLS )
		s_brightness[row][col] = brightness;
}

void LEDMatrix_Fill( uint8_t brightness )
{
	exit_pixel_test();
	exit_gpio_test();
	for( unsigned row = 0; row < LED_MATRIX_ROWS; row++ )
		for( unsigned col = 0; col < LED_MATRIX_COLS; col++ )
			s_brightness[row][col] = brightness;
}

// Bring-up debugging: pokes s_brightness directly with NO side effects
// (doesn't exit pixel-test mode, unlike LEDMatrix_SetPixel) -- for
// building up a custom brightness pattern after LEDMatrix_TestPixel()
// without it immediately unfreezing the row / resetting PWM_PIN.
void LEDMatrix_PokeBrightness( unsigned row, unsigned col, uint8_t brightness )
{
	if( row < LED_MATRIX_ROWS && col < LED_MATRIX_COLS )
		s_brightness[row][col] = brightness;
}

// Isolates exactly one LED: freezes the scan ISR on only its row and
// leaves exactly one pixel on that row at nonzero brightness, so current
// only ever flows through that single (row, col) intersection. Used to
// also narrow PWM_PIN down to that one column's enable bit, back when
// enabling a column lit it on every row it was scanned on regardless of
// that row's brightness byte -- necessary before PWM_PIN was understood
// to be a column-enable *gate*, not a mask (see docs/LEDPWM_MATRIX.md):
// narrowing it to one bit silently cleared bit0, the master gate for the
// rest of that byte, breaking every column but the one under test (and
// column 0/8 themselves only by accident, since testing them satisfies
// their own gate bit). Freezing the row is enough on its own now that
// PWM_PIN is left permanently all-enabled (LEDMatrix_Init(),
// exit_pixel_test()) -- only the frozen row is ever displayed, and only
// one pixel on it is nonzero.
void LEDMatrix_TestPixel( unsigned row, unsigned col )
{
	LEDMatrix_Clear(); // also calls exit_pixel_test() via Fill()
	if( row >= LED_MATRIX_ROWS || col >= LED_MATRIX_COLS )
		return;
	LEDMatrix_SetPixel( row, col, 255 ); // harmless extra exit_pixel_test() call
	s_frozen_row = (int)row;
}

// Bring-up debugging: pokes PWM_PIN_L/H directly, touching nothing else
// (no Clear, no SetPixel, no row freeze/unfreeze) -- for testing whether
// a column's "cascading enable" prerequisites (see
// docs/LEDPWM_MATRIX.md) can be satisfied with their OWN brightness left
// at 0, while a separately-set target pixel stays lit. Call after
// LEDMatrix_TestPixel() to override just the enable mask it set.
void LEDMatrix_SetPwmPinRaw( uint8_t pwm_pin_l, uint8_t pwm_pin_h )
{
	LEDPWM->PP.PWM_PIN = LEDPWM_PWM_PIN( pwm_pin_l, pwm_pin_h, LEDPWM_MATRIX_MODE_PA24 | LEDPWM_PWM_RED_EN | LEDPWM_PWM_GREEN_EN | LEDPWM_PWM_BLUE_EN );
}

// Bring-up debugging: like LEDMatrix_TestPixel() but takes the raw
// PWM_PIN_L/H bitmasks directly -- lets WebUSB probe arbitrary enable-bit
// combinations without a firmware rebuild per combination. Note: in
// MATRIX_MODE=PA24 (8-column, see led_matrix.h), each of PWM_PIN_L's 8
// bits gates one whole column-group's R+G+B together (CH643RM.PDF
// 24.3.13, "each corresponding to a total of 3 PWM pins of RGB") --
// PWM_PIN_H has no physical columns to gate in this mode and is written
// but otherwise expected to be a no-op. Setting bit g lights all three of
// that group's columns (g, g+8, g+16) at full brightness, to show what
// enabling that group's gate actually does. See CMD_RAW_TEST in
// webusb_protocol.c.
void LEDMatrix_RawTest( unsigned row, uint8_t pwm_pin_l, uint8_t pwm_pin_h )
{
	LEDMatrix_Clear();
	if( row >= LED_MATRIX_ROWS )
		return;
	for( unsigned g = 0; g < LED_MATRIX_COLS / 3; g++ )
		if( pwm_pin_l & ( 1u << g ) )
		{
			LEDMatrix_SetPixel( row, g, 255 );
			LEDMatrix_SetPixel( row, g + LED_MATRIX_COLS / 3, 255 );
			LEDMatrix_SetPixel( row, g + 2 * ( LED_MATRIX_COLS / 3 ), 255 );
		}
	LEDPWM->PP.PWM_PIN = LEDPWM_PWM_PIN( pwm_pin_l, pwm_pin_h, LEDPWM_MATRIX_MODE_PA24 | LEDPWM_PWM_RED_EN | LEDPWM_PWM_GREEN_EN | LEDPWM_PWM_BLUE_EN );
	s_frozen_row = (int)row;
}

// Bypasses LEDPWM entirely: halts the peripheral and the scan ISR, then
// drives exactly one row (sink, GPIOB) and one column (source, GPIOA) as
// plain push-pull GPIO outputs, static DC, no multiplexing. Answers "does
// this (row, col) LED/trace/joint physically work at all" independent of
// anything LEDPWM-side -- see "Hardware bring-up: PWM_PIN is not a
// per-row gate" in docs/LEDPWM_MATRIX.md for why that question came up.
// GPIO gpio_set_mode()/push-pull config was already done once in
// LEDMatrix_Init() (led_matrix_gpio_init()) and is reused as-is; only
// OUTDR (which pins are actively driven) changes here. col maps to its
// physical PA pin the same way ledpwm_load_row() does: col 0-7/8-15/16-23
// are group g's red/green/blue pin (PA[g]/PA[8+g]/PA[16+g]).
void LEDMatrix_GpioTest( unsigned row, unsigned col )
{
	if( row >= LED_MATRIX_ROWS || col >= LED_MATRIX_COLS )
		return;

	if( !s_gpio_test_active )
	{
		NVIC_DisableIRQ( LEDPWM_IRQn );
		LEDPWM->CR.FUNC_CTRL = 0; // LED_ENABLE=0 -- stop LEDPWM from also driving these pins
		s_gpio_test_active = 1;
	}

	unsigned pa_pin = ( col % ( LED_MATRIX_COLS / 3 ) ) + 8 * ( col / ( LED_MATRIX_COLS / 3 ) );

	GPIOA->OUTDR &= ~0xFFFFFFu; // all columns idle (active-high -> idle-low)
	GPIOB->OUTDR |= 0xFFFFu;    // all rows idle (active-low -> idle-high)
	GPIOA->OUTDR |= ( 1u << pa_pin ); // source this one column
	GPIOB->OUTDR &= ~( 1u << row );   // sink this one row
}

void LEDMatrix_TestRow( unsigned row )
{
	LEDMatrix_Clear();
	for( unsigned col = 0; col < LED_MATRIX_COLS; col++ )
		LEDMatrix_SetPixel( row, col, 255 );
}

void LEDMatrix_TestCol( unsigned col )
{
	LEDMatrix_Clear();
	for( unsigned row = 0; row < LED_MATRIX_ROWS; row++ )
		LEDMatrix_SetPixel( row, col, 255 );
}

static volatile uint8_t s_stage_buf[LED_MATRIX_PIXEL_COUNT];

void LEDMatrix_StageFrameChunk( uint16_t offset, const uint8_t *data, uint8_t len )
{
	for( uint8_t i = 0; i < len; i++ )
	{
		uint16_t idx = offset + i;
		if( idx < LED_MATRIX_PIXEL_COUNT )
			s_stage_buf[idx] = data[i];
	}
}

void LEDMatrix_CommitStagedFrame( void )
{
	for( unsigned row = 0; row < LED_MATRIX_ROWS; row++ )
		for( unsigned col = 0; col < LED_MATRIX_COLS; col++ )
			s_brightness[row][col] = s_stage_buf[row * LED_MATRIX_COLS + col];
}

void LEDMatrix_Init( void )
{
	led_matrix_gpio_init();

	// COM_E / PWM_PIN / FUNC_CTRL+CYCLE_CFG+FRAME_CFG are each, per the
	// vendor's own ch643.h (EVT/EXAM SDK) and CH643RM.PDF 24.3.1/24.3.6-10/
	// 24.3.11-15/24.3.16-20, grouped 32-bit registers like LED_SWITCH (see
	// ch643_ledpwm.h) -- written atomically here for the same reason, even
	// though these are one-shot init writes rather than the per-row ones
	// that turned out to matter.
	//
	// MATRIX_MODE=PA24 (8C*3*N internal drive, CH643RM.PDF 24.2.2) is what
	// this board is actually wired for -- PWM/column pins (LED anodes) on
	// PA[23:0], COM/row pins (LED cathodes) on PB[15:0]. An earlier
	// revision of this driver used PB24 (an *external*-PMOS-drive mode
	// with PWM/COM on the opposite ports), which happened to produce some
	// working output -- columns 0 and 8 always responded, since those
	// bits' gate gets satisfied by construction -- but was fundamentally
	// addressing the wrong GPIO port for everything else. COM_E now
	// enables PB0-15 (COM_E_PB_L/PB_M) instead of any PA/PC bits.
	LEDPWM->CE.COM_E = LEDPWM_COM_E( 0xFF, 0xFF, 0x00, 0x00 );
	// PWM_PIN_L's 8 bits each gate one whole column-group's R+G+B
	// together (CH643RM.PDF 24.3.13) -- not a per-column mask, and bit0
	// is additionally a master gate for the rest of the byte: with bit0
	// clear, every other bit stays dark regardless of how many are set
	// (confirmed exhaustively on hardware under the old PB24 config;
	// expected to hold here too since it's the same LED_PWM_PIN_L
	// register). PWM_PIN_H has no physical columns to gate in this
	// 8-column mode. Setting every bit once here and leaving it alone
	// lets the per-pixel brightness bytes in the DMA buffer be the only
	// thing controlling what's actually visible -- see "PWM_PIN is a
	// column-enable gate, not a mask" in docs/LEDPWM_MATRIX.md.
	LEDPWM->PP.PWM_PIN = LEDPWM_PWM_PIN( 0xFF, 0x00, LEDPWM_MATRIX_MODE_PA24 | LEDPWM_PWM_RED_EN | LEDPWM_PWM_GREEN_EN | LEDPWM_PWM_BLUE_EN );

	// Refresh timing -- see docs/LEDPWM_MATRIX.md for the full derivation,
	// including a first attempt at this that turned out too aggressive on
	// real hardware (some columns never finished loading before blanking
	// ended -- see "Hardware bring-up: column dropout" there).
	//
	// LED_INTEN_CYC drops from 8-bit/256 to 6-bit/64: the luminance byte
	// (byte0 of the DMA word) is always pegged to 0xFF in
	// ledpwm_load_row(), so that sub-counter contributes no visible
	// grayscale at all -- shrinking it is free speed, not a quality
	// tradeoff. COLOR_CYC stays 8-bit/256 (the default, bit left clear)
	// since that's what the actual 0-255 brightness byte is compared
	// against. CLK_FREQ stays at /2 (unchanged) and INHIBIT is raised to
	// its maximum (16 cycles, up from 3) -- both deliberately conservative,
	// to give the DMA engine *more* absolute (wall-clock) blanking time to
	// shift all 8 DMA words in than the original config had, not less,
	// since the active (non-blanking) portion of each row already
	// dominates total row time many times over and absorbs nearly all of
	// the INTEN_CYC win on its own.
	// INTEN_FULL experiment: luminance is hardcoded 0xFF in
	// ledpwm_load_row() on the assumption it's a harmless full-open
	// pass-through, letting the color byte alone control brightness. The
	// manual only documents INTEN_FULL ("replace luminance data with
	// color data") for PA16I mode specifically, but given CLEAR isn't
	// fully dark even with color bytes confirmed correct (see
	// docs/LEDPWM_MATRIX.md), it's worth testing whether luminance is
	// silently participating rather than being bypassed without it.
	// Constant-current mode (PWM_REF/PWM_PA_CCP/PWM_PB_CCN) temporarily
	// disabled again -- testing whether it's responsible for "every LED
	// lights even with only one pixel set" (PWM_REF's documented purpose
	// is tied to current-mode *key scan*, which inherently senses current
	// across multiple pins at once; a plausible way for it to interfere
	// with per-pixel isolation). See docs/LEDPWM_MATRIX.md and
	// docs/CURRENT_BUDGET.md -- re-enable only after confirming this
	// wasn't the cause.
	LEDPWM->CR.CTRL = LEDPWM_CTRL(
		LEDPWM_LED_ENABLE | LEDPWM_IE_INHIBIT | LEDPWM_PWM_OE_CLR,
		0,
		LEDPWM_CLK_FREQ( 1 ) | LEDPWM_INTEN_CYC( 2 ), // reverted: 8-bit caused visible flicker (confirmed not the real fix)
		LEDPWM_INHIBIT( 15 ) | LEDPWM_PWM_REPEAT( 0 ) );

	// Prime row 0 by hand (this also sets PWM_OE) so the first display
	// cycle, before the first interrupt fires, already has valid data
	// queued. Calls the plain helper, not the ISR itself -- see the note
	// above ledpwm_load_row().
	s_next_row = 0;
	ledpwm_load_row( 0 );

	NVIC_EnableIRQ( LEDPWM_IRQn );
}

// Bring-up debugging: dumps every live LEDPWM register (all four atomic
// 32-bit groups, the read-only status/counter bytes, the four analog trim
// bytes, the live DMA pointer, this driver's own scan-state bytes) plus the
// last row packed into s_row_dma_buf, for the manual register-control panel
// in webui/regpanel.js and for checking hardware behaviour directly over
// WebUSB instead of inferring it from how LEDs look through a camera. See
// webusb_protocol.c's CMD_DEBUG_DUMP and docs/WEBUSB_PROTOCOL.md for the
// exact wire layout (fixed LEDMATRIX_DEBUG_DUMP_LEN bytes, no length
// prefix -- WebUSB_SendPendingReply() only ever transmits the body).
// Returns the number of bytes written to out (always
// LEDMATRIX_DEBUG_DUMP_LEN).
//
// Read racing the scan ISR: this can be called while LEDPWM_IRQHandler is
// mid-update (it's not disabled here), so a snapshot can show, e.g., DMA
// already pointing at the next row while COM_SEL still reflects the
// previous one. Call it repeatedly and look for whether values are
// consistently stuck vs. genuinely cycling, rather than trusting any one
// snapshot.
uint8_t LEDMatrix_DebugDump( uint8_t *out )
{
	uint8_t i = 0;
	// SWITCH group (0x00-0x03): COM_SEL, KEY_CTRL, PWM_OE, DMA_CNT
	out[i++] = LEDPWM->SW.COM_SEL;
	out[i++] = LEDPWM->SW.KEY_CTRL;
	out[i++] = LEDPWM->SW.PWM_OE;
	out[i++] = LEDPWM->SW.DMA_CNT;
	// COM_E group (0x04-0x07): COM_E_PB_L/M/H, COM_E_PAPC
	out[i++] = LEDPWM->CE.COM_E_PB_L;
	out[i++] = LEDPWM->CE.COM_E_PB_M;
	out[i++] = LEDPWM->CE.COM_E_PB_H;
	out[i++] = LEDPWM->CE.COM_E_PAPC;
	// PWM_PIN group (0x08-0x0B): PWM_PIN_L, PWM_PIN_H, reserved, PWM_MOD
	out[i++] = LEDPWM->PP.PWM_PIN_L;
	out[i++] = LEDPWM->PP.PWM_PIN_H;
	out[i++] = LEDPWM->PP.RESERVED_0A;
	out[i++] = LEDPWM->PP.PWM_MOD;
	// CTRL group (0x0C-0x0F): FUNC_CTRL, ANA_CTRL, CYCLE_CFG, FRAME_CFG
	out[i++] = LEDPWM->CR.FUNC_CTRL;
	out[i++] = LEDPWM->CR.ANA_CTRL;
	out[i++] = LEDPWM->CR.CYCLE_CFG;
	out[i++] = LEDPWM->CR.FRAME_CFG;
	// Read-only status/counters (0x10-0x13)
	out[i++] = LEDPWM->DISP_STAT;
	out[i++] = LEDPWM->INTEN_CNT;
	out[i++] = LEDPWM->COLOR_CNT;
	out[i++] = LEDPWM->FRAME_STA;
	// Analog trim bytes (0x14-0x17)
	out[i++] = LEDPWM->INTEN_ADJ;
	out[i++] = LEDPWM->RED_ADJ;
	out[i++] = LEDPWM->GREEN_ADJ;
	out[i++] = LEDPWM->BLUE_ADJ;
	// Live DMA pointer (0x18-0x19, read-only here)
	out[i++] = (uint8_t)( LEDPWM->DMA & 0xFF );
	out[i++] = (uint8_t)( LEDPWM->DMA >> 8 );
	// This driver's own scan-state, not LEDPWM registers
	out[i++] = s_last_loaded_row;
	out[i++] = s_next_row;
	out[i++] = (uint8_t)( s_frozen_row >= 0 ? s_frozen_row : 0xFF );
	// Last row packed into the DMA SRAM buffer (8 words x 4 bytes)
	for( unsigned g = 0; g < LED_MATRIX_COLS / 3; g++ )
	{
		uint32_t word = s_row_dma_buf[g];
		out[i++] = (uint8_t)( word & 0xFF );
		out[i++] = (uint8_t)( ( word >> 8 ) & 0xFF );
		out[i++] = (uint8_t)( ( word >> 16 ) & 0xFF );
		out[i++] = (uint8_t)( ( word >> 24 ) & 0xFF );
	}
	return i;
}

// Bring-up debugging: writes one of the four atomic 32-bit register groups
// directly (see ch643_ledpwm.h -- SWITCH/COM_E/PWM_PIN/CTRL are each one
// combined register on real silicon, and writing them as separate bytes
// was confirmed on hardware not to reliably latch -- "Hardware bring-up:
// only the first word loads" in docs/LEDPWM_MATRIX.md). group is 0=SWITCH,
// 1=COM_E, 2=PWM_PIN, 3=CTRL; value's bytes map to that group's struct
// fields low-to-high (byte0 = the 0x00/0x04/0x08/0x0C field, byte3 = the
// 0x03/0x07/0x0B/0x0F field). No bounds/sanity checking -- this is a raw
// register poke for manual exploration, the same as CMD_RAW_TEST/
// CMD_SET_PWM_PIN already were for PWM_PIN specifically.
void LEDMatrix_WriteReg32( uint8_t group, uint32_t value )
{
	switch( group )
	{
	case 0: LEDPWM->SW.SWITCH = value; break;
	case 1: LEDPWM->CE.COM_E = value; break;
	case 2: LEDPWM->PP.PWM_PIN = value; break;
	case 3: LEDPWM->CR.CTRL = value; break;
	default: break;
	}
}

// Bring-up debugging: writes one of the four independent analog trim
// bytes (0x14-0x17), the only read-write LEDPWM registers that aren't part
// of one of the four atomic 32-bit groups above. offset is the register's
// real address offset (0x14-0x17); anything else is a no-op.
void LEDMatrix_WriteReg8( uint8_t offset, uint8_t value )
{
	switch( offset )
	{
	case 0x14: LEDPWM->INTEN_ADJ = value; break;
	case 0x15: LEDPWM->RED_ADJ = value; break;
	case 0x16: LEDPWM->GREEN_ADJ = value; break;
	case 0x17: LEDPWM->BLUE_ADJ = value; break;
	default: break;
	}
}
