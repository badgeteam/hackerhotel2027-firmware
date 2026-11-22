#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "bootloader.h"
#include "buttons.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "debug_output.h"
#include "infrared.h"
#include "pattern_flash.h"
#include "pattern_player.h"
#include "standby.h"
#include "webusb_protocol.h"

#define TICK_MS 5

int _write(int fd, const char* buf, int size) {
    // This is an override for the built-in write function, to prevent stalling
    // when the debug probe is not connected
    (void)fd;
    debug_output_write(buf, size);
    return size;
}

#define IR_ADDRESS           0x00
#define IR_COMMAND_TOP_ON    0x01  // first half of the rows 255, second half 0
#define IR_COMMAND_BOTTOM_ON 0x02  // first half of the rows 0, second half 255

static const uint8_t s_brightness_levels[] = {8, 16, 32, 64, 128, 255};
#define BRIGHTNESS_LEVEL_COUNT (sizeof(s_brightness_levels) / sizeof(s_brightness_levels[0]))

static void fill_halves(uint8_t top, uint8_t bottom) {
    for (uint8_t row = 0; row < LED_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < LED_MATRIX_COLS; col++) {
            LEDMatrix_SetPixel(row, col, row < LED_MATRIX_ROWS / 2 ? top : bottom);
        }
    }
}

int main(void) {
    SystemInit();
    funGpioInitAll();

    LEDMatrix_Init();
    buttons_init();
    WebUSB_Init();
    infrared_init();

    uint8_t brightness_level = BRIGHTNESS_LEVEL_COUNT - 1;
    LEDMatrix_SetBrightness(s_brightness_levels[brightness_level]);

    for (uint8_t row = 0; row < LED_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < LED_MATRIX_COLS; col++) {
            LEDMatrix_SetPixel(row, col, 255);
        }
    }

    // Autoplay the pattern stored in flash by an earlier upload, if any
    if (PatternFlash_IsValid()) {
        PatternPlayer_Reset();
        g_app_mode = APP_MODE_FLASH_PATTERN;
    }

    bool states[4] = {0};

    while (1) {
        WebUSB_Poll();

        uint8_t buttons = buttons_poll();
        for (uint8_t i = 0; i < 4; i++) {
            if ((buttons >> i) & 1) {
                states[i] = !states[i];
            }
        }
        if (states[0]) {
            standby();
            states[0] = false;      // otherwise we go straight back to sleep
            buttons_ignore_held();  // don't act on the press that woke us
        }

        if (states[1]) {
            jump_to_bootloader();
        }

        if (states[2]) {
            states[2] = false;
            if (brightness_level < BRIGHTNESS_LEVEL_COUNT - 1) brightness_level++;
            LEDMatrix_SetBrightness(s_brightness_levels[brightness_level]);
        }
        if (states[3]) {
            states[3] = false;
            if (brightness_level > 0) brightness_level--;
            LEDMatrix_SetBrightness(s_brightness_levels[brightness_level]);
        }

        infrared_nec_message_t message;
        while (infrared_receive(&message)) {
            if (message.address != IR_ADDRESS || message.repeat) continue;
            if (message.command == IR_COMMAND_TOP_ON) {
                fill_halves(255, 0);
                g_app_mode = APP_MODE_CUSTOM;
            }
            if (message.command == IR_COMMAND_BOTTOM_ON) {
                fill_halves(0, 255);
                g_app_mode = APP_MODE_CUSTOM;
            }
        }

        if (g_app_mode == APP_MODE_FLASH_PATTERN) {
            PatternPlayer_Tick(TICK_MS);
        }

        Delay_Ms(TICK_MS);
    }
}
