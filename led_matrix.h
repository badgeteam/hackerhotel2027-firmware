#ifndef _LED_MATRIX_H
#define _LED_MATRIX_H

// Driver for the starfish board's LED matrix, using the CH643's LEDPWM
// peripheral. See docs/LEDPWM_MATRIX.md for the hardware topology and the
// register-configuration rationale.

#include <stdint.h>

// MATRIX_MODE=PA24 (CH643RM.PDF 24.2.2, "8-column RGB Internal Drive
// Application (8C*3*N)"), matching this board's actual wiring: PWM/column
// pins (LED anodes) on PA[23:0], COM/row pins (LED cathodes) on PB[15:0].
// 8 physical column-groups x 3 channels (R/G/B) each = 24 addressable
// "columns" (col 0-7 = group 0-7 red, 8-15 = green, 16-23 = blue -- see
// ledpwm_load_row() in led_matrix.c); col/row values in this API are
// always in this 0-23 / 0-15 space, independent of which physical pin
// that maps to.
#define LED_MATRIX_COLS 24 // 8 groups x R/G/B, PA[7:0]=R PA[15:8]=G PA[23:16]=B
#define LED_MATRIX_ROWS 16 // COM0-15, PB0-PB15

// Sets up the matrix GPIOs and the LEDPWM peripheral, and starts the row
// scan. Call once at boot, after SystemInit()/funGpioInitAll().
void LEDMatrix_Init( void );

// Sets one LED's brightness (0 = off, 255 = full brightness). Positions
// with no physical LED populated are harmless to write -- current simply
// has nowhere to flow. Safe to call from the main loop while the scan ISR
// is running; brightness values are read a whole row at a time by the ISR,
// so a pattern update can at worst show on a slightly uneven row boundary
// for one frame.
void LEDMatrix_SetPixel( unsigned row, unsigned col, uint8_t brightness );

// Sets every LED to the same brightness.
void LEDMatrix_Fill( uint8_t brightness );

static inline void LEDMatrix_Clear( void ) { LEDMatrix_Fill( 0 ); }

// Bring-up debug aids: clears the matrix, then lights every column of one
// row (or every row of one column) at full brightness -- isolates a whole
// row/column so a dead or miswired one is obvious without checking each
// LED by hand. Shared by webusb_protocol.c's TEST_ROW/TEST_COL commands
// and led_matrix_test.c's front-panel buttons. Out-of-range row/col is
// silently dropped, same as LEDMatrix_SetPixel().
void LEDMatrix_TestRow( unsigned row );
void LEDMatrix_TestCol( unsigned col );

// Isolates exactly one LED: statically enables only its column and
// freezes the scan on only its row, instead of relying on per-row
// brightness data to distinguish it from the other 21 rows sharing that
// column (confirmed on hardware not to work -- see led_matrix.c's
// LEDMatrix_TestPixel() and docs/LEDPWM_MATRIX.md). Any other
// brightness-changing call (SetPixel/Fill, and anything built on them)
// exits this mode automatically. Out-of-range row/col just clears the
// matrix and leaves normal scanning running.
void LEDMatrix_TestPixel( unsigned row, unsigned col );

// Bring-up debugging: like LEDMatrix_TestPixel() but takes raw
// PWM_PIN_L/H bitmasks directly (lights every column whose bit is set,
// not just one) -- lets arbitrary enable-bit combinations be probed from
// the host without a firmware rebuild per combination.
void LEDMatrix_RawTest( unsigned row, uint8_t pwm_pin_l, uint8_t pwm_pin_h );

// Bring-up debugging: pokes PWM_PIN_L/H directly with no other side
// effects (no Clear, no SetPixel, no row freeze change) -- call after
// LEDMatrix_TestPixel() to test satisfying a column's cascading-enable
// prerequisites (docs/LEDPWM_MATRIX.md) without changing which pixel's
// brightness is actually set to anything nonzero.
void LEDMatrix_SetPwmPinRaw( uint8_t pwm_pin_l, uint8_t pwm_pin_h );

// Bring-up debugging: pokes s_brightness directly, no side effects
// (doesn't exit pixel-test mode like LEDMatrix_SetPixel does) -- for
// building a custom brightness pattern after LEDMatrix_TestPixel().
void LEDMatrix_PokeBrightness( unsigned row, unsigned col, uint8_t brightness );

// Bypasses LEDPWM entirely: halts the peripheral/scan ISR and drives one
// row + one column as plain static push-pull GPIO outputs (no
// multiplexing, no PWM). Answers "does this LED/trace/joint physically
// work at all" independent of anything LEDPWM-side -- see
// docs/LEDPWM_MATRIX.md. Any other brightness-changing call exits this
// mode and resumes normal scanning. Out-of-range row/col is a no-op.
void LEDMatrix_GpioTest( unsigned row, unsigned col );

// Bring-up debugging: dumps every live LEDPWM register + the last row
// packed into the DMA buffer into out (caller-allocated, at least
// LEDMATRIX_DEBUG_DUMP_LEN bytes). Returns bytes written. See
// led_matrix.c's LEDMatrix_DebugDump() for the exact field layout and
// webusb_protocol.c's CMD_DEBUG_DUMP for how it reaches the host. 16 bytes
// of atomic register groups (SWITCH/COM_E/PWM_PIN/CTRL, 4 bytes each) + 4
// status bytes + 4 analog trim bytes + 2 DMA-pointer bytes + 3 driver scan-
// state bytes + the DMA buffer (2 columns/word).
#define LEDMATRIX_DEBUG_DUMP_LEN ( 16 + 4 + 4 + 2 + 3 + ( LED_MATRIX_COLS / 3 ) * 4 )
uint8_t LEDMatrix_DebugDump( uint8_t *out );

// Bring-up debugging: manual register control (see webui/regpanel.js).
// Writes one of the four atomic 32-bit register groups (0=SWITCH,
// 1=COM_E, 2=PWM_PIN, 3=CTRL) or one of the four independent analog trim
// bytes (real register offset 0x14-0x17) directly. Raw pokes, no
// validation -- see LEDMatrix_WriteReg32()/LEDMatrix_WriteReg8() in
// led_matrix.c.
void LEDMatrix_WriteReg32( uint8_t group, uint32_t value );
void LEDMatrix_WriteReg8( uint8_t offset, uint8_t value );

#define LED_MATRIX_PIXEL_COUNT ( LED_MATRIX_ROWS * LED_MATRIX_COLS )

// Full-frame upload, used by the WebUSB protocol (see webusb_protocol.c) to
// push an entire pattern in a handful of USB-packet-sized pieces without
// the matrix briefly showing a half-written frame. Writes into a staging
// buffer, invisible until LEDMatrix_CommitStagedFrame() copies it into the
// live one; out-of-range bytes (offset+len beyond LED_MATRIX_PIXEL_COUNT)
// are silently dropped. Row-major: index = row * LED_MATRIX_COLS + col.
void LEDMatrix_StageFrameChunk( uint16_t offset, const uint8_t *data, uint8_t len );
void LEDMatrix_CommitStagedFrame( void );

#endif
