// The badge's USB descriptors (usb_descriptors.h), read back the way a host
// reads them: every length field, the BOS descriptor and its two platform
// capabilities, the Microsoft OS 2.0 descriptor set, the WebUSB URL, the
// serial number, and the table the USB stack answers from. Windows ignores
// a Microsoft OS 2.0 set with a wrong length without saying so, so this
// test checks each one.
#include <stdio.h>
#include <string.h>
#include "usb_descriptors.h"

static int failures;

static void check(int cond, const char* what) {
    if (!cond) {
        printf("FAIL  %s\n", what);
        failures++;
    }
}

static unsigned u16(const uint8_t* p) { return p[0] | p[1] << 8; }
static unsigned long u32(const uint8_t* p) { return u16(p) | (unsigned long)u16(p + 2) << 16; }

// What the USB stack (fsusb.c) sends for a request with this wIndex and
// wValue, as it looks it up in descriptor_list; NULL: it stalls
static const uint8_t* lookup(unsigned index, unsigned value, unsigned* length) {
    for (unsigned i = 0; i < DESCRIPTOR_LIST_ENTRIES; i++) {
        if (descriptor_list[i].lIndexValue == ((uint32_t)index << 16 | value)) {
            *length = descriptor_list[i].length;
            return descriptor_list[i].addr;
        }
    }
    return NULL;
}

// UTF-16LE text at p, n units, equals the ASCII text s (n units of it)
static int utf16_is(const uint8_t* p, const char* s, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        if (u16(p + 2 * i) != (unsigned char)s[i]) return 0;
    return 1;
}

static const uint8_t MS_OS_20_UUID[16] = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
                                          0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F};
static const uint8_t WEBUSB_UUID[16] = {0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47,
                                        0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65};

static void test_device_and_config(void) {
    unsigned n;
    const uint8_t* d = lookup(0, 0x0100, &n);
    check(d && n == 18 && d[0] == 18 && d[1] == 1, "device descriptor: 18 bytes, type 1");
    check(d && u16(d + 2) >= 0x0201, "device descriptor: USB 2.01 or later, so hosts ask for the BOS");
    check(d && d[16] == 3, "device descriptor: serial string 3");
    const uint8_t* c = lookup(0, 0x0200, &n);
    check(c && n == u16(c + 2), "configuration: wTotalLength is what the stack sends");
    // walk the configuration: the lengths add up, one interface
    unsigned at = 0, interfaces = 0;
    while (c && at < n) {
        check(c[at] >= 2, "configuration: no descriptor shorter than 2 bytes");
        if (c[at] < 2) break;
        if (c[at + 1] == 4) interfaces++;
        at += c[at];
    }
    check(at == n, "configuration: its descriptors fill wTotalLength exactly");
    check(interfaces == 1 && c[4] == 1,
          "configuration: one interface, so the Microsoft OS 2.0 set needs no subset headers");
    // USB 2.0 9.4.9: a device that says it can wake the host must accept
    // SET_FEATURE(DEVICE_REMOTE_WAKEUP); fsusb.c stalls it (FUSB_SUPPORTS_SLEEP 0)
    check(c && (c[7] & 0x20) == 0, "configuration: no remote wakeup, which the badge cannot do");
}

// Returns the capability with this UUID in the BOS descriptor, or NULL
static const uint8_t* bos_capability(const uint8_t* bos, unsigned total, const uint8_t uuid[16]) {
    for (unsigned at = 5; at + 20 <= total; at += bos[at]) {
        if (bos[at] < 3) return NULL;
        if (bos[at + 2] == 5 && memcmp(bos + at + 4, uuid, 16) == 0) return bos + at;
    }
    return NULL;
}

