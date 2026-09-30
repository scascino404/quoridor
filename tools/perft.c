/*
 * perft.c - move-tree node counting.
 */
#include "perft.h"

unsigned long perft(const qr_game *g, int depth)
{
    qr_move moves[QR_MAX_MOVES];
    qr_game next;
    unsigned long nodes = 0;
    int n, i;

    if (depth <= 0)
        return 1;

    n = qr_legal_moves(g, moves);
    if (depth == 1)
        return (unsigned long)n;

    for (i = 0; i < n; i++) {
        next = *g;
        /* A generated move that is then rejected is a library bug; counting
         * nothing for it makes the total come out wrong. */
        if (qr_apply_move(&next, &moves[i]) != QR_OK)
            continue;
        nodes += perft(&next, depth - 1);
    }
    return nodes;
}
