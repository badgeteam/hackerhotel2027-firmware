#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "bootloader.h"
#include "buttons.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "debug_output.h"
#include "infrared.h"
#include "app_host.h"
#include "nametag.h"
#include "pattern_player.h"
#include "persist.h"
#include "standby.h"
#include "store.h"
#include "webusb_protocol.h"

#define TICK_MS 5

int _write(int fd, const char* buf, int size) {
    // This is an override for the built-in write function, to prevent stalling
    // when the debug probe is not connected
    (void)fd;
    debug_output_write(buf, size);
    return size;
}

#define IR_ADDRESS           0x00
#define IR_COMMAND_TOP_ON    0x01  // first half of the rows 255, second half 0
#define IR_COMMAND_BOTTOM_ON 0x02  // first half of the rows 0, second half 255

static const uint8_t s_brightness_levels[] = {8, 16, 32, 64, 128, 255};
#define BRIGHTNESS_LEVEL_COUNT (sizeof(s_brightness_levels) / sizeof(s_brightness_levels[0]))

// The next brightness level up (dir 1) or down (dir -1) from the current
// brightness, which an app may have set to anything in between
static void step_brightness(int dir) {
    uint8_t now = LEDMatrix_GetBrightness();
    for (unsigned k = 0; k < BRIGHTNESS_LEVEL_COUNT; k++) {
        unsigned i = dir > 0 ? k : BRIGHTNESS_LEVEL_COUNT - 1 - k;
        if (dir > 0 ? s_brightness_levels[i] > now : s_brightness_levels[i] < now) {
            LEDMatrix_SetBrightness(s_brightness_levels[i]);
            return;
        }
    }
}

static void fill_halves(uint8_t top, uint8_t bottom) {
    for (uint8_t row = 0; row < LED_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < LED_MATRIX_COLS; col++) {
            LEDMatrix_SetPixel(row, col, row < LED_MATRIX_ROWS / 2 ? top : bottom);
        }
    }
}

// Leaving an app on the badge: hold A and C together this long
#define APP_EXIT_CHORD (HH_BTN_A | HH_BTN_C)
#define APP_EXIT_MS 1000

// Starts store item `index`: an animation plays, an app runs. Whatever ran
// before stops; if the item cannot start, the badge shows the name tag.
static void run_item(uint8_t index) {
    AppHost_Stop();
    g_app_mode = APP_MODE_NAMETAG;
    const store_item_t* item = Store_Item(index);
    if (!item) return;
    g_running_item = index;
    if (item->type == STORE_TYPE_ANIMATION) {
        PatternPlayer_Start(Store_Payload(item));
        g_app_mode = APP_MODE_FLASH_PATTERN;
    } else if (item->type == STORE_TYPE_APP && AppHost_Start(Store_Payload(item), item->length, item->name)) {
        g_app_mode = APP_MODE_APP;
    }
}

int main(void) {
    SystemInit();
    funGpioInitAll();

    LEDMatrix_Init();
    buttons_init();
    WebUSB_Init();
    infrared_init();

    Persist_Init();
    // the brightness from last time (B / D or an app set it); full the first time
    LEDMatrix_SetBrightness(Persist_Brightness() ? Persist_Brightness() : 255);

    // Start the boot item from the store, unless a button is held: then
    // the badge starts without it (in case it is an app that hangs).
    // Without a boot item, the name tag.
    g_app_mode = APP_MODE_NAMETAG;
    if (buttons_raw()) {
        buttons_ignore_held();  // that press is not a command
    } else if (Store_BootItem() != STORE_NONE) {
        run_item(Store_BootItem());
    } else if (Store_LegacyPattern()) {  // stored by the firmware before the item store
        PatternPlayer_Start(Store_LegacyPattern());
        g_app_mode = APP_MODE_FLASH_PATTERN;
    }

    bool states[4] = {0};
    uint32_t last_tick = funSysTick32();
    uint32_t chord_ms = 0;
    uint8_t last_mode = 0;  // to start the name tag when it comes on

    while (1) {
        WebUSB_HandleCommand();
        uint8_t run = WebUSB_TakeRunRequest();
        if (run != STORE_NONE) run_item(run);
        // a command from the host (custom mode, STOP, erase) ends the app
        if (g_app_mode != APP_MODE_APP) AppHost_Stop();
        WebUSB_Poll();

        uint32_t elapsed_ms = (funSysTick32() - last_tick) / DELAY_MS_TIME;
        last_tick += elapsed_ms * DELAY_MS_TIME;

        // keep what the host, the app or the buttons changed
        Persist_SetBrightness(LEDMatrix_GetBrightness());
        Persist_Flush(elapsed_ms);

        uint8_t buttons = buttons_poll();
        if (g_app_mode == APP_MODE_APP) {
            // The app has the buttons and the infrared; only A + C held is ours
            uint8_t held = buttons_down();
            chord_ms = (held & APP_EXIT_CHORD) == APP_EXIT_CHORD ? chord_ms + elapsed_ms : 0;
            if (chord_ms >= APP_EXIT_MS || !AppHost_Tick(elapsed_ms, buttons, held)) {
                AppHost_Stop();
                g_app_mode = APP_MODE_NAMETAG;
                buttons_ignore_held();
                chord_ms = 0;
            }
            Delay_Ms(TICK_MS);
            continue;
        }

        for (uint8_t i = 0; i < 4; i++) {
            if ((buttons >> i) & 1) {
                states[i] = !states[i];
            }
        }
        if (states[0]) {
            standby();
            states[0] = false;      // otherwise we go straight back to sleep
            buttons_ignore_held();  // don't act on the press that woke us
        }

        if (states[1]) {
            jump_to_bootloader();
        }

        if (states[2]) {
            states[2] = false;
            step_brightness(1);
        }
        if (states[3]) {
            states[3] = false;
            step_brightness(-1);
        }

        infrared_nec_message_t message;
        while (infrared_receive(&message)) {
            if (message.address != IR_ADDRESS || message.repeat) continue;
            if (message.command == IR_COMMAND_TOP_ON) {
                fill_halves(255, 0);
                g_app_mode = APP_MODE_CUSTOM;
            }
            if (message.command == IR_COMMAND_BOTTOM_ON) {
                fill_halves(0, 255);
                g_app_mode = APP_MODE_CUSTOM;
            }
        }

        if (g_app_mode == APP_MODE_FLASH_PATTERN) {
            PatternPlayer_Tick(elapsed_ms);
        }
        if (g_app_mode == APP_MODE_NAMETAG) {
            if (last_mode != APP_MODE_NAMETAG) Nametag_Start();
            Nametag_Tick(elapsed_ms);
        }
        last_mode = g_app_mode;

        Delay_Ms(TICK_MS);
    }
}
