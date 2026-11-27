#!/usr/bin/env python3
"""The image for the flashing table: all 62 KB of the CH643's flash.

  make factory-image        (builds both parts, then runs this)
  python3 tools/factory_image.py ledmatrix.bin apps/build/factory.hhapp factory.bin

The firmware at 0, then the item store at 30 KB (flash_pages.h) with one
item, the factory test app (apps/factory) under the name FACTORY, and an
erased persistent record (persist.c) at the top. All the rest is 0xFF, as
erased flash reads. A badge flashed with it runs the factory test at the
first power-on (docs/FACTORY_TEST.md); the firmware deletes the app once
the test has passed.

The emulator (tools/emu) uses fresh() for its --fresh start.
"""
import os
import struct
import sys

FLASH_SIZE = 62 * 1024
STORE_BASE = 0x7800          # flash_pages.h STORE_FLASH_BASE - 0x08000000
STORE_SIZE = 0x8000
RECORD_SIZE = 0x400          # PERSIST_FLASH_SIZE, the top of the store
ITEMS_SIZE = STORE_SIZE - RECORD_SIZE
PAGE = 256
ITEM_MAGIC = 0x31494848      # store.h STORE_ITEM_MAGIC, "HHI1"
TYPE_APP = 2
NAME_LEN = 20
APP_MAGIC = 0x50414848       # apps/hhapp.h HH_APP_MAGIC, "HHAP"
FW_MARKER = b"HH27-STARFISH-FW S1 "  # webusb_protocol.c s_fw_info: this store layout
FACTORY = "FACTORY"          # the name the firmware looks for (ledmatrix.c)
# the app, as make -C apps builds it (the tests build their own: HH_FACTORY_APP)
APP = os.environ.get("HH_FACTORY_APP") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps", "build", "factory.hhapp")
EMU_STORE = 0x800            # where the store starts in the emulator's flash file (tools/emu/hw.c)


def item(kind: int, name: str, payload: bytes) -> bytes:
    """A store item as store.c writes it: the 32-byte header, the payload,
    0xFF up to the next page"""
    data = struct.pack("<IB3xI", ITEM_MAGIC, kind, len(payload)) + name.encode()[:NAME_LEN].ljust(NAME_LEN, b"\0")
    data += payload
    return data + b"\xff" * (-len(data) % PAGE)


def items_end(store: bytes) -> int:
    """The offset after the last item of the store (bytes from the store's
    start), as store.c walks it"""
    off = 0
    while off + 32 <= ITEMS_SIZE:
        magic, length = struct.unpack_from("<I4xI", store, off)
        if magic != ITEM_MAGIC or length > ITEMS_SIZE - off - 32:
            break
        off += -(-(32 + length) // PAGE) * PAGE
    return off


def names(store: bytes) -> list:
    """The (type, name) of each item in the store"""
    out, off = [], 0
    while off < items_end(store):
        kind = store[off + 4]
        length = struct.unpack_from("<I", store, off + 8)[0]
        out.append((kind, store[off + 12:off + 32].rstrip(b"\0").decode(errors="replace")))
        off += -(-(32 + length) // PAGE) * PAGE
    return out


def add_item(store: bytearray, kind: int, name: str, payload: bytes) -> bool:
    """Puts an item after the last one, as an upload does; False if it does
    not fit"""
    off = items_end(store)
    data = item(kind, name, payload)
    if off + len(data) > ITEMS_SIZE:
        return False
    store[off:off + len(data)] = data
    return True


def erase_record(store: bytearray):
    """The persistent record erased, as on a newly flashed badge: no name,
    no settings, the factory test not passed"""
    store[ITEMS_SIZE:STORE_SIZE] = b"\xff" * RECORD_SIZE


def fresh(path: str, app: bytes) -> bool:
    """The emulator's flash file at `path` as on a badge newly flashed with
    the factory image, but with its items kept: the persistent record
    erased, and the factory test app stored after the items, unless it is
    there already. False if there is no room for the app."""
    data = bytearray(open(path, "rb").read()) if os.path.exists(path) else bytearray()
    base = STORE_BASE if len(data) == FLASH_SIZE else EMU_STORE
    data += b"\xff" * (base + STORE_SIZE - len(data))
    store = data[base:base + STORE_SIZE]
    erase_record(store)
    ok = (TYPE_APP, FACTORY) in names(store) or add_item(store, TYPE_APP, FACTORY, app)
    data[base:base + STORE_SIZE] = store
    open(path, "wb").write(data)
    return ok


def check_app(app: bytes):
    if len(app) < 24 or struct.unpack_from("<I", app)[0] != APP_MAGIC:
        sys.exit("not an app (.hhapp)")


def factory_image(firmware: bytes, app: bytes) -> bytes:
    if len(firmware) > STORE_BASE:
        sys.exit("the firmware is %d bytes, more than the %d below the store" % (len(firmware), STORE_BASE))
    if FW_MARKER not in firmware:
        sys.exit("not the badge's firmware for this store layout (no %r)" % FW_MARKER.decode())
    check_app(app)
    store = bytearray(b"\xff" * STORE_SIZE)
    if not add_item(store, TYPE_APP, FACTORY, app):
        sys.exit("the app does not fit in the store")
    image = firmware + b"\xff" * (STORE_BASE - len(firmware)) + bytes(store)
    assert len(image) == FLASH_SIZE
    return image


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    firmware, app = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
    open(sys.argv[3], "wb").write(factory_image(firmware, app))
    print("%s: %d bytes: the firmware (%d bytes), and the store with %s (%d bytes, %d pages)" %
          (sys.argv[3], FLASH_SIZE, len(firmware), FACTORY, len(app), len(item(TYPE_APP, FACTORY, app)) // PAGE))


if __name__ == "__main__":
    main()
