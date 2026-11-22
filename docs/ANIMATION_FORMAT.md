# Animation format (.ledanim)

A `.ledanim` file holds one animation for the badge's 384 LED slots. The
web interface writes it (`webui/compression.js`) and the firmware plays it
from flash (`pattern_player.c`). The firmware decodes one frame at a time,
because the whole animation does not fit in 20 KB of RAM.

There are two versions:

- **LA03**: what the web interface writes now.
- **LA02**: the version before LA03. The web interface and the firmware
  still read it.

Both formats were made for this badge. There is no external standard.
LA03 uses well-known techniques (see [References](#references)):
[run-length coding](https://en.wikipedia.org/wiki/Run-length_encoding),
[delta frames](https://en.wikipedia.org/wiki/Delta_encoding), an
[Elias gamma code](https://en.wikipedia.org/wiki/Elias_gamma_coding) and a
very simple form of
[motion compensation](https://en.wikipedia.org/wiki/Motion_compensation).

`webui/compression.js` is the reference. If this document and the code do
not agree, the code is correct, and this document must be fixed.
`tools/codec_test/run.sh` checks that the web interface and the firmware
decode the same frames.

## Slots and values

A slot is one LED channel in the LED matrix: `row * 24 + col`, where
`row` is the COM line (PB0..PB15) and `col` is the PWM channel
(PA0..PA23). The 384 slots are:

- 208 mono LEDs in the centre grid,
- 71 mono LEDs on the outline,
- 35 RGB LEDs on the arms, with 3 slots each.

A frame gives each slot a brightness from 0 (off) to 255 (full).

The file stores *quantized* values: each value has `quant_bits` bits (1,
2, 4 or 8). The decoder keeps the previous frame as quantized values, and
it compares and changes those values, not the brightness.

```
quantize(v)   = round(v / 255 * (2^quant_bits - 1))       (v if quant_bits = 8)
dequantize(l) = round(l / (2^quant_bits - 1) * 255)       (l if quant_bits = 8)
```

`round` rounds halves up (JavaScript `Math.round`; the firmware uses the
integer form `(l * 255 + levels / 2) / levels`).

## Header

The first 8 bytes are the same in both versions:

| Byte | Content |
|---|---|
| 0..3 | magic: `LA03` or `LA02` (ASCII) |
| 4..5 | number of frames, little-endian (1..65535) |
| 6 | frames per second (1..255); each frame shows for `1000 / fps` ms (integer division) |
| 7 | `quant_bits`: 1, 2, 4 or 8 |

The frames follow directly after the header.

## Bit stream

After the header, everything is packed bit by bit, with no padding until
the end of the file. The last byte is padded with zero bits.

- Bits fill each byte from bit 0 (the least significant bit) up.
- A value of n bits is written least significant bit first.

The reader therefore reads bit 0 of byte 8 first.

Each frame starts with a 2-bit frame type. Frame 0 is always a key frame
or a raw frame. The player loops: after the last frame it continues with
frame 0. Because frame 0 sets every slot, the loop needs no reset.

## LA03

### Pixel order

LA03 codes the slots in a spatial order, `LAYOUT_ORDER`. With this order,
LEDs that are neighbours on the badge are also neighbours in the stream.
`tools/gen_layout.py` generates it from `webui/board_data.js`, into
`webui/layout_data.js` (for the web interface) and `layout.c` (for the
firmware).

1. The centre grid, row by row from the top, each row from left to right.
   The grid is 17 columns by 16 rows on a 2 mm pitch. Its corners have no
   LED and are skipped, which leaves 208 positions.
2. The 71 outline LEDs, clockwise, starting at the top.
3. The 35 RGB LEDs, as red, green, blue for each LED, ordered by arm
   (0 = the top arm, then clockwise) and then by rank (0 = nearest the
   centre).

In the sections below, a *position* is an index into this order
(0..383).

### Gamma code

Run lengths and gaps are positive integers. LA03 writes them with an
Elias gamma code, so small numbers cost few bits.

To write n (n ≥ 1), where k = floor(log2(n)):

1. write k zero bits,
2. write one bit with the value 1,
3. write `n - 2^k` as a k-bit value (least significant bit first, like
   every value).

To read: count the zero bits up to the first 1 bit, which gives k. Then
read k bits as a value v. The result is `2^k + v`.

Example: 5 has k = 2, so it is written as `0 0 1`, then 1 as a 2-bit
value: `1 0`. The 5 bits in stream order are `0 0 1 1 0`.

A gamma code costs `2k + 1` bits: 1 costs 1 bit, 2..3 cost 3 bits, 4..7
cost 5 bits, and 256..511 cost 17 bits.

### Frame types

| Type | Name | Content after the 2-bit type |
|---|---|---|
| 0 | key | runs until 384 positions are filled |
| 1 | delta | a delta body |
| 2 | shift | a 2-bit direction, then a delta body |
| 3 | raw | 384 values, `quant_bits` each, in position order |

**Key frame.** The frame is a sequence of runs. Each run is
`gamma(run length)`, then the value (`quant_bits` bits). The run sets
that many positions to the value, from the current position on. The
runs continue until position 384.

**Delta body.** Only the positions that changed:

1. `gamma(count + 1)`, where count is the number of changed positions.
   With no changes, this is 1 bit.
2. For each change, in increasing position order:
   - `gamma(gap)`, where gap is the distance from the previous change
     (the first gap is counted from position -1);
   - the new value (`quant_bits` bits).

All other positions keep their values from the previous frame.

**Shift frame.** First the grid of the previous frame moves one LED. Then
a delta body applies the changes. The direction tells where each grid
LED takes its value from in the previous frame:

| Direction | Picture moves | Grid LED (x, y) takes the value of |
|---|---|---|
| 0 | left | (x + 1, y) |
| 1 | right | (x - 1, y) |
| 2 | up | (x, y + 1) |
| 3 | down | (x, y - 1) |

y = 0 is the top row. If the source is outside the grid, or is a corner
without an LED, the value is 0. The outline and the RGB LEDs do not move.

The firmware shifts in place. It walks the grid starting at the side the
values come from, so it reads each source before it overwrites it.

**Raw frame.** All 384 values, in position order.

### Encoder

For every frame, the encoder calculates the size of each possible frame
type and writes the smallest one:

- a key frame,
- a raw frame,
- a delta frame against the previous frame,
- a shift frame in each of the 4 directions.

Frame 0 has no previous frame, so only key or raw is possible there.

Measured on test animations:

| Content | LA03, as a share of LA02 |
|---|---|
| Bad Apple on the LED positions | 54–58% |
| Text that scrolls one LED per frame | 10–14% |
| Random noise | about 100% |

## LA02

LA02 codes the slots in matrix order (slot 0..383), with fixed widths:
`INDEX_BITS` = 9 bits for a slot index and for a run length.

| Type | Name | Content after the 2-bit type |
|---|---|---|
| 0 | key frame | runs until 384 slots are filled: `run - 1` (9 bits), then the value (`quant_bits`) |
| 1 | index delta | count (9 bits), then for each change: slot (9 bits), value (`quant_bits`) |
| 2 | bitmap delta | 384 one-bit flags (1 = changed, in slot order), then a value for each set flag, in slot order |
| 3 | raw | 384 values (`quant_bits` each), in slot order |

The encoder picked the smallest of these types for each frame.

## References

- [Run-length encoding](https://en.wikipedia.org/wiki/Run-length_encoding):
  the key frames of LA02 and LA03.
- [Delta encoding](https://en.wikipedia.org/wiki/Delta_encoding): the delta
  frames, which store only what changed since the previous frame.
- [Elias gamma coding](https://en.wikipedia.org/wiki/Elias_gamma_coding):
  LA03's run lengths and gaps.
- [Motion compensation](https://en.wikipedia.org/wiki/Motion_compensation):
  what LA03's shift frame does in its simplest form, with one shift of one
  LED for the whole grid.
- [Gamma correction](https://en.wikipedia.org/wiki/Gamma_correction): the
  firmware applies gamma 2.2 to the decoded values (`docs/LEDPWM_MATRIX.md`).
