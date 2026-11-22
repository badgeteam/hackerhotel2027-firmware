#include "store.h"
#include <string.h>
#include "app_host.h"
#include "flash_pages.h"
#include "persist.h"

#define ITEMS_SIZE STORE_ITEMS_SIZE

static const store_item_t* item_at(uint32_t offset) { return (const store_item_t*)(uintptr_t)(STORE_FLASH_BASE + offset); }

static uint32_t page_align(uint32_t n) { return (n + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1); }

// Offset of the item after the one at `offset`, or 0 if there is no valid
// item at `offset`.
static uint32_t next_offset(uint32_t offset) {
    if (offset + sizeof(store_item_t) > ITEMS_SIZE) return 0;
    const store_item_t* it = item_at(offset);
    if (it->magic != STORE_ITEM_MAGIC || it->length > ITEMS_SIZE - offset - sizeof(store_item_t)) return 0;
    return page_align(offset + sizeof(store_item_t) + it->length);
}

// Walks the list: returns the offset of item `index`, or of the end of the
// list if there are fewer items; *count gets how many it passed.
static uint32_t walk(uint8_t index, uint8_t* count) {
    uint32_t offset = 0;
    uint8_t n = 0;
    while (n < index) {
        uint32_t next = next_offset(offset);
        if (!next) break;
        offset = next;
        n++;
    }
    if (count) *count = n;
    return offset;
}

uint8_t Store_Count(void) {
    uint8_t n;
    walk(STORE_NONE, &n);
    return n;
}

const store_item_t* Store_Item(uint8_t index) {
    uint8_t n;
    uint32_t offset = walk(index, &n);
    return n == index && next_offset(offset) ? item_at(offset) : NULL;
}

static uint32_t end_offset(void) { return walk(STORE_NONE, NULL); }

uint8_t Store_FreePages(void) { return (uint8_t)((ITEMS_SIZE - end_offset()) / FLASH_PAGE_SIZE); }

uint8_t Store_BootItem(void) {
    uint8_t index = Persist_BootItem();
    return Store_Item(index) ? index : STORE_NONE;
}

void Store_SetBootItem(uint8_t index) { Persist_SetBootItem(index); }

const uint8_t* Store_LegacyPattern(void) {
    const uint8_t* d = (const uint8_t*)(uintptr_t)STORE_FLASH_BASE;
    return memcmp(d, "LA02", 4) == 0 ? d : NULL;
}

// --- Upload ------------------------------------------------------------

// The new item's first page (header and the start of the payload) stays
// in s_first until Store_Commit(); later pages go through s_page.
static uint8_t s_first[FLASH_PAGE_SIZE];
static uint8_t s_page[FLASH_PAGE_SIZE];
static uint32_t s_start;     // offset of the new item
static uint32_t s_received;  // payload bytes so far
static bool s_writing;
static bool s_overflow;

void Store_EraseAll(void) {
    s_writing = false;
    for (uint32_t off = 0; off < ITEMS_SIZE; off += FLASH_PAGE_SIZE) FlashPages_Erase(STORE_FLASH_BASE + off);
    Persist_SetBootItem(STORE_NONE);
}

int Store_Begin(uint8_t type, const char* name, unsigned name_len) {
    s_writing = false;
    if (type != STORE_TYPE_ANIMATION && type != STORE_TYPE_APP) return STORE_ERR_ARGS;
    s_start = end_offset();
    if (s_start >= ITEMS_SIZE) return STORE_ERR_FULL;
    for (uint32_t off = s_start; off < ITEMS_SIZE; off += FLASH_PAGE_SIZE) FlashPages_Erase(STORE_FLASH_BASE + off);

    memset(s_first, 0xFF, sizeof s_first);
    store_item_t* h = (store_item_t*)s_first;
    memset(h, 0, sizeof *h);
    h->type = type;
    memcpy(h->name, name, name_len < STORE_NAME_LEN ? name_len : STORE_NAME_LEN);
    s_received = 0;
    s_overflow = false;
    s_writing = true;
    return STORE_OK;
}

int Store_Write(const uint8_t* data, unsigned len) {
    if (!s_writing) return STORE_ERR_ARGS;
    for (unsigned i = 0; i < len; i++) {
        uint32_t pos = sizeof(store_item_t) + s_received;  // within the item
        if (s_start + pos >= ITEMS_SIZE) {
            s_overflow = true;
            return STORE_ERR_FULL;
        }
        if (pos < FLASH_PAGE_SIZE) {
            s_first[pos] = data[i];
        } else {
            s_page[pos % FLASH_PAGE_SIZE] = data[i];
            if (pos % FLASH_PAGE_SIZE == FLASH_PAGE_SIZE - 1)
                FlashPages_Program(STORE_FLASH_BASE + s_start + pos - (FLASH_PAGE_SIZE - 1), s_page);
        }
        s_received++;
    }
    return STORE_OK;
}

static bool payload_valid(uint8_t type, const uint8_t* payload, uint32_t length) {
    if (type == STORE_TYPE_ANIMATION) return length >= 8 && (memcmp(payload, "LA03", 4) == 0 || memcmp(payload, "LA02", 4) == 0);
    return length >= sizeof(hh_app_header_t) && AppHost_Check((const hh_app_header_t*)payload, length);
}

int Store_Commit(uint8_t* index) {
    if (!s_writing) return STORE_ERR_ARGS;
    s_writing = false;
    if (s_overflow) return STORE_ERR_FULL;

    store_item_t* h = (store_item_t*)s_first;
    h->magic = STORE_ITEM_MAGIC;
    h->length = s_received;
    // The app header is 24 bytes, so it is always in the first page
    if (!payload_valid(h->type, Store_Payload(h), s_received)) return STORE_ERR_INVALID;

    uint32_t end = sizeof(store_item_t) + s_received;
    if (end > FLASH_PAGE_SIZE && end % FLASH_PAGE_SIZE) {  // the last, partial page
        uint32_t fill = end % FLASH_PAGE_SIZE;
        memset(s_page + fill, 0xFF, FLASH_PAGE_SIZE - fill);
        FlashPages_Program(STORE_FLASH_BASE + s_start + end - fill, s_page);
    }
    FlashPages_Program(STORE_FLASH_BASE + s_start, s_first);

    uint8_t n;
    walk(STORE_NONE, &n);
    *index = n - 1;
    return STORE_OK;
}
