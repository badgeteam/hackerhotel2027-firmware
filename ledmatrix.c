#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bootloader.h"
#include "buttons.h"
#include "ch32fun.h"
#include "charge.h"
#include "ch643_ledpwm.h"
#include "debug_output.h"
#include "infrared.h"
#include "app_host.h"
#include "layout.h"
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
    for (unsigned slot = 0; slot < LED_MATRIX_SLOTS; slot++) LEDMatrix_SetSlot(slot, slot < LED_MATRIX_SLOTS / 2 ? top : bottom);
}

// Hold A this long for the menu; in an app longer, since the app has A too
#define MENU_HOLD_MS 700
#define MENU_HOLD_APP_MS 1000

// A badge that has never saved a brightness starts at 32 of 255 (12.5 %),
// as the old firmware did: full brightness dazzles and drains the battery
#define START_BRIGHTNESS 32

static uint8_t own_brightness(void) { return Persist_Brightness() ? Persist_Brightness() : START_BRIGHTNESS; }

// B or D outside apps: the brightness level shows this long over what runs
#define LEVEL_SHOW_MS 1000
// C outside apps: a press shorter than this, with D up all the time, is a
// tap and shows the next animation or text (browse_next). A longer press
// is no tap, and C + D together is the boot ROM chord, never a tap.
#define BROWSE_TAP_MS 500

// What the LEDs showed when the menu opened, to put back when it closes
static uint8_t s_saved_leds[LED_MATRIX_ROWS * LED_MATRIX_COLS];

static void save_leds(void) {
    LEDMatrix_GetFrame(s_saved_leds);
}

static void restore_leds(void) {
    LEDMatrix_SetFrame(s_saved_leds);
}

static bool item_runs(void) { return g_app_mode == APP_MODE_FLASH_PATTERN || g_app_mode == APP_MODE_APP; }

// Into the USB boot ROM (UPDATE in the menu, or BOOT_ROM from the web interface),
// writing a pending change to the persistent record first
static void enter_boot_rom(void) {
    AppHost_Stop();
    Persist_Flush(PERSIST_SETTLE_MS);
    jump_to_bootloader();
}

// Hold C + D this long, anywhere, for the boot ROM: also on a badge whose
// menu does not work. No app makes people hold both for this long.
#define BOOT_HOLD_MS 3000
#define BOOT_ARROW_MS 1000  // then the arrow runs this long

// An arrow runs down the grid for a second, so people see that the held
// C + D works. Then the badge goes into the boot ROM. `brightness` is the
// brightness from before C or D went down: outside apps, the press of D
// that started the chord also made the LEDs one step darker.
static void boot_rom_arrow(uint8_t brightness) {
    LEDMatrix_SetBrightness(brightness);
    Persist_SetBrightness(brightness);
    for (uint32_t ms = 0; ms < BOOT_ARROW_MS; ms += TICK_MS) {
        LEDMatrix_Clear();
        int shift = (int)(ms * 4 / BOOT_ARROW_MS);  // 4 rows in the second
        // the shaft 3 wide and 9 high, then the head: 7, 5, 3 and 1 wide
        for (int y = 0; y < 13; y++) {
            int half = y < 9 ? 1 : 12 - y;
            for (int x = HH_GRID_W / 2 - half; x <= HH_GRID_W / 2 + half; x++) {
                uint16_t slot = layout_grid[y + shift][x];
                LEDMatrix_SetSlot(slot, 255);
            }
        }
        LEDMatrix_Show();
        Delay_Ms(TICK_MS);
    }
    enter_boot_rom();
}

// In APP_MODE_FLASH_PATTERN: a text item plays (nametag.h), not an animation.
// Both play from the store's flash, so the store treats them the same.
static bool s_text_plays;

// The factory test (docs/FACTORY_TEST.md) is an app, apps/factory, stored
// as FACTORY: `make factory-image` puts it in a new badge's store, and
// tools/badge.py factory stores it. It runs at power-on until the test has
// passed. It runs at full brightness: that is not kept, and the badge's
// own comes back after it.
static bool s_factory;  // the factory app runs

static void factory_end(void) {
    if (s_factory) LEDMatrix_SetBrightness(own_brightness());
    s_factory = false;
}

// The factory app's index in the store, or STORE_NONE. Once the test has
// passed (the app records that, persist.h), the app goes from the store:
// after the pass screen, or at the next power-on if the badge lost power
// or the host stopped the app before that.
static uint8_t factory_item(void) {
    for (uint8_t i = 0; i < Store_Count(); i++) {
        const store_item_t* item = Store_Item(i);
        if (item->type != STORE_TYPE_APP || memcmp(item->name, "FACTORY", 8)) continue;
        if (!Persist_FactoryPassed()) return i;
        Store_Delete(i);
        break;
    }
    return STORE_NONE;
}

