# Builds every app in apps/<name>/ (all its .c files) into build/<name>.hhapp,
# the file the web interface uploads. See apps/README.md. APPS_OUT=DIR builds
# into DIR instead (an absolute path), as the tests do.
# `make webui` also copies them into the web interface's app library.
PREFIX ?= riscv64-elf
CC := $(PREFIX)-gcc
OBJCOPY := $(PREFIX)-objcopy

# rv32imac like the firmware. -msmall-data-limit=0 and --no-relax: the app
# must not use gp, which belongs to the firmware.
CFLAGS := -march=rv32imac -mabi=ilp32 -Os -g -ffreestanding -fno-builtin -nostdlib \
          -ffunction-sections -fdata-sections -msmall-data-limit=0 -Wall -Wextra -I.
# The factory test's KEY step (apps/factory); FACTORY_KEYS=0 leaves it out
FACTORY_KEYS ?= 1
CFLAGS += -DFACTORY_KEYS=$(FACTORY_KEYS)
LDFLAGS := -T app.ld -Wl,--gc-sections -Wl,--no-relax -Wl,--no-warn-rwx-segments -nostartfiles

# The name tag app is the biggest of the recommended set (apps/recommended.txt),
# which has no room to spare in the store: these flags make it about 190
# bytes smaller (no jump tables; short code over fast code; loop invariants
# not moved out of the loops, which spills fewer registers)
build/nametag.elf: CFLAGS += -fno-jump-tables -mtune=size -malign-data=natural -fno-move-loop-invariants -fno-tree-loop-im

APPS := $(patsubst %/,%,$(dir $(wildcard */*.c)))
APPS_OUT ?= build
HHAPPS := $(addprefix $(APPS_OUT)/,$(addsuffix .hhapp,$(sort $(APPS))))

all: $(HHAPPS)

# Every app depends on its own headers and on the shared ones (fx.h, hhapp.h)
.SECONDEXPANSION:
$(APPS_OUT)/%.elf: %/*.c $$(wildcard $$*/*.h) sdk.c app.ld $(wildcard *.h)
	@mkdir -p $(APPS_OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDFLAGS)

$(APPS_OUT)/%.hhapp: $(APPS_OUT)/%.elf
	$(OBJCOPY) -O binary -j .image $< $@
	@echo "$@: $$(stat -c %s $@) bytes"

# The web interface's app library (webui/apps/): the built apps and an index
webui: all
	python3 ../tools/publish_apps.py

clean:
	rm -rf $(APPS_OUT)

.PRECIOUS: $(APPS_OUT)/%.elf
.PHONY: all clean webui
