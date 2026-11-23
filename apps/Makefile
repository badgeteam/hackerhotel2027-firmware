# Builds every app in apps/<name>/ (all its .c files) into build/<name>.hhapp,
# the file the web interface uploads. See apps/README.md.
PREFIX ?= riscv64-elf
CC := $(PREFIX)-gcc
OBJCOPY := $(PREFIX)-objcopy

# rv32imac like the firmware. -msmall-data-limit=0 and --no-relax: the app
# must not use gp, which belongs to the firmware.
CFLAGS := -march=rv32imac -mabi=ilp32 -Os -g -ffreestanding -fno-builtin -nostdlib \
          -ffunction-sections -fdata-sections -msmall-data-limit=0 -Wall -Wextra -I.
LDFLAGS := -T app.ld -Wl,--gc-sections -Wl,--no-relax -Wl,--no-warn-rwx-segments -nostartfiles

APPS := $(patsubst %/,%,$(dir $(wildcard */*.c)))
HHAPPS := $(addprefix build/,$(addsuffix .hhapp,$(sort $(APPS))))

all: $(HHAPPS)

build/%.elf: %/*.c sdk.c hhapp.h app.ld
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDFLAGS)

build/%.hhapp: build/%.elf
	$(OBJCOPY) -O binary -j .image $< $@
	@echo "$@: $$(stat -c %s $@) bytes"

clean:
	rm -rf build

.PRECIOUS: build/%.elf
.PHONY: all clean
