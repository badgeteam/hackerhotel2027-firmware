#include "app_host.h"
#include "app_util.h"
#include <stdio.h>
#include <string.h>
#include "buttons.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "font.h"
#include "infrared.h"
#include "layout.h"
#include "menu.h"
#include "persist.h"
#include "store.h"
#include "standby.h"
#ifdef HH_EMU
#include "emu.h"  // tools/emu: the app runs in an RV32 interpreter there
#endif

static const hh_app_header_t* s_app;  // the running app's header, in app RAM
static bool s_exit;
static uint8_t s_pressed, s_held;
static uint32_t s_random;
static uint32_t s_millis;  // since the app started, counted in ticks
// The app's name in the store (the persist.h key). A copy: Store_Delete()
// can move the item while the app runs.
static char s_name[STORE_NAME_LEN];

static void api_set(uint16_t slot, uint8_t value) {
    if (slot < HH_SLOTS) LEDMatrix_SetPixel(slot / HH_COLS, slot % HH_COLS, value);
}

static uint8_t api_get(uint16_t slot) { return slot < HH_SLOTS ? LEDMatrix_GetPixel(slot / HH_COLS, slot % HH_COLS) : 0; }

static uint8_t api_pressed(void) { return s_pressed; }
static uint8_t api_held(void) { return s_held; }

static uint32_t api_millis(void) { return s_millis; }

static uint32_t api_random(void) {  // xorshift32
    s_random ^= s_random << 13;
    s_random ^= s_random >> 17;
    s_random ^= s_random << 5;
    return s_random;
}

static bool api_ir_receive(hh_ir_t* message) {
    infrared_nec_message_t m;
    if (!infrared_receive(&m)) return false;
    message->address = m.address;
    message->command = m.command;
    message->repeat = m.repeat;
    return true;
}

static bool api_ir_send_packet(uint8_t type, const void* data, uint8_t length) {
    return infrared_send_packet(type, (const uint8_t*)data, length);
}

static bool api_ir_receive_packet(hh_ir_packet_t* packet) {
    infrared_packet_t p;
    if (!infrared_receive_packet(&p)) return false;
    packet->type = p.type;
    packet->length = p.length;
    memcpy(packet->data, p.data, p.length);
    return true;
}

static void api_standby(void) {
    standby();
    buttons_ignore_held();  // the press that woke us is not a press for the app
    s_pressed = 0;
}

static void api_exit(void) { s_exit = true; }

static void api_print(const char* text) { printf("%s", text); }

static void api_set_name(const char* name) { Persist_SetName(name, strlen(name)); }
static bool api_load(void* data, unsigned len) { return Persist_LoadApp(s_name, data, len); }
static bool api_save(const void* data, unsigned len) { return Persist_SaveApp(s_name, data, len); }

static int api_draw_text(int x, int y, const char* text, uint8_t value) {
    if (x == HH_CENTER) x = (HH_GRID_W - Text_Width(text)) / 2;
    return Text_Draw(x, y, text, value);
}

static int api_draw_number(int x, int y, int32_t n, uint8_t value) {
    char text[12];
    return api_draw_text(x, y, AppUtil_Decimal(n, text), value);
}

static void api_set_rgb(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= HH_RGB_COUNT) return;
    api_set(layout_rgb[index].r, r);
    api_set(layout_rgb[index].g, g);
    api_set(layout_rgb[index].b, b);
}

static void api_set_linear(uint16_t slot, uint8_t value) { api_set(slot, layout_linear[value]); }

static int api_step_brightness(int dir) {
    if (dir) Menu_StepBrightness(dir);
    return Menu_BrightnessLevel();
}

static bool api_ir_receive_shot(hh_ir_shot_t* shot) {
    ir_shot_t s;
    if (!infrared_receive_shot(&s)) return false;
    shot->team = s.team;
    shot->trigger = s.trigger;
    shot->command = s.command;
    shot->parameter = s.parameter;
    return true;
}

