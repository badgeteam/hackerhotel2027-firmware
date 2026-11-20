// LEDPWM-driven test patterns for the starfish board's LED matrix,
// selectable with the four front-panel buttons. See README.md and
// docs/LEDPWM_MATRIX.md for the hardware background.
#include "ch32fun.h"
#include "led_matrix.h"
#include "webusb_protocol.h"
#include "pattern_flash.h"
#include "pattern_player.h"

#define TICK_MS 5

// Shared with webusb_protocol.c (see webusb_protocol.h): which of the four
// built-in patterns is showing, and whether the matrix should be running
// one of those at all or just displaying whatever was last uploaded over
// WebUSB.
volatile uint8_t g_app_mode = APP_MODE_BUILTIN;
volatile uint8_t g_app_pattern = 0;

// BTN2/BTN3 (PC4/PC5) fit ch32fun's normal 0-15-per-port pin encoding;
// BTN1/BTN4 (PB20/PB18) don't (see led_matrix.c), so they're read directly.
// BTN1 isn't in here -- it's not a matrix-pattern button any more, see
// "Power button / standby mode" below.
#define BTN2_PIN PC4
#define BTN3_PIN PC5

typedef struct
{
	int ( *read_raw )( void ); // returns 1 when pressed
	uint8_t debounce_count;
	uint8_t pressed;
} Button;

static int btn1_read_raw( void ) { return !( ( GPIOB->INDR >> 20 ) & 1 ); }
static int btn2_read_raw( void ) { return !funDigitalRead( BTN2_PIN ); }
static int btn3_read_raw( void ) { return !funDigitalRead( BTN3_PIN ); }
static int btn4_read_raw( void ) { return !( ( GPIOB->INDR >> 18 ) & 1 ); }

static Button s_buttons[3] =
{
	{ btn2_read_raw, 0, 0 },
	{ btn3_read_raw, 0, 0 },
	{ btn4_read_raw, 0, 0 },
};

#define DEBOUNCE_TICKS 3 // 3 * TICK_MS = 15ms of stable level before it counts

// Returns a bitmask of buttons that were freshly pressed this tick
// (bit0=BTN2, bit1=BTN3, bit2=BTN4).
static unsigned buttons_poll( void )
{
	unsigned pressed_edges = 0;
	for( int i = 0; i < 3; i++ )
	{
		Button *b = &s_buttons[i];
		if( b->read_raw() )
		{
			if( b->debounce_count < DEBOUNCE_TICKS )
				b->debounce_count++;
		}
		else
		{
			b->debounce_count = 0;
			b->pressed = 0;
		}

		if( b->debounce_count == DEBOUNCE_TICKS && !b->pressed )
		{
			b->pressed = 1;
			pressed_edges |= ( 1u << i );
		}
	}
	return pressed_edges;
}

static void buttons_init( void )
{
	// BTN2/BTN3: PC4/PC5, normal pin range.
	funPinMode( BTN2_PIN, GPIO_CFGLR_IN_PUPD );
	funPinMode( BTN3_PIN, GPIO_CFGLR_IN_PUPD );
	GPIOC->OUTDR |= ( 1 << 4 ) | ( 1 << 5 ); // select pull-up (vs. pull-down)

	// BTN1/BTN4: PB20/PB18, need the extended CFGXR register directly.
	GPIOB->CFGXR = ( GPIOB->CFGXR & ~( 0xFu << ( 4 * ( 20 - 16 ) ) ) ) | ( (uint32_t)GPIO_CFGLR_IN_PUPD << ( 4 * ( 20 - 16 ) ) );
	GPIOB->CFGXR = ( GPIOB->CFGXR & ~( 0xFu << ( 4 * ( 18 - 16 ) ) ) ) | ( (uint32_t)GPIO_CFGLR_IN_PUPD << ( 4 * ( 18 - 16 ) ) );
	GPIOB->OUTDR |= ( 1 << 20 ) | ( 1 << 18 ); // select pull-up
}

