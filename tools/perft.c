/*
 * perft.c - move-tree node counting.
 */
#include "perft.h"

static unsigned long count(qr_game *g, int depth, int checked)
{
    qr_move moves[QR_MAX_MOVES];
    qr_pos from;
    unsigned long nodes = 0;
    int n, i;

    if (depth <= 0)
        return 1;

    n = qr_legal_moves(g, moves);
    if (depth == 1)
        return (unsigned long)n;

    for (i = 0; i < n; i++) {
        from = g->pawn[g->to_move];
        if (!checked) {
            qr_apply_unchecked(g, &moves[i]);
        } else if (qr_apply_move(g, &moves[i]) != QR_OK) {
            /* A generated move that is then rejected is a library bug;
             * counting nothing for it makes the total come out wrong. */
            continue;
        }
        nodes += count(g, depth - 1, checked);
        qr_undo_move(g, &moves[i], from);
    }
    return nodes;
}

unsigned long perft(qr_game *g, int depth)
{
    return count(g, depth, 1);
}

unsigned long perft_unchecked(qr_game *g, int depth)
{
    return count(g, depth, 0);
}
