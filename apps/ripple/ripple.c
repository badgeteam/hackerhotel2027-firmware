// Ripple: rings run out from the centre, one every 1.5 s, 11 mm apart;
// the arms ripple in colour with them. B / D: brighter / dimmer. As in the
// previous firmware.
#include "hhapp.h"
#include "fx.h"

static const hh_api_t* hh;

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    fx_brightness_keys(hh, hh->pressed());
    uint8_t w = fx_phase(hh->millis(), 171);
    for (int i = 0; i < FX_MONO_COUNT; i++) {
        uint16_t s = fx_mono(hh, i);
        uint8_t v = sin8((uint8_t)((uint8_t)(hh->polar[s][0] * 6) - w)), v2 = scale8(v, v);
        fx_set(hh, s, scale8(v2, v2));
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        uint8_t r = hh->polar[hh->rgb[i].r][0], v = sin8((uint8_t)((uint8_t)(r * 6) - w));
        fx_hsv(hh, i, (uint8_t)(r * 3 / 2 - w / 2), 255, (uint8_t)(40 + scale8(215, scale8(v, v))));
    }
}

static void init(const hh_api_t* api) { hh = api; }

HH_APP(init, tick);