// --- Power button / standby mode ---------------------------------------
// BTN1 (PB20) used to be "clear matrix" (see buttons_init() above, which
// still configures its GPIO the same way -- just read directly here
// instead of through the Button/buttons_poll() machinery, since this
// needs its own release-debounce state that the other three buttons
// don't). A full, debounced press-then-release of BTN1 now puts the chip
// into Standby mode (CH643RM.PDF 2.3.4) -- lowest power, every clock
// stopped -- woken back up by EXTI20 (also BTN1, configured below) firing
// a full chip reset. See docs/POWER_MANAGEMENT.md for the full rationale
// (why "press and release" rather than "press", and why the arming logic
// below is needed at all).
#define RCC_LPWRRSTF_BIT ( 1u << 31 ) // RCC_LPWRRSTF; ch32x03xhw.h only defines the 0x80000000 hex form
#define RCC_RMVF_BIT     ( 1u << 24 ) // RCC_RMVF

// True for exactly one boot: the one right after Standby mode was woken
// up by EXTI (as opposed to a power-on or externally-triggered reset).
// Read once in main() before RCC_RMVF clears the flag for next time.
static int s_woke_from_standby;

// Whether a BTN1 press-release cycle is currently allowed to trigger
// Standby. Starts false after waking from Standby (the press that woke
// the chip is, physically, very likely still being held at this exact
// instant -- main() resumes at the top, same as any other reset, with no
// memory of "this press already did its job") and only becomes true once
// a full, debounced *release* of BTN1 has been observed -- i.e. the user
// has to let go at least once before another press-release can arm a new
// standby-entry. A cold boot (power-on, not a standby wake) has no such
// ambiguity and starts armed immediately.
static int s_power_btn_armed;
static int s_power_pressed_debounced;
static int s_power_pending_sleep; // debounced press seen while armed; waiting for the matching debounced release
static uint8_t s_power_press_debounce;
static uint8_t s_power_release_debounce;

// Ignore BTN1 entirely for this many ticks right after boot (any boot,
// not just a standby-wake) -- a mechanical switch's contact bounce can
// easily make the raw pin level flicker released/pressed/released for a
// few ms around the moment of either a press or a release, and that's
// long enough to fool the 15ms (DEBOUNCE_TICKS) debounce window into
// seeing a spurious extra press-release cycle right as the chip boots.
// Concretely, this was confirmed as the remaining cause of "it woke up,
// but releasing the button put it right back to sleep" even after
// s_power_btn_armed was already seeded correctly from BTN1's live level
// at boot (bounce around the wake-triggering press/release could still
// slip a fresh press+release through the debouncer while already
// armed). 50 ticks * TICK_MS(5ms) = 250ms, generous next to a typical
// tactile switch's sub-10ms bounce tail.
#define POWER_BOOT_GRACE_TICKS 50
static uint8_t s_power_boot_grace;

// Bring-up debugging: lets CMD_DEBUG_ENTER_STANDBY trigger the exact
// same entry path a real BTN1 press-release would, without needing
// physical access to the button -- e.g. so EXTI_SWIEVR (CH643RM.PDF
// 7.5.1.5, a software-triggered EXTI event) can exercise the wake path
// over SWD too, testing the whole enter+wake cycle with nothing but a
// debug probe.
volatile int g_debug_force_standby = 0;

// Bring-up debugging: snapshot of the boot-time reset-cause decision, so
// it can be inspected after a real standby-wake reset instead of
// guessing from symptoms alone -- see CMD_DEBUG_BOOT_INFO.
volatile uint32_t g_debug_boot_rstsckr = 0;
volatile int g_debug_boot_woke = 0;
volatile int g_debug_boot_armed = 0;

// Bring-up debugging: live power_poll() state, polled repeatedly from
// the host while physically operating BTN1, to see the exact sequence
// of state transitions in real time instead of only before/after
// snapshots -- see CMD_DEBUG_POWER_STATE.
int Power_DebugState( uint8_t *out )
{
	out[0] = (uint8_t)s_power_boot_grace;
	out[1] = (uint8_t)s_power_btn_armed;
	out[2] = (uint8_t)s_power_pending_sleep;
	out[3] = (uint8_t)s_power_pressed_debounced;
	out[4] = s_power_press_debounce;
	out[5] = s_power_release_debounce;
	out[6] = (uint8_t)btn1_read_raw();
	return 7;
}

