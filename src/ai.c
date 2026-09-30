/*
 * ai.c - depth-limited minimax (negamax form) with alpha-beta pruning.
 * Deliberately simple: no move ordering, no transposition table, and it
 * only uses the public qr_* API.
 *
 * The search works on one copy of the game, applying each move and undoing
 * it again. Its moves all come from qr_legal_moves, so they are applied
 * without being checked a second time.
 */
#include "ai.h"

#define AI_WIN 10000
#define AI_INF 30000

static unsigned long rng_next(qr_ai *ai)
{
    ai->rng = (ai->rng * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (ai->rng >> 16) & 0x7FFFUL;
}

void qr_ai_init(qr_ai *ai, unsigned long seed)
{
    ai->rng = seed & 0xFFFFFFFFUL;
    ai->depth = QR_AI_DEFAULT_DEPTH;
    qr_atomic_store(&ai->stop, 0);
}

void qr_ai_stop(qr_ai *ai)
{
    qr_atomic_store(&ai->stop, 1);
}

void qr_ai_clear_stop(qr_ai *ai)
{
    qr_atomic_store(&ai->stop, 0);
}

int qr_ai_evaluate(const qr_game *g, int player)
{
    if (g->winner >= 0)
        return g->winner == player ? AI_WIN : -AI_WIN;
    return qr_shortest_path(g, 1 - player) - qr_shortest_path(g, player);
}

/* Value of g for the player to move, searching `depth` more plies. g is
 * left as it was found. The result is meaningless once a stop is requested;
 * callers must check. */
static int search(qr_ai *ai, qr_game *g, int depth, int alpha, int beta)
{
    qr_move moves[QR_MAX_MOVES];
    qr_pos from;
    int n, i, score, best;

    /* The previous move won, so the player to move has lost. Adding the
     * remaining depth makes quicker wins score higher. */
    if (g->winner >= 0)
        return -(AI_WIN + depth);
    if (depth <= 0)
        return qr_ai_evaluate(g, g->to_move);

    n = qr_legal_moves(g, moves);
    if (n == 0)
        return qr_ai_evaluate(g, g->to_move);

    best = -AI_INF;
    from = g->pawn[g->to_move];
    for (i = 0; i < n; i++) {
        if (qr_atomic_load(&ai->stop))
            return 0;
        qr_apply_unchecked(g, &moves[i]);
        score = -search(ai, g, depth - 1, -beta, -alpha);
        qr_undo_move(g, &moves[i], from);
        if (score > best)
            best = score;
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;
    }
    return best;
}

int qr_ai_choose_move(qr_ai *ai, const qr_game *g, qr_move *out)
{
    qr_move moves[QR_MAX_MOVES];
    qr_game work = *g;
    qr_pos from;
    int n, i, score, is_pawn;
    int best = -AI_INF, best_is_pawn = 0, ties = 0, pick = 0;

    n = qr_legal_moves(&work, moves);
    if (n == 0)
        return -1;

    from = work.pawn[work.to_move];
    for (i = 0; i < n; i++) {
        if (qr_atomic_load(&ai->stop))
            return -1;
        qr_apply_unchecked(&work, &moves[i]);
        /* The window starts one below the best score so far, so that moves
         * which merely equal it come back exact and can be tie-broken. */
        score = -search(ai, &work, ai->depth - 1, -AI_INF, -(best - 1));
        qr_undo_move(&work, &moves[i], from);
        if (qr_atomic_load(&ai->stop))
            return -1;

        /* Among equal scores prefer pawn moves (walls are a finite
         * resource), then pick at random. */
        is_pawn = moves[i].type == QR_MOVE_PAWN;
        if (score > best || (score == best && is_pawn && !best_is_pawn)) {
            best = score;
            best_is_pawn = is_pawn;
            pick = i;
            ties = 1;
        } else if (score == best && is_pawn == best_is_pawn) {
            ties++;
            if (rng_next(ai) % (unsigned long)ties == 0)
                pick = i;
        }
    }

    *out = moves[pick];
    return 0;
}
