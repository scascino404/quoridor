/*
 * test_perft.c - perft regression tests. Same conventions as
 * test_quoridor.c: no framework, failures are printed and the process exits
 * nonzero.
 *
 * The totals pin down move generation as a whole, so that changes to the
 * library's internals cannot alter it unnoticed.
 */
#include <stdio.h>

#include "perft.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) \
    do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static qr_pos sq(const char *s)
{
    qr_pos p;
    p.col = s[0] - 'a';
    p.row = s[1] - '1';
    return p;
}

/* Plays a NULL-terminated list of moves from the starting position. */
static void setup(qr_game *g, const char *const *moves)
{
    qr_move m;

    qr_game_init(g);
    for (; *moves != NULL; moves++)
        CHECK(qr_move_from_str(*moves, &m) == 0 &&
              qr_apply_move(g, &m) == QR_OK);
}

static void test_depth_zero(void)
{
    qr_game g;

    qr_game_init(&g);
    CHECK(perft(&g, 0) == 1UL);
}

static void test_start_position(void)
{
    qr_game g;

    qr_game_init(&g);
    /* e2, d1, f1 + all 128 wall slots */
    CHECK(perft(&g, 1) == 131UL);
    /* After a pawn move the reply has all 131 moves again: 3 * 131 = 393.
     * After a wall the reply loses that slot, the crossing slot and the
     * overlapping slots (2, or 1 for the 32 walls that touch the edge they
     * run towards): 96 * 124 + 32 * 125 = 15904 walls. It has 3 pawn moves
     * except after d8h, e8h, d8v and e8v, which each close one side of
     * e9: 128 * 3 - 4 = 380. */
    CHECK(perft(&g, 2) == 393UL + 15904UL + 380UL);
}

static void test_jump_position(void)
{
    static const char *const moves[] = {
        "e2", "e8", "e3", "e7", "e4", "e6", "e6h", "d4v", "e5", NULL
    };
    qr_game g;

    /* Pawns face to face on e5/e6 with a wall behind e6 and one left of e5,
     * so both straight and diagonal jumps occur in the tree. */
    setup(&g, moves);
    /* e4 (jump), d6, f6 + 128 slots minus 2 occupied, 2 crossing and
     * 4 overlapping */
    CHECK(perft(&g, 1) == 3UL + 120UL);
    /* Recorded from the library as of the first perft version. */
    CHECK(perft(&g, 2) == 14804UL);
}

static void test_walled_position(void)
{
    static const char *const moves[] = { "e2", "e8", "e3h", NULL };
    qr_game g;

    setup(&g, moves);
    /* e7, e9, d8, f8 + 128 slots minus 1 occupied, 1 crossing and
     * 2 overlapping */
    CHECK(perft(&g, 1) == 4UL + 124UL);
    /* Recorded from the library as of the first perft version. */
    CHECK(perft(&g, 2) == 15918UL);
}

static void test_won_lines_end_early(void)
{
    qr_game g;

    qr_game_init(&g);
    g.pawn[0] = sq("e8");
    g.pawn[1] = sq("a5");
    g.walls_left[0] = 0;
    g.walls_left[1] = 0;
    CHECK(perft(&g, 1) == 4UL);          /* e9 d8 f8 e7 */
    /* e9 wins and has no replies; the other three allow a6, a4, b5. */
    CHECK(perft(&g, 2) == 9UL);

    g.pawn[0] = sq("e9");
    g.winner = 0;
    g.to_move = 1;
    CHECK(perft(&g, 0) == 1UL);
    CHECK(perft(&g, 1) == 0UL);
    CHECK(perft(&g, 3) == 0UL);
}

int main(void)
{
    test_depth_zero();
    test_start_position();
    test_jump_position();
    test_walled_position();
    test_won_lines_end_early();

    if (failures) {
        fprintf(stderr, "%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("all %d checks passed\n", checks);
    return 0;
}
