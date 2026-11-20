#ifndef _CH643_LEDPWM_H
#define _CH643_LEDPWM_H

// Register-level definitions for the CH643 LEDPWM peripheral (RGB/mono LED
// matrix dynamic display driver), reverse-engineered from CH643RM.PDF
// chapter 24. ch32fun has no header for this peripheral (it doesn't exist
// on the CH32X035), so this is a small local addition on top of the
// vendored ch32x03xhw.h, kept here rather than in ch32fun/ -- see
// docs/LEDPWM_MATRIX.md for the full register-choice rationale.

#include "ch32fun.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef union
{
	struct
	{
		__IO uint8_t COM_SEL;  // 0x00 LED_COM_SEL      [5:0] active COM pin code
		__IO uint8_t KEY_CTRL; // 0x01 LED_KEY_CTRL      keyboard-scan control (unused here)
		__IO uint8_t PWM_OE;   // 0x02 LED_PWM_OE  bit0  global PWM output enable
		__IO uint8_t DMA_CNT;  // 0x03 LED_DMA_CNT [4:0] words left to auto-load for this row
	};
	// 24.3.1 documents 0x00-0x03 as one combined 32-bit register,
	// LED_SWITCH ([7:0]=COM_SEL, [15:8]=KEY_CTRL, [23:16]=PWM_OE,
	// [31:24]=DMA_CNT) -- see LEDPWM_SWITCH() below and
	// docs/LEDPWM_MATRIX.md for why writing the four bytes separately
	// turned out not to reliably reload DMA_CNT/PWM_OE before the
	// row-switch triggered by the COM_SEL write.
	__IO uint32_t SWITCH;
} LEDPWM_SWITCH_TypeDef;

// 24.3.6-24.3.10: 0x04-0x07 are also one combined 32-bit register, LED_COM_E
// ([7:0]=COM_E_PB_L, [15:8]=COM_E_PB_M, [23:16]=COM_E_PB_H, [31:24]=COM_E_PAPC)
// -- matches the vendor's own ch643.h (EVT/EXAM SDK) exactly. Written once at
// init in this driver, so not implicated in the per-row bug the SWITCH union
// above was for, but kept atomic for consistency with how the silicon
// actually groups these bytes.
typedef union
{
	struct
	{
		__IO uint8_t COM_E_PB_L; // 0x04 LED_COM_E_PB_L    PB[7:0]   COM-enable
		__IO uint8_t COM_E_PB_M; // 0x05 LED_COM_E_PB_M    PB[15:8]  COM-enable
		__IO uint8_t COM_E_PB_H; // 0x06 LED_COM_E_PB_H    PB[23:16] COM-enable
		__IO uint8_t COM_E_PAPC; // 0x07 LED_COM_E_PAPC    PA/PC COM-enable (see bit macros)
	};
	__IO uint32_t COM_E;
} LEDPWM_COM_E_TypeDef;

// 24.3.11-24.3.15: 0x08-0x0B are also one combined 32-bit register,
// LED_PWM_PIN ([7:0]=PWM_PIN_L, [15:8]=PWM_PIN_H, [23:16]=reserved,
// [31:24]=PWM_MOD) -- same vendor-confirmed grouping as LED_COM_E above.
// The vendor's own ch643.h nests this one level deeper than LED_SWITCH/
// LED_COM_E, though: PWM_PIN_L/H form their OWN 16-bit sub-union
// (R16_PWM_PIN), separate from PWM_MOD -- i.e. the column enables are
// meant to be read/written as a pair independently of the matrix-mode
// byte, not necessarily atomically with it. Rewriting the full 32 bits
// (including PWM_MOD) every row, even with PWM_MOD's value unchanged,
// turned out to break per-row enable updates on real hardware -- see
// "Hardware bring-up: PWM_PIN per-row rewrite" in docs/LEDPWM_MATRIX.md.
// Matching the vendor's nesting and writing only R16_PWM_PIN leaves
// PWM_MOD untouched.
typedef union
{
	struct
	{
		union
		{
			struct
			{
				__IO uint8_t PWM_PIN_L; // 0x08 LED_PWM_PIN_L  PWM0-7_EN  (column drive enable)
				__IO uint8_t PWM_PIN_H; // 0x09 LED_PWM_PIN_H  PWM8-15_EN
			};
			__IO uint16_t R16_PWM_PIN; // 0x08-0x09 combined, independent of PWM_MOD below
		};
		__IO uint8_t RESERVED_0A; // 0x0A unlisted in the register map
		__IO uint8_t PWM_MOD;     // 0x0B LED_PWM_MOD    MATRIX_MODE + R/G/B plane enables
	};
	__IO uint32_t PWM_PIN;
} LEDPWM_PWM_PIN_TypeDef;

