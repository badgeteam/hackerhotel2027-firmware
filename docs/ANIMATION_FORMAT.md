# Animation format (.ledanim)

A `.ledanim` file holds one animation for the badge's 384 LED slots. The
web interface writes it (`webui/compression.js`) and the firmware plays it
from flash (`la04.c`, called by `pattern_player.c`). The firmware decodes
one frame at a time, because the whole animation does not fit in RAM.

The current version is **LA04**. The firmware and the web interface read
only LA04. The older versions (LA02, LA03) are gone: the badge has not
shipped yet, so no stored animations had to stay readable.

LA04 was made for this badge. There is no external standard. It uses
well-known techniques (see [References](#references)):
[prediction](https://en.wikipedia.org/wiki/Motion_compensation) from the
previous frames and from neighbouring LEDs,
[run-length coding](https://en.wikipedia.org/wiki/Run-length_encoding)
of what the prediction gets right, and
[canonical Huffman codes](https://en.wikipedia.org/wiki/Canonical_Huffman_code)
whose tables are in the file.

`webui/compression.js` is the reference. If this document and the code do
not agree, the code is correct, and this document must be fixed.
`tools/codec_test/run.sh` checks that the web interface and the firmware
decode the same frames.

## Why LA04

LA04 replaced LA03 in November 2026. It came out best of nine candidate
formats that were built and measured on 27 test animations (the old
firmware's animations and games, video, scrolling text, synthetic
patterns), and checked on 10 more that were kept apart.

| Content (4 bits per LED) | LA04, as a share of LA03 |
|---|---|
| Text effects, scrolling text | 13–35% |
| Recorded games | 24–32% |
| The old firmware's animations | 14–66% |
| Video | 71–81% |
| Random noise | about 100% |

So the store holds about two to four times as much, except for video.

The price:

- The decoder is about 6 KB of code (LA03: 3 KB).
- The decoder needs about 4 KB of tables and frame buffers. It borrows
  the app RAM block (`HH_APP_RAM_BASE`, `apps/hhapp.h`) for them: no app
  runs while an animation plays. The firmware's own RAM does not change.
- The worst frame of the test animations takes about 67,000 instructions,
  about 2 ms at 48 MHz. The average is about 15,000.
- The encoder is slow: up to about 5 seconds for 300 frames at 8 bits per
  LED. The web interface runs it in a worker
  (`webui/compression_worker.js`), so the page does not stop.

## Slots, positions and values

A slot is one LED channel in the LED matrix: `row * 24 + col`, where
`row` is the COM line (PB0..PB15) and `col` is the PWM channel
(PA0..PA23). The 384 slots are:

- 208 mono LEDs in the centre grid,
- 71 mono LEDs on the outline,
- 35 RGB LEDs on the arms, with 3 slots each.

A frame gives each slot a brightness from 0 (off) to 255 (full).

The file codes the slots in **position order** (`layout_order` in
`layout.c`, `LAYOUT_ORDER` in `webui/layout_data.js`, both made by
`tools/gen_layout.py`), so that neighbours on the badge are neighbours in
the stream:

| Positions | LEDs |
|---|---|
| 0..207 | the centre grid, row by row from the top, left to right (the corners have no LED) |
| 208..278 | the outline, clockwise from the top |
| 279..383 | the RGB LEDs, as red, green, blue per LED; LED k = arm × 7 + rank (arm 0 at the top, then clockwise; rank 0 nearest the centre) |

The file stores *quantized* values (levels): each value has q bits (1, 2,
4 or 8), so a level is 0..m with m = 2^q − 1.

```
quantize(v)   = round(v / 255 * m)       (v if q = 8)
dequantize(l) = round(l / m * 255)       (l if q = 8)
```

`round` rounds halves up (JavaScript `Math.round`; the firmware uses the
integer form `(l * 255 + m / 2) / m`).

At q = 8 the encoder may store a value that differs from the original, but
only when the two have the same PWM duty: `layout_gamma` maps 256 values
to 184 duties (all of 0..14 give duty 0), and the badge shows values with
the same duty exactly alike, at every brightness. This saves about 7% at
q = 8. If `layout_gamma` ever changes, re-encode the animations.

## Header

| Byte | Content |
|---|---|
| 0..3 | magic: `LA04` (ASCII) |
| 4..5 | number of frames, little-endian (1..65535) |
| 6 | frames per second (1..255); each frame shows for `1000 / fps` ms (integer division) |
| 7 | q: 1, 2, 4 or 8 |

A bit stream follows directly after the header.

## Bit stream

- The bits are read **most significant bit first**: bit 7 of byte 8 is
  the first bit. (LA03 was the other way round.)
- A value of n bits comes most significant bit first.
- The last byte is padded with zero bits. A reader that goes past the end
  reads zero bits.

Primitives:

- `gamma(n)`, for n ≥ 1 ([Elias gamma](https://en.wikipedia.org/wiki/Elias_gamma_coding)):
  k zero bits, a one bit, then the low k bits of n, with k = floor(log2 n).
- `nb(n)`: the bits for a value 0..n−1: ceil(log2 n), and 0 if n ≤ 1.
- **Bins** for a value x ≥ 0 (a number and its extra bits):
  - x < 8: the bin is x, with no extra bits.
  - Otherwise, with k = floor(log2 x): the bin is 8 + 2(k − 3) + bit k−1
    of x, followed by k − 1 extra bits (the low k − 1 bits of x).

  So each octave above 8 has two bins, and the extra bits give the rest.

### Huffman tables

A table codes n symbols. In the file it is a list of code lengths:

1. `e`, in nb(n) bits: the last symbol that is used.
2. Events, until symbol e has a length. p is the last length that was not
   0; it starts at 0.

| Event | Meaning |
|---|---|
| `00` | length p |
| `01` | length p + 1 |
| `110` | length p − 1 |
| `1110` | length p + 2 |
| `1111` | length p − 2 |
| `101` + 4 bits | this length |
| `100` + gamma(k) | the next k symbols are not used (length 0) |

The lengths are 1..12. The codes are canonical: shorter codes first, and
within one length by symbol. A table with only one used symbol takes 0 bits
per symbol.

## The file header (after the 8 bytes)

1. `gamma(nt + 1)`: nt listed **tuples**, at most 32. A tuple is the three
   prediction modes of a frame: grid, outline, RGB.
2. nt tuples of 12 bits each: the mode index of the grid, the outline and
   RGB, 4 bits each (see [Modes](#modes)).
3. **Frame table 0**, over nt + 1 + 34 symbols:
   - 0..nt−1: a listed tuple,
   - nt: escape (the 12 bits of a tuple follow in the frame),
   - nt+1..nt+34: a hold bin (see [Frames](#frames)).
4. Only if frame table 0 uses a hold bin: **frame table 1**, over nt + 1
   symbols. It is used after a hold (a hold never follows a hold).
5. `fmask`, 12 bits: bit `segment × 4 + family` is set if a frame uses
   that segment and mode family (see [Modes](#modes)).
6. The transition table flag, 1 bit. If it is set:
   - at q = 8 only: the key kind, 1 bit;
   - the table (see [The transition table](#the-transition-table)).
7. The activity flag, 1 bit. If it is set: three thresholds of q + 1 bits
   each.
8. `gamma(ntab + 1)`, then ntab **token tables** (ntab ≤ 16). The token
   alphabet has nres + 18 + 1 symbols, with nres = 1, 3, 10, 18 for
   q = 1, 2, 4, 8 (see [Tokens](#tokens)).
9. The **context map**: which token table each context uses. If ntab ≤ 1,
   nothing is coded and every entry is table 0. Otherwise, for each bit sf
   that is set in fmask, from 0 to 11, the 16 entries `map[sf × 16 + c]`:
   - `1`: all 16 entries are pm, the entry coded last. pm starts at 0 and
     carries over from one family to the next.
   - `0`, then per c = 0..15 one entry. Its candidates are `map[c − 8]`
     (if c ≥ 8), `map[c − 4]` (if c ≥ 4) and pm, in that order; keep the
     first two different values. `0`: the first candidate. `10`: the
     second. `11` + nb(ntab) bits: the table. pm becomes this entry.

The frames follow directly.

### The transition table

The table predicts a level from the two frames before it (prev and prev2):
per key, a delta to prev (−128..127). It is used by mode X.

- Key kind 0: `(prev >> ts) << tk | prev2 >> ts`, with tk = min(q, 4) and
  ts = q − tk. That is 2^(2·tk) entries.
- Key kind 1 (q = 8 only): `(prev >> 4) << 4 | dbin(prev − prev2)`:
  - dbin(0) = 0;
  - 1..7 for |d| in [1], [2, 3], [4, 7], [8, 15], [16, 31], [32, 63],
    [64, 255];
  - plus 7 if d < 0.
- The table is coded as: a run of default entries as `gamma(run + 1)`;
  then, if the table is not complete, one entry: `0` for delta 0, or `1` +
  lb bits for a two's complement delta, with lb = min(q + 1, 8). Again
  until the table is complete.
- The default entry continues the trend at the centre of the key:
  - h = 2^(ts−1) if ts > 0, else 0;
  - A = ((key >> lo) << ts) + h, with lo = tk (kind 0) or 4 (kind 1);
  - kind 0: B = ((key & (2^tk − 1)) << ts) + h;
  - kind 1: B = A − D(lo), with D(x) = rep[x] for x ≤ 7 and −rep[x − 7]
    for x > 7, rep = {0, 1, 2, 5, 11, 23, 47, 100, 0};
  - default = clamp(clamp(2A − B, 0, m) − A, −128, 127).

## Frames

At frame 0, and again each time the animation loops:

- prev and prev2 are all 0 (black),
- the frame table is table 0,
- no hold is active.

Each frame:

- If a hold is active: the frame is the same as the previous one. The hold
  counts down by one.
- Otherwise, read a symbol with the frame table:
  - **A hold bin**: the bin's value + 1 frames (this one included) are the
    same as the previous frame. The next symbol uses frame table 1.
  - **A tuple** (listed, or the 12 bits after an escape): the next symbol
    uses frame table 0. Decode the three segments in order: the grid
    (positions 0..207), the outline (208..278), RGB (279..383).

### Modes

The mode index in a tuple:

| Segment | Mode index 0, 1, 2, ... |
|---|---|
| grid | Z T V S G L R U D P X |
| outline | Z T V S G +1 −1 +2 −2 +3 −3 P X |
| RGB | Z T V S G A B P X |

What each mode predicts for position p (cur is this frame, prev and prev2
the two frames before it):

| Mode | Prediction |
|---|---|
| Z | prev, and no tokens follow: the segment did not change |
| T | prev |
| P | prev2 (blinks, things that go back and forth) |
| V | clamp(2 prev − prev2): a fade goes on at the same speed |
| X | clamp(prev + the transition table's delta for the key of prev, prev2) |
| L, R, U, D | prev at the grid position to the right, left, below or above: the picture moves left, right, up or down one LED. 0 if there is no LED there |
| +k (outline) | prev at outline index (i − k) mod 71: the outline turns k LEDs clockwise |
| S | cur at the spatial neighbour n |
| G | clamp(prev[p] + cur[n] − prev[n]); prev[p] if there is no n |
| A | as S, but n is the same colour and rank on the previous arm (p − 21), for the arms after the first |
| B | as G, with the neighbour of A |

The spatial neighbour n: on the grid the LED to the left, else the LED
above; on the outline the previous LED; for RGB p − 3 (the same colour of
the previous LED), or p − 1 for the green and blue of LED 0.

The mode families: 0 = T, P, L, R, U, D and the outline turns; 1 = V, X;
2 = S, A; 3 = G, B. Each family has its own contexts.

The temporal modes (all except S, G, A and B) first fill the whole
segment with their prediction; the tokens then only fix what is wrong.
The spatial modes S, G, A, B predict each position when it is decoded,
from the positions decoded before it.

Scan order: the positions in order. The exception is the grid under L, R,
U or D: first the positions that come into the picture (they have no
source), then the others, both in position order. For text that scrolls
left, the 16 row ends come first, so it costs about one short run per
frame.

### Tokens

Tokens follow until the segment is full. Each token is read with token
table `map[(segment × 4 + family) × 16 + c]`, where c is the context at
the token's position (the start of a run):

- c = base + 4·T + 8·U, or, with the activity flag, c = base +
  4 × (the number of thresholds below a).
- base: 3 at the start of the segment, 0 after a run, 1 after a residual
  with z ≤ 2, 2 after a larger residual.
- T: prev[p] ≠ prev2[p].
- U: cur[u] ≠ prev[u], with u the position above: the row above on the
  grid, p − 3 for RGB (p ≥ 282), none on the outline. Where nothing is
  decoded yet, cur holds the prediction.
- a = |prev[p] − prev2[p]| + |cur[u] − prev[u]|.

The token symbol s:

- s < nres: **a residual** for this position. z = the bin value of s (with
  its extra bits) + 1; r = −(z + 1) / 2 if z is odd, else z / 2; cur[p] =
  (prediction + r) mod 2^q.
- nres ≤ s < nres + 18: **a run**: the next (bin value + 1) positions are
  their prediction.
- s = nres + 18: the rest of the segment is its prediction.

## The encoder

`compressPattern()` in `webui/compression.js`:

1. Quantize. At q = 8, make two versions that use the duty classes (each
   LED keeps its previous value while that shows the same; or every value
   becomes the lowest one of its class), and keep the smaller result.
2. Find the holds: frames the same as the previous one.
3. Code every segment of every frame under every mode, as tokens.
4. Four passes. Each pass:
   - picks the mode per segment and frame by the estimated bits (with the
     tables of the pass before);
   - counts the symbols per context (segment × family × 16);
   - merges contexts into at most 16 tables, each merge the one that saves
     the most bits (code lengths included);
   - makes length-limited Huffman codes (package-merge, at most 12 bits).
5. After the first pass: choose the activity thresholds, if they beat T/U.
6. The transition table: for q ≤ 4 per key the most frequent next level,
   for q = 8 the median delta (both key kinds are tried). The file is
   encoded with and without the table; the smaller one stays.
7. The tuples used at least twice are listed (at most 32); the others are
   escapes.

## References

- [Huffman coding](https://en.wikipedia.org/wiki/Huffman_coding) and
  [canonical Huffman codes](https://en.wikipedia.org/wiki/Canonical_Huffman_code):
  the frame and token tables.
- [Package-merge](https://en.wikipedia.org/wiki/Package-merge_algorithm):
  Huffman codes with a maximum length.
- [Run-length encoding](https://en.wikipedia.org/wiki/Run-length_encoding):
  the runs of correct predictions.
- [Motion compensation](https://en.wikipedia.org/wiki/Motion_compensation):
  the grid shifts and outline turns.
- [Lossless JPEG](https://en.wikipedia.org/wiki/Lossless_JPEG) and
  [JPEG-LS](https://en.wikipedia.org/wiki/Lossless_JPEG#JPEG-LS): coding the
  error of a prediction from neighbours, with contexts.
- [Elias gamma coding](https://en.wikipedia.org/wiki/Elias_gamma_coding):
  counts in the header.
- [Gamma correction](https://en.wikipedia.org/wiki/Gamma_correction): the
  firmware applies gamma 2.2 to the decoded values (`docs/LEDPWM_MATRIX.md`);
  the encoder's duty classes come from it.
