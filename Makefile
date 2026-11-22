all : flash

TARGET:=ledmatrix
TARGET_MCU:=CH643Q
ADDITIONAL_C_FILES:=debug_output.c ch643_ledpwm.c buttons.c standby.c bootloader.c infrared.c webusb_protocol.c pattern_flash.c pattern_player.c
PREFIX:=riscv64-elf
MINICHLINK:=$(dir $(shell which minichlink))

# Project linker script: ch32fun's own script with the top 32K of flash
# reserved for the stored pattern (must match PATTERN_FLASH_BASE/SIZE in
# pattern_flash.h), so the build fails if the firmware grows into it.
LINKER_SCRIPT:=$(TARGET).ld
FLASH_MEMORY_LINE:=FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 62K
PATTERN_MEMORY_LINES:=FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 30K\n  PATTERN (r) : ORIGIN = 0x00007800, LENGTH = 32K

include ch32fun/ch32fun/ch32fun.mk

$(LINKER_SCRIPT) : $(CH32FUN)/ch32fun.ld Makefile
	$(PREFIX)-gcc -E -P -x c -DTARGET_MCU=$(TARGET_MCU) -DMCU_PACKAGE=$(MCU_PACKAGE) -DTARGET_MCU_LD=$(TARGET_MCU_LD) -DTARGET_MCU_MEMORY_SPLIT=$(TARGET_MCU_MEMORY_SPLIT) $< > $@.tmp
	grep -qF '$(FLASH_MEMORY_LINE)' $@.tmp || { echo "$<: FLASH memory line changed, update the Makefile" >&2; rm -f $@.tmp; exit 1; }
	sed 's/$(FLASH_MEMORY_LINE)/$(PATTERN_MEMORY_LINES)/' $@.tmp > $@
	rm -f $@.tmp

flash : cv_flash
clean : cv_clean
	rm -f $(LINKER_SCRIPT)