static void enter_standby( void )
{
	// Blank the display before cutting power -- otherwise whatever was
	// last displayed stays burned into the viewer's eyes/the photo
	// they're about to take of a "sleeping" badge that still looks lit.
	LEDMatrix_Clear();

	// Standby (CH643RM.PDF 2.3.4): SLEEPDEEP=1 + PDDS=1, then WFI. This
	// stops essentially every peripheral clock in hardware, so there's
	// no separate "turn off peripherals" step needed here -- entering
	// Standby itself is that step. Wakes via a full chip reset (not a
	// resume-in-place the way Stop mode would) -- main() runs again from
	// the top; the WFI below never actually returns.
	PWR->CTLR |= ( 1u << 1 ); // PDDS=1: Standby, not Stop
	NVIC->SCTLR |= ( 1u << 2 ); // SLEEPDEEP=1 (PFIC_SCTLR, CH643RM.PDF 7.5.2.38)
	__WFI();
}

// EXTI20 (PB20/BTN1) wake source. CH643RM.PDF 8.3.2.3's prose claims
// AFIO_EXTICR2 uses a different 2-bit encoding than EXTICR1 for lines
// 16-23 (00=PA, 10=PB, 11=PC) -- that's wrong (garbled PDF table
// extraction, verified against the vendor SDK's GPIO_EXTILineConfig(),
// ch643_gpio.c, which drives AFIO->EXTICR1/2 through one shared array
// with one shared encoding for all 24 lines: GPIO_PortSourceGPIOx is
// 0/1/2 for A/B/C, ch643_gpio.h). Using the manual's claimed "10" for
// PB here was silently routing EXTI20 to PC20 instead of PB20 --
// nothing is wired there, so EXTI's pending flag never latched on a
// real BTN1 press no matter how correct every other register looked
// (confirmed by CMD_DEBUG_EXTI_COUNT staying at 0 and EXTI->INTFR
// staying clear, via CMD_DEBUG_POWER_DUMP, after real presses). Value
// 0b01 selects PB.
static void power_init( void )
{
	g_debug_boot_rstsckr = RCC->RSTSCKR; // raw value, before anything below touches it
	s_woke_from_standby = ( RCC->RSTSCKR & RCC_LPWRRSTF_BIT ) != 0;
	g_debug_boot_woke = s_woke_from_standby;
	// RMVF (CH643RM.PDF 3.4.13) is a level-sensitive clear control ("1:
	// clear the reset flag"), not a self-clearing pulse -- holding it at
	// 1 keeps suppressing every reset-cause flag, including LPWRRSTF, on
	// every future reset too. Leaving this as a bare |= (never cleared
	// back to 0, as the vendor's own OPA_Reset example does: set then
	// immediately clear bit24) meant s_woke_from_standby could never
	// read true again after the first boot -- every standby-wake looked
	// like a cold boot, so s_power_btn_armed started true immediately
	// instead of waiting for a release, and letting go of the very
	// button that woke the chip re-armed and re-triggered Standby right
	// away (confirmed: "it woke up, but when I released the button it
	// turned off again").
	RCC->RSTSCKR |= RCC_RMVF_BIT; // clear reset-cause flags for next boot
	RCC->RSTSCKR &= ~RCC_RMVF_BIT; // ...and release the clear control again

	// AFIO and PWR are both clock-gated peripherals, off by default --
	// ch32fun's SystemInit() only enables GPIOA/B/D + USART1/ADC1.
	// Without RCC_AFIOEN, the AFIO->EXTICR2 write below silently doesn't
	// latch (EXTI line 20 never actually gets routed to PB20, so nothing
	// can ever wake the chip back up); without RCC_PWREN, PWR->CTLR's
	// PDDS write in enter_standby() silently doesn't latch either (the
	// chip falls into Stop mode instead of Standby -- still low-power
	// enough to go dark, but CH643RM.PDF 2.3.3 explicitly keeps SRAM,
	// registers, and IO pin state intact, which is consistent with what
	// was observed on real hardware: the matrix went dark on the first
	// BTN1 press, but the USB D+ pull-up stayed asserted the whole time
	// instead of the chip actually losing state, and a second press
	// never woke it since the EXTI routing never took effect either).
	RCC->APB2PCENR |= RCC_AFIOEN;
	RCC->APB1PCENR |= RCC_PWREN;

	AFIO->EXTICR2 = ( AFIO->EXTICR2 & ~( 0x3u << 8 ) ) | ( 0x1u << 8 ); // EXTI20 -> PB20
	EXTI->FTENR |= ( 1u << 20 ); // wake on the falling edge (idle high via pull-up, press pulls low)
	EXTI->RTENR &= ~( 1u << 20 );
	EXTI->INTFR = ( 1u << 20 ); // clear any stale pending flag before enabling
	EXTI->INTENR |= ( 1u << 20 );

	// TEMPORARY bring-up diagnostic: also wire BTN2 (PC4/EXTI4), BTN3
	// (PC5/EXTI5), BTN4 (PB18/EXTI18) as wake sources (not sleep
	// triggers -- that's still BTN1-only, see power_poll()), so waking
	// via any of the four buttons can be tested independently of
	// BTN1's dual role. Same GPIO_PortSourceGPIOx encoding as above:
	// A/B/C = 0/1/2, 2-bit field per line, shift = 2*(pin&0xF).
	AFIO->EXTICR1 = ( AFIO->EXTICR1 & ~( 0x3u << 8 ) ) | ( 0x2u << 8 ); // EXTI4 -> PC4
	AFIO->EXTICR1 = ( AFIO->EXTICR1 & ~( 0x3u << 10 ) ) | ( 0x2u << 10 ); // EXTI5 -> PC5
	AFIO->EXTICR2 = ( AFIO->EXTICR2 & ~( 0x3u << 4 ) ) | ( 0x1u << 4 ); // EXTI18 -> PB18
	EXTI->FTENR |= ( 1u << 4 ) | ( 1u << 5 ) | ( 1u << 18 );
	EXTI->RTENR &= ~( ( 1u << 4 ) | ( 1u << 5 ) | ( 1u << 18 ) );
	EXTI->INTFR = ( 1u << 4 ) | ( 1u << 5 ) | ( 1u << 18 );
	EXTI->INTENR |= ( 1u << 4 ) | ( 1u << 5 ) | ( 1u << 18 );
	// EXTI18 shares EXTI25_16_IRQn (already enabled below for EXTI20).
	// EXTI4/EXTI5 need EXTI7_0_IRQn (20) instead -- IENR[0], same
	// not-destructive OR pattern.
	NVIC->IENR[EXTI7_0_IRQn >> 5] |= ( 1u << ( EXTI7_0_IRQn & 0x1F ) );
	// Not NVIC_EnableIRQ(EXTI25_16_IRQn): that helper does a destructive
	// NVIC->IENR[n] = (1<<bit) assignment, not a set-bit -- and
	// EXTI25_16_IRQn (41) shares IENR[1] with USBFS_IRQn (45).
	// FSUSBSetup() (fsusb.c) enables its own interrupt the same
	// destructive way, which would silently clear *this* enable bit
	// right back out if it ran afterwards -- EXTI's own INTENR above was
	// (and stayed) correctly set either way, so the peripheral always
	// detected every BTN1 press and latched its pending flag fine, but
	// without the NVIC/PFIC enable bit surviving, the interrupt never
	// actually got delivered to the core to wake it. This was confirmed
	// via CMD_DEBUG_POWER_DUMP as the reason Standby entry worked
	// perfectly (genuine deep sleep, USB dropped, SWD unreachable) but
	// real BTN1 presses never woke it back up: NVIC->IENR[1] read back
	// as 0 during normal runtime even with this |= in place, because
	// main() used to call WebUSB_Init() (-> FSUSBSetup() -> the
	// destructive enable) *after* power_init(). This write is a correct
	// set-bit, but it still needs to run *after* FSUSBSetup() to be the
	// one that's still standing -- see the call order in main().
	NVIC->IENR[EXTI25_16_IRQn >> 5] |= ( 1u << ( EXTI25_16_IRQn & 0x1F ) );

	// Always start disarmed and let POWER_BOOT_GRACE_TICKS +
	// power_poll()'s normal debounce logic arm it once BTN1's level has
	// had time to settle -- not from RCC_RSTSCKR's LPWRRSTF: confirmed
	// via CMD_DEBUG_BOOT_INFO that LPWRRSTF reads 0 right after a
	// genuine standby-wake reset (SWD-unreachable, USB dropped, then
	// woken by a real BTN1 press) -- either this chip doesn't set it the
	// way CH643RM.PDF 3.4.13 describes, or "low-power management reset"
	// means something other than a Standby EXTI wake (the EXTICR
	// encoding bug earlier this session is a reminder this manual's
	// prose isn't always trustworthy). A cold boot starting disarmed
	// just costs one harmless release-then-press-again before the first
	// sleep-entry is accepted, same as a standby-wake always required.
	s_power_btn_armed = 0;
	g_debug_boot_armed = s_power_btn_armed;
	s_power_pressed_debounced = 0;
	s_power_pending_sleep = 0;
	s_power_press_debounce = 0;
	s_power_release_debounce = 0;
	s_power_boot_grace = POWER_BOOT_GRACE_TICKS;
}