// 24.3.16-24.3.20: 0x0C-0x0F are also one combined 32-bit register, LED_CTRL
// ([7:0]=FUNC_CTRL, [15:8]=ANA_CTRL, [23:16]=CYCLE_CFG, [31:24]=FRAME_CFG).
typedef union
{
	struct
	{
		__IO uint8_t FUNC_CTRL;  // 0x0C LED_FUNC_CTRL  LED_ENABLE, IE_INHIBIT, PWM_OE_CLR, ...
		__IO uint8_t ANA_CTRL;   // 0x0D LED_ANA_CTRL   analog constant-current options (unused here)
		__IO uint8_t CYCLE_CFG;  // 0x0E LED_CYCLE_CFG  luminance/color PWM cycle length, clock divider
		__IO uint8_t FRAME_CFG;  // 0x0F LED_FRAME_CFG  row-switch blanking time, color-PWM repeat count
	};
	__IO uint32_t CTRL;
} LEDPWM_CTRL_TypeDef;

typedef struct
{
	LEDPWM_SWITCH_TypeDef  SW;   // 0x00-0x03: COM_SEL/KEY_CTRL/PWM_OE/DMA_CNT, see above
	LEDPWM_COM_E_TypeDef   CE;   // 0x04-0x07: COM_E_PB_L/M/H/PAPC, see above
	LEDPWM_PWM_PIN_TypeDef PP;   // 0x08-0x0B: PWM_PIN_L/H/reserved/PWM_MOD, see above
	LEDPWM_CTRL_TypeDef    CR;   // 0x0C-0x0F: FUNC_CTRL/ANA_CTRL/CYCLE_CFG/FRAME_CFG, see above
	__I  uint8_t  DISP_STAT;   // 0x10 LED_DISP_STAT     bit7 IF_INHIBIT (frame IRQ flag, write 1 to clear)
	__I  uint8_t  INTEN_CNT;   // 0x11 LED_INTEN_CNT     luminance PWM counter (read-only, status)
	__I  uint8_t  COLOR_CNT;   // 0x12 LED_COLOR_CNT     color PWM counter (read-only, status)
	__I  uint8_t  FRAME_STA;   // 0x13 LED_FRAME_STA     repeat/inhibit counters (read-only, status)
	__IO uint8_t  INTEN_ADJ;   // 0x14 LED_INTEN_ADJ     global luminance trim, signed (unused here)
	__IO uint8_t  RED_ADJ;     // 0x15 LED_RED_ADJ       global "red"-plane trim, signed (unused here)
	__IO uint8_t  GREEN_ADJ;   // 0x16 LED_GREEN_ADJ     global "green"-plane trim, signed (unused here)
	__IO uint8_t  BLUE_ADJ;    // 0x17 LED_BLUE_ADJ      global "blue"-plane trim, unused (no blue plane wired)
	__IO uint16_t DMA;         // 0x18 LED_DMA[15:0]     current DMA address into SRAM, auto-incremented
} LEDPWM_TypeDef;

#define LEDPWM_BASE ((uint32_t)0x40026800)
#define LEDPWM      ((LEDPWM_TypeDef *)LEDPWM_BASE)

