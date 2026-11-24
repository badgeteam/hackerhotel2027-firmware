// The firmware's app helpers (app_util.c, hh_api_t from HH_API_VERSION 4),
// against the maths they stand for, and the API's own wrappers in the mock.
#include <math.h>
#include <stdlib.h>
#include "mock_hh.h"

int main(void) {
    (void)mock_rgb;
    int bad = 0;
    for (int a = 0; a < 256; a++) bad += abs(AppUtil_Sin8((uint8_t)a) - (int)lround(128 + 127 * sin(a * M_PI / 128))) > 1;
    CHECK(bad == 0, "sin8 is 128 + 127 sin, to 1 (%d off)", bad);
    CHECK(AppUtil_Sin8(0) == 128 && AppUtil_Sin8(64) == 255 && AppUtil_Sin8(128) == 128 && AppUtil_Sin8(192) == 1,
          "sin8 at the quarters");

    CHECK(AppUtil_Hsv(0, 255, 255) == 0xFF0000 && AppUtil_Hsv(86, 255, 255) == 0x00FF00 &&
              AppUtil_Hsv(172, 255, 255) == 0x0000FF, "hsv: red, green, blue (%06x %06x %06x)",
          AppUtil_Hsv(0, 255, 255), AppUtil_Hsv(86, 255, 255), AppUtil_Hsv(172, 255, 255));
    CHECK(AppUtil_Hsv(123, 0, 77) == 0x4D4D4D, "hsv without saturation: grey");
    bad = 0;
    for (int h = 0; h < 256; h++) {
        uint32_t c = AppUtil_Hsv((uint8_t)h, 255, 200);
        uint8_t hi = HH_RED(c) > HH_GREEN(c) ? HH_RED(c) : HH_GREEN(c);
        hi = hi > HH_BLUE(c) ? hi : HH_BLUE(c);
        bad += hi != 200;
    }
    CHECK(bad == 0, "hsv: the brightest part is the value (%d off)", bad);

    bad = 0;
    for (uint32_t n = 0; n < 70000; n++) {
        uint32_t r = AppUtil_Isqrt(n);
        bad += r * r > n || (r + 1) * (r + 1) <= n;
    }
    CHECK(bad == 0 && AppUtil_Isqrt(0xFFFFFFFFu) == 65535 && AppUtil_Isqrt(1u << 30) == 32768, "isqrt rounds down (%d off)", bad);

    CHECK(AppUtil_Atan8(0, -5) == 0 && AppUtil_Atan8(5, 0) == 64 && AppUtil_Atan8(0, 5) == 128 && AppUtil_Atan8(-5, 0) == 192,
          "atan8: up 0, right 64, down 128, left 192");
    bad = 0;
    for (int y = -40; y <= 40; y++)
        for (int x = -40; x <= 40; x++) {
            if (!x && !y) continue;
            double want = fmod(atan2(x, -y) * 128 / M_PI + 256, 256);
            double d = fabs(AppUtil_Atan8(x, y) - want);
            bad += fmin(d, 256 - d) > 1.01;
        }
    CHECK(bad == 0, "atan8 within 1/256 turn everywhere (%d off)", bad);
    bad = 0;
    for (int s = 0; s < HH_SLOTS; s++) {  // the same angles as polar[]
        if (!layout_polar[s][0]) continue;
        int dx = AppUtil_Sin8(layout_polar[s][1]) - 128, dy = 128 - AppUtil_Sin8((uint8_t)(layout_polar[s][1] + 64));
        int d = abs(AppUtil_Atan8(dx, dy) - layout_polar[s][1]);
        bad += (d < 256 - d ? d : 256 - d) > 1;
    }
    CHECK(bad == 0, "atan8 goes the same way round as polar (%d off)", bad);

    char t[12];
    CHECK(!strcmp(AppUtil_Decimal(0, t), "0") && !strcmp(AppUtil_Decimal(42, t), "42") &&
              !strcmp(AppUtil_Decimal(-7, t), "-7") && !strcmp(AppUtil_Decimal(INT32_MIN, t), "-2147483648"),
          "decimal: 0, 42, -7, the lowest");

    // The API's wrappers (as in app_host.c)
    m_set_rgb(3, 1, 2, 3);
    uint8_t c[3];
    mock_rgb(3, c);
    CHECK(c[0] == 1 && c[1] == 2 && c[2] == 3, "set_rgb");
    m_set_rgb(HH_RGB_COUNT, 9, 9, 9);
    m_set_rgb(-1, 9, 9, 9);
    CHECK(1, "set_rgb ignores a bad index");
    m_set_linear(5, 128);
    CHECK(mock_leds[5] == 186 && layout_gamma[mock_leds[5]] >= 126 && layout_gamma[mock_leds[5]] <= 130,
          "set_linear: half the duty (%d)", mock_leds[5]);
    m_draw_number(HH_CENTER, 0, 123, 255);
    CHECK(!strcmp(mock_text, "123") && mock_text_x == 0, "draw_number, centred (%s at %d)", mock_text, mock_text_x);
    m_draw_text_at(HH_CENTER, 0, "7", 255);
    CHECK(mock_text_x == 6, "draw_text centres one character at 6 (%d)", mock_text_x);

    mock_brightness = 255;
    CHECK(m_step_brightness(0) == 5 && m_step_brightness(1) == 5 && mock_brightness == 255, "the top level stays");
    CHECK(m_step_brightness(-1) == 4 && mock_brightness == 128, "one down: 128");
    mock_brightness = 100;
    CHECK(m_step_brightness(0) == 3 && m_step_brightness(1) == 4 && mock_brightness == 128, "from 100 up: 128");
    mock_brightness = 3;
    CHECK(m_step_brightness(0) == 0 && m_step_brightness(-1) == 0 && mock_brightness == 3, "below the lowest: level 0");
    return mock_report("util");
}