static const hh_api_t s_api = {
    .version = HH_API_VERSION,
    .size = sizeof(hh_api_t),
    .set = api_set,
    .get = api_get,
    .clear = LEDMatrix_Clear,
    .set_brightness = LEDMatrix_SetBrightness,
    .brightness = LEDMatrix_GetBrightness,
    .grid = layout_grid,
    .edge = layout_edge,
    .rgb = layout_rgb,
    .pressed = api_pressed,
    .held = api_held,
    .millis = api_millis,
    .random = api_random,
    .ir_send = infrared_send_nec,
    .ir_receive = api_ir_receive,
    .standby = api_standby,
    .exit = api_exit,
    .print = api_print,
    .name = Persist_Name,
    .set_name = api_set_name,
    .load = api_load,
    .save = api_save,
    .draw_text = api_draw_text,
    .text_width = Text_Width,
    .polar = layout_polar,
    .set_rgb = api_set_rgb,
    .set_linear = api_set_linear,
    .sin8 = AppUtil_Sin8,
    .hsv = AppUtil_Hsv,
    .isqrt = AppUtil_Isqrt,
    .atan8 = AppUtil_Atan8,
    .draw_number = api_draw_number,
    .step_brightness = api_step_brightness,
    .ir_send_packet = api_ir_send_packet,
    .ir_receive_packet = api_ir_receive_packet,
    .ir_receive_shot = api_ir_receive_shot,
};
_Static_assert(HH_IR_PACKET_MAX == IR_PACKET_DATA_MAX, "hhapp.h and infrared.h agree on the packet size");

static bool in_image(uint32_t addr, uint32_t load_end) {
    return addr >= HH_APP_RAM_BASE + sizeof(hh_app_header_t) && addr < load_end;
}

bool AppHost_Check(const hh_app_header_t* h, uint32_t length) {
    uint32_t load_end = (uint32_t)(uintptr_t)h->load_end, bss_end = (uint32_t)(uintptr_t)h->bss_end;
    return h->magic == HH_APP_MAGIC && h->api >= 1 && h->api <= HH_API_VERSION && h->flags == 0 &&
           load_end >= HH_APP_RAM_BASE + sizeof(hh_app_header_t) && load_end - HH_APP_RAM_BASE <= length &&
           bss_end >= load_end && bss_end <= HH_APP_RAM_BASE + HH_APP_RAM_SIZE &&
           in_image((uint32_t)(uintptr_t)h->init, load_end) && in_image((uint32_t)(uintptr_t)h->tick, load_end);
}

bool AppHost_Start(const uint8_t* image, uint32_t length, const char* name) {
    const hh_app_header_t* h = (const hh_app_header_t*)image;
    if (!AppHost_Check(h, length)) return false;
    char* load_end = (char*)(uintptr_t)h->load_end;
    char* bss_end = (char*)(uintptr_t)h->bss_end;
    memcpy((void*)(uintptr_t)HH_APP_RAM_BASE, image, (uint32_t)(uintptr_t)load_end - HH_APP_RAM_BASE);
    memset(load_end, 0, (size_t)(bss_end - load_end));
#ifndef HH_EMU
    // fence.i (the build has no Zifencei, so spelled out): the copy is code now
    __asm__ volatile(".insn i 0x0F, 1, x0, x0, 0" ::: "memory");
#endif

    s_app = (const hh_app_header_t*)HH_APP_RAM_BASE;
    s_exit = false;
    s_pressed = s_held = 0;
    s_millis = 0;
    memcpy(s_name, name, STORE_NAME_LEN);
    s_random = funSysTick32() ^ ESIG->UID0 ^ 0x9E3779B9u;
    if (!s_random) s_random = 1;
    LEDMatrix_Clear();
#ifdef HH_EMU
    if (!emu_app_init(s_app->init, &s_api)) {
        s_app = NULL;
        return false;
    }
#else
    s_app->init(&s_api);
#endif
    return true;
}

bool AppHost_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held) {
    if (!s_app) return false;
    s_pressed = pressed;
    s_held = held;
    s_millis += elapsed_ms;
#ifdef HH_EMU
    if (!emu_app_tick(s_app->tick, elapsed_ms)) return false;  // it faulted or hangs: stop it
#else
    s_app->tick(elapsed_ms);
#endif
    return !s_exit;
}

void AppHost_Stop(void) {
    if (!s_app) return;
    s_app = NULL;
    LEDMatrix_Clear();
}

bool AppHost_Running(void) { return s_app != NULL; }
