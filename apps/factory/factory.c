// Factory test: what a freshly flashed badge runs first, for the
// volunteers at the flashing table (docs/FACTORY_TEST.md). Short, pass or
// fail, no terminal needed. Not for the app library: `make factory-image`
// puts it in a new badge's store as FACTORY, and tools/badge.py factory
// stores it. The firmware starts it at every power-on while it is in the
// store; once it has passed, the firmware deletes it.
//
// Only KEY needs a hand; the rest runs on its own, and the volunteer looks:
//   LED  every LED on: white, then the arms red, green, blue (4.5 s). All
//        lit, the right colours? If not: the repair pile
//   ROW  one LED row at a time top to bottom, then one column left to right
//        (a COM line, then a PWM channel), once (5 s): exactly one moving
//        line or set of dots, never two?
//   KEY  press A, B, C and D once each (20 s). make factory-image
//        FACTORY_KEYS=0 leaves it out: then the whole test runs by itself
//   USB  USB power for a second (10 s)
//   IR   sends test packets until it hears one: its own reflection, or
//        another badge's. After 10 s: IR? with amber arms, and it keeps
//        listening (a hand in front helps); after 30 s it fails
//
// A failed step blinks red with its name; B tries that step again. Hold
// A + B for 3 s to leave the test without passing it (it stays in the
// store and comes back at the next power-on). Every step is logged
// (print, minichlink -T).
#include <stdarg.h>
#include <stddef.h>
#include "hhapp.h"

enum { LED, ROW, KEY, USB, IR, STEPS };
static const char* const NAME[STEPS] = {"LED", "ROW", "KEY", "USB", "IR"};

#define LABEL_MS 1000       // the step's name shows this long first
#define ROW_MS 130          // per line in ROW
#define KEY_TIMEOUT_MS 20000
#define USB_TIMEOUT_MS 10000
#define USB_STEADY_MS 1000
#define IR_EVERY_MS 1000
#define IR_TIMEOUT_MS 10000  // then IR? (a hand in front helps)
#define IR_FAIL_MS 30000     // nothing heard at all: IR fails
#define LED_MS 4500          // LED: 1.5 s white, then 1 s each red, green, blue
#ifndef FACTORY_KEYS
#define FACTORY_KEYS 1       // 0 (make factory-image FACTORY_KEYS=0): no KEY step, the test runs on its own
#endif
#define PASS_MS 3000
#define LEAVE_MS 3000       // hold A + B this long: leave the test
#define TEST_TYPE 'T'       // the IR test packet's type

// The buttons in KEY, where each shows: A top left, B top right, C bottom
// left, D bottom right, as they sit round the badge
static const uint8_t KEYS[4] = {HH_BTN_A, HH_BTN_B, HH_BTN_C, HH_BTN_D};
static const char* const KEY_NAME[4] = {"A", "B", "C", "D"};

typedef enum { RUNNING, PASS, FAIL } state_t;

static const hh_api_t* hh;
static uint8_t s_step;
static state_t s_state;
static uint32_t s_ms;      // since the step (or the pass / fail screen) started
static uint8_t s_keys;     // KEY: the buttons seen, HH_BTN_* bits
static uint32_t s_usb_ms;  // USB: how long USB power has been there without a break
static uint16_t s_ir_sent, s_ir_heard;
static uint32_t s_leave_ms;

// "factory ", then the texts up to NULL, as one line of the log
static void say(const char* first, ...) {
    va_list ap;
    va_start(ap, first);
    hh->print("factory ");
    for (const char* s = first; s; s = va_arg(ap, const char*)) hh->print(s);
    va_end(ap);
    hh->print("\n");
}

static void arms(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < HH_RGB_COUNT; i++) hh->set_rgb(i, r, g, b);
}

static void center(const char* s, int row, uint8_t v) { hh->draw_text(HH_CENTER, row, s, v); }

// The outline fills up as the steps go by
static void progress(void) {
    for (int j = 0; j < HH_EDGE_COUNT; j++)
        if (j * STEPS < (s_step + 1) * HH_EDGE_COUNT) hh->set(hh->edge[j], 60);
}

static void start(uint8_t step) {
    s_step = step;
    s_state = RUNNING;
    s_ms = s_usb_ms = 0;
    s_keys = 0;
    s_ir_sent = s_ir_heard = 0;
    hh->ir_loopback(step == IR);
    say(NAME[step], NULL);
}

static void next(void) {
    if (s_step < STEPS - 1) {
        start(s_step + 1 == KEY && !FACTORY_KEYS ? KEY + 1 : s_step + 1);
        return;
    }
    hh->ir_loopback(false);
    s_state = PASS;
    s_ms = 0;
    say("PASS", NULL);
}

static void fail(const char* why) {
    hh->ir_loopback(false);
    s_state = FAIL;
    s_ms = 0;
    say("FAIL ", NAME[s_step], ": ", why, NULL);
}

