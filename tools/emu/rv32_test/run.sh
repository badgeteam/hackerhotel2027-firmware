#!/bin/sh
# Checks the emulator's RV32 interpreter: work.c built as an app runs in the
# emulator, and built for the PC; the lines they print must be the same.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root="$here/../../.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
make -s -C "$here/.." >/dev/null
"${PREFIX:-riscv64-elf}-gcc" -march=rv32imac -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -msmall-data-limit=0 \
    -I"$root/apps" -T "$root/apps/app.ld" -Wl,--no-relax -Wl,--no-warn-rwx-segments -nostartfiles \
    -o "$out/test.elf" "$here/app.c" "$here/work.c" "$root/apps/sdk.c"
"${PREFIX:-riscv64-elf}-objcopy" -O binary -j .image "$out/test.elf" "$out/test.hhapp"
cc -O2 -o "$out/host" "$here/host.c" "$here/work.c"
"$out/host" > "$out/want"
python3 - "$here/../build/hh27-emu" "$out/flash.bin" "$out/test.hhapp" > "$out/got" <<'PY'
import subprocess, sys, time
emu, flash, app = sys.argv[1:]
p = subprocess.Popen([emu, flash], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def usb(*b):
    p.stdin.write("u " + bytes(b).hex() + "\n"); p.stdin.flush()
    for line in p.stdout:
        if line.startswith("U "): return
data = open(app, "rb").read()
usb(0x21, 2, *b"RV32TEST")
for o in range(0, len(data), 60): usb(0x22, *data[o:o + 60])
p.stdin.write("u 23\n"); p.stdin.flush()
lines, end = [], time.time() + 10
for line in p.stdout:
    if line.startswith("L RV32TEST"): lines.append(line[2:].strip())
    elif line.startswith("L [emu]"): print(line.strip(), file=sys.stderr)
    if len(lines) == 6 or time.time() > end: break
p.kill()
print("\n".join(lines))
PY
if diff "$out/want" "$out/got"; then echo "ok: the interpreter matches the PC ($(wc -l < "$out/want") results)"; else echo "FAIL: the interpreter differs from the PC"; exit 1; fi
