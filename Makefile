CC      = gcc
CFLAGS  = -std=c89 -pedantic -Wall -Wextra -O2 -g

# SDL headers use `long long`; include them as system headers so -pedantic
# does not complain about code we don't own.
SDL_CFLAGS := $(patsubst -I%,-isystem %,$(shell sdl2-config --cflags))
SDL_LIBS   := $(shell sdl2-config --libs)

BUILD   = build
GAME    = $(BUILD)/quoridor
PERFT   = $(BUILD)/perft
TESTS   = $(BUILD)/test_quoridor $(BUILD)/test_ai $(BUILD)/test_perft

LIB_OBJS = $(BUILD)/quoridor.o
AI_OBJS  = $(BUILD)/ai.o $(BUILD)/atomics.o
UI_OBJS  = $(BUILD)/main.o $(BUILD)/ui.o $(BUILD)/font.o

.PHONY: all test run clean

all: $(GAME) $(PERFT) $(TESTS)

$(BUILD):
	mkdir -p $(BUILD)

# Game library: rules only, no SDL.
$(BUILD)/quoridor.o: src/quoridor.c src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# AI: uses the library but is not part of it. No SDL; POSIX threads.
$(BUILD)/ai.o: src/ai.c src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread -c $< -o $@

$(BUILD)/atomics.o: src/atomics.c src/atomics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Perft command-line tool: library only.
$(BUILD)/perft.o: tools/perft.c tools/perft.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/perft_cli.o: tools/perft_cli.c tools/perft.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(PERFT): $(BUILD)/perft_cli.o $(BUILD)/perft.o $(LIB_OBJS)
	$(CC) $^ -o $@

# SDL frontend.
$(BUILD)/main.o: src/main.c src/ui.h src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/ui.o: src/ui.c src/ui.h src/font.h src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/font.o: src/font.c src/font.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(GAME): $(UI_OBJS) $(AI_OBJS) $(LIB_OBJS)
	$(CC) $^ $(SDL_LIBS) -pthread -lm -o $@

# Tests: no SDL.
$(BUILD)/test_quoridor.o: tests/test_quoridor.c tests/check.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/test_ai.o: tests/test_ai.c tests/check.h src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/test_perft.o: tests/test_perft.c tests/check.h tools/perft.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -Itools -c $< -o $@

$(BUILD)/test_quoridor: $(BUILD)/test_quoridor.o $(LIB_OBJS)
	$(CC) $^ -o $@

$(BUILD)/test_ai: $(BUILD)/test_ai.o $(AI_OBJS) $(LIB_OBJS)
	$(CC) $^ -pthread -o $@

$(BUILD)/test_perft: $(BUILD)/test_perft.o $(BUILD)/perft.o $(LIB_OBJS)
	$(CC) $^ -o $@

test: $(TESTS)
	./$(BUILD)/test_quoridor
	./$(BUILD)/test_ai
	./$(BUILD)/test_perft

run: $(GAME)
	./$(GAME)

clean:
	rm -rf $(BUILD)
