#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "bootloader.h"
#include "buttons.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "debug_output.h"
#include "factory.h"
#include "greet.h"
#include "infrared.h"
#include "app_host.h"
#include "menu.h"
#include "nametag.h"
#include "pattern_player.h"
#include "persist.h"
#include "power.h"
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

static void fill_halves(uint8_t top, uint8_t bottom) {
    for (uint8_t row = 0; row < LED_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < LED_MATRIX_COLS; col++) {
            LEDMatrix_SetPixel(row, col, row < LED_MATRIX_ROWS / 2 ? top : bottom);
        }
    }
}

// Hold A this long for the menu; in an app longer, since the app has A too
#define MENU_HOLD_MS 700
#define MENU_HOLD_APP_MS 1000

// What the LEDs showed when the menu opened, to put back when it closes
static uint8_t s_saved_leds[LED_MATRIX_ROWS * LED_MATRIX_COLS];

static void save_leds(void) {
    for (unsigned i = 0; i < sizeof s_saved_leds; i++) s_saved_leds[i] = LEDMatrix_GetPixel(i / LED_MATRIX_COLS, i % LED_MATRIX_COLS);
}

static void restore_leds(void) {
    for (unsigned i = 0; i < sizeof s_saved_leds; i++) LEDMatrix_SetPixel(i / LED_MATRIX_COLS, i % LED_MATRIX_COLS, s_saved_leds[i]);
}

static bool item_runs(void) { return g_app_mode == APP_MODE_FLASH_PATTERN || g_app_mode == APP_MODE_APP; }

// Into the USB boot ROM (UPDATE in the menu, or BOOT_ROM from the web interface),
// writing a pending change to the persistent record first
static void enter_boot_rom(void) {
    AppHost_Stop();
    Persist_Flush(PERSIST_SETTLE_MS);
    jump_to_bootloader();
}

// In APP_MODE_FLASH_PATTERN: a text item plays (nametag.h), not an animation.
// Both play from the store's flash, so the store treats them the same.
static bool s_text_plays;

