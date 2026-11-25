# LED matrix

The badge drives all its LEDs from one matrix with the CH643's LEDPWM
peripheral (`ch643_ledpwm.c`).

The matrix has two kinds of lines:

| Lines | Pins | Connects to | How the firmware drives them |
|---|---|---|---|
| **COM lines** (the rows) | PB0..PB15 | LED cathodes | one row at a time |
| **PWM channels** (the columns) | PA0..PA23 | LED anodes, through series resistors | all 24 at the same time, each with an 8-bit duty |

The driver goes through the rows from an interrupt, one row after the
other, again and again.

## Cells

The matrix has 16 × 24 = 384 cells. A cell is the index
`row * 24 + col`:

- the firmware calls it a **slot** (`apps/hhapp.h`, `layout.c`),
- `FRAME_CHUNK` in `webusb_protocol.h` and the `frame` arrays in the web
  interface use the same index.

Each cell holds one LED channel:

- A **mono LED** (208 in the centre grid, 71 on the outline) uses one
  cell.
- An **RGB LED** (35, on the arms) uses three cells. The red, green and
  blue anodes are on the same PWM channel, and their cathodes are on
  three different rows. In `webui/board_data.js` these rows are `row`
  (red), `green_row` and `blue_row`.

208 + 71 + 3 × 35 = 384, so every cell has an LED.

The LEDPWM peripheral gets its data per row as 8 words of 32 bits. In
word g, the bytes are luminance, then the duty of PA[g], PA[g+8] and
PA[g+16]. The peripheral calls these three bytes red, green and blue,
after its PA groups. That has nothing to do with the colour of the LED on
a pin.

## Where the LEDs are

`webui/board_data.js` gives each LED its position on the board (in mm)
and its row and column. `tools/extract_board_data.py` makes it from
`hackerhotel2027.kicad_pcb` in the public repository
badgeteam/hackerhotel2027-hardware, at the commit its `HW_REV` names.
It needs Python 3 only, no KiCad. `tools/board_test/run.sh` checks that
the committed file matches the board. "Layout tables" in
`CONTRIBUTING.md` says how to follow a hardware change.

`tools/gen_layout.py` makes the layout tables from `board_data.js`:

- the centre grid (17 × 16, 2 mm pitch),
- the outline in clockwise order,
- the RGB LEDs by arm and rank,
- LA04's spatial pixel order, and the LA04 decoder's neighbour tables
  (`la04_tables.h`),
- the gamma table.

The script writes `layout.c` for the firmware and `webui/layout_data.js`
for the web interface.

## Brightness

A cell holds a value from 0 to 255. The LEDPWM peripheral has two PWMs
in each word: a luminance PWM (the word's first byte, 6 bits here:
`LEDPWM_INTEN_CYC(2)`) that dims the whole word, and the 8-bit duty of
each pin. The driver uses them like this:

1. **Gamma 2.2, into the duty.** The values are brightness as a screen
   shows it, and the LEDs are linear. Any value above 0 gets a duty of at
   least 1, so the faintest values glow.
2. **Global brightness, into the luminance.** Buttons B and D set it, and
   the badge keeps it when it is powered off (`persist.c`). Brightness b
   gives b / 4 of the 64 luminance steps (255: full).

Because the global brightness does not scale the 8-bit duty, dim values
keep their place at low brightness. When the driver scaled the duty, a
value below 95 went dark at brightness 8 (#16).
