CROSS  ?=
CC      = $(CROSS)gcc
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
SDL_LIBS   ?= $(shell pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)
CFLAGS  = -O2 -Wall -Wextra -std=c99 $(SDL_CFLAGS)
LDFLAGS =
LIBS    = $(SDL_LIBS) -lm

SRC = src/main.c
BIN = drumkit

all: $(BIN)

$(BIN): $(SRC) src/main.c
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LIBS)

selftest: CFLAGS += -DDK_TEST
selftest: $(BIN)
	./$(BIN) --selftest

knulli: $(BIN)
	$(CROSS)strip $(BIN)

clean:
	rm -f $(BIN) build/*.o

.PHONY: all selftest knulli clean
