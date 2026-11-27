#include "factory.h"
#include <stddef.h>
#include "app_util.h"
#include "debug_output.h"
#include "ch643_ledpwm.h"
#include "font.h"
#include "infrared.h"
#include "layout.h"
#include "menu.h"
#include "power.h"

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
#define FACTORY_KEYS 1       // 0 (make FACTORY_KEYS=0): no KEY step, the test runs on its own
#endif
#define PASS_MS 3000
#define LEAVE_MS 3000       // hold A + B this long: leave the test
#define TEST_TYPE 'T'       // the IR test packet's type

// The buttons in KEY, where each shows: A top left, B top right, C bottom
// left, D bottom right, as they sit round the badge
static const uint8_t KEYS[4] = {HH_BTN_A, HH_BTN_B, HH_BTN_C, HH_BTN_D};
static const char* const KEY_NAME[4] = {"A", "B", "C", "D"};

typedef enum { RUNNING, PASS, FAIL } state_t;

static uint8_t s_step;
static state_t s_state;
static uint32_t s_ms;      // since the step (or the pass / fail screen) started
static uint8_t s_keys;     // KEY: the buttons seen, HH_BTN_* bits
static uint32_t s_usb_ms;  // USB: how long USB power has been there without a break
static uint16_t s_ir_sent, s_ir_heard;
static uint32_t s_leave_ms;

static void set(uint16_t slot, uint8_t v) {
    LEDMatrix_SetSlot(slot, v);
}

static void arms(uint8_t r, uint8_t g, uint8_t b) { Menu_Arms(r, g, b, 0); }

static void center(const char* s, int row, uint8_t v) { Text_Draw((HH_GRID_W - Text_Width(s)) / 2, row, s, v); }

// The outline fills up as the steps go by
static void progress(void) {
    for (int j = 0; j < HH_EDGE_COUNT; j++)
        if (j * STEPS < (s_step + 1) * HH_EDGE_COUNT) set(layout_edge[j], 60);
}

static void start(uint8_t step) {
    s_step = step;
    s_state = RUNNING;
    s_ms = s_usb_ms = 0;
    s_keys = 0;
    s_ir_sent = s_ir_heard = 0;
    infrared_set_loopback(step == IR);
    debug_log("factory ", NAME[step], NULL);
}

static void next(void) {
    if (s_step < STEPS - 1) {
        start(s_step + 1 == KEY && !FACTORY_KEYS ? KEY + 1 : s_step + 1);
        return;
    }
    infrared_set_loopback(false);
    s_state = PASS;
    s_ms = 0;
    debug_log("factory PASS", NULL);
}

static void fail(const char* why) {
    infrared_set_loopback(false);
    s_state = FAIL;
    s_ms = 0;
    debug_log("factory FAIL ", NAME[s_step], ": ", why, NULL);
}

// The test runs at full brightness, whatever the badge's own (a new
// badge's is 12.5 %), so a weak LED or a wrong colour shows; the badge's
// own comes back when the test ends
static uint8_t s_brightness;

void Factory_Start(void) {
    s_brightness = LEDMatrix_GetBrightness();
    LEDMatrix_SetBrightness(255);
    s_leave_ms = 0;
    start(LED);
}

static factory_status_t done(void) {
    LEDMatrix_SetBrightness(s_brightness);
    return FACTORY_DONE;
}

