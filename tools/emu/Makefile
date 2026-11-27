# The emulator: the firmware built as a PC program (hh27-emu), run by emu.py.
#   make -C tools/emu && python3 tools/emu/emu.py
ROOT := ../..
# The firmware files that run unchanged; hw.c stands in for the rest
FIRMWARE := ledmatrix.c webusb_protocol.c store.c persist.c pattern_player.c la04.c app_host.c app_util.c ir_codec.c layout.c buttons.c font.c nametag.c menu.c power.c
SRCS := $(addprefix $(ROOT)/,$(FIRMWARE)) hw.c rv32.c app.c
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -DHH_EMU -Istub -I. -I$(ROOT) -Dmain=firmware_main

build/hh27-emu: $(SRCS) $(wildcard stub/*.h *.h $(ROOT)/*.h $(ROOT)/apps/hhapp.h)
	@mkdir -p build
	# ledmatrix.c's main() becomes firmware_main(); hw.c has the program's main()
	$(CC) $(CFLAGS) -c $(ROOT)/ledmatrix.c -o build/ledmatrix.o
	$(CC) $(filter-out -Dmain=firmware_main,$(CFLAGS)) -o $@ build/ledmatrix.o $(filter-out $(ROOT)/ledmatrix.c,$(SRCS)) -lpthread

clean:
	rm -rf build

.PHONY: clean