// Only needs to clear the pending flag -- waking the core from WFI is
// the entire point, there's no other work to do in the ISR, and Standby
// wakes via a full reset anyway so nothing here survives past it.
// g_debug_exti_count: bring-up debugging only (CMD_DEBUG_EXTI_COUNT) --
// lets the EXTI/AFIO routing be verified independently of Standby itself
// (does the interrupt fire *at all* on a BTN1 press during normal
// operation?) before trusting it as a wake source.
volatile uint32_t g_debug_exti_count = 0;

void EXTI25_16_IRQHandler( void ) __attribute__((interrupt));
void EXTI25_16_IRQHandler( void )
{
	EXTI->INTFR = ( 1u << 20 ) | ( 1u << 18 ); // BTN1 and BTN4 (diagnostic) share this vector
	g_debug_exti_count++;
}

// TEMPORARY bring-up diagnostic: BTN2/BTN3 wake sources (see power_init()).
void EXTI7_0_IRQHandler( void ) __attribute__((interrupt));
void EXTI7_0_IRQHandler( void )
{
	EXTI->INTFR = ( 1u << 4 ) | ( 1u << 5 );
	g_debug_exti_count++;
}

static void power_poll( void )
{
	if( s_power_boot_grace > 0 )
	{
		s_power_boot_grace--;
		return;
	}

	if( btn1_read_raw() )
	{
		s_power_release_debounce = 0;
		if( s_power_press_debounce < DEBOUNCE_TICKS )
			s_power_press_debounce++;
		if( s_power_press_debounce == DEBOUNCE_TICKS && !s_power_pressed_debounced )
		{
			s_power_pressed_debounced = 1;
			if( s_power_btn_armed )
				s_power_pending_sleep = 1;
		}
	}
	else
	{
		s_power_press_debounce = 0;
		s_power_pressed_debounced = 0;
		if( s_power_release_debounce < DEBOUNCE_TICKS )
			s_power_release_debounce++;
		if( s_power_release_debounce == DEBOUNCE_TICKS )
		{
			if( !s_power_btn_armed )
			{
				// The post-wake "let go of the button that woke us up"
				// release -- now it's safe to treat a future press-release
				// as a real request to sleep again.
				s_power_btn_armed = 1;
			}
			else if( s_power_pending_sleep )
			{
				s_power_pending_sleep = 0;
				enter_standby();
			}
		}
	}
}

