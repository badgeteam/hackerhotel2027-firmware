#pragma once
// Runs a native app (apps/hhapp.h) out of the item store: copies its image
// to the app RAM block, then calls its init() once and its tick() from the
// main loop.
#include <stdbool.h>
#include <stdint.h>
#include "apps/hhapp.h"

// True if h is the header of an app this firmware can run, from an image
// of `length` bytes.
bool AppHost_Check(const hh_app_header_t* h, uint32_t length);

// Loads and starts the app; false if the image is not a valid app. name:
// the app's name in the store (STORE_NAME_LEN bytes), the key for its
// persistent data.
bool AppHost_Start(const uint8_t* image, uint32_t length, const char* name);

// One tick: the buttons that went down since the last tick (HH_BTN_*) and
// the ones down now. Returns false once the app has asked to exit.
bool AppHost_Tick(uint32_t elapsed_ms, uint8_t pressed, uint8_t held);

void AppHost_Stop(void);
bool AppHost_Running(void);
