// A stand-in for the firmware's app API (apps/hhapp.h), for testing an app
// on the PC: include this, then the app's .c file. The LEDs are an array,
// the buttons and the clock are set by the test, load / save keep the data
// in memory, and the layout tables are the badge's (layout.c, linked in).
// Text is drawn into the LEDs by the firmware's own font.c.
//
// The overdraw check: with MOCK_OVERDRAW=1 in the environment, the mock
// keeps the pixels of every text drawn in a tick, and mock_tick_end() (the
// tests' run helpers call it after each tick) notes each lit text pixel that
// the app changed afterwards in the same tick, unless a later text drew it.
// Such a pixel fails the test (mock_report, overdraw_run.c's exit status),
// and at exit the report lists them with the call that drew over them.
// MOCK_OVERDRAW=all also lists, without failing, the pixels the octagon's
// corners or the grid's edges clip and the text's gaps lit afterwards.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "apps/hhapp.h"
#include "layout.h"
#include "app_util.c"  // the firmware's own helpers (sin8, hsv, ...)

char __hh_load_end[1], __hh_bss_end[1];  // the app's linker symbols (HH_APP)

static uint8_t mock_leds[HH_SLOTS];
static uint8_t mock_pressed_now;
static uint32_t mock_ms;
static uint32_t mock_seed = 1;
static uint8_t mock_data[HH_APP_DATA_MAX];
static unsigned mock_data_len;
static unsigned mock_saves;
static char mock_text[64];  // the last text drawn

static void od_wrote(uint16_t s, const void* pc, const char* how);  // the overdraw report, below
#define OD_PC __builtin_extract_return_addr(__builtin_return_address(0))
static void m_set(uint16_t s, uint8_t v) {
    if (s < HH_SLOTS) mock_leds[s] = v, od_wrote(s, OD_PC, "set");
}
static uint8_t m_get(uint16_t s) { return s < HH_SLOTS ? mock_leds[s] : 0; }
static void m_clear(void) {
    memset(mock_leds, 0, sizeof mock_leds);
    for (uint16_t i = 0; i < HH_SLOTS; i++) od_wrote(i, OD_PC, "clear");
}
static uint8_t mock_brightness = 255;
static void m_set_brightness(uint8_t b) { mock_brightness = b; }
static uint8_t m_brightness(void) { return mock_brightness; }
static uint8_t m_pressed(void) { return mock_pressed_now; }
static uint8_t mock_held_now;  // held without a new press
static uint8_t m_held(void) { return mock_pressed_now | mock_held_now; }
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
// The firmware's font.c draws the text, through this
static void mock_text_pixel(uint16_t slot, uint8_t v);
void LEDMatrix_SetPixel(uint8_t row, uint8_t col, uint8_t v) {
    uint16_t slot = (uint16_t)(row * HH_COLS + col);
    if (slot < HH_SLOTS) mock_leds[slot] = v, mock_text_pixel(slot, v);
}
#include "font.c"

// The overdraw report (MOCK_OVERDRAW=1)
#ifndef MOCK_APP
#define MOCK_APP __BASE_FILE__
#endif
static const char* mock_app = MOCK_APP;  // the app's name in the report (anim_test sets it)
static int od_on = -1;  // -1: not read from the environment yet; 2: MOCK_OVERDRAW=all
static int od_drawing;  // the text being drawn, + 1, while font.c draws it
#define OD_TEXTS 32
static struct { int x, y; uint8_t v; char t[24]; } od_text[OD_TEXTS];  // this tick's texts
static int od_texts;
static uint8_t od_owner[HH_SLOTS];  // the last text that lit the slot this tick, + 1
static uint8_t od_gap[HH_SLOTS];    // an unlit pixel of a glyph's 5 x 8 box: the text, + 1
static uint8_t od_value[HH_SLOTS];
static const void* od_writer[HH_SLOTS];  // what changed it after the text: a return address
static const char* od_how[HH_SLOTS];     // and through which call
static unsigned long od_ticks;

static void od_report(void);
static bool od_enabled(void) {
    if (od_on < 0) {
        const char* e = getenv("MOCK_OVERDRAW");
        od_on = !e || !*e || *e == '0' ? 0 : !strcmp(e, "all") ? 2 : 1;
        if (od_on) atexit(od_report);
    }
    return od_on;
}
static void mock_text_pixel(uint16_t slot, uint8_t v) {
    if (!od_drawing) return;
    od_owner[slot] = (uint8_t)od_drawing, od_gap[slot] = 0, od_value[slot] = v, od_writer[slot] = 0, od_how[slot] = 0;
}
// A write by the app (set, clear, set_rgb, set_linear) after a text
static void od_wrote(uint16_t s, const void* pc, const char* how) {
    if (od_on > 0 && s < HH_SLOTS && (od_owner[s] || od_gap[s])) od_writer[s] = pc, od_how[s] = how;
}