// --- Test patterns -----------------------------------------------------------
// Each pattern gets a frame counter that resets to 0 whenever it's
// (re)selected, so switching patterns always restarts the animation cleanly.

#define PATTERN_ALL_ON      0
#define PATTERN_COLUMN_CHASE 1
#define PATTERN_ROW_CHASE   2
#define PATTERN_BREATHE     3
#define PATTERN_TEST_DEMO   4

static void pattern_all_on( unsigned frame )
{
	(void)frame;
	LEDMatrix_Fill( 180 );
}

static void pattern_column_chase( unsigned frame )
{
	unsigned active = ( frame / 4 ) % LED_MATRIX_COLS;
	for( unsigned col = 0; col < LED_MATRIX_COLS; col++ )
		for( unsigned row = 0; row < LED_MATRIX_ROWS; row++ )
			LEDMatrix_SetPixel( row, col, col == active ? 255 : 0 );
}

static void pattern_row_chase( unsigned frame )
{
	unsigned active = ( frame / 4 ) % LED_MATRIX_ROWS;
	for( unsigned row = 0; row < LED_MATRIX_ROWS; row++ )
		for( unsigned col = 0; col < LED_MATRIX_COLS; col++ )
			LEDMatrix_SetPixel( row, col, row == active ? 255 : 0 );
}

// Triangle wave, 0..255..0 over `period` ticks total (so ~period/2 ticks
// each way). Shared by pattern_breathe() and pattern_test_demo() below.
static uint8_t triangle_wave_255( unsigned frame, unsigned period )
{
	unsigned half = period / 2;
	unsigned phase = frame % period;
	return (uint8_t)( ( phase < half ) ? phase : ( period - 1 - phase ) );
}

