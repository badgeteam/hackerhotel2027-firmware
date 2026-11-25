# Firmware update from the web interface

The web interface's **Firmware** panel flashes new firmware over USB, in
Chrome, Edge or Opera (WebUSB). It uses the CH643's USB boot ROM. The
protocol is the one `minichlink -w ledmatrix.bin flash -b` uses.

| File | What it does |
|---|---|
| `webui/firmware_update.js` | the panel: versions, image checks, the flow |
| `webui/wchisp.js` | the boot ROM client (WCH USB ISP over WebUSB) |
| `webusb_protocol.c` | the firmware's `BOOT_ROM` and `FW_INFO` commands |
| `tools/isp_test/isp_test.js` | offline tests with a mock boot ROM and a mock badge |

The stored items, the name and the settings stay. They are in the top
32 KB of flash, from 0x7800. The boot ROM erases from address 0 only, and
`wchisp.js` refuses to erase or program at 0x7800 or above.

## What you do

1. Click **Connect badge** and pick **Hackerhotel 2027 starfish**. If the
   badge has older firmware, **Your badge** says so and shows an
   **Update firmware** button. It takes you to the **Firmware** panel.
2. In the **Firmware** panel, click **Update firmware to VERSION**. To
   flash a `.bin` of your own, open **Advanced: flash a .bin file** and
   click **Choose .bin file…** first. The button then reads
   **Flash NAME.bin**.
3. Read the question that appears, and click **Flash now**. **Cancel**
   closes it and changes nothing.
4. In the browser's box, pick "Unknown device from QinHeng Electronics"
   and click **Connect**.
5. Keep the badge plugged in until the status line starts with **Done:**.

When the badge already runs this site's firmware, the button is off and
the status line says why. The same goes for a browser without WebUSB and
for the emulator.

For keyboard and screen reader users:

- **Update firmware** moves focus to the question, **Flash now** to the
  status line, and **Cancel** back to **Update firmware**.
- The status line and the badge's version line are live regions. During
  the update the status line changes once per phase and per 25%, so a
  screen reader does not read every packet. The progress bar shows every
  step.

## The flow

1. The panel asks the badge for its version (`FW_INFO`, 0x41). The reply
   is `HH27-STARFISH-FW S1 <commit>`. `S1` is the layout of the item
   store. Firmware without `FW_INFO` is older firmware.
2. The panel checks the image: at most 30 KB, at least 1 KB, it contains
   the `HH27-STARFISH-FW` marker, and its store layout is the badge's.
3. The user clicks twice: **Update firmware** to see what happens,
   **Flash now** to start. Chrome opens its device chooser only from a
   click.
4. The panel sends `BOOT_ROM` (0x40). The badge replies, writes a pending
   change to the persistent record, and restarts into the boot ROM
   (`1a86:55e0`).
5. The user picks "Unknown device from QinHeng Electronics" in the
   chooser. The boot ROM has no serial number, so Chrome asks every time.
6. If no host talks to the boot ROM, it starts the firmware again after
   about 4.6 s. While the chooser is open, the panel sends the badge back
   into the boot ROM each time the firmware comes back. The user can take
   as long as necessary.
7. `wchisp.js` writes the image (see below). The boot ROM then starts the
   new firmware.
8. The panel connects to the badge again and asks for its version. Right
   after the restart, the first question can get no answer, so the panel
   asks up to three times.

Without a connected badge, the user starts the boot ROM on the badge: hold
**A** for the menu, press **C** to reach **UPDATE**, and tap **A**.

## Write order

1. `IDENTIFY`, `READ_CONFIG`, `ISP_KEY`.
2. An empty `PROGRAM`. It writes a half-filled page buffer from an earlier
   try in the same boot ROM session. The erase that follows clears it.
3. `ERASE` of the sectors the image needs, at least 8.
4. Pages 1 and up, then `VERIFY` of those pages.
5. Page 0, then `VERIFY` of page 0.
6. `ISP_END` (reason 1): the boot ROM starts the firmware.

Page 0 goes last. From the erase until page 0 is written, the first flash
word is 0xFFFFFFFF. The boot ROM then starts by itself at power-on. If an
update stops before page 0, the user unplugs the badge, plugs it in again,
and updates again.

## Protocol

Each command is one bulk OUT packet on endpoint 2 and one bulk IN reply on
endpoint 2 (64-byte packets).

