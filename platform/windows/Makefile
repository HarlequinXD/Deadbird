# Deadbird for Windows (64-bit): the engine's Windows layer (engine/platform/windows) + the game.
# Builds with MinGW-w64, natively (MSYS2) or cross-compiled from Linux:
#   make -C platform/windows        ->  platform/windows/Deadbird.exe
ROOT := ../..
CC ?= x86_64-w64-mingw32-gcc
ifeq ($(CC),cc)
CC := x86_64-w64-mingw32-gcc
endif
CFLAGS ?= -Os -std=c99 -Wall -Wextra -Wno-unused-parameter
# small: unused functions dropped, no unwind tables, symbols stripped
SIZE := -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-ident -s -Wl,--gc-sections
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/gen -I$(ROOT)/assets
PLAT := $(ROOT)/engine/platform/windows
SRC := $(PLAT)/win_main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c) $(wildcard $(ROOT)/gen/*.c)

WINDRES ?= $(patsubst %gcc,%windres,$(CC))

Deadbird.exe: $(SRC) deadbird_res.o $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h) $(wildcard $(ROOT)/gen/*.h)
	$(CC) $(CFLAGS) $(SIZE) $(INC) $(SRC) deadbird_res.o -o $@ -mwindows -static -lgdi32 -lwinmm -lxinput9_1_0

deadbird_res.o: deadbird.rc deadbird.ico deadbird.manifest
	$(WINDRES) deadbird.rc -O coff -o $@

clean:
	rm -f Deadbird.exe deadbird_res.o