static void pattern_breathe( unsigned frame )
{
	// ~256 ticks each way (~2.5s total at TICK_MS=5).
	LEDMatrix_Fill( triangle_wave_255( frame, 512 ) );
}

// --- Startup demo: breathe + RGB-only + chase, over the star's RGB LEDs ----
// See docs/LEDPWM_MATRIX.md / led_matrix.h for the matrix's col 0-7/8-15/
// 16-23 DMA-packing layout -- that's about how one row's 24 brightness
// bytes get shipped to the LEDPWM peripheral, and has nothing to do with
// which (row, col) cells have an actual red/green/blue LED soldered on
// them. That's a board-layout fact, not derivable from led_matrix.c at
// all; the table below is copied from webui/board_data.js's `rgb_leds`
// (generated from the real KiCad layout by tools/extract_board_data.py --
// see README.md's "Matrix coverage"), the authoritative source for it.
//
// The board's 35 real 4-pin RGB LEDs are the "star" points fanned out
// around the mono/white-LED center cluster: they occupy columns 17-23
// exclusively (confirmed against webui/board_data.js: every one of the
// matrix's 384 cells is covered exactly once between the 279 mono LEDs
// at cols 0-16 and these 35 RGB LEDs' 3 cells each at cols 17-23, with no
// overlap). For one RGB LED, `col` is its shared anode (fixed), and
// red_row/green_row/blue_row are its three separate cathode rows.
//
// Ordered column-major (col 17 = star tip .. col 23 = nearest the
// center), then row-major within a column (red_row 0, 3, 6, 9, 12 = the
// star's 5 points) -- so stepping through this array in order sweeps
// ring-by-ring around the star (one step along all 5 points, then the
// next step in) rather than tip-to-base along one point at a time.
typedef struct
{
	uint8_t col;
	uint8_t red_row;
	uint8_t green_row;
	uint8_t blue_row;
} RgbLedPos;

static const RgbLedPos s_rgb_leds[] =
{
	{ 17, 0, 1, 2 }, { 17, 3, 4, 5 }, { 17, 6, 7, 8 }, { 17, 9, 10, 11 }, { 17, 12, 13, 14 },
	{ 18, 0, 1, 2 }, { 18, 3, 4, 5 }, { 18, 6, 7, 8 }, { 18, 9, 10, 11 }, { 18, 12, 13, 14 },
	{ 19, 0, 1, 2 }, { 19, 3, 4, 5 }, { 19, 6, 7, 8 }, { 19, 9, 10, 11 }, { 19, 12, 13, 14 },
	{ 20, 0, 1, 2 }, { 20, 3, 4, 5 }, { 20, 6, 7, 8 }, { 20, 9, 10, 11 }, { 20, 12, 13, 14 },
	{ 21, 0, 1, 2 }, { 21, 3, 4, 5 }, { 21, 6, 7, 8 }, { 21, 9, 10, 11 }, { 21, 12, 13, 14 },
	{ 22, 0, 1, 2 }, { 22, 3, 4, 5 }, { 22, 6, 7, 8 }, { 22, 9, 10, 11 }, { 22, 12, 13, 14 },
	{ 23, 0, 1, 2 }, { 23, 3, 4, 5 }, { 23, 6, 7, 8 }, { 23, 9, 10, 11 }, { 23, 12, 13, 14 },
};
#define RGB_LED_COUNT ( sizeof( s_rgb_leds ) / sizeof( s_rgb_leds[0] ) )

#define RGB_RED   0x1u
#define RGB_GREEN 0x2u
#define RGB_BLUE  0x4u

// Sets every RGB LED's channel(s) named in color_mask to `level`, the
// rest of that LED's channels to 0. Doesn't touch the mono/white cells
// (cols 0-16) at all -- see white_leds_set() for those.
static void rgb_leds_set_color( unsigned color_mask, uint8_t level )
{
	for( unsigned i = 0; i < RGB_LED_COUNT; i++ )
	{
		const RgbLedPos *p = &s_rgb_leds[i];
		LEDMatrix_SetPixel( p->red_row,   p->col, ( color_mask & RGB_RED )   ? level : 0 );
		LEDMatrix_SetPixel( p->green_row, p->col, ( color_mask & RGB_GREEN ) ? level : 0 );
		LEDMatrix_SetPixel( p->blue_row,  p->col, ( color_mask & RGB_BLUE )  ? level : 0 );
	}
}

