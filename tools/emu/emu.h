// Shared between the emulator's parts (hw.c, rv32.c, app.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>

// The emulated RAM, mapped at the address it has on the chip: the app runs
// there as it does on the badge (HH_APP_RAM_BASE), and the 8 KB below it,
// the firmware's RAM on the chip, is the app's stack here.
#define EMU_RAM_BASE 0x20000000u
#define EMU_RAM_SIZE 0x5000u
#define EMU_STACK_TOP 0x20002000u

// Where the app finds the API table and the layout tables (read-only for
// the app). Calls to API functions are jumps to EMU_TRAP_BASE + 4 * n.
#define EMU_ROM_BASE 0x1F000000u
#define EMU_ROM_SIZE 0x1000u
#define EMU_TRAP_BASE 0x1F100000u
#define EMU_RETURN 0x1F1FFFF0u  // the return address of a call from the emulator

void emu_log(const char* fmt, ...) __attribute__((format(__printf__, 1, 2)));

// rv32.c: run app code until the function at `fn` returns
typedef struct {
    uint32_t x[32];
    uint32_t pc;
} rv_cpu;

// Calls fn(args...) on cpu. Traps (jumps into EMU_TRAP_BASE) go to
// trap(cpu, n). Returns false, with err set, if the app faults or does not
// return within max_steps instructions.
bool rv_call(rv_cpu* cpu, uint32_t fn, const uint32_t* args, int nargs, uint64_t max_steps,
             void (*trap)(rv_cpu* cpu, unsigned n), char* err, int err_len);

// app.c: what app_host.c calls instead of the app's init() and tick()
struct hh_api_t_;
bool emu_app_init(uint32_t init, const void* api);
bool emu_app_tick(uint32_t tick, uint32_t elapsed_ms);
