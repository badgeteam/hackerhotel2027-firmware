#!/usr/bin/env python3
"""Copies the built apps into the web interface's app library.

  make -C apps webui        (builds the apps, then runs this)

For every apps/build/<name>.hhapp it writes webui/apps/<name>.hhapp, and
webui/apps/index.json lists them with what the web interface shows: the
name it stores the app under, the size, the app API version the app
needs (from its header), and a description (the first sentences of the
comment at the top of the app's first .c file).
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
    (OUT / "index.json").write_text(json.dumps({"apps": apps}, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
