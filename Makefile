all : flash

TARGET:=led_matrix_test
TARGET_MCU:=CH643Q
ADDITIONAL_C_FILES:=led_matrix.c webusb_protocol.c pattern_flash.c pattern_player.c # fsusb.h pulls in fsusb.c itself (local copy, see docs/WEBUSB_PROTOCOL.md)

# The riscv64-linux-gnu-gcc that ch32fun.mk picks by default on this system
# lacks the rv32/ilp32 multilib, so force the bare-metal toolchain instead.
PREFIX:=riscv64-elf

# Use the minichlink already built and available on PATH, rather than
# building a copy inside the vendored ch32fun checkout.
MINICHLINK:=$(dir $(shell which minichlink))

include ch32fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
