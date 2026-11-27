// A test app for tools/ir_test/emu_raw_test.py: ir_send_raw()
// (HH_API_VERSION 7). It prints "RAWTEST <what> <0 or 1>" for what each
// call returns:
//
//   at init        first: a short Sony-like frame at 40 kHz (1)
//                  busy: the same again at once, while it goes out (0)
//   after 200 ms   carrier: 10 kHz, out of range (0)
//                  count: no marks (0)
//                  long: 2 s of marks and spaces (1), still going out
//                  when the test stops the app and starts it again
#include "hhapp.h"

static const hh_api_t* hh;
static uint32_t s_ms;
static bool s_done;

// a Sony leader and 4 bits, then a space before the next frame
static const uint16_t s_sony[] = {2400, 600, 1200, 600, 600, 600, 1200, 600, 600, 25000};
static const uint16_t s_long[40] = {
    500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000,
    500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000,
    500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000, 500, 50000};

static void report(const char* what, bool ok) {
    char line[32] = "RAWTEST ";
    unsigned n = 8;
    while (*what) line[n++] = *what++;
    line[n++] = ' ';
    line[n++] = ok ? '1' : '0';
    line[n++] = '\n';
    line[n] = 0;
    hh->print(line);
}

static void init(const hh_api_t* api) {
    hh = api;
    report("first", hh->ir_send_raw(s_sony, sizeof s_sony / sizeof *s_sony, 40000));
    report("busy", hh->ir_send_raw(s_sony, sizeof s_sony / sizeof *s_sony, 40000));
}

static void tick(uint32_t elapsed_ms) {
    s_ms += elapsed_ms;
    if (s_done || s_ms < 200) return;
    s_done = true;
    report("carrier", hh->ir_send_raw(s_sony, 2, 10000));
    report("count", hh->ir_send_raw(s_sony, 0, 40000));
    report("long", hh->ir_send_raw(s_long, sizeof s_long / sizeof *s_long, 38000));
}

HH_APP(init, tick);
