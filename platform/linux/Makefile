# Deadbird for Linux (64-bit): the engine's Linux layer (engine/platform/linux, SDL2) + the game,
# and Deadbird Studio (the game and level editor).
#   sudo apt install build-essential libsdl2-dev          (Debian, Ubuntu, Mint)
#   sudo dnf install gcc make SDL2-devel                  (Fedora)
#   sudo pacman -S base-devel sdl2                        (Arch)
#   make -C platform/linux                  ->  platform/linux/deadbird
#   make -C platform/linux deadbird-studio  ->  platform/linux/deadbird-studio
ROOT := ../..
CC ?= cc
SDL_CONFIG ?= sdl2-config
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS := $(shell $(SDL_CONFIG) --libs)
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/gen -I$(ROOT)/assets
PLAT := $(ROOT)/engine/platform/linux
SRC := $(PLAT)/linux_main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c) $(wildcard $(ROOT)/gen/*.c)
HDR := $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h) $(wildcard $(ROOT)/gen/*.h)

deadbird: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(INC) $(SRC) -o $@ $(SDL_LIBS) -lm

# the Studio: its full-resolution UI drawn by the software rasteriser (LINUX_HIRES)
STUDIO_INC := $(INC) -I$(ROOT)/engine/fw -I$(ROOT)/engine/studio -I$(ROOT)/studio -I$(ROOT)/examples/fps
STUDIO_SRC := $(PLAT)/linux_main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/engine/fw/*.c) \
              $(wildcard $(ROOT)/engine/studio/*.c) $(wildcard $(ROOT)/studio/*.c) $(ROOT)/examples/fps/sector_world.c \
              $(addprefix $(ROOT)/game/,level.c edmap.c editor.c edui.c ed3d.c ui.c assets.c fx.c hud.c) $(wildcard $(ROOT)/gen/*.c)
deadbird-studio: $(STUDIO_SRC) $(HDR) $(wildcard $(ROOT)/engine/*/*.h) $(wildcard $(ROOT)/studio/*.h)
	$(CC) $(CFLAGS) -DLINUX_HIRES $(SDL_CFLAGS) $(STUDIO_INC) $(STUDIO_SRC) -o $@ $(SDL_LIBS) -lm

clean:
	rm -f deadbird deadbird-studio
