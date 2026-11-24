// The badge's hardware, for the firmware built as a PC program (hh27-emu).
//
// The firmware's own files run unchanged (see the Makefile); this file
// stands in for the parts that touch the chip: the LED matrix, the IR
// driver, standby, the boot ROM jump, flash, the USB endpoint, the clock.
//
// hh27-emu talks to the frontend (emu.py) over stdin and stdout, one line
// per message:
//
//   to the frontend                      from the frontend
//   F <brightness> <384 values, hex>     b <buttons held, hex: bit 0 A, 1 C, 2 B, 3 D>
//   U <reply, hex>                       u <USB packet, hex>
//   I <address> <command>  (IR sent)     i <address> <command>  (IR received)
//   P <packet, hex>  (IR packet sent)    p <packet, hex>  (IR packet received)
//                    a packet: type, length, data (no CRC)
//   S 1 / S 0  (standby on / off)
//   L <text>   (the firmware's log)
//   B          (jumped to the boot ROM; the program then exits)
//
// "Interrupts": the firmware holds s_lock while it runs and lets go only
// in Delay_Ms(); the reader thread takes it to hand over a USB packet,
// IR or the buttons. So, like on the chip, an interrupt never runs in the
// middle of the firmware's own work, only between two of its steps.
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "apps/hhapp.h"
#include "ch32fun.h"
#include "ch643_ledpwm.h"
#include "emu.h"
#include "flash_pages.h"
#include "emu_usb.h"
#include "infrared.h"
#include "pattern_player.h"
#include "standby.h"
#include "webusb_protocol.h"

GPIO_TypeDef emu_gpio[3];
AFIO_TypeDef emu_afio;
RCC_TypeDef emu_rcc;
USBFS_TypeDef emu_usbfs;
ESG_TypeDef emu_esig;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;   // see above
static pthread_mutex_t s_out = PTHREAD_MUTEX_INITIALIZER;    // stdout

// --- Output ----------------------------------------------------------------

static void out_line(const char* fmt, ...) __attribute__((format(__printf__, 1, 2)));
static void out_line(const char* fmt, ...) {
    pthread_mutex_lock(&s_out);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
    pthread_mutex_unlock(&s_out);
}

static void out_hex(char tag, const uint8_t* data, int len) {
    static const char HEX[] = "0123456789abcdef";
    char line[2 + 2 * 512 + 1];
    int n = 0;
    line[n++] = tag;
    line[n++] = ' ';
    for (int i = 0; i < len && n < (int)sizeof line - 3; i++) {
        line[n++] = HEX[data[i] >> 4];
        line[n++] = HEX[data[i] & 15];
    }
    line[n] = 0;
    out_line("%s", line);
}

// The log: the firmware's printf and debug output, a line at a time
static char s_log[256];
static int s_log_len;

static void log_bytes(const char* buf, int size) {
    for (int i = 0; i < size; i++) {
        if (buf[i] == '\n' || s_log_len == (int)sizeof s_log - 1) {
            s_log[s_log_len] = 0;
            out_line("L %s", s_log);
            s_log_len = 0;
        }
        if (buf[i] != '\n') s_log[s_log_len++] = buf[i];
    }
}

int emu_printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_bytes(buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
    return n;
}

void debug_output_write(const char* buf, int size) { log_bytes(buf, size); }

void emu_log(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out_line("L [emu] %s", buf);
}

// --- Time ------------------------------------------------------------------

static uint64_t now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

uint32_t funSysTick32(void) { return (uint32_t)now_us(); }

// --- LED matrix (ch643_ledpwm.h) ---------------------------------------------

static uint8_t s_cells[LED_MATRIX_ROWS][LED_MATRIX_COLS];
static uint8_t s_brightness = 255;
static bool s_leds_on;
static uint8_t s_sent[1 + HH_SLOTS];
static uint64_t s_sent_at;

void LEDMatrix_Init(void) { s_leds_on = true; }
void LEDMatrix_Deinit(void) { s_leds_on = false; }
void LEDMatrix_SetPixel(uint8_t row, uint8_t col, uint8_t v) {
    if (row < LED_MATRIX_ROWS && col < LED_MATRIX_COLS) s_cells[row][col] = v;
}
uint8_t LEDMatrix_GetPixel(uint8_t row, uint8_t col) {
    return row < LED_MATRIX_ROWS && col < LED_MATRIX_COLS ? s_cells[row][col] : 0;
}
void LEDMatrix_Clear(void) { memset(s_cells, 0, sizeof s_cells); }
void LEDMatrix_Show(void) {}  // the emulator sends a frame from Delay_Ms(), after the drawing
void LEDMatrix_SetBrightness(uint8_t b) { s_brightness = b; }
uint8_t LEDMatrix_GetBrightness(void) { return s_brightness; }

