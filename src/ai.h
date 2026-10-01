/*
 * ai.h - computer opponent for the Quoridor library.
 *
 * No I/O and no SDL. qr_ai_choose_move blocks until it has an answer; it
 * searches on several threads of its own (POSIX threads) and returns when
 * its time budget or depth limit is reached. A caller that wants to stay
 * responsive runs it on another thread and uses qr_ai_stop to abort it.
 */
#ifndef AI_H
#define AI_H

#include "atomics.h"
#include "quoridor.h"

#define QR_AI_DEFAULT_TIME_MS 2000L
#define QR_AI_DEFAULT_THREADS 4
#define QR_AI_MAX_THREADS     16

typedef struct {
    unsigned long rng;      /* PRNG state, used to vary the order of equal moves */
    long          time_ms;  /* time budget per move in milliseconds; 0 = none */
    int           depth;    /* maximum depth in plies; 0 = none */
    int           threads;  /* search threads, 1 to QR_AI_MAX_THREADS */
    qr_atomic_int stop;     /* nonzero aborts the search; see qr_ai_stop */
} qr_ai;

/* Sets the default time budget and thread count, no depth limit, and
 * clears the stop flag. Also clears what earlier searches learned, which is
 * otherwise kept from move to move in a table shared by all searches in
 * the process (64 MB, allocated on first use). After this, with one thread
 * and no time budget (only a depth limit), the same seed always produces
 * the same moves. */
void qr_ai_init(qr_ai *ai, unsigned long seed);

/* Static evaluation from `player`'s point of view; higher is better. In
 * hundredths of a step: mostly the difference of the two shortest paths,
 * plus the walls each player has left and a bonus for the player to move.
 * A won game, or a race that can no longer be lost, scores far beyond
 * anything else. */
int  qr_ai_evaluate(const qr_game *g, int player);

/* Chooses a move for the player to move. Searches until the time budget
 * runs out or the depth limit is reached; if neither is set, uses
 * QR_AI_DEFAULT_TIME_MS. Returns 0 and fills *out, or -1 (leaving *out
 * untouched) if there is no legal move, the search was stopped, or memory
 * for the search could not be allocated. Several searches may run at the
 * same time, each with its own qr_ai. */
int  qr_ai_choose_move(qr_ai *ai, const qr_game *g, qr_move *out);

/* Aborts a search running on another thread; the only function here that
 * may be called while qr_ai_choose_move is running. The request is sticky,
 * so that one made just before the search starts is not lost: every search
 * fails until qr_ai_clear_stop is called. */
void qr_ai_stop(qr_ai *ai);
void qr_ai_clear_stop(qr_ai *ai);

#endif /* AI_H */