```
command: [cmd] [len lo] [len hi] [payload: len bytes]
reply:   [cmd] [0x00] [len lo] [len hi] [status or data: len bytes]
```

| Cmd | Name | Payload | Reply |
|---|---|---|---|
| 0xa1 | `IDENTIFY` | 0x52, 0x11, `MCU ISP & WCH.CN` | chip id, device type 0x24 (CH643) |
| 0xa7 | `READ_CONFIG` | 0x1f, 0x00 | 26 bytes, see below |
| 0xa3 | `ISP_KEY` | 30 seed bytes (all 0) | the sum of the 8 key bytes |
| 0xa4 | `ERASE` | sectors (1 KB), 0, 0, 0 | status |
| 0xa5 | `PROGRAM` | address (4, LE), 1 pad byte, data XOR key | status, always 0 |
| 0xa6 | `VERIFY` | as `PROGRAM` | 0: same; 0xf5: differs; 0xfe: refused |
| 0xa2 | `ISP_END` | 1 | status; then the ROM leaves the bus |

`wchisp.js` does not send `WRITE_CONFIG` (0xa8). To clear read protection
erases the whole flash, the items too.

### READ_CONFIG reply

| Bytes | Content |
|---|---|
| 6, 7 | RDPR and its complement. RDPR 0xa5: not read-protected |
| 8 .. 13 | USER, DATA0, DATA1 and their complements |
| 14 .. 17 | WRPR |
| 19 .. 21 | bootloader version, for example 2, 6, 0 = "2.60" |
| 22 .. 29 | the chip's UID |

### The key

The key is 8 bytes. With the all-zero seed, each of the first 7 bytes is
the sum of the 8 UID bytes (mod 256). The last byte is that sum plus the
chip id. Data byte `i` of each `PROGRAM` or `VERIFY` packet is XORed with
key byte `i mod 8`. The count starts again in each packet.

### Rules of the boot ROM

These rules come from a disassembly of the badge's boot ROM.

- `PROGRAM` collects data in a 256-byte page buffer. When the buffer is
  full, the ROM programs the page of the packet that filled it. A packet
  of fewer than 56 bytes, or an empty one, programs the buffer, padded
  with 0xFF. So `wchisp.js` sends each page as 56 + 56 + 56 + 56 + 32
  bytes, and each page complete before the next.
- `VERIFY` addresses and lengths are multiples of 8. After a mismatch,
  every `VERIFY` answers 0xfe until the next `ERASE`.
- `ERASE` always starts at address 0. The ROM erases 8 to 62 sectors.
- A reply that comes too late stays in the IN endpoint. It would answer
  the next command. After a timeout, `wchisp.js` closes the device, reads
  and drops waiting replies, and opens it again.

## Errors

`wchisp.js` throws a `WchIspError`. Its `code` is one of:

| Code | Meaning |
|---|---|
| `bad-argument`, `bad-sequence` | a programming error in the caller |
| `bad-image`, `image-too-large` | the image is empty, starts with 0xFFFFFFFF, or reaches the item store |
| `wrong-device`, `open-failed` | the device is not the boot ROM, or cannot be opened |
| `identify-rejected`, `wrong-chip` | the boot ROM is not a CH643's |
| `read-protected` | the chip is read-protected; the updater does not continue |
| `key-mismatch` | the boot ROM computed another key |
| `erase-failed`, `program-failed`, `end-failed` | the boot ROM refused the command |
| `verify-mismatch`, `verify-refused` | the flash is not the image |
| `timeout`, `bad-reply`, `disconnected`, `usb-error`, `send-failed`, `receive-failed` | USB problems |
| `session-dead` | a command after a USB problem; open the device again |
| `aborted` | the caller cancelled |

`flashFirmware()` also sets `phase` (where it stopped) and
`bootPageWritten`. If `bootPageWritten` is false, page 0 is not touched:
the badge starts the boot ROM or its old firmware.

## Tests

```sh
node tools/isp_test/isp_test.js
```

- Compares the packets with minichlink's (`ch32fun/minichlink/pgm-wch-isp.c`).
- Updates a mock boot ROM, and unplugs it at each command of an update.
  Each time, the item store must stay and a new update must work.
- Sends late replies, a half-filled page buffer, and a stuck flash bit.
- Runs the Firmware panel through a whole update, with a mock badge and a
  mock `navigator.usb`. It checks the texts, where the focus goes, and how
  often the status line changes.
