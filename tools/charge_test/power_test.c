// The charger's debounce (power.c) on the PC, against the emulator's stand-in
// for the chip (tools/emu/stub): CHRG (PB22) must keep its level for 500 ms,
// and one slow main loop pass counts as at most 20 ms of it, so a line that
// toggles (no battery) is never taken from a single sample.
#include <stdarg.h>
#include <stdio.h>
#include "ch32fun.h"
#include "power.h"

GPIO_TypeDef emu_gpio[3];
AFIO_TypeDef emu_afio;
RCC_TypeDef emu_rcc;
USBFS_TypeDef emu_usbfs;
ESG_TypeDef emu_esig;

#undef printf
int emu_printf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int r = vprintf(fmt, a);
    va_end(a);
    return r;
}

static int failures;
static void check(int cond, const char* what) {
    printf("%s%s\n", cond ? "ok    " : "FAIL  ", what);
    if (!cond) failures++;
}

static void chrg_low(int low) { GPIOB->INDR = low ? ~(1u << 22) : ~0u; }

// CHRG toggles every 100 ms for `ms`, in passes of `pass` ms; true if charging started
static int toggle(unsigned* t, unsigned ms, unsigned pass) {
    int started = 0;
    for (unsigned end = *t + ms; *t < end; *t += pass) {
        chrg_low(*t / 100 % 2);
        started |= Power_Tick(pass);
    }
    return started;
}

int main(void) {
    GPIOC->INDR = ~0u;
    chrg_low(0);
    Power_Init();
    unsigned t = 0;

    check(!toggle(&t, 12000, 6), "CHRG toggling every 100 ms: not charging");
    chrg_low(1);
    check(!Power_Tick(600) && !Power_Charging(), "one 600 ms pass with CHRG low: still not charging");
    check(!toggle(&t, 12000, 6) && !Power_Charging(), "and the toggling goes on: not charging");

    chrg_low(0);
    Power_Tick(5);  // what the toggling left of a low phase is forgotten
    chrg_low(1);
    int started = 0;
    for (int i = 0; i < 24; i++) started |= Power_Tick(600);
    check(!started, "24 slow passes with CHRG low: not yet (25 passes needed)");
    check(Power_Tick(600) && Power_Charging(), "the 25th: charging starts");

    chrg_low(0);
    for (int i = 0; i < 99; i++) Power_Tick(5);
    check(Power_Charging(), "CHRG high for 495 ms: still charging");
    Power_Tick(5);
    check(!Power_Charging(), "for 500 ms: charging stops");

    chrg_low(1);
    for (int i = 0; i < 99; i++) started |= Power_Tick(5);
    check(!started, "CHRG low for 495 ms: not yet");
    check(Power_Tick(5), "for 500 ms: charging starts (the returned edge)");
    check(!Power_Tick(5) && Power_Charging(), "only once");

    printf("%s  charger debounce: %d failures\n", failures ? "FAIL" : "ok  ", failures);
    return failures != 0;
}
