// Heartbeat: a double beat every 2.2 s, travelling out from the centre at
// 120 mm/s; the arms glow red with it. B / D: brighter / dimmer. As in the
// previous firmware.
#include "hhapp.h"
#include "fx.h"

static const hh_api_t* hh;

static uint32_t pulse(uint32_t x, uint32_t c, uint32_t peak) {
    uint32_t d = x > c ? x - c : c - x, fall = d * peak / 150;
    return fall < peak ? peak - fall : 0;
}

// The beat's brightness r 1/4 mm out, ms into the 2.2 s cycle (2 ms per 1/4 mm)
static uint8_t beat(uint32_t ms, uint8_t r) {
    uint32_t x = (ms % 2200 + 2200 * 4 - r * 2u) % 2200, v = pulse(x, 200, 255) + pulse(x, 550, 180);
    return (uint8_t)(v > 255 ? 255 : v);
}

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    fx_brightness_keys(hh, hh->pressed());
    uint32_t ms = hh->millis();
    for (int i = 0; i < FX_MONO_COUNT; i++) {
        uint16_t s = fx_mono(hh, i);
        fx_set(hh, s, scale8(beat(ms, hh->polar[s][0]), 230));
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint32_t v = 15u + beat(ms, hh->polar[hh->rgb[i].r][0]);
        uint8_t level = (uint8_t)(v > 255 ? 255 : v);
        fx_rgb(hh, i, scale8(255, level), scale8(13, level), scale8(46, level));
    }
}

static void init(const hh_api_t* api) { hh = api; }

HH_APP(init, tick);
