// Twinkle: every LED of the grid and the outline sparkles at a speed and
// time of its own, and a comet runs up each arm every 3 s, the arms one
// after the other. B / D: brighter / dimmer. As in the previous firmware.
#include "hhapp.h"
#include "fx.h"

static const hh_api_t* hh;

static void tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    fx_brightness_keys(hh, hh->pressed());
    uint32_t ms = hh->millis();
    hh->clear();
    for (int i = 0; i < FX_MONO_COUNT; i++) {
        uint16_t s = fx_mono(hh, i);
        uint32_t h = s * 2654435761u;  // each LED's speed and phase, from a cheap hash of its slot
        uint8_t v = sin8((uint8_t)(fx_phase(ms, 40 + (h >> 28) * 12) + (uint8_t)(h >> 8)));
        uint32_t lit = v > 215 ? (v - 215u) * 6u : 0;
        fx_set(hh, s, (uint8_t)(lit > 255 ? 255 : lit));
    }
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        const hh_rgb_t* l = &hh->rgb[i];
        int p = (uint8_t)(fx_phase(ms, 85) + l->arm * 51) * 11 * 16 / 256 - 32;  // the comet's head, 16 per rank
        int d = p - l->rank * 16;
        if (d >= 0 && d < 64) fx_hsv(hh, i, (uint8_t)(140 + l->arm * 25), (uint8_t)(150 + d), (uint8_t)(255 >> (d / 16)));
    }
}

static void init(const hh_api_t* api) { hh = api; }

HH_APP(init, tick);
