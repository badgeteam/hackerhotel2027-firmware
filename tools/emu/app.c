// Runs an app's init() and tick() in the RV32 interpreter (rv32.c), with
// the firmware's API (apps/hhapp.h) behind it.
//
// The app sees hh_api_t as the badge's 32-bit firmware lays it out, in the
// emulated ROM: function members are jumps to EMU_TRAP_BASE + 4 * member,
// which trap() turns into calls to the firmware's own functions (app_host.c's
// table); the layout tables are copied into the ROM.
#include <stddef.h>
#include <string.h>

#include "apps/hhapp.h"
#include "emu.h"

// The members of hh_api_t after version and size, in order
enum {
    M_SET, M_GET, M_CLEAR, M_SET_BRIGHTNESS, M_BRIGHTNESS, M_GRID, M_EDGE, M_RGB, M_PRESSED, M_HELD,
    M_MILLIS, M_RANDOM, M_IR_SEND, M_IR_RECEIVE, M_STANDBY, M_EXIT, M_PRINT, M_NAME, M_SET_NAME, M_LOAD,
    M_SAVE, M_DRAW_TEXT, M_TEXT_WIDTH, M_POLAR, M_SET_RGB, M_SET_LINEAR, M_SIN8, M_HSV, M_ISQRT, M_ATAN8,
    M_DRAW_NUMBER, M_STEP_BRIGHTNESS, M_IR_SEND_PACKET, M_IR_RECEIVE_PACKET, M_IR_RECEIVE_SHOT, M_COUNT
};
_Static_assert(sizeof(hh_api_t) == 8 + 8 * M_COUNT, "app.c lists every member of hh_api_t");

static const hh_api_t* s_api;
static uint32_t s_name_addr;  // a copy of the name in the ROM, for name()

static uint32_t rom_put(uint32_t* at, const void* data, uint32_t len) {
    uint32_t addr = *at;
    memcpy((void*)(uintptr_t)addr, data, len);
    *at = (addr + len + 3) & ~3u;
    return addr;
}

static void build_rom(void) {
    uint32_t* table = (uint32_t*)(uintptr_t)EMU_ROM_BASE;
    uint32_t at = EMU_ROM_BASE + 4 + 4 * M_COUNT;
    table[0] = HH_API_VERSION | (uint32_t)(4 + 4 * M_COUNT) << 16;  // version, size on the badge
    for (unsigned m = 0; m < M_COUNT; m++) table[1 + m] = EMU_TRAP_BASE + 4 * m;
    table[1 + M_GRID] = rom_put(&at, s_api->grid, sizeof(uint16_t) * HH_GRID_W * HH_GRID_H);
    table[1 + M_EDGE] = rom_put(&at, s_api->edge, sizeof(uint16_t) * HH_EDGE_COUNT);
    table[1 + M_RGB] = rom_put(&at, s_api->rgb, sizeof(hh_rgb_t) * HH_RGB_COUNT);
    table[1 + M_POLAR] = rom_put(&at, s_api->polar, 2 * HH_SLOTS);
    s_name_addr = at;
    at += HH_NAME_MAX + 4;
}

static bool in_ram(uint32_t a, uint32_t n) { return a >= EMU_RAM_BASE && a + n <= EMU_RAM_BASE + EMU_RAM_SIZE && a + n >= a; }

// A NUL-terminated string in the app's RAM or ROM, or NULL
static const char* app_string(uint32_t a) {
    for (uint32_t i = 0; i < 256; i++) {
        bool ok = in_ram(a + i, 1) || (a + i >= EMU_ROM_BASE && a + i < EMU_ROM_BASE + EMU_ROM_SIZE);
        if (!ok) return NULL;
        if (!*(const char*)(uintptr_t)(a + i)) return (const char*)(uintptr_t)a;
    }
    return NULL;
}

