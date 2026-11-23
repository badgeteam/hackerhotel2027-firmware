#include "menu.h"
#include <string.h>
#include "apps/hhapp.h"
#include "ch643_ledpwm.h"
#include "font.h"
#include "layout.h"
#include "persist.h"
#include "store.h"

#define FRAME_MS 33              // about 30 frames per second
#define HOLD_MS 700              // a press this long is a hold, shorter a tap
#define MENU_TIMEOUT_MS 20000    // no button: the menu closes
#define EDIT_TIMEOUT_MS 60000    // no button: the editor closes without saving
#define REPEAT_DELAY_MS 400      // previous / next held: repeat after this
#define REPEAT_MS 110            // and then every this
#define TEXT_Y 4                 // the label's top row
#define KEY_NEXT HH_BTN_D

// The menu's entries: the name tag, the store's items, then the settings
enum { ENTRY_NAMETAG, ENTRY_ITEM, ENTRY_EDIT, ENTRY_BRIGHTNESS, ENTRY_KEYS, ENTRY_SLEEP, ENTRY_UPDATE };
#define ENTRIES_AFTER_ITEMS 5

// The characters of the name editor, in the order previous / next step
// through them
static const char s_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.!?'*:/#@&+_";

static const uint8_t s_levels[] = {8, 16, 32, 64, 128, 255};  // brightness

static unsigned s_cursor;  // the entry shown
static bool s_editing;
static uint32_t s_shown_ms;  // since the entry (or the editor's position) changed
static uint32_t s_idle_ms;   // since a button was last down
static uint32_t s_frame_ms = FRAME_MS;

static uint32_t s_a_ms;  // how long A has been down
static bool s_a_down, s_a_held, s_a_ignore;
static uint8_t s_repeat_key;
static uint32_t s_repeat_ms;

static char s_name[HH_NAME_MAX + 1];  // the editor's name, blank-padded
static uint8_t s_pos;                 // the editor's position

static uint8_t key_prev(void) { return Persist_Keys() == MENU_KEYS_VERTICAL ? HH_BTN_B : HH_BTN_C; }
static uint8_t key_back(void) { return Persist_Keys() == MENU_KEYS_VERTICAL ? HH_BTN_C : HH_BTN_B; }

static unsigned entry_count(void) { return 1u + Store_Count() + ENTRIES_AFTER_ITEMS; }

static int entry_kind(unsigned e, uint8_t* item) {
    unsigned items = Store_Count();
    if (e == 0) return ENTRY_NAMETAG;
    if (e <= items) {
        *item = (uint8_t)(e - 1);
        return ENTRY_ITEM;
    }
    return ENTRY_EDIT + (int)(e - items - 1);
}

// Tap (1) or hold (2) of A, or nothing (0)
static int a_gesture(uint32_t elapsed_ms, uint8_t held) {
    bool down = held & HH_BTN_A;
    if (s_a_ignore) {  // the hold that opened the menu: wait for its release
        if (!down) s_a_ignore = false;
        return 0;
    }
    if (down) {
        s_a_ms = s_a_down ? s_a_ms + elapsed_ms : 0;
        s_a_down = true;
        if (!s_a_held && s_a_ms >= HOLD_MS) {
            s_a_held = true;
            return 2;
        }
        return 0;
    }
    int tap = s_a_down && !s_a_held;
    s_a_down = s_a_held = false;
    return tap;
}

// -1 / 1 for previous / next: on the press, and repeated while it is held
static int step(uint32_t elapsed_ms, uint8_t pressed, uint8_t held) {
    uint8_t prev = key_prev();
    if (pressed & (prev | KEY_NEXT)) {
        s_repeat_key = pressed & KEY_NEXT ? KEY_NEXT : prev;
        s_repeat_ms = 0;
        return s_repeat_key == KEY_NEXT ? 1 : -1;
    }
    if (!s_repeat_key || !(held & s_repeat_key)) {
        s_repeat_key = 0;
        return 0;
    }
    s_repeat_ms += elapsed_ms;
    if (s_repeat_ms < REPEAT_DELAY_MS) return 0;
    s_repeat_ms -= REPEAT_MS;
    return s_repeat_key == KEY_NEXT ? 1 : -1;
}

