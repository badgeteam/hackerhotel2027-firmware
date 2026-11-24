// Galaxy: a five-armed spiral turns once every 3 s, fading towards the
// outside; the arms turn through the colours with it. B / D: brighter /
// dimmer. As in the previous firmware.
#include "hhapp.h"
#include "fx.h"

static const hh_api_t* hh;

static uint8_t spiral(uint8_t r, uint8_t a) { return (uint8_t)(a * 5 + r * 5 / 2); }

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    fx_brightness_keys(hh, hh->pressed());
    uint8_t w = fx_phase(hh->millis(), 85);
    for (int i = 0; i < FX_MONO_COUNT; i++) {
        uint16_t s = fx_mono(hh, i);
        uint8_t r = hh->polar[s][0], a = hh->polar[s][1];
        uint8_t v = sin8((uint8_t)(spiral(r, a) - w));
        int fall = 255 - r * 3 / 4;
        fx_set(hh, s, scale8(scale8(scale8(v, v), v), (uint8_t)(fall < 51 ? 51 : fall)));
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t r = hh->polar[hh->rgb[i].r][0], a = hh->polar[hh->rgb[i].r][1];
        uint8_t v = sin8((uint8_t)(spiral(r, a) - w));
        fx_hsv(hh, i, (uint8_t)(a + w), 255, (uint8_t)(40 + scale8(215, scale8(v, v))));
    }
}

static void init(const hh_api_t* api) { hh = api; }

HH_APP(init, tick);