static void trap(rv_cpu* cpu, unsigned m) {
    uint32_t* x = cpu->x;
    uint32_t a0 = x[10], a1 = x[11], r = 0;
    switch (m) {
        case M_SET: s_api->set((uint16_t)a0, (uint8_t)a1); break;
        case M_GET: r = s_api->get((uint16_t)a0); break;
        case M_CLEAR: s_api->clear(); break;
        case M_SET_BRIGHTNESS: s_api->set_brightness((uint8_t)a0); break;
        case M_BRIGHTNESS: r = s_api->brightness(); break;
        case M_PRESSED: r = s_api->pressed(); break;
        case M_HELD: r = s_api->held(); break;
        case M_MILLIS: r = s_api->millis(); break;
        case M_RANDOM: r = s_api->random(); break;
        case M_IR_SEND: r = s_api->ir_send((uint16_t)a0, (uint8_t)a1); break;
        case M_IR_RECEIVE:
            r = in_ram(a0, sizeof(hh_ir_t)) && s_api->ir_receive((hh_ir_t*)(uintptr_t)a0);
            break;
        case M_STANDBY: s_api->standby(); break;
        case M_EXIT: s_api->exit(); break;
        case M_PRINT: {
            const char* s = app_string(a0);
            if (s) s_api->print(s);
            break;
        }
        case M_NAME: {
            const char* name = s_api->name();
            memset((void*)(uintptr_t)s_name_addr, 0, HH_NAME_MAX + 1);
            memcpy((void*)(uintptr_t)s_name_addr, name, strnlen(name, HH_NAME_MAX));
            r = s_name_addr;
            break;
        }
        case M_SET_NAME: {
            const char* s = app_string(a0);
            if (s) s_api->set_name(s);
            break;
        }
        case M_LOAD: r = in_ram(a0, a1) && s_api->load((void*)(uintptr_t)a0, a1); break;
        case M_SAVE: r = in_ram(a0, a1) && s_api->save((const void*)(uintptr_t)a0, a1); break;
        case M_DRAW_TEXT: {
            const char* s = app_string(x[12]);
            r = s ? (uint32_t)s_api->draw_text((int32_t)a0, (int32_t)a1, s, (uint8_t)x[13]) : 0;
            break;
        }
        case M_TEXT_WIDTH: {
            const char* s = app_string(a0);
            r = s ? (uint32_t)s_api->text_width(s) : 0;
            break;
        }
        case M_SET_RGB: s_api->set_rgb((int32_t)a0, (uint8_t)a1, (uint8_t)x[12], (uint8_t)x[13]); break;
        case M_SET_LINEAR: s_api->set_linear((uint16_t)a0, (uint8_t)a1); break;
        case M_SIN8: r = s_api->sin8((uint8_t)a0); break;
        case M_HSV: r = s_api->hsv((uint8_t)a0, (uint8_t)a1, (uint8_t)x[12]); break;
        case M_ISQRT: r = s_api->isqrt(a0); break;
        case M_ATAN8: r = s_api->atan8((int32_t)a0, (int32_t)a1); break;
        case M_DRAW_NUMBER: r = (uint32_t)s_api->draw_number((int32_t)a0, (int32_t)a1, (int32_t)x[12], (uint8_t)x[13]); break;
        case M_STEP_BRIGHTNESS: r = (uint32_t)s_api->step_brightness((int32_t)a0); break;
        case M_IR_SEND_PACKET:
            r = (uint8_t)x[12] <= HH_IR_PACKET_MAX && in_ram(a1, (uint8_t)x[12]) &&
                s_api->ir_send_packet((uint8_t)a0, (const void*)(uintptr_t)a1, (uint8_t)x[12]);
            break;
        case M_IR_RECEIVE_PACKET:
            r = in_ram(a0, sizeof(hh_ir_packet_t)) && s_api->ir_receive_packet((hh_ir_packet_t*)(uintptr_t)a0);
            break;
        case M_IR_RECEIVE_SHOT:
            r = in_ram(a0, sizeof(hh_ir_shot_t)) && s_api->ir_receive_shot((hh_ir_shot_t*)(uintptr_t)a0);
            break;
        default: emu_log("the app called hh_api_t member %u, which is not a function", m); break;
    }
    x[10] = r;
}

static rv_cpu s_cpu;

static bool call(const char* what, uint32_t fn, const uint32_t* args, int nargs, uint64_t max_steps) {
    char err[160];
    if (rv_call(&s_cpu, fn, args, nargs, max_steps, trap, err, sizeof err)) return true;
    emu_log("app %s(): %s; the app stops", what, err);
    return false;
}

bool emu_app_init(uint32_t init, const void* api) {
    s_api = api;
    build_rom();
    uint32_t arg = EMU_ROM_BASE;
    return call("init", init, &arg, 1, 100000000);
}

bool emu_app_tick(uint32_t tick, uint32_t elapsed_ms) {
    // a tick must return quickly on the badge; here 20 million
    // instructions (about half a second of the badge's CPU) is the limit
    return call("tick", tick, &elapsed_ms, 1, 20000000);
}