void Menu_StepBrightness(int dir) {
    uint8_t now = LEDMatrix_GetBrightness();
    unsigned n = sizeof s_levels;
    for (unsigned k = 0; k < n; k++) {
        unsigned i = dir > 0 ? k : n - 1 - k;
        if (dir > 0 ? s_levels[i] > now : s_levels[i] < now) {
            LEDMatrix_SetBrightness(s_levels[i]);
            return;
        }
    }
}

static void set_slot(uint16_t slot, uint8_t v) { LEDMatrix_SetPixel(slot / HH_COLS, slot % HH_COLS, v); }

static void arms(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        set_slot(layout_rgb[i].r, r);
        set_slot(layout_rgb[i].g, g);
        set_slot(layout_rgb[i].b, b);
    }
}

// The outline lights the part from..to of 0..total, clockwise from the top
static void outline(unsigned from, unsigned to, unsigned total) {
    for (unsigned j = 0; j < HH_EDGE_COUNT; j++) {
        unsigned at = j * total;
        set_slot(layout_edge[j], at >= from * HH_EDGE_COUNT && at < to * HH_EDGE_COUNT ? 200 : 0);
    }
}

static void copy_label(char* dst, const char* src, unsigned max) {
    unsigned i = 0;
    for (; i < max && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void draw_menu(void) {
    LEDMatrix_Clear();
    char label[STORE_NAME_LEN + 1];
    uint8_t item = 0;
    int kind = entry_kind(s_cursor, &item);
    const store_item_t* it = kind == ENTRY_ITEM ? Store_Item(item) : NULL;
    switch (kind) {
        case ENTRY_NAMETAG: copy_label(label, "NAME TAG", sizeof label - 1); break;
        case ENTRY_ITEM: copy_label(label, it ? it->name : "?", STORE_NAME_LEN); break;
        case ENTRY_EDIT: copy_label(label, "EDIT NAME", sizeof label - 1); break;
        case ENTRY_BRIGHTNESS: copy_label(label, "BRIGHTNESS", sizeof label - 1); break;
        case ENTRY_KEYS:
            copy_label(label, Persist_Keys() == MENU_KEYS_VERTICAL ? "BUTTONS B D" : "BUTTONS C D", sizeof label - 1);
            break;
        case ENTRY_SLEEP: copy_label(label, "SLEEP", sizeof label - 1); break;
        default: copy_label(label, "UPDATE", sizeof label - 1); break;
    }
    Text_DrawLabel(label, TEXT_Y, s_shown_ms, 255);
    outline(s_cursor, s_cursor + 1, entry_count());
    // The arms show the group: the name tag orange, animations purple,
    // apps green, the badge's settings white
    if (kind == ENTRY_NAMETAG)
        arms(255, 110, 0);
    else if (kind == ENTRY_ITEM && it && it->type == STORE_TYPE_ANIMATION)
        arms(150, 0, 255);
    else if (kind == ENTRY_ITEM)
        arms(0, 255, 60);
    else
        arms(120, 120, 120);
}

static unsigned name_length(void) {  // without the blanks at the end
    unsigned n = HH_NAME_MAX;
    while (n && s_name[n - 1] == ' ') n--;
    return n;
}

static void draw_editor(void) {
    LEDMatrix_Clear();
    // The name, with the character being changed bright in the middle and
    // a blinking line under it
    int x = (HH_GRID_W - FONT_W) / 2 - s_pos * FONT_ADVANCE;
    Text_Draw(x, TEXT_Y, s_name, 80);
    char c[2] = {s_name[s_pos], 0};
    Text_Draw(x + s_pos * FONT_ADVANCE, TEXT_Y, c, 255);
    if (s_shown_ms % 600 < 400)
        for (int dx = 0; dx < FONT_W; dx++) set_slot(layout_grid[TEXT_Y + FONT_H][(HH_GRID_W - FONT_W) / 2 + dx], 255);
    unsigned len = name_length();
    outline(0, s_pos + 1u, (len > s_pos ? len : s_pos) + 1u);  // how far into the name
    arms(120, 120, 120);
}

static void start_editor(void) {
    memset(s_name, ' ', HH_NAME_MAX);
    s_name[HH_NAME_MAX] = 0;
    const char* now = Persist_Name();
    for (unsigned i = 0; i < HH_NAME_MAX && now[i]; i++) s_name[i] = now[i];
    s_pos = 0;
    s_editing = true;
    s_shown_ms = 0;
}

static void edit_char(int dir) {
    const char* at = strchr(s_chars, s_name[s_pos]);
    int n = (int)sizeof s_chars - 1;
    int i = at && s_name[s_pos] ? (int)(at - s_chars) : 0;
    s_name[s_pos] = s_chars[(i + dir + n) % n];
}

void Menu_Open(uint8_t current) {
    s_editing = false;
    s_cursor = current < Store_Count() ? 1u + current : 0;
    s_shown_ms = s_idle_ms = 0;
    s_frame_ms = FRAME_MS;
    s_a_ignore = true;  // A is still down from the hold that opened the menu
    s_a_down = s_a_held = false;
    s_repeat_key = 0;
}

menu_result_t Menu_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held, uint8_t* item) {
    int a = a_gesture(elapsed_ms, held);
    int dir = step(elapsed_ms, pressed, held);
    s_idle_ms = held ? 0 : s_idle_ms + elapsed_ms;
    s_shown_ms += elapsed_ms;

    if (s_editing) {
        if (s_idle_ms >= EDIT_TIMEOUT_MS) return MENU_KEEP;  // not saved
        if (dir) edit_char(dir);
        unsigned len = name_length();
        if (pressed & key_back()) s_pos = (uint8_t)(s_pos ? s_pos - 1u : len < HH_NAME_MAX ? len : HH_NAME_MAX - 1u);
        if (a == 1) {  // next position; one blank after the end, back to the start
            s_pos = (uint8_t)(s_pos >= len || s_pos + 1 >= HH_NAME_MAX ? 0 : s_pos + 1);
            s_shown_ms = 0;
        }
        if (a == 2) {
            Persist_SetName(s_name, len);
            return MENU_NAMETAG;
        }
    } else {
        unsigned count = entry_count();
        if (s_cursor >= count) s_cursor = count - 1;  // the store changed
        if (a == 2 || s_idle_ms >= MENU_TIMEOUT_MS) return MENU_KEEP;
        if (dir) {
            s_cursor = (s_cursor + count + (unsigned)dir) % count;
            s_shown_ms = 0;
        }
        if (a == 1) {
            switch (entry_kind(s_cursor, item)) {
                case ENTRY_NAMETAG: return MENU_NAMETAG;
                case ENTRY_ITEM: return MENU_RUN;
                case ENTRY_EDIT: start_editor(); break;
                case ENTRY_BRIGHTNESS:  // the next level, from the top back to the lowest
                    if (LEDMatrix_GetBrightness() >= s_levels[sizeof s_levels - 1])
                        LEDMatrix_SetBrightness(s_levels[0]);
                    else
                        Menu_StepBrightness(1);
                    break;
                case ENTRY_KEYS:
                    Persist_SetKeys(Persist_Keys() == MENU_KEYS_VERTICAL ? MENU_KEYS_HORIZONTAL : MENU_KEYS_VERTICAL);
                    s_shown_ms = 0;
                    break;
                case ENTRY_SLEEP: return MENU_SLEEP;
                default: return MENU_UPDATE;
            }
        }
    }

    s_frame_ms += elapsed_ms;
    if (s_frame_ms >= FRAME_MS || pressed) {
        s_frame_ms = 0;
        if (s_editing)
            draw_editor();
        else
            draw_menu();
    }
    return MENU_OPEN;
}
