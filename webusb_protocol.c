#include "webusb_protocol.h"
#include <stdbool.h>
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "fsusb.h"  // includes fsusb.c itself, so only include it in this file
#include "pattern_flash.h"
#include "pattern_player.h"

#define CMD_SET_PIXEL 0x01
#define CMD_FRAME_CHUNK 0x02
#define CMD_FRAME_COMMIT 0x03
#define CMD_CLEAR 0x05
#define CMD_PATTERN_BEGIN 0x10
#define CMD_PATTERN_CHUNK 0x11
#define CMD_PATTERN_COMMIT 0x12
#define CMD_QUERY 0xFF

#define FRAME_PIXELS (LED_MATRIX_ROWS * LED_MATRIX_COLS)

volatile uint8_t g_app_mode = APP_MODE_CUSTOM;

static uint8_t s_stage_frame[FRAME_PIXELS];
static volatile bool s_reply_pending;
static volatile bool s_pattern_committed;

// Called from the USB interrupt for every packet on the bulk OUT endpoint
void WebUSBBulkDataOut(uint8_t* data, int len) {
    if (len < 1) return;

    switch (data[0]) {
        case CMD_SET_PIXEL:
            if (len >= 4) {
                LEDMatrix_SetPixel(data[1], data[2], data[3]);
                g_app_mode = APP_MODE_CUSTOM;
            }
            break;

        case CMD_FRAME_CHUNK:
            if (len >= 3) {
                uint16_t offset = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
                for (int i = 3; i < len; i++, offset++) {
                    if (offset < FRAME_PIXELS) s_stage_frame[offset] = data[i];
                }
            }
            break;

        case CMD_FRAME_COMMIT:
            for (unsigned i = 0; i < FRAME_PIXELS; i++) {
                LEDMatrix_SetPixel(i / LED_MATRIX_COLS, i % LED_MATRIX_COLS, s_stage_frame[i]);
            }
            g_app_mode = APP_MODE_CUSTOM;
            break;

        case CMD_CLEAR:
            LEDMatrix_Clear();
            g_app_mode = APP_MODE_CUSTOM;
            break;

        case CMD_PATTERN_BEGIN:
            g_app_mode = APP_MODE_CUSTOM;  // stop playing from the region we're about to erase
            PatternFlash_EraseAll();
            PatternFlash_BeginWrite();
            break;

        case CMD_PATTERN_CHUNK:
            if (len >= 2) PatternFlash_WriteBytes(&data[1], (uint8_t)(len - 1));
            break;

        case CMD_PATTERN_COMMIT:
            PatternFlash_FinishWrite();
            s_pattern_committed = true;  // start playback from the main loop, see WebUSB_Poll()
            break;

        case CMD_QUERY:
        default:
            break;
    }

    // One pending reply at a time; if the host hasn't read the previous one
    // yet, it will see the current state with its next poll anyway
    s_reply_pending = true;
}

void WebUSB_Init(void) {
    s_reply_pending = false;
    FSUSBSetup();
}

void WebUSB_Deinit(void) {
    NVIC_DisableIRQ(USBFS_IRQn);
    USBFS->BASE_CTRL = RB_UC_RESET_SIE | RB_UC_CLR_ALL;  // also drops DEV_PU_EN: off the bus
    USBFS->UDEV_CTRL = 0;
    // Pull-ups and USB pin function off. USB_PHY_V33=1 (its reset value)
    // keeps the USB PHY's LDO off; 0 would enable it. Same for the (unused)
    // USB PD PHY on PC14/PC15, which resets to 0 (buck on) and should be 1
    // for plain GPIO use (CH643RM 8.3.2.4).
    AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_IOEN)) | USB_PHY_V33 | USBPD_PHY_V33;

    // PC16/PC17 (D-/D+) to pull-down instead of floating. Their internally
    // shorted twins PC11/PC10 stay floating inputs, so nothing pulls against it.
    GPIOC->CFGXR = (GPIOC->CFGXR & ~0xFFu) | ((uint32_t)GPIO_CFGLR_IN_PUPD << 0) | ((uint32_t)GPIO_CFGLR_IN_PUPD << 4);
    GPIOC->BSXR = (1u << 0 | 1u << 1) << 16;  // OUTDR=0 selects the pull-down

    RCC->AHBPCENR &= ~RCC_USBFS;
    s_reply_pending = false;
}

void WebUSB_Poll(void) {
    if (s_pattern_committed) {
        s_pattern_committed = false;
        PatternPlayer_Reset();
        g_app_mode = APP_MODE_FLASH_PATTERN;
    }

    if (!s_reply_pending) return;
    uint8_t* buffer = USBFS_GetEPBufferIfAvailable(USB_EP_TX);
    if (!buffer) return;
    buffer[0] = g_app_mode;
    buffer[1] = LEDMatrix_GetBrightness();
    USBFS_SendEndpoint(USB_EP_TX, 2);
    s_reply_pending = false;
}
