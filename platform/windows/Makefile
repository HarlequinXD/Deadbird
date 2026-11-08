# Deadbird for Windows (64-bit). Builds with MinGW-w64, natively (MSYS2) or cross-compiled
# from Linux:  make -C platform/windows        ->  platform/windows/Deadbird.exe
ROOT := ../..
CC ?= x86_64-w64-mingw32-gcc
ifeq ($(CC),cc)
CC := x86_64-w64-mingw32-gcc
endif
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/assets
SRC := win_main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c)

WINDRES ?= $(patsubst %gcc,%windres,$(CC))

Deadbird.exe: $(SRC) deadbird_res.o $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) deadbird_res.o -o $@ -mwindows -static -lgdi32 -lwinmm -lxinput9_1_0 -lm

deadbird_res.o: deadbird.rc deadbird.ico deadbird.manifest
	$(WINDRES) deadbird.rc -O coff -o $@

clean:
	rm -f Deadbird.exe deadbird_res.o
