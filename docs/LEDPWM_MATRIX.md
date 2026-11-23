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
and its row and column. The file was made from the PCB with
`tools/extract_board_data.py`, which is not in this repository.

`tools/gen_layout.py` makes the layout tables from `board_data.js`:

- the centre grid (17 × 16, 2 mm pitch),
- the outline in clockwise order,
- the RGB LEDs by arm and rank,
- LA03's spatial pixel order,
- the gamma table.

The script writes `layout.c` for the firmware and `webui/layout_data.js`
for the web interface.

## Brightness

A cell holds a value from 0 to 255. Each time the driver loads a row, it
converts each value for the PWM in two steps:

1. **Gamma 2.2.** The values are brightness as a screen shows it, and the
   LEDs are linear.
2. **Global brightness.** Buttons B and D set it, and the badge keeps it
   when it is powered off (`persist.c`).
