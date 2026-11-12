# Deadbird for Windows (64-bit): the engine's Windows layer (engine/platform/windows) + the game,
# and Deadbird Studio (the game and level editor). Builds with MinGW-w64, natively (MSYS2) or
# cross-compiled from Linux:
#   make -C platform/windows                      ->  platform/windows/Deadbird.exe
#   make -C platform/windows DeadbirdStudio.exe   ->  platform/windows/DeadbirdStudio.exe
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

# the Studio: its full-resolution UI drawn by the software rasteriser (WIN_HIRES), Deadbird's
# level editor embedded, the gameplay framework it play-tests with. -O2: it draws every pixel.
STUDIO_CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
STUDIO_INC := $(INC) -I$(ROOT)/engine/fw -I$(ROOT)/engine/studio -I$(ROOT)/studio -I$(ROOT)/examples/fps
STUDIO_SRC := $(PLAT)/win_main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/engine/fw/*.c) \
              $(wildcard $(ROOT)/engine/studio/*.c) $(wildcard $(ROOT)/studio/*.c) $(ROOT)/examples/fps/sector_world.c \
              $(addprefix $(ROOT)/game/,level.c edmap.c editor.c edui.c ed3d.c ui.c assets.c fx.c hud.c) $(wildcard $(ROOT)/gen/*.c)
DeadbirdStudio.exe: $(STUDIO_SRC) studio_res.o $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/engine/*/*.h) $(wildcard $(ROOT)/studio/*.h) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(STUDIO_CFLAGS) -DWIN_HIRES $(SIZE) $(STUDIO_INC) $(STUDIO_SRC) studio_res.o -o $@ -mwindows -static -lgdi32 -lwinmm -lxinput9_1_0 -lm

studio_res.o: studio.rc deadbird.ico studio.manifest
	$(WINDRES) studio.rc -O coff -o $@

clean:
	rm -f Deadbird.exe deadbird_res.o DeadbirdStudio.exe studio_res.o