// ROW: the COM line of each grid row top to bottom, then the PWM channel of
// each grid column left to right, then the channels the grid does not use
// (they drive only the outline and the arms): in the order you see them,
// not in wiring order
static void draw_row(uint32_t t) {
    unsigned k = t / ROW_MS % (HH_ROWS + HH_COLS);
    if (k < HH_ROWS) {
        unsigned com = layout_grid[k][HH_GRID_W / 2] / HH_COLS;  // the middle column has a LED in every row
        for (unsigned c = 0; c < HH_COLS; c++) set((uint16_t)(com * HH_COLS + c), 255);
        return;
    }
    k -= HH_ROWS;
    uint32_t used = 0;  // the PWM channels of the grid's columns
    for (unsigned c = 0; c < HH_GRID_W; c++) used |= 1u << (layout_grid[HH_GRID_H / 2 - 1][c] % HH_COLS);
    unsigned pwm;
    if (k < HH_GRID_W) {
        pwm = layout_grid[HH_GRID_H / 2 - 1][k] % HH_COLS;  // row 7 has a LED in every column
    } else {
        unsigned n = k - HH_GRID_W;
        for (pwm = 0; pwm < HH_COLS; pwm++)
            if (!(used >> pwm & 1) && n-- == 0) break;
        if (pwm >= HH_COLS) return;
    }
    for (unsigned r = 0; r < HH_ROWS; r++) set((uint16_t)(r * HH_COLS + pwm), 255);
}

factory_status_t Factory_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held) {
    uint32_t now = s_ms;
    s_ms += elapsed_ms;
    bool blink = now / 500 % 2;
    LEDMatrix_Clear();

    // A + B held: leave the test, not passed
    s_leave_ms = (held & (HH_BTN_A | HH_BTN_B)) == (HH_BTN_A | HH_BTN_B) ? s_leave_ms + elapsed_ms : 0;
    if (s_leave_ms >= LEAVE_MS) {
        infrared_set_loopback(false);
        debug_log("factory left", NULL);
        return done();
    }

    if (s_state == PASS) {
        arms(0, 255, 40);
        for (int j = 0; j < HH_EDGE_COUNT; j++) set(layout_edge[j], 200);
        center("OK", 4, 255);
        return now == 0 ? FACTORY_PASSED : now >= PASS_MS ? done() : FACTORY_BUSY;
    }
    if (s_state == FAIL) {
        arms(blink ? 255 : 60, 0, 0);
        center(NAME[s_step], 2, 255);
        center("B", 9, 60);
        if (pressed & HH_BTN_B) start(s_step);
        return FACTORY_BUSY;
    }
    if (now < LABEL_MS) {
        center(NAME[s_step], 4, 255);
        progress();
        return FACTORY_BUSY;
    }
    uint32_t t = now - LABEL_MS;

    // drain the IR test packets in every step, so old ones do not pass IR
    infrared_packet_t p;
    while (infrared_receive_packet(&p))
        if (s_step == IR && p.type == TEST_TYPE) s_ir_heard++;

    switch (s_step) {
        case LED: {
            uint32_t ph = t % 4500;  // 1.5 s white, then 1 s per colour
            for (uint16_t s = 0; s < HH_SLOTS; s++) set(s, 255);
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
                Text_Draw(i % 2 ? 10 : 2, i < 2 ? 2 : 9, KEY_NAME[i], s_keys & KEYS[i] ? 255 : 25);
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
            bool usb = Power_Usb();
            s_usb_ms = usb ? s_usb_ms + elapsed_ms : 0;
            center("USB", 2, usb || blink ? 255 : 30);
            if (Power_Charging()) center("CHG", 9, 120);
            progress();
            if (s_usb_ms >= USB_STEADY_MS) next();
            else if (t >= USB_TIMEOUT_MS) fail("no USB power on PC3");
            break;
        }
        default:  // IR
            if (t / IR_EVERY_MS >= s_ir_sent && infrared_send_packet(TEST_TYPE, (const uint8_t*)"TEST", 4)) s_ir_sent++;
            if (t < IR_TIMEOUT_MS) {
                char n[12];
                center(AppUtil_Decimal(s_ir_heard, n), 2, 255);
            } else {
                // nothing heard yet: keep listening, and ask
                center("IR?", 2, blink ? 255 : 90);
                arms(255, 120, 0);
                if (t >= IR_FAIL_MS) {
                    fail("nothing heard");
                    break;
                }
            }
            {
                char n[12];
                center(AppUtil_Decimal(s_ir_sent, n), 9, 80);
            }
            progress();
            if (s_ir_heard) next();
            break;
    }
    return FACTORY_BUSY;
}
