#include "persist.h"
#include <stddef.h>
#include <string.h>
#include "ch32fun.h"
#include "flash_pages.h"
#include "store.h"

#define RECORD_SIZE 512u
#define COPY_ADDR(i) (PERSIST_FLASH_BASE + (i) * RECORD_SIZE)
#define RECORD_MAGIC 0x31504848u  // "HHP1"
#define FACTORY_PASSED 0xA5

typedef struct {
    uint32_t key;  // FNV-1a of the app's name; 0 = free
    uint8_t data[HH_APP_DATA_MAX];
} app_slot_t;

typedef struct {
    uint32_t magic;  // RECORD_MAGIC
    uint32_t seq;    // the higher one of the two copies is the newest
    uint32_t crc;    // CRC-32 of everything after this field
    uint8_t boot_item;
    uint8_t brightness;  // 0: never saved (records before it had 0 here)
    uint8_t keys;  // MENU_KEYS_* (menu.h); 0 in records from before it
    uint8_t factory;  // FACTORY_PASSED once the factory test passed (factory.h); 0 in records from before it
    char name[PERSIST_NAME_LEN];  // NUL-padded
    uint8_t reserved2[16];
    app_slot_t apps[PERSIST_APP_SLOTS];
} record_t;

_Static_assert(sizeof(record_t) == RECORD_SIZE, "the record is two flash pages");
_Static_assert(2 * RECORD_SIZE == PERSIST_FLASH_SIZE, "two copies");

static record_t s_rec;                        // the current state
static char s_name[PERSIST_NAME_LEN + 1];     // s_rec.name, NUL-terminated
static uint8_t s_newest;                      // copy that holds s_rec's last flushed state
static volatile bool s_dirty;
static volatile uint32_t s_quiet_ms;  // since the last change

static uint32_t crc32(const uint8_t* p, unsigned n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

static uint32_t record_crc(const record_t* r) {
    unsigned skip = offsetof(record_t, boot_item);
    return crc32((const uint8_t*)r + skip, RECORD_SIZE - skip);
}

static bool valid(const record_t* r) { return r->magic == RECORD_MAGIC && r->crc == record_crc(r); }

static void name_changed(void) {
    memcpy(s_name, s_rec.name, PERSIST_NAME_LEN);
    s_name[PERSIST_NAME_LEN] = 0;
}

void Persist_Init(void) {
    const record_t* a = (const record_t*)(uintptr_t)COPY_ADDR(0);
    const record_t* b = (const record_t*)(uintptr_t)COPY_ADDR(1);
    bool va = valid(a), vb = valid(b);
    if (va && (!vb || (int32_t)(a->seq - b->seq) >= 0)) {
        s_rec = *a;
        s_newest = 0;
    } else if (vb) {
        s_rec = *b;
        s_newest = 1;
    } else {  // nothing stored yet
        memset(&s_rec, 0, sizeof s_rec);
        s_rec.magic = RECORD_MAGIC;
        s_rec.boot_item = STORE_NONE;
        s_newest = 1;  // the first flush goes to copy 0
    }
    name_changed();
    s_dirty = false;
}

uint8_t Persist_BootItem(void) { return s_rec.boot_item; }

static void changed(void) {
    s_dirty = true;
    s_quiet_ms = 0;
}

// A setting changes: written out once it settles (Persist_Flush)
static void set_byte(uint8_t* field, uint8_t v) {
    if (*field == v) return;
    *field = v;
    changed();
}

void Persist_SetBootItem(uint8_t index) { set_byte(&s_rec.boot_item, index); }

uint8_t Persist_Brightness(void) { return s_rec.brightness; }

void Persist_SetBrightness(uint8_t brightness) { set_byte(&s_rec.brightness, brightness); }

uint8_t Persist_Keys(void) { return s_rec.keys; }

void Persist_SetKeys(uint8_t keys) { set_byte(&s_rec.keys, keys); }

bool Persist_FactoryPassed(void) { return s_rec.factory == FACTORY_PASSED; }

void Persist_SetFactoryPassed(bool passed) { set_byte(&s_rec.factory, passed ? FACTORY_PASSED : 0); }

const char* Persist_Name(void) { return s_name; }

void Persist_SetName(const char* name, unsigned len) {
    char n[PERSIST_NAME_LEN] = {0};
    for (unsigned i = 0; i < len && i < PERSIST_NAME_LEN && name[i]; i++) n[i] = name[i];
    if (memcmp(n, s_rec.name, PERSIST_NAME_LEN) == 0) return;
    memcpy(s_rec.name, n, PERSIST_NAME_LEN);
    name_changed();
    changed();
}

static uint32_t app_key(const char* app_name) {
    uint32_t h = 2166136261u;  // FNV-1a over the whole NUL-padded name
    for (unsigned i = 0; i < STORE_NAME_LEN; i++) h = (h ^ (uint8_t)app_name[i]) * 16777619u;
    return h ? h : 1;  // 0 marks a free slot
}

static app_slot_t* find_slot(uint32_t key) {
    for (unsigned i = 0; i < PERSIST_APP_SLOTS; i++)
        if (s_rec.apps[i].key == key) return &s_rec.apps[i];
    return NULL;
}

static bool app_in_store(uint32_t key) {
    for (uint8_t i = 0; i < Store_Count(); i++)
        if (app_key(Store_Item(i)->name) == key) return true;
    return false;
}

bool Persist_LoadApp(const char* app_name, void* data, unsigned len) {
    app_slot_t* slot = find_slot(app_key(app_name));
    if (len > HH_APP_DATA_MAX) len = HH_APP_DATA_MAX;
    if (!slot) return false;
    memcpy(data, slot->data, len);
    return true;
}

bool Persist_SaveApp(const char* app_name, const void* data, unsigned len) {
    if (len > HH_APP_DATA_MAX) return false;
    uint32_t key = app_key(app_name);
    app_slot_t* slot = find_slot(key);
    if (!slot) slot = find_slot(0);
    for (unsigned i = 0; !slot && i < PERSIST_APP_SLOTS; i++)  // reuse the slot of an app that is gone
        if (!app_in_store(s_rec.apps[i].key)) slot = &s_rec.apps[i];
    if (!slot) return false;
    uint8_t d[HH_APP_DATA_MAX] = {0};
    memcpy(d, data, len);
    if (slot->key == key && memcmp(slot->data, d, sizeof d) == 0) return true;  // nothing new
    slot->key = key;
    memcpy(slot->data, d, sizeof d);
    changed();
    return true;
}

void Persist_Flush(uint32_t elapsed_ms) {
    if (!s_dirty) return;
    s_quiet_ms += elapsed_ms;
    if (s_quiet_ms < PERSIST_SETTLE_MS) return;
    static record_t copy;  // a snapshot: an interrupt may change s_rec meanwhile
    __disable_irq();
    s_dirty = false;
    s_rec.seq++;
    copy = s_rec;
    __enable_irq();
    copy.crc = record_crc(&copy);

    uint8_t target = s_newest ^ 1;
    for (unsigned p = 0; p < RECORD_SIZE / FLASH_PAGE_SIZE; p++) {
        FlashPages_Erase(COPY_ADDR(target) + p * FLASH_PAGE_SIZE);
        FlashPages_Program(COPY_ADDR(target) + p * FLASH_PAGE_SIZE, (const uint8_t*)&copy + p * FLASH_PAGE_SIZE);
    }
    s_newest = target;
}
