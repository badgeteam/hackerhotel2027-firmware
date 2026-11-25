#include "webusb_protocol.h"
#include <stdbool.h>
#include <string.h>
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#ifdef HH_EMU
#include "emu_usb.h"  // tools/emu: the emulator stands in for the USB stack
#else
#include "fsusb.h"  // includes fsusb.c itself, so only include it in this file
#endif
#include "infrared.h"
#include "persist.h"
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
#define CMD_ITEM_DELETE 0x27
#define CMD_NAME_GET 0x30
#define CMD_NAME_SET 0x31
#define CMD_BRIGHTNESS 0x32
#define CMD_BOOT_ROM 0x40
#define CMD_FW_INFO 0x41
#define CMD_IR_RAW 0x50  // reply: count, then that many marks / spaces (infrared_raw_read)
#define CMD_FACTORY 0x52  // start the factory test (factory.h), as on a freshly flashed badge
#define CMD_IR_SHOT 0x51  // team, trigger, command, parameter: send a shot (tools/badge.py shoot)
#define CMD_QUERY 0xFF

#ifndef FW_VERSION
#define FW_VERSION "dev"  // the Makefile sets the git commit
#endif
// What FW_INFO returns, and a marker the web interface looks for in a .bin
// before it flashes it: only badge firmware has it. S1 is the item store's
// layout (flash_pages.h, store.h, persist.h); a firmware with another
// layout would not read the stored items, so it gets another number.
static const char s_fw_info[FW_INFO_LEN] = "HH27-STARFISH-FW S1 " FW_VERSION;

#define FRAME_PIXELS (LED_MATRIX_ROWS * LED_MATRIX_COLS)

volatile uint8_t g_app_mode = APP_MODE_CUSTOM;
volatile uint8_t g_running_item = STORE_NONE;

static uint8_t s_stage_frame[FRAME_PIXELS];
static volatile bool s_reply_pending;
static volatile uint8_t s_result;      // of the last command, for the reply
static volatile uint8_t s_info_item;   // ITEM_INFO asked about this item
static volatile bool s_name_reply;     // NAME_GET: the reply carries the name
static volatile bool s_fw_reply;       // FW_INFO: the reply carries s_fw_info
static volatile bool s_ir_raw_reply;   // IR_RAW: the reply carries the IR log
static volatile bool s_factory_request;  // FACTORY: the main loop starts the factory test
static volatile bool s_boot_request;   // BOOT_ROM: reboot once the reply is out
static uint32_t s_boot_wait_ms;
static volatile uint8_t s_run_request = STORE_NONE;

static void commit(void) {
    uint8_t index;
    s_result = (uint8_t)Store_Commit(&index);
    if (s_result == STORE_OK) {
        Store_SetBootItem(index);
        s_run_request = index;
    }
}

// The USB interrupt only copies a command here; the main loop runs it
// (WebUSB_Poll). Commands erase and program flash, which can take a long
// time, and the main loop writes flash too (persist.c): in the interrupt,
// the two could interleave their flash sequences, and a long interrupt
// holds up the USB stack.
#define CMD_MAX 64
static uint8_t s_cmd[CMD_MAX];
static volatile int s_cmd_len;  // > 0: a command waits for the main loop

// Called from the USB interrupt for every packet on the bulk OUT endpoint
void WebUSBBulkDataOut(uint8_t* data, int len) {
    if (len < 1) return;
    if (len > CMD_MAX) len = CMD_MAX;
    memcpy(s_cmd, data, (unsigned)len);
    s_cmd_len = len;
}

// Runs in the main loop (handle_command), so what plays can be changed
// here before its flash moves.
static void delete_item(uint8_t index) {
    bool on = g_app_mode == APP_MODE_FLASH_PATTERN || g_app_mode == APP_MODE_APP;
    bool animation = g_app_mode == APP_MODE_FLASH_PATTERN;
    uint8_t running = g_running_item;
    // The deleted item stops. An animation after it plays from flash that
    // moves: it stops, and starts again at its new index. An app runs
    // from RAM, so it keeps running.
    if (on && Store_Item(index) && (running == index || (animation && running > index))) g_app_mode = APP_MODE_CUSTOM;
    s_result = (uint8_t)Store_Delete(index);
    if (s_result == STORE_OK && on && running == index) g_app_mode = APP_MODE_NAMETAG;
    if (s_result != STORE_OK || !on || running == index || running < index) return;
    if (animation)
        s_run_request = running - 1;
    else
        g_running_item = running - 1;
}

