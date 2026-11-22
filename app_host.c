#include "app_host.h"
#include <stdio.h>
#include <string.h>
#include "buttons.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "infrared.h"
#include "layout.h"
#include "standby.h"

static const hh_app_header_t* s_app;  // the running app's header, in app RAM
static bool s_exit;
static uint8_t s_pressed, s_held;
static uint32_t s_random;
static uint32_t s_millis;  // since the app started, counted in ticks

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

static void api_standby(void) {
    standby();
    buttons_ignore_held();  // the press that woke us is not a press for the app
    s_pressed = 0;
}

static void api_exit(void) { s_exit = true; }

static void api_print(const char* text) { printf("%s", text); }

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
};

static bool in_image(uint32_t addr, uint32_t load_end) {
    return addr >= HH_APP_RAM_BASE + sizeof(hh_app_header_t) && addr < load_end;
}

bool AppHost_Check(const hh_app_header_t* h, uint32_t length) {
    uint32_t load_end = (uint32_t)h->load_end, bss_end = (uint32_t)h->bss_end;
    return h->magic == HH_APP_MAGIC && h->api >= 1 && h->api <= HH_API_VERSION && h->flags == 0 &&
           load_end >= HH_APP_RAM_BASE + sizeof(hh_app_header_t) && load_end - HH_APP_RAM_BASE <= length &&
           bss_end >= load_end && bss_end <= HH_APP_RAM_BASE + HH_APP_RAM_SIZE &&
           in_image((uint32_t)h->init, load_end) && in_image((uint32_t)h->tick, load_end);
}

bool AppHost_Start(const uint8_t* image, uint32_t length) {
    const hh_app_header_t* h = (const hh_app_header_t*)image;
    if (!AppHost_Check(h, length)) return false;
    char* load_end = h->load_end;
    char* bss_end = h->bss_end;
    memcpy((void*)HH_APP_RAM_BASE, image, (uint32_t)load_end - HH_APP_RAM_BASE);
    memset(load_end, 0, (size_t)(bss_end - load_end));
    // fence.i (the build has no Zifencei, so spelled out): the copy is code now
    __asm__ volatile(".insn i 0x0F, 1, x0, x0, 0" ::: "memory");

    s_app = (const hh_app_header_t*)HH_APP_RAM_BASE;
    s_exit = false;
    s_pressed = s_held = 0;
    s_millis = 0;
    s_random = funSysTick32() ^ ESIG->UID0 ^ 0x9E3779B9u;
    if (!s_random) s_random = 1;
    LEDMatrix_Clear();
    s_app->init(&s_api);
    return true;
}

bool AppHost_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held) {
    if (!s_app) return false;
    s_pressed = pressed;
    s_held = held;
    s_millis += elapsed_ms;
    s_app->tick(elapsed_ms);
    return !s_exit;
}

void AppHost_Stop(void) {
    if (!s_app) return;
    s_app = NULL;
    LEDMatrix_Clear();
}

bool AppHost_Running(void) { return s_app != NULL; }