// At most 50 frames a second, and only when something changed
static void send_frame(void) {
    uint64_t t = now_us();
    if (t - s_sent_at < 20000) return;
    uint8_t f[1 + HH_SLOTS];
    f[0] = s_leds_on ? s_brightness : 0;
    memcpy(f + 1, s_cells, HH_SLOTS);
    if (!memcmp(f, s_sent, sizeof f)) return;
    memcpy(s_sent, f, sizeof f);
    s_sent_at = t;
    out_hex('F', f, sizeof f);  // the frontend splits off the brightness
}

// --- Buttons -----------------------------------------------------------------

// buttons.c reads the pins: PB20 (A), PC4 (C), PC5 (B), PB18 (D), low when pressed
static void set_buttons(uint8_t held) {
    uint32_t b = 0xFFFFFFFFu, c = 0xFFFFFFFFu;
    if (held & HH_BTN_A) b &= ~(1u << 20);
    if (held & HH_BTN_D) b &= ~(1u << 18);
    if (held & HH_BTN_C) c &= ~(1u << 4);
    if (held & HH_BTN_B) c &= ~(1u << 5);
    GPIOB->INDR = b;
    GPIOC->INDR = c;
}

static bool any_button(void) { return (~GPIOB->INDR & (1u << 20 | 1u << 18)) || (~GPIOC->INDR & (1u << 4 | 1u << 5)); }

// --- USB endpoint (fsusb.h) ----------------------------------------------------

static uint8_t s_ep[64];
void FSUSBSetup(void) {}
uint8_t* USBFS_GetEPBufferIfAvailable(int ep) {
    (void)ep;
    return s_ep;
}
int USBFS_SendEndpoint(int ep, int len) {
    (void)ep;
    out_hex('U', s_ep, len);
    return 0;
}

// --- Infrared (infrared.h) -----------------------------------------------------

static infrared_nec_message_t s_ir_queue[16];
static unsigned s_ir_head, s_ir_tail;
static uint64_t s_ir_busy_until;
static bool s_ir_on;

void infrared_init(void) { s_ir_on = true; }
bool infrared_deinit(void) {
    bool was = s_ir_on;
    s_ir_on = false;
    return was;
}
bool infrared_tx_busy(void) { return now_us() < s_ir_busy_until; }
bool infrared_send_nec(uint16_t address, uint8_t command) {
    if (!s_ir_on || infrared_tx_busy()) return false;
    s_ir_busy_until = now_us() + 68000;  // a NEC frame takes about 68 ms
    out_line("I %u %u", address, command);
    return true;
}
bool infrared_send_nec_raw(uint32_t data) { return infrared_send_nec((uint16_t)(data & 0xFFFF), (uint8_t)(data >> 16)); }
bool infrared_send_nec_repeat(void) { return !infrared_tx_busy(); }
bool infrared_receive(infrared_nec_message_t* m) {
    if (s_ir_tail == s_ir_head) return false;
    *m = s_ir_queue[s_ir_tail++ % 16];
    return true;
}
static void ir_received(uint16_t address, uint8_t command) {
    if (!s_ir_on || s_ir_head - s_ir_tail >= 16) return;
    s_ir_queue[s_ir_head++ % 16] = (infrared_nec_message_t){address, command, false};
}

// Packets: as long on air as on the badge (ir_codec.h), and the receiver
// keeps 3, as infrared.c does
static infrared_packet_t s_ir_packets[3];
static unsigned s_ir_packet_head, s_ir_packet_tail;

bool infrared_send_packet(uint8_t type, const uint8_t* data, uint8_t length) {
    if (!s_ir_on || infrared_tx_busy() || length > IR_PACKET_DATA_MAX) return false;
    uint8_t p[2 + IR_PACKET_DATA_MAX] = {type, length};
    memcpy(p + 2, data, length);
    unsigned ones = 0;
    for (unsigned i = 0; i < 2u + length; i++) ones += (unsigned)__builtin_popcount(p[i]);
    ones += 4;  // about half the CRC's bits
    unsigned bits = 8 * (length + 3u);
    s_ir_busy_until = now_us() + 6750 + bits * 1124 + ones * 1125 + 562;
    out_hex('P', p, 2u + length);
    return true;
}
bool infrared_receive_shot(ir_shot_t* shot) {  // the emulator has no shots yet
    (void)shot;
    return false;
}
unsigned infrared_raw_read(uint16_t* out, unsigned max) {  // the emulator's IR has no marks and spaces
    (void)out, (void)max;
    return 0;
}
bool infrared_receive_packet(infrared_packet_t* packet) {
    if (s_ir_packet_tail == s_ir_packet_head) return false;
    *packet = s_ir_packets[s_ir_packet_tail++ % 3];
    return true;
}
static void ir_packet_received(const uint8_t* p, int n) {
    if (!s_ir_on || n < 2 || p[1] > IR_PACKET_DATA_MAX || n != 2 + p[1] || s_ir_packet_head - s_ir_packet_tail >= 3) return;
    infrared_packet_t* q = &s_ir_packets[s_ir_packet_head++ % 3];
    q->type = p[0];
    q->length = p[1];
    memcpy(q->data, p + 2, p[1]);
}