static void test_bos(void) {
    unsigned n;
    const uint8_t* b = lookup(0, 0x0F00, &n);
    check(b != NULL, "GET_DESCRIPTOR BOS (0x0F) is answered");
    if (!b) return;
    check(b[0] == 5 && b[1] == 0x0F, "BOS: header 5 bytes, type 0x0F");
    check(u16(b + 2) == n, "BOS: wTotalLength is what the stack sends");
    unsigned at = 5, caps = 0;
    while (at < n) {
        check(b[at] >= 3 && b[at + 1] == 0x10, "BOS: each capability is a device capability (0x10)");
        if (b[at] < 3) break;
        at += b[at];
        caps++;
    }
    check(at == n, "BOS: the capabilities fill wTotalLength exactly");
    check(caps == b[4], "BOS: bNumDeviceCaps is the number of capabilities");

    const uint8_t* ms = bos_capability(b, n, MS_OS_20_UUID);
    check(ms != NULL, "BOS: the Microsoft OS 2.0 platform capability is there");
    if (ms) {
        check(ms[0] == 28 && ms[3] == 0, "Microsoft OS 2.0 capability: 28 bytes, reserved 0");
        check(u32(ms + 20) == 0x06030000, "Microsoft OS 2.0 capability: Windows 8.1 (0x06030000)");
        check(u16(ms + 24) == sizeof(ms_os_20_set), "Microsoft OS 2.0 capability: the set's total length");
        check(ms[26] == USB_VENDOR_CODE, "Microsoft OS 2.0 capability: the vendor code");
        check(ms[27] == 0, "Microsoft OS 2.0 capability: no alternate enumeration");
    }
    const uint8_t* wu = bos_capability(b, n, WEBUSB_UUID);
    check(wu != NULL, "BOS: the WebUSB platform capability is there");
    if (wu) {
        check(wu[0] == 24 && wu[3] == 0, "WebUSB capability: 24 bytes, reserved 0");
        check(u16(wu + 20) == 0x0100, "WebUSB capability: version 1.00");
        check(wu[22] == USB_VENDOR_CODE, "WebUSB capability: the vendor code");
        check(wu[23] == 1, "WebUSB capability: landing page 1");
    }
}

static void test_ms_os_20_set(void) {
    // Windows asks with bmRequestType 0xC0, bRequest the vendor code, wValue 0, wIndex 7
    unsigned n;
    const uint8_t* s = lookup(7, 0, &n);
    check(s == (const uint8_t*)&ms_os_20_set, "vendor request wIndex 7 returns the Microsoft OS 2.0 set");
    if (!s) return;
    check(n == MS_OS_20_SET_LEN && n == sizeof(ms_os_20_set), "Microsoft OS 2.0 set: sent whole");
    // set header
    check(u16(s) == 10 && u16(s + 2) == 0, "set header: 10 bytes, type 0");
    check(u32(s + 4) == 0x06030000, "set header: Windows 8.1 (0x06030000)");
    check(u16(s + 8) == n, "set header: wTotalLength is the whole set");
    // the features after it: their lengths fill the set exactly
    unsigned at = 10, compat = 0, prop = 0;
    while (at + 4 <= n) {
        unsigned len = u16(s + at), type = u16(s + at + 2);
        check(len >= 4 && at + len <= n, "feature descriptor: a length that fits in the set");
        if (len < 4 || at + len > n) break;
        check(type != 1 && type != 2, "no configuration or function subset header (one interface)");
        if (type == 3) {
            compat++;
            check(len == 20, "compatible ID: 20 bytes");
            check(memcmp(s + at + 4, "WINUSB\0\0", 8) == 0, "compatible ID: WINUSB");
            static const uint8_t zero[8];
            check(memcmp(s + at + 12, zero, 8) == 0, "compatible ID: no sub-compatible ID");
        } else if (type == 4) {
            prop++;
            const uint8_t* p = s + at;
            unsigned name_len = u16(p + 6), data_len = u16(p + 8 + name_len);
            check(u16(p + 4) == 7, "registry property: REG_MULTI_SZ (7)");
            check(len == 10 + name_len + data_len, "registry property: its length is 10 + name + data");
            check(name_len == 42 && utf16_is(p + 8, "DeviceInterfaceGUIDs", 21),
                  "registry property: name DeviceInterfaceGUIDs, UTF-16 with its terminator");
            const uint8_t* g = p + 10 + name_len;
            check(data_len == 80, "registry property: data 80 bytes (38 characters, two terminators)");
            check(u16(g) == '{' && u16(g + 74) == '}' && u16(g + 76) == 0 && u16(g + 78) == 0,
                  "registry property: {GUID}, then two terminators (REG_MULTI_SZ)");
            int hex = 1;
            for (int i = 1; i < 37; i++) {
                unsigned c = u16(g + 2 * i);
                int dash = i == 9 || i == 14 || i == 19 || i == 24;
                if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) hex = 0;
            }
            check(hex, "registry property: the GUID is 8-4-4-4-12 hex digits");
        } else {
            check(0, "feature descriptor: only a compatible ID and a registry property");
        }
        at += len;
    }
    check(at == n, "Microsoft OS 2.0 set: the descriptors fill wTotalLength exactly");
    check(compat == 1 && prop == 1, "Microsoft OS 2.0 set: one compatible ID, one registry property");
}

