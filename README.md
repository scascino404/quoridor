# Quoridor

Two-player Quoridor in C89 with an SDL2 frontend, playable against another
human or against the computer.

## Build

Requires gcc, make, SDL2 (`sdl2-config` on the PATH) and POSIX threads.

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
| `src/ai.[ch]` | AI: multi-threaded alpha-beta search over the library's public API. No SDL; POSIX threads. Not part of the library. |
| `src/atomics.[ch]` | Atomic values, used to stop an AI search from another thread and to share the AI's transposition table between its threads. The only code that relies on compiler extensions (GCC/Clang `__atomic` builtins). Not part of the library. |
| `tools/perft.[ch]` | Perft: counts the move tree to a given depth over the library's public API. Not part of the library. |
| `tools/perft_cli.c` | Command-line perft, built as `build/perft`. No SDL. |
| `src/ui.[ch]` | SDL2 rendering and input; runs the AI on a worker thread. |
| `src/font.[ch]` | Embedded 5x7 bitmap font. |
| `src/main.c` | Window and event loop. |
| `tests/check.h` | What the tests share: the `CHECK` macro and its report. No framework. |
| `tests/test_quoridor.c` | Rule tests. |
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

The AI searches for about two seconds per move on four threads:

- Iterative-deepening alpha-beta (principal variation search) with a
  transposition table shared by the threads, killer and history move
  ordering, and null-move pruning. The threads search the same tree at
  slightly different depths (Lazy SMP).
- At the root every legal move is considered; deeper in the tree only the
  walls that cut one of the opponent's shortest paths, the only walls that
  can lengthen it.
- The evaluation is the difference between the two players' shortest paths,
  plus the walls each has left and a bonus for the player to move. A player
  who moves first and can no longer be walled in wins the race if its path
  is no longer than the opponent's.

With the default two seconds it typically completes 16 to 20 plies. The
time budget, depth limit and thread count are fields of `qr_ai` (see
`src/ai.h`).

## Perft

`build/perft` counts the move sequences of each length up to a given depth,
to check move generation against known totals and to time the library.

```
build/perft 3                      # depths 1 to 3 from the starting position
build/perft -m "e2 e8 e3h" 2       # play these moves first
build/perft -d 2                   # divide: the depth 2 total, split by first move
build/perft -u 4                   # apply the generated moves without validating them again
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
| 4 | 247569030 |

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
