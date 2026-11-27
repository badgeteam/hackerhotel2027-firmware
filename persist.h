#pragma once
// Persistent settings: the owner's name, the boot item, the global
// brightness, and a little data for each app (high scores and the like). It survives power-off,
// Store_EraseAll() and new apps; the record lives in flash next to the
// item store (flash_pages.h).
//
// One 512-byte record, kept twice (two pages each). A write goes to the
// copy that is not the newest, with a higher sequence number and a CRC,
// so losing power halfway leaves the previous copy in place. Changes are
// made in RAM and written by Persist_Flush() from the main loop, never
// from an interrupt: flash writes stall the CPU for milliseconds. It
// waits until nothing has changed for PERSIST_SETTLE_MS, so stepping the
// brightness five times is one write, not five.
#include <stdbool.h>
#include <stdint.h>
#include "apps/hhapp.h"

#define PERSIST_NAME_LEN HH_NAME_MAX
#define PERSIST_APP_SLOTS 14
#define PERSIST_SETTLE_MS 1000

void Persist_Init(void);  // reads the newest valid copy

uint8_t Persist_BootItem(void);  // STORE_NONE if unset
void Persist_SetBootItem(uint8_t index);

uint8_t Persist_Brightness(void);  // the global brightness; 0 if never saved
void Persist_SetBrightness(uint8_t brightness);

// The name, NUL-terminated (up to PERSIST_NAME_LEN characters)
// How the menu uses the buttons: MENU_KEYS_* (menu.h)
uint8_t Persist_Keys(void);
void Persist_SetKeys(uint8_t keys);

// Whether the factory test (apps/factory) has passed on this badge
bool Persist_FactoryPassed(void);
void Persist_SetFactoryPassed(bool passed);

const char* Persist_Name(void);
void Persist_SetName(const char* name, unsigned len);

// An app's data (up to HH_APP_DATA_MAX bytes), by the app's name in the
// store (STORE_NAME_LEN bytes, NUL-padded), so it comes back when the same
// app is stored again. Load copies len bytes and returns false if the app
// has saved nothing. Save returns false if len is too big, or if all
// PERSIST_APP_SLOTS slots belong to other apps that are still in the store
// (a slot of an app that is gone gets reused).
bool Persist_LoadApp(const char* app_name, void* data, unsigned len);
bool Persist_SaveApp(const char* app_name, const void* data, unsigned len);

// Writes the record to flash once something changed and then nothing
// changed for PERSIST_SETTLE_MS. Call from the main loop with the time
// since the previous call.
void Persist_Flush(uint32_t elapsed_ms);