static void handle_command(const uint8_t* data, int len) {
    s_result = STORE_OK;
    s_info_item = STORE_NONE;
    s_name_reply = false;
    s_fw_reply = false;
    s_ir_raw_reply = false;

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
            g_app_mode = APP_MODE_NAMETAG;
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
            g_app_mode = APP_MODE_NAMETAG;
            break;

        case CMD_ITEM_DELETE:
            if (len < 2) {
                s_result = STORE_ERR_ARGS;
            } else {
                delete_item(data[1]);
            }
            break;

        case CMD_NAME_GET:
            s_name_reply = true;
            break;

        case CMD_BOOT_ROM:
            s_boot_request = true;
            s_boot_wait_ms = 0;
            break;

        case CMD_FW_INFO:
            s_fw_reply = true;
            break;

        case CMD_IR_RAW:
            s_ir_raw_reply = true;
            break;

        case CMD_FACTORY:
            s_factory_request = true;
            break;

        case CMD_IR_SHOT:
            if (len < 5) {
                s_result = STORE_ERR_ARGS;
            } else {
                ir_shot_t shot = {data[1], data[2], data[3], data[4]};
                if (!infrared_send_shot(IrShot_Encode(&shot))) s_result = STORE_ERR_ARGS;  // busy
            }
            break;

        case CMD_NAME_SET:
            Persist_SetName((const char*)&data[1], (unsigned)(len - 1));
            s_name_reply = true;
            break;

        case CMD_BRIGHTNESS:  // the main loop saves it (Persist_SetBrightness), as it does B and D
            if (len < 2 || !data[1])
                s_result = STORE_ERR_ARGS;
            else
                LEDMatrix_SetBrightness(data[1]);
            break;

        case CMD_QUERY:
        default:
            break;
    }

    s_reply_pending = true;
}

void WebUSB_Init(void) {
    s_reply_pending = false;
    s_cmd_len = 0;
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

static void drop_unread_reply(void) {
#ifndef HH_EMU  // the emulator's endpoint is never busy
    if (FSUSBCTX.USBFS_Endp_Busy[USB_EP_TX]) {
        USBFSD_UEP_TX_CTRL(USB_EP_TX) = (USBFSD_UEP_TX_CTRL(USB_EP_TX) & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
        FSUSBCTX.USBFS_Endp_Busy[USB_EP_TX] = 0;
    }
#endif
}

void WebUSB_HandleCommand(void) {
    if (s_cmd_len) {
        uint8_t cmd[CMD_MAX];
        NVIC_DisableIRQ(USBFS_IRQn);
        int len = s_cmd_len;
        memcpy(cmd, s_cmd, (unsigned)len);
        s_cmd_len = 0;
        // The host sends a command only after it has read the reply to the
        // previous one, or has given up on it (a timeout, a closed page).
        // A reply still armed now is one nobody reads: drop it, so this
        // command's reply is the one the host gets.
        drop_unread_reply();
        NVIC_EnableIRQ(USBFS_IRQn);
        handle_command(cmd, len);
    }
}

void WebUSB_Poll(void) {
    // The reply waits here until the endpoint is free
    if (!s_reply_pending) return;
    uint8_t* buffer = USBFS_GetEPBufferIfAvailable(USB_EP_TX);
    if (!buffer) return;
    buffer[0] = g_app_mode;
    buffer[1] = LEDMatrix_GetBrightness();
    buffer[2] = s_result;
    buffer[3] = Store_Count();
    buffer[4] = g_app_mode == APP_MODE_FLASH_PATTERN || g_app_mode == APP_MODE_APP ? g_running_item : STORE_NONE;
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
    } else if (s_name_reply) {
        memset(&buffer[8], 0, PERSIST_NAME_LEN);
        memcpy(&buffer[8], Persist_Name(), strlen(Persist_Name()));
        len = 8 + PERSIST_NAME_LEN;
    } else if (s_fw_reply) {
        memcpy(&buffer[8], s_fw_info, FW_INFO_LEN);
        len = 8 + FW_INFO_LEN;
    } else if (s_ir_raw_reply) {
        uint16_t raw[27];  // 8 + 1 + 2 * 27 = 63 bytes
        unsigned n = infrared_raw_read(raw, 27);
        buffer[8] = (uint8_t)n;
        for (unsigned i = 0; i < n; i++) buffer[9 + 2 * i] = (uint8_t)raw[i], buffer[10 + 2 * i] = (uint8_t)(raw[i] >> 8);
        len = 9 + 2 * (int)n;
    }
    USBFS_SendEndpoint(USB_EP_TX, len);
    s_reply_pending = false;
}

bool WebUSB_BootRomDue(uint32_t elapsed_ms) {
    if (!s_boot_request) return false;
    s_boot_wait_ms += elapsed_ms;
#ifndef HH_EMU
    bool reply_out = !s_reply_pending && !FSUSBCTX.USBFS_Endp_Busy[USB_EP_TX];
#else
    bool reply_out = !s_reply_pending;
#endif
    // once the host has the reply, or after 200 ms if it does not read it
    return reply_out || s_boot_wait_ms >= 200;
}

bool WebUSB_TakeFactoryRequest(void) {
    bool r = s_factory_request;
    s_factory_request = false;
    return r;
}

uint8_t WebUSB_TakeRunRequest(void) {
    uint8_t r = s_run_request;
    s_run_request = STORE_NONE;
    return r;
}
