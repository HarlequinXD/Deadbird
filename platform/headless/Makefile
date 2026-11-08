# Headless PC build of the engine (no window): renders test frames to BMP.
ROOT := ../..
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter
SRC := main.c $(wildcard $(ROOT)/engine/*.c) $(wildcard $(ROOT)/game/*.c)
INC := -I$(ROOT)/engine -I$(ROOT)/game -I$(ROOT)/assets

deadbird_headless: $(SRC) $(wildcard $(ROOT)/engine/*.h) $(wildcard $(ROOT)/game/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

clean:
	rm -f deadbird_headless *.bmp
