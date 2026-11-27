# App API reference

Every member of `hh_api_t` (`apps/hhapp.h`), the table the firmware hands
an app in `init()`. `hhapp.h` is the contract; this page explains it with
examples. For building, the emulator and the library, see
[apps/README.md](../apps/README.md).

```c
#include "hhapp.h"

static const hh_api_t* hh;

static void init(const hh_api_t* api) { hh = api; }

static void tick(uint32_t elapsed_ms) {
    hh->clear();
    hh->draw_number(HH_CENTER, 4, hh->millis() / 1000, 255);  // seconds since the start
    if (hh->pressed() & HH_BTN_B) hh->set_rgb(0, 255, 0, 0);
}

HH_APP(init, tick);
```

## Versions

`HH_API_VERSION` grows when members are added. New members only ever go at
the end of `hh_api_t`. The firmware does not start an app built against a
newer version than its own. `tools/badge.py list` shows the badge's
version ("app API"), and the web interface asks for an update when the
badge is older than the page.

| Version | Added |
|---|---|
| 1 | LEDs, layout tables, buttons, time, random, IR, system, persistent data |
| 2 | `draw_text`, `text_width` |
| 3 | `polar` |
| 4 | `set_rgb`, `set_linear`, `sin8`, `hsv`, `isqrt`, `atan8`, `draw_number`, `step_brightness`; `HH_CENTER` for `draw_text` |
| 5 | `ir_send_packet`, `ir_receive_packet` |
| 6 | `ir_receive_shot` |
| 7 | `ir_send_raw` |

| Member | Version |
|---|---|
| `version`, `size` | 1 |

`version` is the firmware's `HH_API_VERSION`; `size` is `sizeof(hh_api_t)`
in the firmware.

## The badge

| Thing | Size | Where |
|---|---|---|
| Centre grid | 17 × 16, 208 LEDs (the corners are `HH_NONE`) | `grid[y][x]`, `y = 0` at the top |
| Outline | 71 LEDs, clockwise from the top | `edge[i]` |
| RGB LEDs | 35: 5 arms of 7 | `rgb[i]`: slots `.r .g .b`, `.arm` (0 = top, then clockwise), `.rank` (0 = nearest the centre .. 6 = the tip) |

An arm's centre line is ranks 0, 3, 4, 5, 6; ranks 1 and 2 are its sides.
The buttons sit next to arms: A arm 4, B arm 1, C arm 3, D arm 2.

Every LED has a *slot*, `row * 24 + col` in the LED matrix (`HH_SLOTS` =
384). The layout tables turn places into slots.

## LEDs

| Member | |
|---|---|
| `void set(uint16_t slot, uint8_t value)` | 0 = off .. 255 = full. Ignores `HH_NONE` and slots out of range. |
| `uint8_t get(uint16_t slot)` | What `set()` put there this frame. |
| `void clear(void)` | All LEDs off. |
| `void set_rgb(int index, uint8_t r, uint8_t g, uint8_t b)` | RGB LED `rgb[index]`. Ignores a bad index. (4) |
| `void set_linear(uint16_t slot, uint8_t value)` | `value` in linear light, the PWM duty. (4) |
| `void set_brightness(uint8_t b)`, `uint8_t brightness(void)` | The global brightness, 255 = unscaled. The badge keeps it over power-off. |
| `int step_brightness(int dir)` | One of the menu's levels up (`dir > 0`) or down (`dir < 0`), as B and D outside apps; returns the level now, 0 .. `HH_BRIGHTNESS_LEVELS - 1`. `step_brightness(0)` only returns the level. (4) |

