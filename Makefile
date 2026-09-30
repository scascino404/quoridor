CC      = gcc
CFLAGS  = -std=c89 -pedantic -Wall -Wextra -O2 -g
AR      = ar

# SDL headers use `long long`; include them as system headers so -pedantic
# does not complain about code we don't own.
SDL_CFLAGS := $(patsubst -I%,-isystem %,$(shell sdl2-config --cflags))
SDL_LIBS   := $(shell sdl2-config --libs)

BUILD   = build
LIB     = $(BUILD)/libquoridor.a
GAME    = $(BUILD)/quoridor
TESTS   = $(BUILD)/test_quoridor $(BUILD)/test_ai

UI_OBJS = $(BUILD)/main.o $(BUILD)/ui.o $(BUILD)/font.o

.PHONY: all test run clean

all: $(GAME) $(TESTS)

$(BUILD):
	mkdir -p $(BUILD)

# Game library: no SDL.
$(BUILD)/quoridor.o: src/quoridor.c src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ai.o: src/ai.c src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/atomics.o: src/atomics.c src/atomics.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(BUILD)/quoridor.o $(BUILD)/ai.o $(BUILD)/atomics.o
	$(AR) rcs $@ $^

# SDL frontend.
$(BUILD)/main.o: src/main.c src/ui.h src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/ui.o: src/ui.c src/ui.h src/font.h src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/font.o: src/font.c src/font.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(GAME): $(UI_OBJS) $(LIB)
	$(CC) $(UI_OBJS) $(LIB) $(SDL_LIBS) -lm -o $@

# Tests: library only.
$(BUILD)/test_quoridor.o: tests/test_quoridor.c src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/test_ai.o: tests/test_ai.c src/ai.h src/atomics.h src/quoridor.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/test_%: $(BUILD)/test_%.o $(LIB)
	$(CC) $^ -o $@

test: $(TESTS)
	./$(BUILD)/test_quoridor
	./$(BUILD)/test_ai

run: $(GAME)
	./$(GAME)

clean:
	rm -rf $(BUILD)