// What the report adds up: one entry per text, place, pixel set and writer
#define OD_FINDS 4096
typedef struct {
    int kind;  // 0 overdrawn, 1 octagon corner, 2 off the grid, 3 lit later in a glyph's gaps
    const char* app;
    char t[24];
    int x, y;
    uint8_t v;
    const void* writer;
    const char* how;
    uint8_t px[HH_GRID_H][HH_GRID_W];  // the pixels concerned
    unsigned long frames;
    uint8_t after;  // overdrawn: the value the pixel ended with (the last one seen)
    unsigned long tick;  // the last tick that counted in frames
} od_find_t;
static od_find_t* od_finds;
static int od_nfinds, od_lost;
static od_find_t* od_find(int kind, const char* t, int x, int y, uint8_t v, const void* writer, const char* how) {
    if (!od_finds) od_finds = calloc(OD_FINDS, sizeof *od_finds);
    for (int i = 0; i < od_nfinds; i++) {
        od_find_t* f = &od_finds[i];
        if (f->kind == kind && f->app == mock_app && f->x == x && f->y == y && f->v == v && f->writer == writer && !strcmp(f->t, t)) return f;
    }
    if (od_nfinds == OD_FINDS) return od_lost++, NULL;
    od_find_t* f = &od_finds[od_nfinds++];
    f->kind = kind, f->app = mock_app, f->x = x, f->y = y, f->v = v, f->writer = writer, f->how = how;
    snprintf(f->t, sizeof f->t, "%s", t);
    return f;
}

// The source line of a return address, by addr2line on this program
static void od_where(const void* pc, char* out, size_t n) {
    snprintf(out, n, "?");
    if (!pc) return;
    unsigned long base = 0;
    FILE* e = fopen("/proc/self/exe", "rb");
    unsigned char hdr[18] = {0};
    if (e) {
        if (fread(hdr, 1, sizeof hdr, e) != sizeof hdr) hdr[16] = 0;
        fclose(e);
    }
    FILE* m = hdr[16] == 3 ? fopen("/proc/self/maps", "r") : NULL;  // ET_DYN (PIE): addresses from where it loaded
    if (m) {
        if (fscanf(m, "%lx", &base) != 1) base = 0;  // the first mapping: the program
        fclose(m);
    }
    char cmd[512], exe[256];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len <= 0) return;
    exe[len] = 0;
    snprintf(cmd, sizeof cmd, "addr2line -s -e '%s' %lx 2>/dev/null", exe, (unsigned long)pc - base - 1);
    FILE* p = popen(cmd, "r");
    if (!p) return;
    if (fgets(out, (int)n, p)) out[strcspn(out, " \n")] = 0;
    pclose(p);
}

static void od_pixels(const uint8_t px[HH_GRID_H][HH_GRID_W]) {
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++)
            if (px[r][c]) printf(" (%d,%d)", c, r);
}

// The entries of texts drawn over (kind 0): what fails
__attribute__((unused)) static int od_overwritten(void) {
    int over = 0;
    for (int i = 0; i < od_nfinds; i++) over += od_finds[i].kind == 0;
    return over + od_lost;  // a full table may have lost some
}

static void od_report(void) {
    if (!od_enabled()) return;
    const char* prog = strrchr(MOCK_APP, '/') ? strrchr(MOCK_APP, '/') + 1 : MOCK_APP;
    printf("OVERDRAW %s: %lu ticks, %d overwritten text entries%s\n", prog, od_ticks, od_overwritten() - od_lost,
           od_lost ? " (table full, some lost)" : "");
    static const char* what[] = {"OVERWRITTEN", "OCTAGON", "OFF-GRID", "IN-GAPS"};
    for (int kind = 0; kind < (od_on > 1 ? 4 : 1); kind++)
        for (int i = 0; i < od_nfinds; i++) {
            od_find_t* f = &od_finds[i];
            if (f->kind != kind) continue;
            const char* app = strrchr(f->app, '/') ? strrchr(f->app, '/') + 1 : f->app;
            printf("  %s %s \"%s\" at x=%d y=%d value %d: %lu frames; pixels", what[kind], app, f->t, f->x, f->y, f->v, f->frames);
            od_pixels(f->px);
            if (kind == 0 || kind == 3) {
                char where[256];
                od_where(f->writer, where, sizeof where);
                printf("; ends %d, by %s() at %s", f->after, f->how ? f->how : "?", where);
            }
            printf("\n");
        }
}