The values are brightness as a screen shows it: the firmware applies gamma
2.2, so 128 looks half as bright, not nearly full. Any value above 0
glows. `set_linear()` is for effects that think in light (the previous
firmware's animations, and `apps/fx.h`): half the value is half the light.

The firmware shows the frame when `tick()` returns. What an app set stays
until it sets it again or calls `clear()`, so an app can redraw only what
changed, or start each frame with `clear()`.

## Layout tables

| Member | |
|---|---|
| `const uint16_t (*grid)[HH_GRID_W]` | `grid[y][x]`, the centre grid |
| `const uint16_t *edge` | the outline |
| `const hh_rgb_t *rgb` | the RGB LEDs |
| `const uint8_t (*polar)[2]` | per slot: distance from the grid's centre in 1/4 mm (at most 255), and angle in 1/256 turn, clockwise from the top. The outline is 21 to 45 mm out. An RGB LED's three slots have the same place. (3) |

A ripple from the middle, over every LED:

```c
uint8_t w = hh->millis() / 4;
for (uint16_t s = 0; s < HH_SLOTS; s++)
    hh->set(s, hh->sin8((uint8_t)(hh->polar[s][0] * 6 - w)));
```

## Maths and colour (4)

| Member | |
|---|---|
| `uint8_t sin8(uint8_t angle)` | 128 + 127 sin(angle / 256 turn), 1 .. 255. `sin8(angle + 64)` is the cosine. |
| `uint32_t hsv(uint8_t h, uint8_t s, uint8_t v)` | A colour, `0xRRGGBB`: hue `h` in 1/256 turn (0 red, 85 green, 170 blue), saturation, value. Take it apart with `HH_RED()`, `HH_GREEN()`, `HH_BLUE()`. |
| `uint32_t isqrt(uint32_t n)` | The square root, rounded down. |
| `uint8_t atan8(int x, int y)` | The angle of (x, y), with y down as on the grid, in 1/256 turn clockwise from straight up: up 0, right 64, down 128, left 192. The same way round as `polar`. Within 1/256 turn. |

A point at angle `a` and radius `r`, the other way: `x = (sin8(a) - 128) * r / 127`,
`y = (128 - sin8(a + 64)) * r / 127`.

A rainbow round the arms:

```c
for (int i = 0; i < HH_RGB_COUNT; i++) {
    uint32_t c = hh->hsv((uint8_t)(hh->rgb[i].arm * 51 + hh->millis() / 20), 255, 200);
    hh->set_rgb(i, HH_RED(c), HH_GREEN(c), HH_BLUE(c));
}
```

## Text (2, 4)

| Member | |
|---|---|
| `int draw_text(int x, int y, const char *text, uint8_t value)` | Text on the centre grid, its top left corner at column `x`, row `y`. Returns its width. |
| `int text_width(const char *text)` | The width, without drawing. |
| `int draw_number(int x, int y, int32_t n, uint8_t value)` | `n` in decimal, as `draw_text`. (4) |

The font is 5 × 8 (`HH_FONT_W`, `HH_FONT_H`: 7 rows and one for the
descenders of g j p q y), all of printable ASCII; other bytes show as a
blank. Each character takes `HH_FONT_ADVANCE` (6) columns; the width
leaves out the blank column after the last one. The grid is 17 wide, so
three characters fit.

`x` and `y` may be off the grid, for scrolling: what falls outside, or on a
corner without an LED, is skipped. Lit pixels get `value`; the rest is left
alone. With `x = HH_CENTER` (4) the text is centred:

```c
hh->draw_number(HH_CENTER, 4, score, 255);
```

## Buttons

| Member | |
|---|---|
| `uint8_t pressed(void)` | The buttons that went down since the previous tick. |
| `uint8_t held(void)` | The buttons down now. |

The bits are `HH_BTN_A` .. `HH_BTN_D`, by the silkscreen letters,
debounced. Holding A for a second opens the badge's menu: the app gets no
ticks while it is open. The app sees that press of A too, so do not give
a long press of A a meaning of its own; taps of A are fine. Holding C and
D together for 3 s reboots the badge into its USB boot ROM, so do not make
people hold both for that long.

## Time and chance

| Member | |
|---|---|
| `uint32_t millis(void)` | Milliseconds since the app started, as of this tick. |
| `uint32_t random(void)` | 32 random bits (xorshift, seeded from the chip's ID and the time). |

`tick(elapsed_ms)` comes about every 5 ms; `elapsed_ms` is the time since
the previous one. After the menu was open it can be long: a game that
steps at a fixed rate should cap how many steps it catches up.

## Infrared

| Member | |
|---|---|
| `bool ir_send(uint16_t address, uint8_t command)` | Sends an NEC frame (about 68 ms). False while the previous one is still going out. |
| `bool ir_receive(hh_ir_t *message)` | False when nothing came in. |

`hh_ir_t` has `.address` (8-bit for standard NEC, 16-bit for extended
NEC), `.command` and `.repeat` (an NEC repeat code: the sender holds its
button). Samsung TV remotes come in as 16-bit addresses with both bytes the
same (0x0707 for a TV); a held Samsung key repeats the whole frame. See
`apps/eye` for a remote's keys.

### Packets, badge to badge (5)

| Member | |
|---|---|
| `bool ir_send_packet(uint8_t type, const void *data, uint8_t length)` | Sends a packet: a type and up to `HH_IR_PACKET_MAX` (32) bytes. False while the previous packet or NEC frame still goes out, or if `length` is too long. |
| `bool ir_receive_packet(hh_ir_packet_t *packet)` | False when none came in. |

`hh_ir_packet_t` has `.type`, `.length` and `.data[HH_IR_PACKET_MAX]`. A
CRC on air makes sure that what arrives is what was sent; a damaged packet
is dropped. The firmware keeps up to 3 packets that came in, so read them
every tick.

A packet takes 6.75 ms + 1.1 to 2.2 ms per bit, 8 × (length + 3) bits:
0.2 s for 10 bytes, 0.65 s at most. Nothing confirms that a packet
arrived: answer each packet, and send again (after a random wait, so two badges do not keep
colliding) when no answer comes.

Pick a type of your own for your app's packets. `'H'` and `'R'` are SAY
HI's name swap: outside apps the firmware answers them; while an app runs,
the app gets them like any other. `apps/four` (4 in a row) uses `'F'`, and is a
full example: invites, moves, answers and sending again.

`apps/nametag` (NAMETAG) uses `'H'` and `'R'` themselves, so that it
also swaps names with badges that run no app. After the name it can put a
NUL and one info byte (`0x10` | the banana team, 15 for none); the
firmware shows a name only up to the NUL, so a plain badge shows "HI
ALICE". Every 20 to 60 s it also sends a beacon, type `'N'` with the same
contents: other NAMETAG badges learn the name from it, and the firmware
ignores the type. (An `'R'` would not do: a plain badge doing SAY HI takes
any `'R'` as its answer.) About every 12 minutes it says HELLO by itself,
so that it also learns the names of badges that run no app; each of those
shows "HI ALICE" for 5 s then. It answers a plain badge's HELLO in time
to end before that badge calls again, 1.5 s after its HELLO began.

```c
// Ping: B sends, any badge that hears it lights up
static void tick(uint32_t elapsed_ms) {
    if (hh->pressed() & HH_BTN_B) hh->ir_send_packet('P', "ping", 4);
    hh_ir_packet_t p;
    while (hh->ir_receive_packet(&p))
        if (p.type == 'P') hh->set_rgb(0, 0, 255, 0);
}
```

On air, a packet is NEC-coded bits after a 4.5 ms mark and a 2.25 ms space
(see `ir_codec.h`), so it is never taken for a remote's frame.

### Shots from blasters and bananas (6)

| Member | |
|---|---|
| `bool ir_receive_shot(hh_ir_shot_t *shot)` | A shot from a Hacker Hotel 2025 banana or a Fri3d Camp 2022 Time Blaster, with a good CRC. False when none came in; the firmware keeps up to 7. |

`hh_ir_shot_t` has `.team` (0 .. 7), `.trigger` (1: a trigger pull),
`.command` and `.parameter` (0 .. 15):

| Command | | Parameter |
|---|---|---|
| `HH_SHOT_SHOOT` (1) | a shot | the channel |
| `HH_SHOT_HEAL` (2) | a heal | the channel |
| `HH_SHOT_TEAM` (8) | the banana: "take my team" | the channel |
| `HH_SHOT_BYTE` (10) | the banana: one byte typed into its serial port, `HH_SHOT_BYTE_OF(shot)` | part of the byte |

The banana's team is an index: 0 red, 1 green, 2 blue, 3 yellow, 4 magenta,
5 light blue. The Time Blaster's is one bit per colour: 1 red, 2 green,
4 blue. Before each shot the banana sends a NEC frame (address 0) for the
bar's LED strips, which `ir_receive()` gets: command 0x44 red, 0x40 green,
0x43 blue, 0x16 yellow, 0x0D magenta, 0x09 light blue.

On air a shot is 16 bits with JVC timing (leader 8.4 ms + 4.2 ms), with no
edge after its stop mark: the firmware takes it as a shot after 4 ms of
silence. `apps/banana` is an example; `tools/badge.py irlog` shows what any
remote or blaster sends.

### Raw frames (7)

| Member | |
|---|---|
| `bool ir_send_raw(const uint16_t *us, uint16_t count, uint32_t carrier_hz)` | Sends `count` marks and spaces, alternating and starting with a mark, `us[i]` microseconds each, on a carrier of `carrier_hz`. The firmware reads `us` while the frame goes out, so it must be the app's own data (a `const` table or a global), not an array on the stack, which is refused. False while something still goes out (this or another `ir_send*`: they share the transmitter), for `count` 0, or for a carrier outside 20000 .. 60000. |

For remote controls that `ir_send()` does not speak: Sony (40 kHz), RC5
and RC6 (36 kHz), Panasonic, and so on. A mark is the carrier on, a space
off; each lasts at least one carrier period. End with a space to keep the
line quiet that long before the next frame: `send` returns false until it
is over.

The firmware reads `us[]` while the frame goes out, so leave the array as
it is until then; a `const` table is simplest. When the app stops (the
menu, the web interface, `exit()`), a frame that still goes out stops
too. The badge has one small IR LED, so expect a few metres, not a room.
The receiver only decodes the frames above; `tools/badge.py irlog` on
another badge shows the marks and spaces of a 38 kHz frame.

```c
// B: a Sony "power" (12 bits, command 21, device 1), three times
static const uint16_t sony_power[] = {
    2400, 600,                                     // leader
    1200, 600, 600, 600, 1200, 600, 600, 600,      // command 21 = 1010100, LSB first
    1200, 600, 600, 600, 600, 600,
    1200, 600, 600, 600, 600, 600, 600, 600,       // device 1 = 10000
    600, 25000,                                    // the last bit, then the gap
};
static int left;

static void tick(uint32_t elapsed_ms) {
    if (hh->pressed() & HH_BTN_B) left = 3;
    if (left && hh->ir_send_raw(sony_power, sizeof sony_power / sizeof *sony_power, 40000)) left--;
}
```

In the emulator, each raw frame shows in the terminal, and
`tools/emu/headless.py` keeps them in `ir_raw`.

## System

| Member | |
|---|---|
| `void standby(void)` | Sleep until a button wakes the badge; returns after. That press is not passed on. |
| `void exit(void)` | Leave the app once this tick returns. The badge shows the name tag. |
| `void print(const char *text)` | Debug output over SWIO (`minichlink -T`); in the emulator, its log. |

## Persistent data

| Member | |
|---|---|
| `const char *name(void)` | The owner's name, NUL-terminated; `""` if not set. |
| `void set_name(const char *name)` | At most `HH_NAME_MAX` (32) characters. |
| `bool load(void *data, unsigned len)` | False if this app saved nothing yet. |
| `bool save(const void *data, unsigned len)` | At most `HH_APP_DATA_MAX` (28) bytes. False if there is no room. |

The data lives in flash, kept by the app's name in the store: it stays
over power-off, a store erase and a new upload of the app. Save only when
something changed (a new high score), not every tick: each save is a flash
write.

## Size

An app's image and its `.bss` must fit in 12 KB of RAM (`HH_APP_RAM_SIZE`),
and all stored items share 31 KB of flash. The helpers of version 4 are in
the firmware once, so an app that uses them stays small. Each call goes
through the table, so a helper called thousands of times a frame is a
little slower than an inline copy; that hardly ever matters at 30 frames a
second.
