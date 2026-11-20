#include "ch32fun.h"
#include "fsusb.h"
#include "led_matrix.h"
#include "webusb_protocol.h"
#include "pattern_flash.h"
#include "pattern_player.h"

// Command bytes, OUT endpoint (host -> device). See docs/WEBUSB_PROTOCOL.md.
#define CMD_SET_PIXEL      0x01
#define CMD_FRAME_CHUNK    0x02
#define CMD_FRAME_COMMIT   0x03
#define CMD_SELECT_BUILTIN 0x04
#define CMD_CLEAR          0x05
#define CMD_TEST_ROW       0x06
#define CMD_TEST_COL       0x07
#define CMD_DEBUG_DUMP     0x08
#define CMD_TEST_PIXEL     0x09
#define CMD_GPIO_TEST      0x0A
#define CMD_RAW_TEST       0x0B
#define CMD_SET_PWM_PIN    0x0C
#define CMD_POKE_BRIGHTNESS 0x0D
#define CMD_REG_WRITE32    0x0E
#define CMD_REG_WRITE8     0x0F
#define CMD_PATTERN_BEGIN  0x10
#define CMD_PATTERN_CHUNK  0x11
#define CMD_PATTERN_COMMIT 0x12
#define CMD_DEBUG_ENTER_STANDBY 0x13
#define CMD_DEBUG_EXTI_COUNT 0x14
#define CMD_DEBUG_POWER_DUMP 0x15
#define CMD_DEBUG_BOOT_INFO 0x16
#define CMD_DEBUG_POWER_STATE 0x17
#define CMD_QUERY          0xFF

// One reply is queued per processed command: normally [mode, pattern], or
// the longer LEDMatrix_DebugDump() payload for CMD_DEBUG_DUMP. gs_reply[0]
// is the length (0 = empty), mirroring the simple one-pending-packet
// scheme the original webusb_vendor demo used. Sized for the debug dump
// (the largest reply) plus its length byte.
static volatile uint8_t gs_reply[1 + LEDMATRIX_DEBUG_DUMP_LEN];

void WebUSB_Init( void )
{
	FSUSBSetup();
}