// Builds the combined 32-bit LED_SWITCH value (24.3.1) for one atomic
// write to LEDPWM->SW.SWITCH -- com_sel/pwm_oe/dma_cnt as one bus
// transaction, instead of three separate byte stores to COM_SEL/PWM_OE/
// DMA_CNT. KEY_CTRL (bits 15:8) stays 0, unused by this driver.
#define LEDPWM_SWITCH( com_sel, pwm_oe, dma_cnt ) \
	( ( (uint32_t)(dma_cnt) << 24 ) | ( (uint32_t)(pwm_oe) << 16 ) | (uint32_t)(com_sel) )

// Same atomic-write rationale as LEDPWM_SWITCH() above, for the other two
// registers the vendor's ch643.h also groups as one 32-bit union. All four
// COM-enable bytes are independent (PB[7:0]/PB[15:8]/PB[23:16]/PA+PC), so
// this takes all four explicitly rather than assuming the PB ones are 0 --
// which board of PA/PB carries COM vs PWM depends on MATRIX_MODE (see
// LEDPWM_PWM_PIN() below), so both are live possibilities, not "PB always
// 0 since our board's rows are on PA" (true for the external-PB24 config
// this project started with, false for the internal-PA24 config the
// actual board wiring turned out to need).
#define LEDPWM_COM_E( com_e_pb_l, com_e_pb_m, com_e_pb_h, com_e_papc ) \
	( ( (uint32_t)(com_e_papc) << 24 ) | ( (uint32_t)(com_e_pb_h) << 16 ) | \
	  ( (uint32_t)(com_e_pb_m) << 8 ) | (uint32_t)(com_e_pb_l) )
#define LEDPWM_PWM_PIN( pwm_pin_l, pwm_pin_h, pwm_mod ) \
	( ( (uint32_t)(pwm_mod) << 24 ) | ( (uint32_t)(pwm_pin_h) << 8 ) | (uint32_t)(pwm_pin_l) )
// Column enables only, as a 16-bit pair -- see LEDPWM_PWM_PIN_TypeDef's
// R16_PWM_PIN for why this is written separately from PWM_MOD.
#define LEDPWM_R16_PWM_PIN( pwm_pin_l, pwm_pin_h ) \
	( ( (uint16_t)(pwm_pin_h) << 8 ) | (uint16_t)(pwm_pin_l) )
#define LEDPWM_CTRL( func_ctrl, ana_ctrl, cycle_cfg, frame_cfg ) \
	( ( (uint32_t)(frame_cfg) << 24 ) | ( (uint32_t)(cycle_cfg) << 16 ) | \
	  ( (uint32_t)(ana_ctrl) << 8 ) | (uint32_t)(func_ctrl) )

// ---- LED_ANA_CTRL bits (0x0D) ----
// 24.3.18: pure on/off enables, no documented current *value* anywhere in
// CH643RM.PDF or CH643DS0.PDF -- checked both the register description and
// the datasheet's absolute-maximum/electrical-characteristics tables, which
// only list destructive per-pin limits (60mA PA source / 100mA PA,PB sink),
// not a regulated target for this mode. PWM_REF's own text ties it mainly
// to "current key scan" detection ("this bit must be on if the current key
// scan function is used"), not confirmed as a general LED-safe current
// source. Measure actual current with a multimeter before relying on this
// to replace the board's series resistors -- see docs/CURRENT_BUDGET.md.
#define LEDPWM_PWM_REF    (1 << 4) // global CC-mode enable, also required for current-mode key scan
#define LEDPWM_PWM_PB_CCN (1 << 5) // PB sink-current CC (internal NMOS) -- our columns
#define LEDPWM_PWM_PA_CCP (1 << 6) // PA source-current CC (internal PMOS) -- our rows
#define LEDPWM_PWM_PA_CCN (1 << 7) // PA sink-current CC (internal NMOS) -- not used, PA is source-only here

