/*
 * perft.h - move-tree node counting for the Quoridor library.
 *
 * Pure C89, no I/O. Not part of the library: it only uses the public qr_*
 * API, so the counts pin down the behaviour of move generation and the time
 * taken measures its speed.
 */
#ifndef PERFT_H
#define PERFT_H

#include "quoridor.h"

/* Number of move sequences of exactly `depth` plies from g; 1 for depth 0.
 * A finished game has no moves, so lines that end early count nothing.
 * The last ply is counted without applying its moves. Moves are made and
 * undone on g itself, which is left as it was found. */
unsigned long perft(qr_game *g, int depth);

#endif /* PERFT_H */