static int m_text_width(const char* t) { return Text_Width(t); }
static int m_draw_text(int x, int y, const char* t, uint8_t v) {  // x is a column here, not HH_CENTER
    snprintf(mock_text, sizeof mock_text, "%s", t);
    if (!od_enabled()) return Text_Draw(x, y, t, v);
    // Pixels the octagon's corners or the grid's edges take away
    uint8_t corner[HH_GRID_H][HH_GRID_W] = {{0}}, off[HH_GRID_H][HH_GRID_W] = {{0}};
    int n_corner = 0, n_off = 0;
    for (int k = 0; t[k]; k++)
        for (int dx = 0; dx < HH_FONT_W; dx++)
            for (int dy = 0; dy < HH_FONT_H; dy++) {
                int col = x + k * HH_FONT_ADVANCE + dx, row = y + dy;
                if (!(glyph(t[k])[dx] >> dy & 1)) {
                    uint16_t g = col >= 0 && col < HH_GRID_W && row >= 0 && row < HH_GRID_H ? layout_grid[row][col] : HH_NONE;
                    if (g != HH_NONE && od_texts < OD_TEXTS && !od_owner[g])
                        od_gap[g] = (uint8_t)(od_texts + 1), od_writer[g] = 0, od_how[g] = 0;
                    continue;
                }
                if (col < 0 || col >= HH_GRID_W || row < 0 || row >= HH_GRID_H) {
                    int c = col < 0 ? 0 : col >= HH_GRID_W ? HH_GRID_W - 1 : col;  // where it leaves the grid
                    int r = row < 0 ? 0 : row >= HH_GRID_H ? HH_GRID_H - 1 : row;
                    off[r][c] = 1, n_off++;
                } else if (layout_grid[row][col] == HH_NONE) {
                    corner[row][col] = 1, n_corner++;
                }
            }
    for (int kind = 1; kind <= 2; kind++) {
        if (!(kind == 1 ? n_corner : n_off)) continue;
        od_find_t* f = od_find(kind, t, x, y, v, 0, 0);
        if (!f) continue;
        f->frames++;
        for (int r = 0; r < HH_GRID_H; r++)
            for (int c = 0; c < HH_GRID_W; c++) f->px[r][c] |= kind == 1 ? corner[r][c] : off[r][c];
    }
    if (od_texts < OD_TEXTS) {
        od_text[od_texts].x = x, od_text[od_texts].y = y, od_text[od_texts].v = v;
        snprintf(od_text[od_texts].t, sizeof od_text[od_texts].t, "%s", t);
        od_drawing = ++od_texts;
    }
    int w = Text_Draw(x, y, t, v);
    od_drawing = 0;
    return w;
}

static int mock_text_x;  // where the last text went: HH_CENTER is worked out
static int m_draw_text_at(int x, int y, const char* t, uint8_t v) {
    mock_text_x = x == HH_CENTER ? (HH_GRID_W - m_text_width(t)) / 2 : x;
    return m_draw_text(mock_text_x, y, t, v);
}
static int m_draw_number(int x, int y, int32_t n, uint8_t v) {
    char text[12];
    return m_draw_text_at(x, y, AppUtil_Decimal(n, text), v);
}