// Sets every mono/white LED to `level`, leaving the 35 RGB LEDs' cells at
// 0 -- callers that want color on them too call rgb_leds_set_color()
// right after. Simplest correct way to hit "every cell that isn't one of
// s_rgb_leds's three channels" without a second lookup table: fill
// everything, then punch the known RGB cells back to 0.
static void white_leds_set( uint8_t level )
{
	LEDMatrix_Fill( level );
	rgb_leds_set_color( 0, 0 );
}

// Stage durations, in ticks (TICK_MS=5ms each): a couple of whole-matrix
// breathe cycles, then the RGB LEDs breathing red/green/blue each with
// and without the white LEDs along, then a chase over just the RGB LEDs
// (one lit at a time, ring-by-ring per s_rgb_leds's order) in red, green,
// blue, and the four two/three-way combinations, before looping back to
// the start.
#define DEMO_BREATHE_PERIOD     512u // one full fade up+down -- matches pattern_breathe()
#define DEMO_STARTUP_FRAMES     ( 2u * DEMO_BREATHE_PERIOD ) // "a couple of times"

#define DEMO_RGB_BREATHE_PHASES 6u // red+white, red, green+white, green, blue+white, blue
#define DEMO_RGB_BREATHE_FRAMES ( DEMO_RGB_BREATHE_PHASES * DEMO_BREATHE_PERIOD )

#define DEMO_CHASE_STEP_FRAMES  4u // per-LED step speed -- matches pattern_column_chase()/pattern_row_chase()
#define DEMO_CHASE_COMBOS       7u // R, G, B, R+G, R+B, G+B, R+G+B
#define DEMO_CHASE_COMBO_FRAMES ( RGB_LED_COUNT * DEMO_CHASE_STEP_FRAMES )
#define DEMO_CHASE_FRAMES       ( DEMO_CHASE_COMBOS * DEMO_CHASE_COMBO_FRAMES )

#define DEMO_TOTAL_FRAMES ( DEMO_STARTUP_FRAMES + DEMO_RGB_BREATHE_FRAMES + DEMO_CHASE_FRAMES )

static const uint8_t s_demo_breathe_mask[DEMO_RGB_BREATHE_PHASES] =
{
	RGB_RED, RGB_RED, RGB_GREEN, RGB_GREEN, RGB_BLUE, RGB_BLUE,
};
static const uint8_t s_demo_breathe_white[DEMO_RGB_BREATHE_PHASES] =
{
	1, 0, 1, 0, 1, 0,
};
static const uint8_t s_demo_chase_mask[DEMO_CHASE_COMBOS] =
{
	RGB_RED, RGB_GREEN, RGB_BLUE,
	RGB_RED | RGB_GREEN, RGB_RED | RGB_BLUE, RGB_GREEN | RGB_BLUE,
	RGB_RED | RGB_GREEN | RGB_BLUE,
};

static void pattern_test_demo( unsigned frame )
{
	unsigned t = frame % DEMO_TOTAL_FRAMES;

	if( t < DEMO_STARTUP_FRAMES )
	{
		LEDMatrix_Fill( triangle_wave_255( t, DEMO_BREATHE_PERIOD ) );
		return;
	}
	t -= DEMO_STARTUP_FRAMES;

	if( t < DEMO_RGB_BREATHE_FRAMES )
	{
		unsigned phase = t / DEMO_BREATHE_PERIOD;
		uint8_t level = triangle_wave_255( t % DEMO_BREATHE_PERIOD, DEMO_BREATHE_PERIOD );
		white_leds_set( s_demo_breathe_white[phase] ? level : 0 );
		rgb_leds_set_color( s_demo_breathe_mask[phase], level );
		return;
	}
	t -= DEMO_RGB_BREATHE_FRAMES;

	unsigned combo = t / DEMO_CHASE_COMBO_FRAMES;
	unsigned active = ( t % DEMO_CHASE_COMBO_FRAMES ) / DEMO_CHASE_STEP_FRAMES;
	const RgbLedPos *p = &s_rgb_leds[active];
	unsigned mask = s_demo_chase_mask[combo];

	LEDMatrix_Clear();
	LEDMatrix_SetPixel( p->red_row,   p->col, ( mask & RGB_RED )   ? 255 : 0 );
	LEDMatrix_SetPixel( p->green_row, p->col, ( mask & RGB_GREEN ) ? 255 : 0 );
	LEDMatrix_SetPixel( p->blue_row,  p->col, ( mask & RGB_BLUE )  ? 255 : 0 );
}

