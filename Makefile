all : flash

TARGET:=ledmatrix
TARGET_MCU:=CH643Q
ADDITIONAL_C_FILES:=debug_output.c ch643_ledpwm.c buttons.c standby.c bootloader.c infrared.c webusb_protocol.c flash_pages.c store.c persist.c pattern_player.c app_host.c layout.c font.c nametag.c menu.c
PREFIX:=riscv64-elf
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

include ch32fun/ch32fun/ch32fun.mk

$(LINKER_SCRIPT) : $(CH32FUN)/ch32fun.ld Makefile
	$(PREFIX)-gcc -E -P -x c -DTARGET_MCU=$(TARGET_MCU) -DMCU_PACKAGE=$(MCU_PACKAGE) -DTARGET_MCU_LD=$(TARGET_MCU_LD) -DTARGET_MCU_MEMORY_SPLIT=$(TARGET_MCU_MEMORY_SPLIT) $< > $@.tmp
	grep -qF '$(FLASH_MEMORY_LINE)' $@.tmp || { echo "$<: FLASH memory line changed, update the Makefile" >&2; rm -f $@.tmp; exit 1; }
	grep -qF '$(RAM_MEMORY_LINE)' $@.tmp || { echo "$<: RAM memory line changed, update the Makefile" >&2; rm -f $@.tmp; exit 1; }
	sed -e 's/$(FLASH_MEMORY_LINE)/$(STORE_MEMORY_LINES)/' -e 's/$(RAM_MEMORY_LINE)/$(FIRMWARE_RAM_LINE)/' $@.tmp > $@
	rm -f $@.tmp

flash : cv_flash
clean : cv_clean
	rm -f $(LINKER_SCRIPT)