static void test_webusb_url(void) {
    // Chrome asks with bmRequestType 0xC0, bRequest the vendor code, wValue 1 (the page), wIndex 2
    unsigned n;
    const uint8_t* u = lookup(2, 1, &n);
    check(u != NULL, "vendor request wIndex 2, wValue 1 returns the landing page");
    if (!u) return;
    static const char url[] = "badge.team/hackerhotel2027-tool/";
    check(n == u[0] && n == 3 + strlen(url), "URL descriptor: bLength is what the stack sends");
    check(u[1] == 3 && u[2] == 1, "URL descriptor: type 3, scheme https://");
    check(memcmp(u + 3, url, strlen(url)) == 0, "URL descriptor: https://badge.team/hackerhotel2027-tool/");
    check(!lookup(2, 0, &n) && !lookup(7, 1, &n), "no other URL index, no other Microsoft OS 2.0 value");
}

static void test_strings(void) {
    unsigned n;
    const uint8_t uid[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    usb_serial_init(uid);  // as fsusb.c does at start
    // the strings the device descriptor names: each bLength is what the stack sends
    for (unsigned i = 1; i <= 3; i++) {
        const uint8_t* s = lookup(0x0409, 0x0300 | i, &n);
        check(s && s[0] == n && s[1] == 3 && n % 2 == 0, "string descriptor: bLength is what the stack sends");
    }
    const uint8_t* s = lookup(0x0409, 0x0303, &n);
    check(s && n == 2 + 2 * USB_SERIAL_DIGITS && s[0] == n && s[1] == 3,
          "serial: 16 UTF-16 digits, bLength 34, type 3");
    check(s && utf16_is(s + 2, "0123456789ABCDEF", 16), "serial: the unique ID's bytes in memory order, in hex");
    const uint8_t uid2[8] = {0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    usb_serial_init(uid2);
    check(s && utf16_is(s + 2, "FEDCBA9876543210", 16), "serial: another chip, another serial");
}

static void test_table(void) {
    // every entry's own length field agrees with what the stack sends, and
    // no two entries answer the same request
    for (unsigned i = 0; i < DESCRIPTOR_LIST_ENTRIES; i++) {
        const uint8_t* d = descriptor_list[i].addr;
        unsigned n = descriptor_list[i].length;
        unsigned own;
        if (d == (const uint8_t*)&ms_os_20_set)
            own = u16(d + 8);
        else if (d == config_descriptor || d == bos_descriptor)
            own = u16(d + 2);
        else
            own = d[0];
        check(own == n, "descriptor_list: each length is the descriptor's own");
        for (unsigned j = 0; j < i; j++)
            check(descriptor_list[j].lIndexValue != descriptor_list[i].lIndexValue, "descriptor_list: no duplicates");
    }
}

int main(void) {
    test_device_and_config();
    test_bos();
    test_ms_os_20_set();
    test_webusb_url();
    test_strings();
    test_table();
    if (failures) {
        printf("%d USB descriptor checks failed\n", failures);
        return 1;
    }
    printf("ok    USB descriptors: device, configuration, BOS, Microsoft OS 2.0 set, WebUSB URL, strings\n");
    return 0;
}
