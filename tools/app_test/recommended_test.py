#!/usr/bin/env python3
"""The recommended set (apps/recommended.txt) as webui/apps/index.json has
it: every app there, in order, at the API the firmware has, and it fits in
the store with room to spare (tools/publish_apps.py)."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import publish_apps as p  # noqa: E402

index = json.loads((ROOT / "webui" / "apps" / "index.json").read_text())
api = int(re.search(r"#define HH_API_VERSION (\d+)", (ROOT / "apps" / "hhapp.h").read_text()).group(1))
names = [n.upper() for n in p.recommended()]
fails = []
if index.get("recommended") != names:
    fails.append("index.json's recommended set is not apps/recommended.txt: run make -C apps webui")
by = {a["name"]: a for a in index["apps"]}
for n in names:
    a = by.get(n)
    if not a:
        fails.append(f"{n}: not in the library")
    elif not (ROOT / "webui" / "apps" / a["file"]).is_file():
        fails.append(f"{n}: {a['file']} missing")
    elif a["api"] > api:
        fails.append(f"{n}: needs API {a['api']}, the firmware has {api}")
    elif not a.get("recommended"):
        fails.append(f"{n}: not marked recommended")
used = sum(p.store_bytes(by[n]["bytes"]) for n in names if n in by)
if p.STORE_BYTES - used < p.FREE_AFTER:
    fails.append(f"the set takes {used} of {p.STORE_BYTES} bytes, less than {p.FREE_AFTER} left")
if len(set(names)) != len(names):
    fails.append("an app is in the set twice")
for f in fails:
    print("FAIL  " + f)
print("%s  recommended set: %d apps, %d bytes, %d free" % ("FAIL" if fails else "ok  ", len(names), used, p.STORE_BYTES - used))
sys.exit(1 if fails else 0)
