#include "greet.h"
#include <string.h>
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "font.h"
#include "infrared.h"
#include "layout.h"
#include "persist.h"

#define CALL_EVERY_MS 1500  // HELLO again (a HELLO and a REPLY take 1.3 s at most)
#define CALLS 4
#define FAIL_SHOW_MS 3000   // "NO REPLY"
#define SHOW_MIN_MS 5000    // "HI <name>": at least this, or once scrolled through
#define REPLY_WITHIN_MS 1000  // give up sending a REPLY the transmitter never frees for
#define TEXT_Y 4
#define SCROLL_CPS 10       // as Text_DrawLabel

enum { IDLE, CALLING, SHOWING, FAILED };

static uint8_t s_state;
static uint32_t s_ms;           // in this state
static uint32_t s_call_ms;      // since the last HELLO went out
static uint8_t s_calls;         // HELLOs sent
static uint32_t s_show_ms;      // how long SHOWING lasts
static char s_text[3 + IR_PACKET_DATA_MAX + 1];  // "HI <name>", or "HI!"
static int32_t s_reply_in = -1; // ms until our REPLY goes out; -1: none due
static uint32_t s_rand;

static uint32_t random32(void) {  // xorshift32
    if (!s_rand) s_rand = ESIG->UID0 ^ 0x2027BEEFu;
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return s_rand;
}

static bool send_name(uint8_t type) {
    const char* name = Persist_Name();
    return infrared_send_packet(type, (const uint8_t*)name, (uint8_t)strnlen(name, IR_PACKET_DATA_MAX));
}

static void show(const infrared_packet_t* p) {
    if (p->length) {
        memcpy(s_text, "HI ", 3);
        memcpy(s_text + 3, p->data, p->length);
        s_text[3 + p->length] = 0;
    } else {
        strcpy(s_text, "HI!");
    }
    int w = Text_Width(s_text);
    // a text that scrolls: once through, from the start back to the start
    s_show_ms = w <= HH_GRID_W ? SHOW_MIN_MS : (uint32_t)(w + 8) * 1000 / SCROLL_CPS + 500;
    if (s_show_ms < SHOW_MIN_MS) s_show_ms = SHOW_MIN_MS;
    s_state = SHOWING;
    s_ms = 0;
}

void Greet_SayHi(void) {
    s_state = CALLING;
    s_ms = 0;
    s_calls = 0;
    s_call_ms = CALL_EVERY_MS;  // at once
}

void Greet_Cancel(void) { s_state = IDLE; }

bool Greet_Tick(uint32_t elapsed_ms) {
    s_ms += elapsed_ms;

    infrared_packet_t p;
    while (infrared_receive_packet(&p)) {
        if (p.type == GREET_HELLO) {
            // Answer after a random 20 .. 270 ms, so two badges that both
            // got this HELLO do not answer at the same moment
            s_reply_in = 20 + (int32_t)(random32() % 250);
            show(&p);
        } else if (p.type == GREET_REPLY && s_state == CALLING) {
            show(&p);
        }
    }

    if (s_reply_in >= 0) {
        s_reply_in -= (int32_t)elapsed_ms;
        if (s_reply_in <= 0 && send_name(GREET_REPLY)) s_reply_in = -1;
        if (s_reply_in < -REPLY_WITHIN_MS) s_reply_in = -1;
    }

    switch (s_state) {
        case CALLING:
            s_call_ms += elapsed_ms;
            if (s_call_ms >= CALL_EVERY_MS) {
                if (s_calls == CALLS) {
                    s_state = FAILED;
                    s_ms = 0;
                } else if (send_name(GREET_HELLO)) {
                    s_calls++;
                    s_call_ms = 0;
                }
            }
            break;
        case SHOWING:
            if (s_ms >= s_show_ms) s_state = IDLE;
            break;
        case FAILED:
            if (s_ms >= FAIL_SHOW_MS) s_state = IDLE;
            break;
    }
    return s_state != IDLE;
}

static void set_slot(uint16_t slot, uint8_t v) { LEDMatrix_SetSlot(slot, v); }

static void arms(uint8_t r, uint8_t g, uint8_t b, uint8_t rank_min) {
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* led = &layout_rgb[i];
        bool on = led->rank >= rank_min;
        set_slot(led->r, on ? r : 0);
        set_slot(led->g, on ? g : 0);
        set_slot(led->b, on ? b : 0);
    }
}

void Greet_Draw(void) {
    LEDMatrix_Clear();
    if (s_state == CALLING) {
        // "HI?" and a light running round the outline; the arms pulse from
        // the tips in towards the centre, as if calling out
        Text_DrawLabel("HI?", TEXT_Y, s_ms, 255);
        unsigned head = s_ms / 20 % HH_EDGE_COUNT;
        for (unsigned k = 0; k < 8; k++) set_slot(layout_edge[(head + HH_EDGE_COUNT - k) % HH_EDGE_COUNT], (uint8_t)(255 - 30 * k));
        arms(0, 90, 255, (uint8_t)(6 - s_ms / 120 % 7));
    } else if (s_state == SHOWING) {
        // The other badge's name; the arms green, the outline lit
        Text_DrawLabel(s_text, TEXT_Y, s_ms, 255);
        for (unsigned j = 0; j < HH_EDGE_COUNT; j++) set_slot(layout_edge[j], (j + s_ms / 60) % 6 ? 60 : 255);
        arms(0, 255, 60, 0);
    } else if (s_state == FAILED) {
        Text_DrawLabel("NO REPLY", TEXT_Y, s_ms, 255);
        arms(255, 40, 0, 0);
    }
}
