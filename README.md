# Quoridor

Two-player Quoridor in C89 with an SDL2 frontend.

## Build

Requires gcc, make and SDL2 (`sdl2-config` on the PATH).

```
make          # build/quoridor and build/test_quoridor
make test     # run the rule tests
make run      # play
make clean
```

## Layout

| File | Purpose |
| --- | --- |
| `src/quoridor.[ch]` | Game library: rules, legality, path check, notation. No SDL. Built as `build/libquoridor.a`. |
| `src/ui.[ch]` | SDL2 rendering and input. |
| `src/font.[ch]` | Embedded 5x7 bitmap font. |
| `src/main.c` | Window and event loop. |
| `tests/test_quoridor.c` | Rule tests (no framework). |

## Playing

Purple starts on e1 and must reach rank 9; yellow starts on e9 and must reach rank 1.
Each player has 10 walls.

- Click a square marked with a dot to move there.
- Hover a groove between squares to preview a wall (gray = legal, red = illegal),
  click to place it. The wall extends toward the half of the square the cursor is in.
- `U` / Backspace: undo, `N`: new game, `Esc` / `Q`: quit.

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
