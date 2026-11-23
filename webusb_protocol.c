#include "webusb_protocol.h"
#include <stdbool.h>
#include <string.h>
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "fsusb.h"  // includes fsusb.c itself, so only include it in this file
#include "store.h"
#include "apps/hhapp.h"

#define CMD_SET_PIXEL 0x01
#define CMD_FRAME_CHUNK 0x02
#define CMD_FRAME_COMMIT 0x03
#define CMD_CLEAR 0x05
#define CMD_PATTERN_BEGIN 0x10
#define CMD_PATTERN_CHUNK 0x11
#define CMD_PATTERN_COMMIT 0x12
#define CMD_STORE_ERASE 0x20
#define CMD_ITEM_BEGIN 0x21
#define CMD_ITEM_CHUNK 0x22
#define CMD_ITEM_COMMIT 0x23
#define CMD_ITEM_INFO 0x24
#define CMD_ITEM_RUN 0x25
#define CMD_STOP 0x26
#define CMD_QUERY 0xFF

#define FRAME_PIXELS (LED_MATRIX_ROWS * LED_MATRIX_COLS)

volatile uint8_t g_app_mode = APP_MODE_CUSTOM;
volatile uint8_t g_running_item = STORE_NONE;

static uint8_t s_stage_frame[FRAME_PIXELS];
static volatile bool s_reply_pending;
static volatile uint8_t s_result;      // of the last command, for the reply
static volatile uint8_t s_info_item;   // ITEM_INFO asked about this item
static volatile uint8_t s_run_request = STORE_NONE;

static void commit(void) {
    uint8_t index;
    s_result = (uint8_t)Store_Commit(&index);
    if (s_result == STORE_OK) {
        Store_SetBootItem(index);
        s_run_request = index;
    }
}

// Called from the USB interrupt for every packet on the bulk OUT endpoint
void WebUSBBulkDataOut(uint8_t* data, int len) {
    if (len < 1) return;
    s_result = STORE_OK;
    s_info_item = STORE_NONE;

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
            Store_EraseAll();
            s_result = (uint8_t)Store_Begin(STORE_TYPE_ANIMATION, "PATTERN", 7);
            break;

        case CMD_STORE_ERASE:
            g_app_mode = APP_MODE_CUSTOM;
            Store_EraseAll();
            break;

        case CMD_ITEM_BEGIN:
            s_result = len >= 2 ? (uint8_t)Store_Begin(data[1], (const char*)&data[2], (unsigned)(len - 2))
                                : STORE_ERR_ARGS;
            break;

        case CMD_PATTERN_CHUNK:
        case CMD_ITEM_CHUNK:
            s_result = (uint8_t)Store_Write(&data[1], (unsigned)(len - 1));
            break;

        case CMD_PATTERN_COMMIT:
        case CMD_ITEM_COMMIT:
            commit();
            break;

        case CMD_ITEM_INFO:
            if (len < 2) {
                s_result = STORE_ERR_ARGS;
            } else if (!Store_Item(data[1])) {
                s_result = STORE_ERR_NOT_FOUND;
            } else {
                s_info_item = data[1];
            }
            break;

        case CMD_ITEM_RUN:
            if (len < 2) {
                s_result = STORE_ERR_ARGS;
            } else if (!Store_Item(data[1])) {
                s_result = STORE_ERR_NOT_FOUND;
            } else {
                if (len >= 3 && data[2]) Store_SetBootItem(data[1]);
                s_run_request = data[1];
            }
            break;

        case CMD_STOP:
            g_app_mode = APP_MODE_CUSTOM;
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
    if (!s_reply_pending) return;
    uint8_t* buffer = USBFS_GetEPBufferIfAvailable(USB_EP_TX);
    if (!buffer) return;
    buffer[0] = g_app_mode;
    buffer[1] = LEDMatrix_GetBrightness();
    buffer[2] = s_result;
    buffer[3] = Store_Count();
    buffer[4] = g_app_mode == APP_MODE_CUSTOM ? STORE_NONE : g_running_item;
    buffer[5] = Store_BootItem();
    buffer[6] = Store_FreePages();
    buffer[7] = HH_API_VERSION;
    int len = 8;
    const store_item_t* item = s_info_item != STORE_NONE ? Store_Item(s_info_item) : NULL;
    if (item) {
        buffer[8] = item->type;
        for (int i = 0; i < 4; i++) buffer[9 + i] = (uint8_t)(item->length >> (8 * i));
        memcpy(&buffer[13], item->name, STORE_NAME_LEN);
        len = 13 + STORE_NAME_LEN;
    }
    USBFS_SendEndpoint(USB_EP_TX, len);
    s_reply_pending = false;
}

uint8_t WebUSB_TakeRunRequest(void) {
    uint8_t r = s_run_request;
    s_run_request = STORE_NONE;
    return r;
}