void WebUSBBulkDataOut( uint8_t *data, int len )
{
	if( len < 1 )
		return;

	switch( data[0] )
	{
	case CMD_SET_PIXEL:
		if( len >= 4 )
		{
			LEDMatrix_SetPixel( data[1], data[2], data[3] );
			g_app_mode = APP_MODE_CUSTOM;
		}
		break;

	case CMD_FRAME_CHUNK:
		if( len >= 3 )
		{
			uint16_t offset = (uint16_t)data[1] | ( (uint16_t)data[2] << 8 );
			LEDMatrix_StageFrameChunk( offset, &data[3], (uint8_t)( len - 3 ) );
		}
		break;

	case CMD_FRAME_COMMIT:
		LEDMatrix_CommitStagedFrame();
		g_app_mode = APP_MODE_CUSTOM;
		break;

	case CMD_SELECT_BUILTIN:
		if( len >= 2 )
		{
			g_app_pattern = data[1] % 5;
			g_app_mode = APP_MODE_BUILTIN;
		}
		break;

	case CMD_CLEAR:
		LEDMatrix_Clear();
		// Every other mutating command (SET_PIXEL, FRAME_COMMIT, TEST_ROW,
		// TEST_COL) switches to custom mode; this one didn't. If
		// g_app_mode was still APP_MODE_BUILTIN (e.g. fresh boot, or
		// after a physical button press), the main loop's very next tick
		// re-ran whatever built-in pattern was active -- including
		// "breathe" or either chase pattern, which continuously write
		// nonzero data -- silently undoing the clear within one TICK_MS.
		// See docs/LEDPWM_MATRIX.md.
		g_app_mode = APP_MODE_CUSTOM;
		break;

	// Isolate a single row/column at full brightness, for bring-up
	// debugging a board where some rows/columns don't light as expected --
	// much faster than clicking through 16 or 22 individual LEDs by hand
	// to tell whether a whole row/column is dead vs. just one LED on it.
	case CMD_TEST_ROW:
		if( len >= 2 )
		{
			LEDMatrix_TestRow( data[1] );
			g_app_mode = APP_MODE_CUSTOM;
		}
		break;

	case CMD_TEST_COL:
		if( len >= 2 )
		{
			LEDMatrix_TestCol( data[1] );
			g_app_mode = APP_MODE_CUSTOM;
		}
		break;

	// Isolates one LED by freezing the scan to its row and statically
	// enabling only its column -- see LEDMatrix_TestPixel() for why
	// TEST_ROW/SET_PIXEL alone can't do this (a column's enable bit
	// lights it on every row it's scanned on, not just one).
	case CMD_TEST_PIXEL:
		if( len >= 3 )
		{
			LEDMatrix_TestPixel( data[1], data[2] );
			g_app_mode = APP_MODE_CUSTOM;
		}
		break;

	// Bypasses LEDPWM entirely, driving one row + one column as plain
	// static GPIO -- isolates whether a dead pixel is an LEDPWM-side
	// issue or a physical board/pin fault. See LEDMatrix_GpioTest().
	case CMD_GPIO_TEST:
		if( len >= 3 )
		{
			LEDMatrix_GpioTest( data[1], data[2] );
			g_app_mode = APP_MODE_CUSTOM;
		}
		break;

	// Probes arbitrary PWM_PIN_L/H bitmasks directly (row, pwm_pin_l,
	// pwm_pin_h) -- see LEDMatrix_RawTest().
	case CMD_RAW_TEST:
		if( len >= 4 )
		{
			LEDMatrix_RawTest( data[1], data[2], data[3] );
			g_app_mode = APP_MODE_CUSTOM;
		}
		break;

	// Pokes PWM_PIN_L/H directly with no other side effects -- see
	// LEDMatrix_SetPwmPinRaw(). Meant to follow a TEST_PIXEL call.
	case CMD_SET_PWM_PIN:
		if( len >= 3 )
			LEDMatrix_SetPwmPinRaw( data[1], data[2] );
		break;

	// Pokes brightness directly, no side effects -- see
	// LEDMatrix_PokeBrightness().
	case CMD_POKE_BRIGHTNESS:
		if( len >= 4 )
			LEDMatrix_PokeBrightness( data[1], data[2], data[3] );
		break;

	// Manual register control (webui/regpanel.js): writes one atomic
	// 32-bit register group directly -- data[1]=group (0=SWITCH,
	// 1=COM_E, 2=PWM_PIN, 3=CTRL), data[2..5]=value bytes 0..3
	// (little-endian, matching that group's struct field order). See
	// LEDMatrix_WriteReg32().
	case CMD_REG_WRITE32:
		if( len >= 6 )
		{
			uint32_t value = (uint32_t)data[2] | ( (uint32_t)data[3] << 8 ) |
				( (uint32_t)data[4] << 16 ) | ( (uint32_t)data[5] << 24 );
			LEDMatrix_WriteReg32( data[1], value );
		}
		break;

	// Manual register control: writes one of the four independent
	// analog trim bytes -- data[1]=register offset (0x14-0x17),
	// data[2]=value. See LEDMatrix_WriteReg8().
	case CMD_REG_WRITE8:
		if( len >= 3 )
			LEDMatrix_WriteReg8( data[1], data[2] );
		break;

	// Flash pattern storage (see pattern_flash.h/pattern_player.h and
	// docs/ANIMATION_FORMAT.md): starts a fresh upload by erasing the
	// whole reserved flash region. Slow (~64 page erases) -- the host is
	// expected to wait for the reply before sending PATTERN_CHUNKs.
	case CMD_PATTERN_BEGIN:
		PatternFlash_EraseAll();
		PatternFlash_BeginWrite();
		break;

	// Appends raw .ledanim bytes to the flash region, one USB packet's
	// worth at a time -- see PatternFlash_WriteBytes() for how this
	// reassembles page-aligned flash writes out of arbitrarily-sized
	// chunks.
	case CMD_PATTERN_CHUNK:
		if( len >= 2 )
			PatternFlash_WriteBytes( &data[1], (uint8_t)( len - 1 ) );
		break;

	// Flushes the final partial page, then immediately starts playing
	// the newly-stored pattern on repeat.
	case CMD_PATTERN_COMMIT:
		PatternFlash_FinishWrite();
		PatternPlayer_Reset();
		g_app_mode = APP_MODE_FLASH_PATTERN;
		break;

	// Bring-up debugging (docs/POWER_MANAGEMENT.md): triggers Standby
	// mode the same way a real BTN1 press-release would, without
	// needing physical button access -- the reply for this command
	// never actually goes out (the chip's about to stop responding to
	// USB entirely), so don't wait on one.
	case CMD_DEBUG_ENTER_STANDBY:
		g_debug_force_standby = 1;
		return;

	// Bring-up debugging: reports how many times EXTI20 (BTN1) has fired
	// since boot, as a little-endian u32 -- lets the EXTI/AFIO routing be
	// verified independently of Standby (press BTN1 during completely
	// normal operation, no sleep involved, and check whether this counts
	// up at all).
	case CMD_DEBUG_EXTI_COUNT:
		if( gs_reply[0] == 0 )
		{
			gs_reply[0] = 4;
			gs_reply[1] = (uint8_t)( g_debug_exti_count & 0xFF );
			gs_reply[2] = (uint8_t)( ( g_debug_exti_count >> 8 ) & 0xFF );
			gs_reply[3] = (uint8_t)( ( g_debug_exti_count >> 16 ) & 0xFF );
			gs_reply[4] = (uint8_t)( ( g_debug_exti_count >> 24 ) & 0xFF );
		}
		return;

	// Bring-up debugging: dumps the live value of every register that
	// matters for Standby entry/EXTI wake, read right before a sleep
	// attempt -- lets a misconfiguration be caught without needing
	// another real-hardware sleep/wake/powercycle round trip. 11
	// little-endian u32 words, in this order: RCC->APB2PCENR,
	// RCC->APB1PCENR, AFIO->EXTICR2, EXTI->FTENR, EXTI->RTENR,
	// EXTI->INTENR, EXTI->INTFR, PWR->CTLR, NVIC->IENR[1],
	// GPIOB->CFGXR, GPIOB->INDR.
	case CMD_DEBUG_POWER_DUMP:
		if( gs_reply[0] == 0 )
		{
			uint32_t words[11] = {
				RCC->APB2PCENR, RCC->APB1PCENR, AFIO->EXTICR2,
				EXTI->FTENR, EXTI->RTENR, EXTI->INTENR, EXTI->INTFR,
				PWR->CTLR, NVIC->IENR[1], GPIOB->CFGXR, GPIOB->INDR,
			};
			gs_reply[0] = sizeof( words );
			for( unsigned i = 0; i < sizeof( words ) / 4; i++ )
			{
				gs_reply[1 + i * 4 + 0] = (uint8_t)( words[i] & 0xFF );
				gs_reply[1 + i * 4 + 1] = (uint8_t)( ( words[i] >> 8 ) & 0xFF );
				gs_reply[1 + i * 4 + 2] = (uint8_t)( ( words[i] >> 16 ) & 0xFF );
				gs_reply[1 + i * 4 + 3] = (uint8_t)( ( words[i] >> 24 ) & 0xFF );
			}
		}
		return;

	// Bring-up debugging: live LEDPWM register state instead of the usual
	// 2-byte status reply -- see LEDMatrix_DebugDump() and
	// docs/WEBUSB_PROTOCOL.md for the field layout. Bypasses the generic
	// reply building below (gs_reply[0] is already nonzero once this
	// fills it), same "drop if a reply's still pending" rule as any other
	// command.
	case CMD_DEBUG_DUMP:
		if( gs_reply[0] == 0 )
		{
			uint8_t dump_len = LEDMatrix_DebugDump( (uint8_t *)&gs_reply[1] );
			gs_reply[0] = dump_len;
		}
		return;

	// Bring-up debugging: boot-time reset-cause snapshot, captured once
	// in power_init() before anything (including RMVF) can change it --
	// see g_debug_boot_rstsckr/_woke/_armed. 6 bytes: u32 raw RCC->RSTSCKR
	// at boot, then s_woke_from_standby, then s_power_btn_armed (each as
	// a single byte, 0 or 1).
	case CMD_DEBUG_BOOT_INFO:
		if( gs_reply[0] == 0 )
		{
			gs_reply[0] = 6;
			gs_reply[1] = (uint8_t)( g_debug_boot_rstsckr & 0xFF );
			gs_reply[2] = (uint8_t)( ( g_debug_boot_rstsckr >> 8 ) & 0xFF );
			gs_reply[3] = (uint8_t)( ( g_debug_boot_rstsckr >> 16 ) & 0xFF );
			gs_reply[4] = (uint8_t)( ( g_debug_boot_rstsckr >> 24 ) & 0xFF );
			gs_reply[5] = (uint8_t)( g_debug_boot_woke ? 1 : 0 );
			gs_reply[6] = (uint8_t)( g_debug_boot_armed ? 1 : 0 );
		}
		return;

	// Bring-up debugging: live power_poll() state + EXTI count in one
	// shot, meant to be polled rapidly (every ~50-100ms) while BTN1 is
	// physically operated, to catch the exact sequence of debounce/arm
	// transitions in real time. Power_DebugState() writes 7 bytes
	// (boot_grace, armed, pending_sleep, pressed_debounced,
	// press_debounce, release_debounce, raw pin level); byte 7..10 is
	// g_debug_exti_count (u32 little-endian).
	case CMD_DEBUG_POWER_STATE:
		if( gs_reply[0] == 0 )
		{
			uint8_t n = (uint8_t)Power_DebugState( (uint8_t *)&gs_reply[1] );
			gs_reply[1 + n + 0] = (uint8_t)( g_debug_exti_count & 0xFF );
			gs_reply[1 + n + 1] = (uint8_t)( ( g_debug_exti_count >> 8 ) & 0xFF );
			gs_reply[1 + n + 2] = (uint8_t)( ( g_debug_exti_count >> 16 ) & 0xFF );
			gs_reply[1 + n + 3] = (uint8_t)( ( g_debug_exti_count >> 24 ) & 0xFF );
			gs_reply[0] = n + 4;
		}
		return;

	case CMD_QUERY:
	default:
		break; // fall through to the status reply below
	}

	if( gs_reply[0] == 0 )
	{
		gs_reply[0] = 2;
		gs_reply[1] = g_app_mode;
		gs_reply[2] = g_app_pattern;
	}
	// else: previous reply hasn't gone out yet (host is polling faster
	// than once per USB round trip) -- drop this one, the next poll will
	// see current state anyway.
}

void WebUSB_SendPendingReply( void )
{
	if( !gs_reply[0] )
		return;

	uint8_t *buffer = USBFS_GetEPBufferIfAvailable( USB_EP_TX );
	if( !buffer )
		return;

	uint8_t len = gs_reply[0];
	for( uint8_t i = 0; i < len; i++ )
		buffer[i] = gs_reply[i + 1];
	USBFS_SendEndpoint( USB_EP_TX, len );
	gs_reply[0] = 0;
}