// After each tick: the text pixels that did not end as drawn
__attribute__((unused)) static void mock_tick_end(void) {
    if (!od_enabled()) return;
    od_ticks++;
    for (int r = 0; r < HH_GRID_H; r++)
        for (int c = 0; c < HH_GRID_W; c++) {
            uint16_t s = layout_grid[r][c];
            if (s != HH_NONE && od_gap[s] && od_writer[s] && mock_leds[s]) {  // something lit in the text's gaps
                int i = od_gap[s] - 1;
                od_find_t* f = od_find(3, od_text[i].t, od_text[i].x, od_text[i].y, od_text[i].v, od_writer[s], od_how[s]);
                if (f) {
                    if (f->tick != od_ticks) f->frames++, f->tick = od_ticks;
                    f->px[r][c] = 1, f->after = mock_leds[s];
                }
            }
            if (s == HH_NONE || !od_owner[s]) continue;
            if (mock_leds[s] != od_value[s]) {
                int i = od_owner[s] - 1;
                od_find_t* f = od_find(0, od_text[i].t, od_text[i].x, od_text[i].y, od_text[i].v, od_writer[s], od_how[s]);
                if (f) {
                    if (f->tick != od_ticks) f->frames++, f->tick = od_ticks;
                    f->px[r][c] = 1, f->after = mock_leds[s];
                }
            }
        }
    memset(od_owner, 0, sizeof od_owner);
    memset(od_gap, 0, sizeof od_gap);
    od_texts = 0;
}
static void m_set_rgb(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= HH_RGB_COUNT) return;
    const void* pc = OD_PC;
    uint16_t sl[3] = {layout_rgb[i].r, layout_rgb[i].g, layout_rgb[i].b};
    uint8_t v[3] = {r, g, b};
    for (int k = 0; k < 3; k++)
        if (sl[k] < HH_SLOTS) mock_leds[sl[k]] = v[k], od_wrote(sl[k], pc, "set_rgb");
}
static void m_set_linear(uint16_t s, uint8_t v) {
    if (s < HH_SLOTS) mock_leds[s] = AppUtil_Linear(layout_gamma, v), od_wrote(s, OD_PC, "set_linear");
}
static int m_step_brightness(int dir) {  // as menu.c
    static const uint8_t levels[HH_BRIGHTNESS_LEVELS] = {8, 16, 32, 64, 128, 255};
    for (int k = 0; dir && k < HH_BRIGHTNESS_LEVELS; k++) {
        int i = dir > 0 ? k : HH_BRIGHTNESS_LEVELS - 1 - k;
        if (dir > 0 ? levels[i] > mock_brightness : levels[i] < mock_brightness) {
            mock_brightness = levels[i];
            break;
        }
    }
    int i = HH_BRIGHTNESS_LEVELS - 1;
    while (i > 0 && levels[i] > mock_brightness) i--;
    return i;
}

static hh_ir_shot_t mock_shot;  // the next shot, if mock_shot_pending
static bool mock_shot_pending;
static bool m_ir_receive_shot(hh_ir_shot_t* s) {
    if (!mock_shot_pending) return false;
    *s = mock_shot;
    mock_shot_pending = false;
    return true;
}

static hh_ir_shot_t mock_sent_shot;  // the last shot the app sent
static unsigned mock_shots_sent;
static bool m_ir_send_shot(const hh_ir_shot_t* s) {
    mock_sent_shot = *s;
    mock_shots_sent++;
    return true;
}

static bool m_ir_send_packet(uint8_t type, const void* d, uint8_t n) { (void)type, (void)d, (void)n; return true; }
static bool m_ir_receive_packet(hh_ir_packet_t* p) { (void)p; return false; }

static const hh_api_t mock_api = {
    .version = HH_API_VERSION, .size = sizeof(hh_api_t),
    .set = m_set, .get = m_get, .clear = m_clear, .set_brightness = m_set_brightness, .brightness = m_brightness,
    .grid = layout_grid, .edge = layout_edge, .rgb = layout_rgb,
    .pressed = m_pressed, .held = m_held, .millis = m_millis, .random = m_random,
    .ir_send = m_ir_send, .ir_receive = m_ir_receive, .standby = m_standby, .exit = m_exit, .print = m_print,
    .name = m_name, .set_name = m_set_name, .load = m_load, .save = m_save,
    .draw_text = m_draw_text_at, .text_width = m_text_width, .polar = layout_polar,
    .set_rgb = m_set_rgb, .set_linear = m_set_linear, .sin8 = AppUtil_Sin8, .hsv = AppUtil_Hsv,
    .isqrt = AppUtil_Isqrt, .atan8 = AppUtil_Atan8, .draw_number = m_draw_number,
    .step_brightness = m_step_brightness, .ir_send_packet = m_ir_send_packet, .ir_receive_packet = m_ir_receive_packet, .ir_receive_shot = m_ir_receive_shot, .ir_send_shot = m_ir_send_shot,
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

__attribute__((unused)) static int mock_report(const char* what) {
    if (od_on > 0) CHECK(!od_overwritten(), "no text drawn over in the same tick (%d entries, OVERDRAW below)", od_overwritten());
    printf("%s  %s: %d of %d checks\n", failures ? "FAIL" : "ok  ", what, checks - failures, checks);
    return failures != 0;
}

// The RGB values of LED i of layout_rgb
__attribute__((unused)) static void mock_rgb(int i, uint8_t c[3]) {
    c[0] = mock_leds[layout_rgb[i].r], c[1] = mock_leds[layout_rgb[i].g], c[2] = mock_leds[layout_rgb[i].b];
}