// ROW: the COM line of each grid row top to bottom, then the PWM channel of
// each grid column left to right, then the channels the grid does not use
// (they drive only the outline and the arms): in the order you see them,
// not in wiring order
static void draw_row(uint32_t t) {
    unsigned k = t / ROW_MS % (HH_ROWS + HH_COLS);
    if (k < HH_ROWS) {
        unsigned com = hh->grid[k][HH_GRID_W / 2] / HH_COLS;  // the middle column has a LED in every row
        for (unsigned c = 0; c < HH_COLS; c++) hh->set((uint16_t)(com * HH_COLS + c), 255);
        return;
    }
    k -= HH_ROWS;
    uint32_t used = 0;  // the PWM channels of the grid's columns
    for (unsigned c = 0; c < HH_GRID_W; c++) used |= 1u << (hh->grid[HH_GRID_H / 2 - 1][c] % HH_COLS);
    unsigned pwm;
    if (k < HH_GRID_W) {
        pwm = hh->grid[HH_GRID_H / 2 - 1][k] % HH_COLS;  // row 7 has a LED in every column
    } else {
        unsigned n = k - HH_GRID_W;
        for (pwm = 0; pwm < HH_COLS; pwm++)
            if (!(used >> pwm & 1) && n-- == 0) break;
        if (pwm >= HH_COLS) return;
    }
    for (unsigned r = 0; r < HH_ROWS; r++) hh->set((uint16_t)(r * HH_COLS + pwm), 255);
}

// The test runs at full brightness, whatever the badge's own (a new
// badge's is 12.5 %), so a weak LED or a wrong colour shows. The firmware
// puts the badge's own back when the app stops, and does not keep 255.
static void init(const hh_api_t* api) {
    hh = api;
    hh->set_brightness(255);
    s_leave_ms = 0;
    start(LED);
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed(), held = hh->held();
    uint32_t now = s_ms;
    s_ms += elapsed_ms;
    bool blink = now / 500 % 2;
    hh->clear();

    // A + B held: leave the test, not passed
    s_leave_ms = (held & (HH_BTN_A | HH_BTN_B)) == (HH_BTN_A | HH_BTN_B) ? s_leave_ms + elapsed_ms : 0;
    if (s_leave_ms >= LEAVE_MS) {
        hh->ir_loopback(false);
        say("left", NULL);
        hh->exit();
        return;
    }

    if (s_state == PASS) {
        arms(0, 255, 40);
        for (int j = 0; j < HH_EDGE_COUNT; j++) hh->set(hh->edge[j], 200);
        center("OK", 4, 255);
        if (now == 0) hh->factory_passed();  // once: the badge keeps it
        if (now >= PASS_MS) hh->exit();      // and the firmware deletes this app
        return;
    }
    if (s_state == FAIL) {
        arms(blink ? 255 : 60, 0, 0);
        center(NAME[s_step], 2, 255);
        center("B", 9, 60);
        if (pressed & HH_BTN_B) start(s_step);
        return;
    }
    if (now < LABEL_MS) {
        center(NAME[s_step], 4, 255);
        progress();
        return;
    }
    uint32_t t = now - LABEL_MS;

    // drain the IR test packets in every step, so old ones do not pass IR
    hh_ir_packet_t p;
    while (hh->ir_receive_packet(&p))
        if (s_step == IR && p.type == TEST_TYPE) s_ir_heard++;

    switch (s_step) {
        case LED: {
            uint32_t ph = t % 4500;  // 1.5 s white, then 1 s per colour
            for (uint16_t s = 0; s < HH_SLOTS; s++) hh->set(s, 255);
            if (ph >= 1500) {
                unsigned c = (ph - 1500) / 1000;
                arms(c == 0 ? 255 : 0, c == 1 ? 255 : 0, c == 2 ? 255 : 0);
            }
            if (t >= LED_MS) next();  // look: all lit, the right colours?
            break;
        }
        case ROW:
            draw_row(t);
            if (t >= (HH_ROWS + HH_COLS) * ROW_MS) next();  // once through: one line at a time, never two?
            break;
        case KEY:
            for (int i = 0; i < 4; i++) {
                if (pressed & KEYS[i]) s_keys |= KEYS[i];
                hh->draw_text(i % 2 ? 10 : 2, i < 2 ? 2 : 9, KEY_NAME[i], s_keys & KEYS[i] ? 255 : 25);
            }
            progress();
            if (s_keys == (HH_BTN_A | HH_BTN_B | HH_BTN_C | HH_BTN_D)) {
                next();
            } else if (t >= KEY_TIMEOUT_MS) {
                char why[] = "missing: A B C D";  // the ones seen become dots
                for (int i = 0; i < 4; i++)
                    if (s_keys & KEYS[i]) why[9 + 2 * i] = '.';
                fail(why);
            }
            break;
        case USB: {
            bool usb = hh->usb_power();
            s_usb_ms = usb ? s_usb_ms + elapsed_ms : 0;
            center("USB", 2, usb || blink ? 255 : 30);
            if (hh->charging()) center("CHG", 9, 120);
            progress();
            if (s_usb_ms >= USB_STEADY_MS) next();
            else if (t >= USB_TIMEOUT_MS) fail("no USB power on PC3");
            break;
        }
        default:  // IR
            if (t / IR_EVERY_MS >= s_ir_sent && hh->ir_send_packet(TEST_TYPE, "TEST", 4)) s_ir_sent++;
            if (t < IR_TIMEOUT_MS) {
                hh->draw_number(HH_CENTER, 2, s_ir_heard, 255);
            } else {
                // nothing heard yet: keep listening, and ask
                center("IR?", 2, blink ? 255 : 90);
                arms(255, 120, 0);
                if (t >= IR_FAIL_MS) {
                    fail("nothing heard");
                    break;
                }
            }
            hh->draw_number(HH_CENTER, 9, s_ir_sent, 80);
            progress();
            if (s_ir_heard) next();
            break;
    }
}

HH_APP(init, tick);
