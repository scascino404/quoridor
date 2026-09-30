/*
 * ai.h - computer opponent for the Quoridor library.
 *
 * Pure C89, no I/O, no SDL and no threads of its own: qr_ai_choose_move
 * blocks until it has an answer. A caller that wants to stay responsive runs
 * it on another thread and uses qr_ai_stop to abort it.
 */
#ifndef AI_H
#define AI_H

#include "atomics.h"
#include "quoridor.h"

#define QR_AI_DEFAULT_DEPTH 2

typedef struct {
    unsigned long rng;    /* PRNG state, used only to break ties */
    int           depth;  /* plies searched; at least 1 */
    qr_atomic_int stop;   /* nonzero aborts the search; see qr_ai_stop */
} qr_ai;

/* Sets the default depth and clears the stop flag. The same seed always
 * produces the same moves. */
void qr_ai_init(qr_ai *ai, unsigned long seed);

/* Static evaluation from `player`'s point of view; higher is better.
 * (opponent's shortest path - player's shortest path), or a large
 * positive/negative value when the game is won/lost. */
int  qr_ai_evaluate(const qr_game *g, int player);

/* Chooses a move for the player to move. Returns 0 and fills *out, or -1
 * (leaving *out untouched) if there is no legal move or the search was
 * stopped. */
int  qr_ai_choose_move(qr_ai *ai, const qr_game *g, qr_move *out);

/* Aborts a search running on another thread; the only function here that
 * may be called while qr_ai_choose_move is running. The request is sticky,
 * so that one made just before the search starts is not lost: every search
 * fails until qr_ai_clear_stop is called. */
void qr_ai_stop(qr_ai *ai);
void qr_ai_clear_stop(qr_ai *ai);

#endif /* AI_H */
