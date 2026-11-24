// A stand-in for the firmware's app API (apps/hhapp.h), for testing an app
// on the PC: include this, then the app's .c file. The LEDs are an array,
// the buttons and the clock are set by the test, load / save keep the data
// in memory, and the layout tables are the badge's (layout.c, linked in).
// Text is not drawn; text_width is the real one.
//
//   mock_tick(ms, pressed)  runs one tick of the app with those buttons
#pragma once
#include <stdio.h>
#include <string.h>
#include "apps/hhapp.h"
#include "layout.h"

char __hh_load_end[1], __hh_bss_end[1];  // the app's linker symbols (HH_APP)

static uint8_t mock_leds[HH_SLOTS];
static uint8_t mock_pressed_now;
static uint32_t mock_ms;
static uint32_t mock_seed = 1;
static uint8_t mock_data[HH_APP_DATA_MAX];
static unsigned mock_data_len;
static unsigned mock_saves;
static char mock_text[64];  // the last text drawn

static void m_set(uint16_t s, uint8_t v) { if (s < HH_SLOTS) mock_leds[s] = v; }
static uint8_t m_get(uint16_t s) { return s < HH_SLOTS ? mock_leds[s] : 0; }
static void m_clear(void) { memset(mock_leds, 0, sizeof mock_leds); }
static void m_set_brightness(uint8_t b) { (void)b; }
static uint8_t m_brightness(void) { return 255; }
static uint8_t m_pressed(void) { return mock_pressed_now; }
static uint8_t m_held(void) { return mock_pressed_now; }
static uint32_t m_millis(void) { return mock_ms; }
static uint32_t m_random(void) { return (mock_seed = mock_seed * 1103515245u + 12345u) >> 8; }
static bool m_ir_send(uint16_t a, uint8_t c) { (void)a, (void)c; return true; }
static hh_ir_t mock_ir;       // the next IR message, if mock_ir_pending
static bool mock_ir_pending;
static bool m_ir_receive(hh_ir_t* m) {
    if (!mock_ir_pending) return false;
    *m = mock_ir;
    mock_ir_pending = false;
    return true;
}
static void m_standby(void) {}
static void m_exit(void) {}
static void m_print(const char* t) { (void)t; }
static const char* m_name(void) { return ""; }
static void m_set_name(const char* n) { (void)n; }
static bool m_load(void* d, unsigned n) {
    if (!mock_data_len || n > mock_data_len) return false;
    memcpy(d, mock_data, n);
    return true;
}
static bool m_save(const void* d, unsigned n) {
    if (n > HH_APP_DATA_MAX) return false;
    memcpy(mock_data, d, n);
    mock_data_len = n;
    mock_saves++;
    return true;
}
static int m_text_width(const char* t) { int n = (int)strlen(t); return n ? n * HH_FONT_ADVANCE - 1 : 0; }
static int m_draw_text(int x, int y, const char* t, uint8_t v) {
    (void)x, (void)y, (void)v;
    snprintf(mock_text, sizeof mock_text, "%s", t);
    return m_text_width(t);
}

static const hh_api_t mock_api = {
    .version = HH_API_VERSION, .size = sizeof(hh_api_t),
    .set = m_set, .get = m_get, .clear = m_clear, .set_brightness = m_set_brightness, .brightness = m_brightness,
    .grid = layout_grid, .edge = layout_edge, .rgb = layout_rgb,
    .pressed = m_pressed, .held = m_held, .millis = m_millis, .random = m_random,
    .ir_send = m_ir_send, .ir_receive = m_ir_receive, .standby = m_standby, .exit = m_exit, .print = m_print,
    .name = m_name, .set_name = m_set_name, .load = m_load, .save = m_save,
    .draw_text = m_draw_text, .text_width = m_text_width,
};

// The test's checks
static int checks, failures;
#define CHECK(cond, ...)                      \
    do {                                      \
        checks++;                             \
        if (!(cond)) {                        \
            failures++;                       \
            printf("FAIL  " __VA_ARGS__);     \
            printf("  (line %d)\n", __LINE__); \
        }                                     \
    } while (0)

static int mock_report(const char* what) {
    printf("%s  %s: %d of %d checks\n", failures ? "FAIL" : "ok  ", what, checks - failures, checks);
    return failures != 0;
}

// The RGB values of LED i of layout_rgb
static void mock_rgb(int i, uint8_t c[3]) {
    c[0] = mock_leds[layout_rgb[i].r], c[1] = mock_leds[layout_rgb[i].g], c[2] = mock_leds[layout_rgb[i].b];
}
