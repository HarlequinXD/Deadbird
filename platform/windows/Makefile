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

Deadbird.exe: $(SRC) $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ -mwindows -static -lgdi32 -lwinmm -lm

clean:
	rm -f Deadbird.exe
