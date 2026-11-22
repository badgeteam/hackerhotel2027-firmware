#ifndef _CH643_LEDPWM_H
#define _CH643_LEDPWM_H

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
    __IO uint32_t SWITCH;
} LEDPWM_SWITCH_TypeDef;

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

#define LEDPWM_SWITCH( com_sel, pwm_oe, dma_cnt ) \
    ( ( (uint32_t)(dma_cnt) << 24 ) | ( (uint32_t)(pwm_oe) << 16 ) | (uint32_t)(com_sel) )
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
#define LEDPWM_INTEN_FULL     (1 << 1)
#define LEDPWM_PWM_OE_CLR     (1 << 2)
#define LEDPWM_COM_AHEAD(x)   (((x) & 3) << 3)
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

#define LEDPWM_IRQn       RCC_IRQn
#define LEDPWM_IRQHandler RCC_IRQHandler

#ifdef __cplusplus
}
#endif

#endif
