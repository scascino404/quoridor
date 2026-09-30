# Quoridor

Two-player Quoridor in C89 with an SDL2 frontend, playable against another
human or against a simple AI.

## Build

Requires gcc, make and SDL2 (`sdl2-config` on the PATH).

```
make          # build/quoridor, build/perft and the tests
make test     # run the rule, AI and perft tests
make run      # play
make clean
```

## Layout

| File | Purpose |
| --- | --- |
| `src/quoridor.[ch]` | Game library: rules, legality, path check, notation. No SDL. |
| `src/ai.[ch]` | AI: depth-limited minimax over the library's public API. No SDL, no threads of its own. Not part of the library. |
| `src/atomics.[ch]` | Atomic int, used to stop an AI search from another thread. The only code that relies on compiler extensions (GCC/Clang `__atomic` builtins). Not part of the library. |
| `tools/perft.[ch]` | Perft: counts the move tree to a given depth over the library's public API. Not part of the library. |
| `tools/perft_cli.c` | Command-line perft, built as `build/perft`. No SDL. |
| `src/ui.[ch]` | SDL2 rendering and input; runs the AI on a worker thread. |
| `src/font.[ch]` | Embedded 5x7 bitmap font. |
| `src/main.c` | Window and event loop. |
| `tests/test_quoridor.c` | Rule tests (no framework). |
| `tests/test_ai.c` | AI tests. |
| `tests/test_perft.c` | Perft totals for a few positions, as a regression test for move generation. |

## Playing

Purple starts on e1 and must reach rank 9; yellow starts on e9 and must reach rank 1.
Each player has 10 walls. Purple moves first.

- Click a square marked with a dot to move there.
- Hover a groove between squares to preview a wall (gray = legal, red = illegal),
  click to place it. The wall extends toward the half of the square the cursor is in.
- The last move is highlighted in the mover's colour.
- `U` / Backspace: undo, `N`: new game, `Esc` / `Q`: quit.

### Human or AI

The game starts as human (purple) against the AI (yellow). Each seat's
`HUMAN` / `AI` label, left of its wall stock, is a toggle: click it, or press
`1` (purple) or `2` (yellow). Seats can be switched at any time, including
AI against AI.

Against the AI, undo goes back to your previous turn, taking back the AI's
reply as well. With no human seated, undo does nothing.

## AI

Two-ply minimax with alpha-beta pruning: the AI picks the move whose worst
outcome, after the opponent's best reply, leaves it furthest ahead, measured
as the opponent's shortest path minus its own. Equal moves are decided in
favour of pawn moves, then at random.

## Perft

`build/perft` counts the move sequences of each length up to a given depth,
to check move generation against known totals and to time the library.

```
build/perft 3                      # depths 1 to 3 from the starting position
build/perft -m "e2 e8 e3h" 2       # play these moves first
build/perft -d 2                   # divide: the depth 2 total, split by first move
```

A line that ends in a win before the full depth counts nothing. The last ply
is counted from the generated move list without applying the moves. Times are
CPU time.

From the starting position:

| Depth | Nodes |
| --- | --- |
| 1 | 131 |
| 2 | 16677 |
| 3 | 2062264 |

## Rules implemented

- Pawns move one square orthogonally.
- An adjacent opponent can be jumped straight over; if a wall or the board edge
  is behind it, the pawn may instead move diagonally to either side of it
  (unless a wall is in the way).
- Walls are two squares long, may not overlap or cross, and must leave every
  player a path to their goal row.

## Notation

Squares are `a1`..`i9`. A wall is written as its anchor square plus orientation,
e.g. `e3h` (horizontal wall above e3 and f3) or `e3v` (vertical wall right of e3 and e4).