// --- Standby and the boot ROM ----------------------------------------------------

void standby(void) {
    bool playing = PatternPlayer_Pause();
    WebUSB_Deinit();
    LEDMatrix_Deinit();
    bool ir = infrared_deinit();
    out_line("S 1");
    send_frame();
    while (!any_button()) Delay_Ms(10);  // any button wakes the badge
    out_line("S 0");
    LEDMatrix_Init();
    WebUSB_Init();
    if (ir) infrared_init();
    if (playing) PatternPlayer_Resume();
}

void jump_to_bootloader(void) {
    out_line("B");
    exit(0);
}

// --- Flash (flash_pages.h) --------------------------------------------------------

static bool in_store(uint32_t addr) { return addr >= STORE_FLASH_BASE && addr + FLASH_PAGE_SIZE <= STORE_FLASH_BASE + STORE_FLASH_SIZE; }

void FlashPages_Erase(uint32_t addr) {
    if (in_store(addr)) memset((void*)(uintptr_t)addr, 0xFF, FLASH_PAGE_SIZE);
}

void FlashPages_Program(uint32_t addr, const uint8_t* data) {
    if (in_store(addr)) memcpy((void*)(uintptr_t)addr, data, FLASH_PAGE_SIZE);
}

// The store lives in a file, mapped at the address it has on the chip, so
// the firmware's pointers into flash work as they are. A new file starts
// erased (all 0xFF).
static void map_flash(const char* path) {
    uintptr_t page = STORE_FLASH_BASE & ~0xFFFu;
    size_t size = (STORE_FLASH_BASE + STORE_FLASH_SIZE) - page;
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        exit(1);
    }
    off_t old = lseek(fd, 0, SEEK_END);
    if (old < (off_t)size) {
        uint8_t ff[4096];
        memset(ff, 0xFF, sizeof ff);
        for (off_t n = old; n < (off_t)size; n += sizeof ff)
            if (write(fd, ff, sizeof ff) < 0) exit(1);
    }
    void* p = mmap((void*)page, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    if (p != (void*)page) {
        fprintf(stderr, "cannot map the flash at 0x%lx\n", (unsigned long)page);
        exit(1);
    }
}

static void map_ram(uintptr_t start, size_t size) {
    void* p = mmap((void*)start, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void*)start) {
        fprintf(stderr, "cannot map RAM at 0x%lx\n", (unsigned long)start);
        exit(1);
    }
    memset(p, 0, size);
}

// --- The loop: Delay_Ms() and the reader thread ------------------------------------

void Delay_Ms(uint32_t ms) {
    send_frame();
    pthread_mutex_unlock(&s_lock);
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
    pthread_mutex_lock(&s_lock);
}

static int hex_bytes(const char* s, uint8_t* out, int max) {
    int n = 0;
    while (n < max && s[0] && s[1] && s[0] != '\n') {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static void* reader(void* arg) {
    (void)arg;
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        pthread_mutex_lock(&s_lock);
        if (line[0] == 'b') {
            set_buttons((uint8_t)strtoul(line + 2, NULL, 16));
        } else if (line[0] == 'u') {
            uint8_t p[64];
            int n = hex_bytes(line + 2, p, sizeof p);
            if (n > 0) WebUSBBulkDataOut(p, n);
        } else if (line[0] == 'p') {
            uint8_t p[2 + IR_PACKET_DATA_MAX];
            ir_packet_received(p, hex_bytes(line + 2, p, sizeof p));
        } else if (line[0] == 'i') {
            unsigned a, c;
            if (sscanf(line + 2, "%u %u", &a, &c) == 2) ir_received((uint16_t)a, (uint8_t)c);
        }
        pthread_mutex_unlock(&s_lock);
    }
    exit(0);  // the frontend went away
}

int firmware_main(void);  // ledmatrix.c's main()

int main(int argc, char** argv) {
    const char* flash = argc > 1 ? argv[1] : "hh27-emu-flash.bin";
    map_flash(flash);
    map_ram(EMU_RAM_BASE, EMU_RAM_SIZE);
    map_ram(EMU_ROM_BASE, EMU_ROM_SIZE);  // the API table for apps (app.c)
    emu_esig.UID0 = (uint32_t)now_us() ^ (uint32_t)getpid() * 2654435761u;
    set_buttons(argc > 2 ? (uint8_t)strtoul(argv[2], NULL, 16) : 0);  // held at power-on

    pthread_mutex_lock(&s_lock);
    pthread_t t;
    pthread_create(&t, NULL, reader, NULL);
    return firmware_main();
}
