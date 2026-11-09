# Headless PC build of the engine (no window): renders test frames to BMP.
ROOT := ../..
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
GEN := $(wildcard $(ROOT)/gen/*.c)
SRC := main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c) $(GEN)
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/gen -I$(ROOT)/assets

deadbird_headless: $(SRC) $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h) $(wildcard $(ROOT)/assets/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

clean:
	rm -f deadbird_headless gaittest stresstest *.bmp


# side-on walk/run contact sheet driven by the real locomotion code
GAIT_SRC := gaittest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/player.c $(ROOT)/game/fk.c \
            $(ROOT)/game/fkclips.c $(ROOT)/game/level.c $(ROOT)/game/fx.c $(ROOT)/game/hud.c $(ROOT)/game/assets.c $(GEN)
gaittest: $(GAIT_SRC) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(GAIT_SRC) -o $@

# long random-input movement run that fails on glitches
STRESS_SRC := stresstest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/player.c $(ROOT)/game/fk.c \
              $(ROOT)/game/fkclips.c $(ROOT)/game/level.c $(ROOT)/game/fx.c $(ROOT)/game/hud.c $(ROOT)/game/assets.c $(GEN)
stresstest: $(STRESS_SRC) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(STRESS_SRC) -o $@

# fly-through screenshots of the level (no player)
LABVIEW_SRC := labview.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/level.c $(GEN)
labview: $(LABVIEW_SRC) $(wildcard $(ROOT)/gen/*.h)
	$(CC) $(CFLAGS) $(INC) $(LABVIEW_SRC) -o $@

# solves the right-forearm roll that keeps the chainsaw's edge leading each combo swing
rolltest: rolltest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/fk.c $(ROOT)/game/fkclips.c
	$(CC) $(CFLAGS) $(INC) rolltest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/fk.c $(ROOT)/game/fkclips.c $(ROOT)/game/assets.c $(GEN) -o $@ -lm

# combat system: chainsaw combo vs a dummy enemy, a wall and the floor (fails on regressions)
COMBAT_SRC := combattest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/player.c $(ROOT)/game/fk.c \
              $(ROOT)/game/fkclips.c $(ROOT)/game/level.c $(ROOT)/game/combat.c $(ROOT)/game/assets.c $(GEN)
combattest: $(COMBAT_SRC) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(COMBAT_SRC) -o $@