typedef void ( *PatternFn )( unsigned frame );
static const PatternFn s_patterns[5] =
{
	pattern_all_on,
	pattern_column_chase,
	pattern_row_chase,
	pattern_breathe,
	pattern_test_demo,
};

int main()
{
	SystemInit();
	funGpioInitAll();

	LEDMatrix_Init();
	buttons_init();
	// WebUSB_Init() before power_init(): FSUSBSetup() enables its own IRQ
	// via ch32fun's NVIC_EnableIRQ(), which (see power_init()'s comment)
	// does a destructive NVIC->IENR[n] = (1<<bit) write rather than a
	// set-bit -- and USBFS_IRQn (45) shares IENR[1] with EXTI25_16_IRQn
	// (41). Confirmed via CMD_DEBUG_POWER_DUMP that the previous order
	// (power_init() first) left NVIC->IENR[1] == 0 by the time the main
	// loop was running: FSUSBSetup()'s destructive write happened second
	// and silently wiped the EXTI enable bit power_init() had just set.
	// power_init()'s own write is a real |=, so doing it last is enough --
	// no need to touch the vendored fsusb.c.
	WebUSB_Init();
	power_init();

	// Boot into the startup demo (breathe + RGB-only + chase, see
	// pattern_test_demo() above) rather than pattern 0 -- overridden right
	// below if a flash pattern takes over instead.
	g_app_pattern = PATTERN_TEST_DEMO;

	// Autoplay: if a pattern was uploaded to flash in an earlier session
	// (see pattern_flash.h/CMD_PATTERN_COMMIT), play it on repeat from
	// boot instead of the default built-in pattern -- works with no host
	// connected at all, including right after waking from Standby.
	if( PatternFlash_IsValid() )
	{
		PatternPlayer_Reset();
		g_app_mode = APP_MODE_FLASH_PATTERN;
	}

	uint8_t last_pattern = g_app_pattern;
	unsigned frame = 0;

	// Bring-up debugging remap (see docs/LEDPWM_MATRIX.md): front-panel
	// buttons drive the same row/column test aids as the WebUSB
	// TEST_ROW/TEST_COL commands, for fast iteration without a host
	// connected. BTN2=full on, BTN3/BTN4 step s_test_col backward/forward
	// through TEST_COL (BTN1 is the power button, see above). Built-in
	// patterns are still reachable over WebUSB (SELECT_BUILTIN), just not
	// from the buttons while this remap is in place.
	int s_test_col = 0;

	while( 1 )
	{
		Delay_Ms( TICK_MS );

		WebUSB_SendPendingReply();
		if( g_debug_force_standby )
		{
			g_debug_force_standby = 0;
			enter_standby();
		}
		power_poll();

		unsigned pressed = buttons_poll();
		if( pressed & ( 1u << 0 ) ) // BTN2
		{
			LEDMatrix_Fill( 255 );
			g_app_mode = APP_MODE_CUSTOM;
		}
		if( pressed & ( 1u << 1 ) ) // BTN3
		{
			s_test_col = ( s_test_col - 1 + LED_MATRIX_COLS ) % LED_MATRIX_COLS;
			LEDMatrix_TestCol( s_test_col );
			g_app_mode = APP_MODE_CUSTOM;
		}
		if( pressed & ( 1u << 2 ) ) // BTN4
		{
			s_test_col = ( s_test_col + 1 ) % LED_MATRIX_COLS;
			LEDMatrix_TestCol( s_test_col );
			g_app_mode = APP_MODE_CUSTOM;
		}

		if( g_app_mode == APP_MODE_BUILTIN )
		{
			if( g_app_pattern != last_pattern )
				frame = 0; // restart the animation cleanly on every switch
			s_patterns[g_app_pattern]( frame );
			frame++;
		}
		else if( g_app_mode == APP_MODE_FLASH_PATTERN )
		{
			PatternPlayer_Tick( TICK_MS );
		}
		// APP_MODE_CUSTOM: nothing to do here -- the matrix already shows
		// whatever LEDMatrix_SetPixel()/CommitStagedFrame() last wrote,
		// from webusb_protocol.c.
		last_pattern = g_app_pattern;
	}
}
