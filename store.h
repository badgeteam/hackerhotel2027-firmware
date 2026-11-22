#pragma once
// The item store: animations and apps, kept in the top 32 KB of the code
// flash (flash_pages.h), so they survive power loss and the boot item can
// start without a host.
//
// Items lie one after the other, each one starting on a 256-byte page: a
// 32-byte store_item_t header, then the payload (a .ledanim animation or
// a .hhapp app). The first page with no item header ends the list. The
// last 1 KB of the region is not for items: persist.c keeps the name, the
// boot item and the apps' data there.
//
// A new item goes after the last one. Its first page, with the header, is
// written last, on Store_Commit(), so an upload that breaks off leaves no
// half item behind. Items cannot be deleted one by one: Store_EraseAll()
// empties the store (the persistent record stays).
#include <stdbool.h>
#include <stdint.h>

#define STORE_ITEM_MAGIC 0x31494848u  // "HHI1"
#define STORE_TYPE_ANIMATION 1        // .ledanim, webui/compression.js
#define STORE_TYPE_APP 2              // .hhapp, apps/hhapp.h
#define STORE_NAME_LEN 20
#define STORE_NONE 0xFF  // no item

typedef struct {
    uint32_t magic;  // STORE_ITEM_MAGIC
    uint8_t type;    // STORE_TYPE_*
    uint8_t reserved[3];
    uint32_t length;             // payload bytes, after this header
    char name[STORE_NAME_LEN];   // padded with NULs, not always NUL-terminated
} store_item_t;

enum {
    STORE_OK = 0,
    STORE_ERR_ARGS = 1,       // a bad command or index, or no upload going on
    STORE_ERR_FULL = 2,       // no room left
    STORE_ERR_NOT_FOUND = 3,  // no item with that index
    STORE_ERR_INVALID = 4,    // not a valid animation or app
};

uint8_t Store_Count(void);
const store_item_t* Store_Item(uint8_t index);  // NULL past the end
static inline const uint8_t* Store_Payload(const store_item_t* item) { return (const uint8_t*)(item + 1); }
uint8_t Store_FreePages(void);  // 256-byte pages left for new items

// The item the badge starts at power-on; STORE_NONE if there is none.
// Kept by persist.c, which writes it to flash from the main loop.
uint8_t Store_BootItem(void);
void Store_SetBootItem(uint8_t index);

// A .ledanim at the start of the region, from firmware that stored a
// single pattern there, without an item header; NULL if there is none.
const uint8_t* Store_LegacyPattern(void);

void Store_EraseAll(void);

// Upload a new item: Store_Begin() erases the free space, Store_Write()
// appends payload bytes, Store_Commit() checks the payload and writes the
// header. Each returns STORE_OK or a STORE_ERR_*.
int Store_Begin(uint8_t type, const char* name, unsigned name_len);
int Store_Write(const uint8_t* data, unsigned len);
int Store_Commit(uint8_t* index);  // the new item's index