// Starts store item `index`: an animation or a text plays, an app runs.
// Whatever ran before stops; if the item cannot start, the badge shows the
// name tag.
static void run_item(uint8_t index) {
    AppHost_Stop();
    g_app_mode = APP_MODE_NAMETAG;
    const store_item_t* item = Store_Item(index);
    if (!item) return;
    g_running_item = index;
    s_text_plays = item->type == STORE_TYPE_TEXT;
    if (item->type == STORE_TYPE_ANIMATION) {
        if (PatternPlayer_Start(Store_Payload(item), item->length)) g_app_mode = APP_MODE_FLASH_PATTERN;
    } else if (s_text_plays) {
        if (Nametag_StartText(Store_Payload(item), item->length)) g_app_mode = APP_MODE_FLASH_PATTERN;
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
    Power_Init();

    Persist_Init();
    // the brightness from last time (B / D or an app set it); full the first time
    LEDMatrix_SetBrightness(Persist_Brightness() ? Persist_Brightness() : 255);

    // Start the boot item from the store, unless a button is held: then
    // the badge starts without it (in case it is an app that hangs).
    // Without a boot item, the name tag.
    g_app_mode = APP_MODE_NAMETAG;
    // A badge whose factory test has not passed yet runs that first (the
    // emulator only when asked: its tests start on a new flash file)
#ifndef HH_EMU
    bool factory = !Persist_FactoryPassed();
#else
    bool factory = emu_factory_at_power_on && !Persist_FactoryPassed();
#endif
    if (factory) {
        Factory_Start();
    } else if (buttons_raw()) {
        buttons_ignore_held();  // that press is not a command
    } else if (Store_BootItem() != STORE_NONE) {
        run_item(Store_BootItem());
    }

    uint32_t last_tick = funSysTick32();
    uint32_t hold_ms = 0;   // how long A has been down
    uint8_t last_mode = 0;  // to start the name tag when it comes on
    bool menu_open = false;
    bool greeting = false;  // greet.h has the LEDs; what they showed before is saved
    uint8_t menu_mode = 0, menu_item = STORE_NONE;  // what ran when it opened

    while (1) {
        LEDMatrix_Show();  // what the previous pass drew (the app, the menu, the name tag, the host)
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

        if (WebUSB_BootRomDue(elapsed_ms)) enter_boot_rom();

        uint8_t buttons = buttons_poll();
        uint8_t held = buttons_down();

        // Banana shots: outside apps (which take their own) they are taken
        // every pass, so none shows later; the name tag shows them, only
        // while it is on (and not those left from an app: see below)
        ir_shot_t shot;
        while (g_app_mode != APP_MODE_APP && infrared_receive_shot(&shot))
            if (g_app_mode == APP_MODE_NAMETAG && !factory && !menu_open && !greeting)
                Nametag_Shot(shot.team, shot.command);

        // The factory test (factory.h) has the LEDs and the buttons until
        // it passes or is left; USB goes on working
        if (WebUSB_TakeFactoryRequest() && !factory) {
            AppHost_Stop();
            menu_open = false;
            g_app_mode = APP_MODE_NAMETAG;
            factory = true;
            Factory_Start();
        }
        if (factory) {
            factory_status_t f = Factory_Tick(elapsed_ms, buttons, held);
            if (f == FACTORY_PASSED) Persist_SetFactoryPassed(true);
            if (f == FACTORY_DONE) {
                factory = false;
                hold_ms = 0;
                buttons_ignore_held();
                g_app_mode = APP_MODE_NAMETAG;
                if (Store_BootItem() != STORE_NONE) run_item(Store_BootItem());
            }
            Delay_Ms(TICK_MS);
            continue;
        }

        // The menu (menu.h). While it is open, what ran before waits; a
        // command from the host that changes what runs closes it.
        if (menu_open && (g_app_mode != menu_mode || (item_runs() && g_running_item != menu_item))) menu_open = false;
        if (menu_open) {
            uint8_t item = STORE_NONE;
            menu_result_t r = Menu_Tick(elapsed_ms, buttons, held, &item);
            if (r != MENU_OPEN) {
                menu_open = false;
                hold_ms = 0;
                buttons_ignore_held();  // the press that closed it is not for what runs next
                if (r == MENU_RUN) {
                    run_item(item);
                } else if (r == MENU_NAMETAG) {
                    AppHost_Stop();
                    g_app_mode = APP_MODE_NAMETAG;
                } else if (r == MENU_UPDATE) {
                    enter_boot_rom();
                } else if (r == MENU_SAY_HI) {  // what ran goes on after the greeting
                    restore_leds();
                    Greet_SayHi();
                } else {  // KEEP or SLEEP: what ran goes on, as it was
                    restore_leds();
                    if (r == MENU_SLEEP) {
                        standby();
                        buttons_ignore_held();  // the press that woke the badge
                    }
                }
            }
            Delay_Ms(TICK_MS);
            continue;
        }

        // Hold A: the menu, also from an app (which gets the press of A too)
        hold_ms = held & HH_BTN_A ? hold_ms + elapsed_ms : 0;
        if (hold_ms >= (g_app_mode == APP_MODE_APP ? MENU_HOLD_APP_MS : MENU_HOLD_MS)) {
            if (greeting) {  // the menu puts back what showed before the greeting
                Greet_Cancel();
                restore_leds();
                greeting = false;
            }
            save_leds();
            menu_mode = g_app_mode;
            menu_item = g_running_item;
            Menu_Open(item_runs() ? g_running_item : STORE_NONE);
            menu_open = true;
            hold_ms = 0;
            Delay_Ms(TICK_MS);
            continue;
        }

        if (g_app_mode == APP_MODE_APP) {
            // The app has the buttons and the infrared. The name tag starts
            // afresh after it, which drops the shots the app left queued.
            last_mode = APP_MODE_APP;
            if (!AppHost_Tick(elapsed_ms, buttons, held)) {
                AppHost_Stop();
                g_app_mode = APP_MODE_NAMETAG;
                buttons_ignore_held();
            }
            Delay_Ms(TICK_MS);
            continue;
        }

        // Outside apps: the name swap (greet.h). While it shows, what ran
        // before waits; B, C or D ends it.
        bool greets = Greet_Tick(elapsed_ms);
        if (greets && (buttons & (HH_BTN_B | HH_BTN_C | HH_BTN_D))) {
            Greet_Cancel();
            greets = false;
            buttons = 0;  // that press is only for ending the greeting
        }
        if (greets != greeting) {
            if (greets)
                save_leds();
            else
                restore_leds();
            greeting = greets;
        }
        if (greeting) {
            Greet_Draw();
            last_mode = g_app_mode;
            Delay_Ms(TICK_MS);
            continue;
        }

        // Outside apps, B and D are the brightness
        if (buttons & HH_BTN_B) Menu_StepBrightness(1);
        if (buttons & HH_BTN_D) Menu_StepBrightness(-1);

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
            if (s_text_plays)
                Nametag_Tick(elapsed_ms);
            else
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
