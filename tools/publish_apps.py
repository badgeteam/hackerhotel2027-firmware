#!/usr/bin/env python3
"""Copies the built apps into the web interface's app library.

  make -C apps webui        (builds the apps, then runs this)

For every apps/build/<name>.hhapp it writes webui/apps/<name>.hhapp, and
webui/apps/index.json lists them with what the web interface shows: the
name it stores the app under, the size, the app API version the app
needs (from its header), and a description (the first sentences of the
comment at the top of the app's first .c file).

apps/recommended.txt names the recommended set: the apps a badge ships
with, in the order they are installed. index.json lists them under
"recommended", and this refuses a set that does not fit in the store with
FREE_AFTER bytes left for people's own texts and animations.
"""
import json
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "apps" / "build"
OUT = ROOT / "webui" / "apps"
RECOMMENDED = ROOT / "apps" / "recommended.txt"
STORE_BYTES = 31744  # the store's room for items (store.h STORE_ITEMS_SIZE)
ITEM_HEADER = 32     # store_item_t
PAGE = 256           # each item takes whole flash pages
FREE_AFTER = 4096    # the recommended set leaves at least this much free
# Not for the library: the factory test (make factory-image and
# tools/badge.py factory store it; it deletes itself once it passed)
NOT_IN_LIBRARY = {"factory"}


def store_bytes(length: int) -> int:
    return -(-(ITEM_HEADER + length) // PAGE) * PAGE


def recommended() -> list:
    return [l.strip() for l in RECOMMENDED.read_text().splitlines() if l.strip() and not l.startswith("#")]


def description(app_dir: Path) -> str:
    sources = sorted(app_dir.glob("*.c"))
    lines = []
    for line in sources[0].read_text().splitlines() if sources else []:
        if not line.startswith("//"):
            break
        lines.append(line[2:].strip())
    text = " ".join(lines)
    return text


def main() -> int:
    OUT.mkdir(exist_ok=True)
    for old in OUT.glob("*.hhapp"):
        old.unlink()
    apps = []
    for hhapp in sorted(BUILD.glob("*.hhapp")):
        if hhapp.stem in NOT_IN_LIBRARY:
            continue
        data = hhapp.read_bytes()
        magic, api = struct.unpack_from("<IH", data, 0)
        if magic != 0x50414848:  # "HHAP"
            print(f"{hhapp}: not an app, skipped", file=sys.stderr)
            continue
        name = hhapp.stem
        shutil.copyfile(hhapp, OUT / hhapp.name)
        apps.append({
            "name": name.upper(),
            "file": hhapp.name,
            "bytes": len(data),
            "api": api,
            "description": description(ROOT / "apps" / name),
        })
        print(f"webui/apps/{hhapp.name}: {len(data)} bytes, API {api}")
    names = recommended()
    by_name = {a["name"]: a for a in apps}
    missing = [n for n in names if n.upper() not in by_name]
    if missing:
        print(f"apps/recommended.txt: no such app: {', '.join(missing)}", file=sys.stderr)
        return 1
    used = sum(store_bytes(by_name[n.upper()]["bytes"]) for n in names)
    print(f"recommended set: {len(names)} apps, {used} of {STORE_BYTES} bytes in the store, {STORE_BYTES - used} free")
    if STORE_BYTES - used < FREE_AFTER:
        print(f"apps/recommended.txt: the set leaves less than {FREE_AFTER} bytes free", file=sys.stderr)
        return 1
    for a in apps:
        a["recommended"] = a["name"] in [n.upper() for n in names]
    index = {"apps": apps, "recommended": [n.upper() for n in names]}
    (OUT / "index.json").write_text(json.dumps(index, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
