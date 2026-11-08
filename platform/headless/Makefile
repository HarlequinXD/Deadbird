# Headless PC build of the engine (no window): renders test frames to BMP.
ROOT := ../..
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
SRC := main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c)
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/assets

all: deadbird_headless posetest

deadbird_headless: $(SRC) $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

clean:
	rm -f deadbird_headless posetest *.bmp

# animation contact sheet (every clip through the IK rig)
posetest: posetest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/anim.c $(ROOT)/game/clips.c $(ROOT)/game/rig.c
	$(CC) $(CFLAGS) $(INC) posetest.c $(wildcard $(ROOT)/engine/*.c) $(ROOT)/game/anim.c $(ROOT)/game/clips.c $(ROOT)/game/rig.c -o $@
