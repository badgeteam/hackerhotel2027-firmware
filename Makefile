all : flash

TARGET:=ledmatrix
TARGET_MCU:=CH643Q
ADDITIONAL_C_FILES:=debug_output.c ch643_ledpwm.c buttons.c standby.c bootloader.c infrared.c ir_codec.c power.c factory.c webusb_protocol.c flash_pages.c store.c persist.c pattern_player.c la04.c app_host.c app_util.c layout.c font.c nametag.c menu.c charge.c
PREFIX?=riscv64-elf
MINICHLINK:=$(dir $(shell which minichlink))

# Project linker script: ch32fun's own script with the top 32K of flash
# reserved for the item store (must match STORE_FLASH_BASE/SIZE in
# flash_pages.h), so the build fails if the firmware grows into it; and
# with the top 12K of RAM left to apps (HH_APP_RAM_BASE/SIZE in
# apps/hhapp.h), so the firmware's data and stack stay below it.
LINKER_SCRIPT:=$(TARGET).ld
FLASH_MEMORY_LINE:=FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 62K
STORE_MEMORY_LINES:=FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 30K\n  STORE (r) : ORIGIN = 0x00007800, LENGTH = 32K
RAM_MEMORY_LINE:=RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
FIRMWARE_RAM_LINE:=RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 8K

# The firmware's identity (FW_INFO, webusb_protocol.c): the git commit, and
# -dirty if the firmware's own sources differ from it
FW_VERSION:=$(shell git rev-parse --short=7 HEAD 2>/dev/null || echo unknown)$(shell git diff --quiet HEAD -- ':(glob)*.c' ':(glob)*.h' ':(glob)apps/hhapp.h' Makefile 2>/dev/null || echo -dirty)
EXTRA_CFLAGS+=-DFW_VERSION=\"$(FW_VERSION)\"
# The factory test's KEY step (factory.h); FACTORY_KEYS=0: the test runs by itself
FACTORY_KEYS?=1
EXTRA_CFLAGS+=-DFACTORY_KEYS=$(FACTORY_KEYS)

include ch32fun/ch32fun/ch32fun.mk

$(LINKER_SCRIPT) : $(CH32FUN)/ch32fun.ld Makefile
	$(PREFIX)-gcc -E -P -x c -DTARGET_MCU=$(TARGET_MCU) -DMCU_PACKAGE=$(MCU_PACKAGE) -DTARGET_MCU_LD=$(TARGET_MCU_LD) -DTARGET_MCU_MEMORY_SPLIT=$(TARGET_MCU_MEMORY_SPLIT) $< > $@.tmp
	grep -qF '$(FLASH_MEMORY_LINE)' $@.tmp || { echo "$<: FLASH memory line changed, update the Makefile" >&2; rm -f $@.tmp; exit 1; }
	grep -qF '$(RAM_MEMORY_LINE)' $@.tmp || { echo "$<: RAM memory line changed, update the Makefile" >&2; rm -f $@.tmp; exit 1; }
	sed -e 's/$(FLASH_MEMORY_LINE)/$(STORE_MEMORY_LINES)/' -e 's/$(RAM_MEMORY_LINE)/$(FIRMWARE_RAM_LINE)/' $@.tmp > $@
	rm -f $@.tmp

# The web interface's firmware (webui/firmware/, not in git): what its
# Firmware panel flashes when it is served from this checkout
webui-firmware : $(TARGET).bin
	mkdir -p webui/firmware
	cp $(TARGET).bin webui/firmware/ledmatrix.bin
	# the version as built into the image (its FW_INFO marker), not as of now
	printf '{"commit": "%s", "date": "%s", "bytes": %s}\n' "$$(grep -ao 'HH27-STARFISH-FW S[0-9]* [^[:cntrl:]]*' $(TARGET).bin | cut -d' ' -f3)" "$$(git log -1 --format=%cs HEAD)" "$$(stat -c %s $(TARGET).bin)" > webui/firmware/firmware.json

flash : cv_flash
clean : cv_clean
	rm -f $(LINKER_SCRIPT)
