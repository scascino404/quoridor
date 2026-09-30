/*
 * test_perft.c - perft regression tests.
 *
 * The totals pin down move generation as a whole, so that changes to the
 * library's internals cannot alter it unnoticed.
 */
#include <string.h>

#include "check.h"
#include "perft.h"

/* Pawns face to face on e5/e6 with a wall behind e6 and one left of e5, so
 * both straight and diagonal jumps occur in the tree. */
static const char *const JUMP_MOVES[] = {
    "e2", "e8", "e3", "e7", "e4", "e6", "e6h", "d4v", "e5", NULL
};

/* 15 walls that leave corridors, so many slots would block a path; the
 * first player has one wall left and soon only moves the pawn. */
static const char *const TIGHT_MOVES[] = {
    "f2h", "e8", "b5h", "d8", "a3v", "h4h", "g3v", "d7", "h6h", "d8",
    "f1", "b6v", "d5h", "g5v", "d6h", "f5h", "g1", "c7v", "a5v", "d7",
    "b2h", "b4h", NULL
};

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
    /* Recorded from the library as of the first perft version. */
    CHECK(perft(&g, 3) == 2062264UL);
}

static void test_jump_position(void)
{
    qr_game g;

    setup(&g, JUMP_MOVES);
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

static void test_crowded_positions(void)
{
    /* 13 walls, both players with several left: most wall slots touch
     * other walls, and a few would shut a pawn in. */
    static const char *const open_moves[] = {
        "e3v", "e8", "a7h", "d2v", "f2h", "h8v", "e2", "a3h", "a6v", "d6h",
        "e1", "f8", "f1", "g8", "f2", "b4v", "f1", "d4v", "f2", "b8v", "g3v",
        "f5h", NULL
    };
    qr_game g;

    /* All totals recorded from the library as of the first perft version. */
    setup(&g, open_moves);
    CHECK(perft(&g, 1) == 85UL);
    CHECK(perft(&g, 2) == 7036UL);
    CHECK(perft(&g, 3) == 553793UL);

    setup(&g, TIGHT_MOVES);
    CHECK(perft(&g, 1) == 72UL);
    CHECK(perft(&g, 2) == 4881UL);
    CHECK(perft(&g, 3) == 28244UL);
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

static void test_leaves_game_unchanged(void)
{
    qr_game g, before;

    setup(&g, JUMP_MOVES);
    before = g;
    perft(&g, 2);
    CHECK(memcmp(&g, &before, sizeof before) == 0);

    /* a tree with winning moves in it */
    qr_game_init(&g);
    g.pawn[0] = sq("e8");
    g.pawn[1] = sq("e2");
    before = g;
    perft(&g, 3);
    CHECK(memcmp(&g, &before, sizeof before) == 0);
}

/* Applying the generated moves unchecked gives the same totals. */
static void test_unchecked(void)
{
    qr_game g, before;

    qr_game_init(&g);
    before = g;
    CHECK(perft_unchecked(&g, 0) == 1UL);
    CHECK(perft_unchecked(&g, 1) == 131UL);
    CHECK(perft_unchecked(&g, 3) == 2062264UL);
    CHECK(memcmp(&g, &before, sizeof before) == 0);

    setup(&g, TIGHT_MOVES);
    before = g;
    CHECK(perft_unchecked(&g, 4) == perft(&g, 4));
    CHECK(memcmp(&g, &before, sizeof before) == 0);

    /* a tree with winning moves in it */
    qr_game_init(&g);
    g.pawn[0] = sq("e8");
    g.pawn[1] = sq("e2");
    before = g;
    CHECK(perft_unchecked(&g, 3) == perft(&g, 3));
    CHECK(memcmp(&g, &before, sizeof before) == 0);
}

int main(void)
{
    test_depth_zero();
    test_start_position();
    test_jump_position();
    test_walled_position();
    test_crowded_positions();
    test_won_lines_end_early();
    test_leaves_game_unchanged();
    test_unchecked();

    return check_report();
}
