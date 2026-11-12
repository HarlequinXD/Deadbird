# Headless PC build of the engine (no window): renders test frames to BMP.
ROOT := ../..
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
GEN := $(wildcard $(ROOT)/gen/*.c)
# the engine without its runtime (engine.c needs a game's App): for the tests of single systems
ENG := $(filter-out %/engine.c,$(wildcard $(ROOT)/engine/*.c))
FILES := $(ROOT)/engine/platform/headless/files_posix.c
SRC := main.c $(FILES) $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c) $(GEN)
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/gen -I$(ROOT)/assets

deadbird_headless: $(SRC) $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h) $(wildcard $(ROOT)/assets/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

clean:
	rm -f deadbird_headless gaittest stresstest vistest labview rolltest combattest edtest edittest *.bmp


# side-on walk/run contact sheet driven by the real locomotion code
GAIT_SRC := gaittest.c $(ENG) $(ROOT)/game/player.c $(ROOT)/game/fk.c \
            $(ROOT)/game/fkclips.c $(ROOT)/game/level.c $(ROOT)/game/fx.c $(ROOT)/game/hud.c $(ROOT)/game/assets.c $(GEN)
gaittest: $(GAIT_SRC) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(GAIT_SRC) -o $@

# long random-input movement run that fails on glitches
STRESS_SRC := stresstest.c $(ENG) $(ROOT)/game/player.c $(ROOT)/game/fk.c \
              $(ROOT)/game/fkclips.c $(ROOT)/game/level.c $(ROOT)/game/fx.c $(ROOT)/game/hud.c $(ROOT)/game/assets.c $(GEN)
stresstest: $(STRESS_SRC) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(STRESS_SRC) -o $@

# fly-through screenshots of the level (no player)
LABVIEW_SRC := labview.c $(ROOT)/game/edmap.c $(ENG) $(ROOT)/game/level.c $(GEN)
labview: $(LABVIEW_SRC) $(wildcard $(ROOT)/gen/*.h)
	$(CC) $(CFLAGS) $(INC) $(LABVIEW_SRC) -o $@

# solves the right-forearm roll that keeps the chainsaw's edge leading each combo swing
rolltest: rolltest.c $(ENG) $(ROOT)/game/fk.c $(ROOT)/game/fkclips.c
	$(CC) $(CFLAGS) $(INC) rolltest.c $(ENG) $(ROOT)/game/fk.c $(ROOT)/game/fkclips.c $(ROOT)/game/assets.c $(GEN) -o $@ -lm

# combat system: chainsaw combo vs a dummy enemy, a wall and the floor (fails on regressions)
COMBAT_SRC := combattest.c $(ENG) $(ROOT)/game/player.c $(ROOT)/game/fk.c \
              $(ROOT)/game/fkclips.c $(ROOT)/game/level.c $(ROOT)/game/combat.c $(ROOT)/game/assets.c $(GEN)
combattest: $(COMBAT_SRC) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(COMBAT_SRC) -o $@

# the portal visibility along the route vs drawing every room: fails on wrongly hidden geometry
VIS_SRC := vistest.c $(ENG) $(ROOT)/game/level.c $(GEN)
vistest: $(VIS_SRC) $(wildcard $(ROOT)/game/*.h) $(wildcard $(ROOT)/gen/*.h)
	$(CC) $(CFLAGS) $(INC) $(VIS_SRC) -o $@

# the editor's map code (game/edmap.c) must compile maps exactly like tools/make_map.py
EDTEST_SRC := edtest.c $(ROOT)/game/edmap.c $(ROOT)/gen/map_src.c $(ROOT)/gen/map_e1.c $(ROOT)/gen/map_test.c
edtest: $(EDTEST_SRC) $(ROOT)/game/edmap.h $(ROOT)/gen/map.h
	$(CC) $(CFLAGS) $(INC) $(EDTEST_SRC) -o $@

# the level editor driven by a scripted pad: screenshots, a saved level, a play test
EDIT_SRC := edittest.c $(FILES) $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c) $(GEN)
edittest: $(EDIT_SRC) $(wildcard $(ROOT)/game/*.h) $(wildcard $(ROOT)/gen/*.h)
	$(CC) $(CFLAGS) $(INC) $(EDIT_SRC) -o $@

# ---- the engine's own sample game (examples/hello) on the generic headless runner
HELLO_SRC := $(ROOT)/engine/platform/headless/headless_main.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/examples/hello/hello.c
hello_headless: $(HELLO_SRC) $(wildcard $(ROOT)/engine/*.h)
	$(CC) $(CFLAGS) -I$(ROOT)/engine $(HELLO_SRC) -o $@

# ---- the framework's sample FPS (examples/fps): a game made only of data, in the sector world
FW := $(wildcard $(ROOT)/engine/fw/*.c)
WORLD := $(ROOT)/game/level.c $(GEN)
FPS_SRC := $(ROOT)/engine/platform/headless/headless_main.c $(FILES) $(wildcard $(ROOT)/engine/*.c) $(FW) \
           $(ROOT)/examples/fps/fps.c $(ROOT)/examples/fps/sector_world.c $(WORLD)
fps_headless: $(FPS_SRC) $(wildcard $(ROOT)/engine/fw/*.h) $(ROOT)/examples/fps/hellgate_game.h
	$(CC) $(CFLAGS) -I$(ROOT)/engine -I$(ROOT)/engine/fw -I$(ROOT)/examples/fps $(INC) $(FPS_SRC) -o $@

# the framework's state machines, actions and combat on HELL GATE (fails on regressions)
FWTEST_SRC := fwtest.c $(FILES) $(wildcard $(ROOT)/engine/*.c) $(FW) $(ROOT)/examples/fps/sector_world.c $(WORLD)
fwtest: $(FWTEST_SRC) $(wildcard $(ROOT)/engine/fw/*.h) $(ROOT)/examples/fps/hellgate_game.h
	$(CC) $(CFLAGS) -I$(ROOT)/engine -I$(ROOT)/engine/fw -I$(ROOT)/examples/fps $(INC) $(FWTEST_SRC) -o $@