// ---- LED_COM_E_PAPC bits (0x07) ----
#define LEDPWM_COM_PA_7_0_EN   (1 << 0)
#define LEDPWM_COM_PA_15_8_EN  (1 << 1)
#define LEDPWM_COM_PA_23_16_EN (1 << 2)
#define LEDPWM_COM_PC_7_0_EN   (1 << 3)
#define LEDPWM_COM_PC_15_8_EN  (1 << 4)
#define LEDPWM_COM_PC_20_16_EN (1 << 5)

// ---- LED_PWM_MOD bits (0x0B) ----
#define LEDPWM_MATRIX_MODE_PA24PB24 0
#define LEDPWM_MATRIX_MODE_PA24     1
#define LEDPWM_MATRIX_MODE_PB24     2
#define LEDPWM_MATRIX_MODE_PB16PA8  3
#define LEDPWM_MATRIX_MODE_PA15     4
#define LEDPWM_MATRIX_MODE_PA16I    6
#define LEDPWM_PWM_RED_EN   (1 << 4)
#define LEDPWM_PWM_GREEN_EN (1 << 5)
#define LEDPWM_PWM_BLUE_EN  (1 << 6)

// ---- LED_FUNC_CTRL bits (0x0C) ----
#define LEDPWM_LED_ENABLE     (1 << 0)
// "Forced full-amplitude luminance output... can be used for PA16I
// monochrome mode to replace luminance data with color data" (24.3.17).
// Not used here -- PA16I-specific per the manual's own wording, and we
// already get full luminance by setting the luminance byte to 0xFF
// (ledpwm_load_row()), which works correctly for uniform-fill patterns.
#define LEDPWM_INTEN_FULL     (1 << 1)
#define LEDPWM_PWM_OE_CLR     (1 << 2)
#define LEDPWM_COM_AHEAD(x)   (((x) & 3) << 3)
// Red PWM data halving mode, "for dual LEDs in series" (24.3.17) -- not
// our wiring (no series-paired LEDs on one channel), left off.
#define LEDPWM_REG_HALF       (1 << 6)
#define LEDPWM_IE_INHIBIT     (1 << 7)

// ---- LED_CYCLE_CFG bits (0x0E) ----
#define LEDPWM_CLK_FREQ(x)   (((x) & 3) << 0) // 0=/1 1=/2 2=/3 3=/4
#define LEDPWM_INTEN_CYC(x)  (((x) & 3) << 4) // 0=8bit/256 1=7bit/128 2,3=6bit/64
#define LEDPWM_COLOR_CYC_7BIT (1 << 6)         // 0=8bit/256, 1=7bit/128

// ---- LED_FRAME_CFG bits (0x0F) ----
#define LEDPWM_PWM_REPEAT(x) (((x) & 7) << 0) // N-1 repeats of the color-PWM cycle per row
#define LEDPWM_INHIBIT(x)    (((x) & 15) << 4) // row-switch blanking, in luminance-PWM cycles

// ---- LED_DISP_STAT bits (0x10) ----
#define LEDPWM_IF_INHIBIT   (1 << 7) // frame/row-done interrupt flag, write 1 to clear
#define LEDPWM_LED_IF_SET   (1 << 6) // WO: write 1 to force IF_INHIBIT, entering the ISR on demand (unused here)
#define LEDPWM_INHIBIT_CTRL (1 << 4) // RO: 1 = safe to reload COM_SEL/DMA now

// ch32fun's CH32X035 vector table (which CH643 otherwise matches exactly,
// see docs/LEDPWM_MATRIX.md) calls interrupt slot 19 "RCC". On real CH643
// silicon that slot is LEDPWM instead -- these aliases just let the rest
// of the code refer to it by its correct name without editing the
// vendored header.
#define LEDPWM_IRQn       RCC_IRQn
#define LEDPWM_IRQHandler RCC_IRQHandler

#ifdef __cplusplus
}
#endif

#endif