// Starts store item `index`: an animation or a text plays, an app runs.
// Whatever ran before stops; if the item cannot start, the badge shows the
// name tag.
static void run_item(uint8_t index) {
    AppHost_Stop();
    factory_end();
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
        s_factory = !memcmp(item->name, "FACTORY", 8);
    }
}

// A tap of C outside apps: the next stop in the ring of the name tag, then
// every animation and text in store order, then the name tag again. Apps
// are not in the ring: an app keeps C for itself, so browsing into one
// would end the browsing (leave an app with the menu). From the name tag or
// the host's drawing (custom mode): the first animation or text. An item
// that cannot start (the store checks less than the player) is skipped too,
// so it does not hide the items after it.
static void browse_next(void) {
    int next = g_app_mode == APP_MODE_FLASH_PATTERN ? g_running_item + 1 : 0;
    for (; next < Store_Count(); next++) {
        const store_item_t* item = Store_Item((uint8_t)next);
        if (item && item->type != STORE_TYPE_APP) {
            run_item((uint8_t)next);
            if (g_app_mode == APP_MODE_FLASH_PATTERN) return;
        }
    }
    g_app_mode = APP_MODE_NAMETAG;
}

int main(void) {
    SystemInit();
    funGpioInitAll();
    Layout_Init();  // the LEDs' gamma, before the row scan starts

    LEDMatrix_Init();
    buttons_init();
    WebUSB_Init();
    infrared_init();
    Power_Init();

    Persist_Init();
    // the brightness from last time (B / D, the host or an app set it)
    LEDMatrix_SetBrightness(own_brightness());

    // Start the boot item from the store, unless a button is held: then
    // the badge starts without it (in case it is an app that hangs).
    // Without a boot item, the name tag.
    g_app_mode = APP_MODE_NAMETAG;
    // The factory test first, while it is in the store and has not passed
    uint8_t factory = factory_item();
    if (factory != STORE_NONE) {
        run_item(factory);
    } else if (buttons_raw()) {
        buttons_ignore_held();  // that press is not a command
    } else if (Store_BootItem() != STORE_NONE) {
        run_item(Store_BootItem());
    }

    uint32_t last_tick = funSysTick32();
    uint32_t hold_ms = 0;   // how long A has been down
    uint32_t boot_ms = 0;   // how long C + D have been down together
    uint8_t chord_brightness = own_brightness();  // from before C or D went down
    uint8_t last_mode = 0;  // to start the name tag when it comes on
    bool menu_open = false;
    uint32_t level_ms = 0;     // the brightness level shows this much longer
    bool level_drawn = false;  // the previous pass drew it over what runs
    bool outside = false;      // the previous pass ran outside apps, to its end
    uint32_t c_ms = 0;         // how long C has been down
    bool c_tap = false;        // C went down outside apps, and so far it is a tap
    bool charge = false;  // charge.h has the LEDs; what they showed before is saved
    // what ran when the menu opened, or the charging screen started (never both at once)
    uint8_t menu_mode = 0, menu_item = STORE_NONE;

    while (1) {
        LEDMatrix_Show();  // what the previous pass drew (the app, the menu, the name tag, the host)
        if (level_drawn) restore_leds();  // what runs keeps its own frame
        level_drawn = false;
        bool was_outside = outside;
        outside = false;
        WebUSB_HandleCommand();
        uint8_t run = WebUSB_TakeRunRequest();
        if (run != STORE_NONE) run_item(run);
        // a command from the host (custom mode, STOP, erase) ends the app
        if (g_app_mode != APP_MODE_APP) AppHost_Stop();
        if (!AppHost_Running()) factory_end();
        WebUSB_Poll();

        uint32_t elapsed_ms = (funSysTick32() - last_tick) / DELAY_MS_TIME;
        last_tick += elapsed_ms * DELAY_MS_TIME;
        level_ms = level_ms > elapsed_ms ? level_ms - elapsed_ms : 0;

        // keep what the host, the app or the buttons changed (not the
        // factory test's full brightness: factory_end() puts the badge's back)
        if (!s_factory) Persist_SetBrightness(LEDMatrix_GetBrightness());
        Persist_Flush(elapsed_ms);

        if (WebUSB_BootRomDue(elapsed_ms)) enter_boot_rom();

        // Charging starts: the charging screen (charge.h), but not over an
        // app (the factory test is one), what the host drew (custom mode) or
        // the menu
        if (Power_Tick(elapsed_ms) && g_app_mode != APP_MODE_APP && g_app_mode != APP_MODE_CUSTOM && !menu_open) {
            save_leds();
            menu_mode = g_app_mode;  // the menu is closed, so its record is free
            menu_item = g_running_item;
            Charge_Start();
            charge = true;
        }
        // While the charging screen or the menu is on, what ran before waits;
        // a command from the host that changes what runs ends either
        if ((charge || menu_open) && (g_app_mode != menu_mode || (item_runs() && g_running_item != menu_item)))
            charge = menu_open = false;

        uint8_t buttons = buttons_poll();
        uint8_t held = buttons_down();

        // Hold C + D: the boot ROM, from the name tag, the menu, an app or
        // the factory test. The brightness is kept from before the chord,
        // but the badge's own from the factory test (not its full brightness).
        if (!(held & (HH_BTN_C | HH_BTN_D))) chord_brightness = s_factory ? own_brightness() : LEDMatrix_GetBrightness();
        boot_ms = (held & (HH_BTN_C | HH_BTN_D)) == (HH_BTN_C | HH_BTN_D) ? boot_ms + elapsed_ms : 0;
        if (boot_ms >= BOOT_HOLD_MS) boot_rom_arrow(chord_brightness);

        // Banana shots: outside apps (which take their own) they are taken
        // every pass, so none shows later; the name tag shows them, only
        // while it is on (and not those left from an app: see below)
        ir_shot_t shot;
        while (g_app_mode != APP_MODE_APP && infrared_receive_shot(&shot))
            if (g_app_mode == APP_MODE_NAMETAG && !menu_open && !charge)
                Nametag_Shot(shot.team, shot.command);

        // The menu (menu.h). While it is open, what ran before waits; a
        // command from the host that changes what runs closes it (above).
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

        // Hold A: the menu, also from an app (which gets the press of A too),
        // but not from the factory test
        hold_ms = held & HH_BTN_A && !s_factory ? hold_ms + elapsed_ms : 0;
        if (hold_ms >= (g_app_mode == APP_MODE_APP ? MENU_HOLD_APP_MS : MENU_HOLD_MS)) {
            if (charge) {  // the menu puts back what the charging screen covered (A held since before it came)
                restore_leds();
                charge = false;
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
                if (s_factory) {  // the factory test is over: then the boot item, as at power-on
                    factory_end();
                    uint8_t left = factory_item();  // still stored: left, not passed
                    uint8_t boot = Store_BootItem();
                    if (boot != STORE_NONE && boot != left) run_item(boot);
                }
            }
            Delay_Ms(TICK_MS);
            continue;
        }

        // Outside apps the firmware has no use for packets (an app's, such
        // as the NAMETAG app's name swap): drop them, so none waits for the
        // next app
        infrared_packet_t packet;
        while (infrared_receive_packet(&packet)) {}

        // The charging screen: what ran before waits. A button ends it, and
        // that press does nothing else; the end of charging ends it too.
        if (charge && (buttons || !Power_Charging())) {
            restore_leds();
            charge = false;
            buttons = 0;
        }
        if (charge) {
            Charge_Draw(elapsed_ms);
            last_mode = g_app_mode;
            Delay_Ms(TICK_MS);
            continue;
        }

        // Outside apps, B and D are the brightness
        if (buttons & HH_BTN_B) Menu_StepBrightness(1);
        // (not D while C is down: that is the start of the boot ROM chord)
        if ((buttons & HH_BTN_D) && !(held & HH_BTN_C)) Menu_StepBrightness(-1);

        // and the level shows for a moment (at the end of the pass)
        if (buttons & (HH_BTN_B | HH_BTN_D)) level_ms = LEVEL_SHOW_MS;

        // A tap of C shows the next animation or text (browse_next), on its
        // release. B and D stay the brightness, as the menu's BUTTONS setting
        // only changes the menu, so C alone browses. Not a tap: a press that
        // started in an app, the menu or the factory test; a long press; D
        // down at any time during it (C + D is a chord).
        if (!was_outside) c_tap = false;
        if (buttons & HH_BTN_C) {
            c_tap = true;
            c_ms = 0;
        }
        if (held & HH_BTN_C) {
            c_ms += elapsed_ms;
            if ((held & HH_BTN_D) || c_ms >= BROWSE_TAP_MS) c_tap = false;
        } else if (c_tap) {
            c_tap = false;
            browse_next();
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
        outside = g_app_mode != APP_MODE_APP;
        // The brightness level goes over the frame only for the next
        // LEDMatrix_Show(): then what was under it comes back, so what runs
        // needs to draw nothing again when the level goes. The saved LEDs
        // are free here: the menu and the charging screen keep them only
        // while they are on, and neither is on at the end of this pass.
        if (level_ms && outside) {
            save_leds();
            Menu_DrawBrightness();
            level_drawn = true;
        }

        Delay_Ms(TICK_MS);
    }
}
