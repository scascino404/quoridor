/*
 * test_quoridor.c - rule tests for the Quoridor library. No framework:
 * each CHECK failure is printed and the process exits nonzero.
 */
#include <stdio.h>
#include <string.h>

#include "quoridor.h"

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

static qr_move mv(const char *s)
{
    qr_move m;
    if (qr_move_from_str(s, &m) != 0) {
        fprintf(stderr, "bad move string in test: %s\n", s);
        failures++;
        m.type = QR_MOVE_PAWN;
        m.pos.col = -1;
        m.pos.row = -1;
        m.orient = QR_WALL_NONE;
    }
    return m;
}

static qr_status play(qr_game *g, const char *s)
{
    qr_move m = mv(s);
    return qr_apply_move(g, &m);
}

static qr_status check(const qr_game *g, const char *s)
{
    qr_move m = mv(s);
    return qr_check_move(g, &m);
}

static qr_pos sq(const char *s)
{
    qr_pos p;
    p.col = s[0] - 'a';
    p.row = s[1] - '1';
    return p;
}

static int count_pawn_moves(const qr_game *g)
{
    qr_pos out[QR_MAX_PAWN_MOVES];
    return qr_pawn_moves(g, out);
}

static void test_init(void)
{
    qr_game g;
    qr_move moves[QR_MAX_MOVES];

    qr_game_init(&g);
    CHECK(g.pawn[0].col == 4 && g.pawn[0].row == 0);
    CHECK(g.pawn[1].col == 4 && g.pawn[1].row == 8);
    CHECK(g.walls_left[0] == 10 && g.walls_left[1] == 10);
    CHECK(g.to_move == 0);
    CHECK(g.winner == -1);
    CHECK(qr_shortest_path(&g, 0) == 8);
    CHECK(qr_shortest_path(&g, 1) == 8);
    /* e2, d1, f1 + all 128 wall slots */
    CHECK(qr_legal_moves(&g, moves) == 3 + 128);
}

static void test_basic_pawn_moves(void)
{
    qr_game g;

    qr_game_init(&g);
    CHECK(count_pawn_moves(&g) == 3);
    CHECK(check(&g, "e2") == QR_OK);
    CHECK(check(&g, "d1") == QR_OK);
    CHECK(check(&g, "f1") == QR_OK);
    CHECK(check(&g, "e1") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "e3") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "d2") == QR_ERR_BAD_PAWN_MOVE);

    CHECK(play(&g, "e2") == QR_OK);
    CHECK(g.to_move == 1);
    CHECK(g.pawn[0].row == 1);
    CHECK(play(&g, "e8") == QR_OK);
    CHECK(g.to_move == 0);
}

static void test_walls_block_steps(void)
{
    qr_game g;

    qr_game_init(&g);
    CHECK(play(&g, "e1h") == QR_OK);        /* above e1/f1 */
    CHECK(g.walls_left[0] == 9);
    CHECK(qr_is_blocked(&g, sq("e1"), sq("e2")));
    CHECK(qr_is_blocked(&g, sq("f1"), sq("f2")));
    CHECK(!qr_is_blocked(&g, sq("d1"), sq("d2")));
    CHECK(!qr_is_blocked(&g, sq("g1"), sq("g2")));
    CHECK(play(&g, "d8v") == QR_OK);        /* between d/e on ranks 8-9 */
    CHECK(g.walls_left[1] == 9);
    CHECK(qr_is_blocked(&g, sq("d8"), sq("e8")));
    CHECK(qr_is_blocked(&g, sq("d9"), sq("e9")));
    CHECK(!qr_is_blocked(&g, sq("d7"), sq("e7")));

    CHECK(check(&g, "e2") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "d1") == QR_OK);
    CHECK(count_pawn_moves(&g) == 2);

    /* Non-adjacent and off-board steps are "blocked". */
    CHECK(qr_is_blocked(&g, sq("a1"), sq("a3")));
    CHECK(qr_is_blocked(&g, sq("a1"), sq("b2")));
}

static void test_wall_placement_rules(void)
{
    qr_game g;
    qr_move m;

    qr_game_init(&g);
    CHECK(play(&g, "e1h") == QR_OK);
    CHECK(check(&g, "e1h") == QR_ERR_WALL_OVERLAP);
    CHECK(check(&g, "d1h") == QR_ERR_WALL_OVERLAP);
    CHECK(check(&g, "f1h") == QR_ERR_WALL_OVERLAP);
    CHECK(check(&g, "e1v") == QR_ERR_WALL_CROSSES);
    CHECK(check(&g, "c1h") == QR_OK);       /* touches end to end */
    CHECK(check(&g, "g1h") == QR_OK);
    CHECK(check(&g, "e2h") == QR_OK);       /* parallel, one row up */
    CHECK(check(&g, "d1v") == QR_OK);       /* T-junction */
    CHECK(check(&g, "f1v") == QR_OK);

    CHECK(play(&g, "a3v") == QR_OK);
    CHECK(check(&g, "a2v") == QR_ERR_WALL_OVERLAP);
    CHECK(check(&g, "a4v") == QR_ERR_WALL_OVERLAP);
    CHECK(check(&g, "a5v") == QR_OK);

    m.type = QR_MOVE_WALL;
    m.orient = QR_WALL_H;
    m.pos.col = 8;
    m.pos.row = 0;
    CHECK(qr_check_move(&g, &m) == QR_ERR_WALL_OUT_OF_BOUNDS);
    m.pos.col = 0;
    m.pos.row = -1;
    CHECK(qr_check_move(&g, &m) == QR_ERR_WALL_OUT_OF_BOUNDS);
    m.pos.row = 0;
    m.orient = QR_WALL_NONE;
    CHECK(qr_check_move(&g, &m) == QR_ERR_WALL_OUT_OF_BOUNDS);
}

static void test_wall_cannot_block_path(void)
{
    qr_game g;

    qr_game_init(&g);
    /* Box player 0 into {d1, e1}: roof over d1/e1, walls left and right. */
    CHECK(play(&g, "d1h") == QR_OK);
    CHECK(play(&g, "c1v") == QR_OK);
    CHECK(check(&g, "e1v") == QR_ERR_WALL_BLOCKS_PATH);
    CHECK(check(&g, "e2v") == QR_OK);       /* leaves f1 -> f2 open */
    CHECK(qr_shortest_path(&g, 0) == 9);    /* e1 -> f1 -> f2 ... f9 */
}

static void test_shortest_path(void)
{
    qr_game g;

    qr_game_init(&g);
    CHECK(play(&g, "d1h") == QR_OK);
    /* e1 -> f1 -> f2 ... f9: 1 + 8 = 9 steps */
    CHECK(qr_shortest_path(&g, 0) == 9);
    /* e9 ... e2 -> f2 -> f1: the same wall costs player 1 a step too */
    CHECK(qr_shortest_path(&g, 1) == 9);
}

static void test_straight_jump(void)
{
    qr_game g;

    qr_game_init(&g);
    g.pawn[0] = sq("e4");
    g.pawn[1] = sq("e5");
    CHECK(count_pawn_moves(&g) == 4);
    CHECK(check(&g, "e6") == QR_OK);
    CHECK(check(&g, "e5") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "d5") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "e3") == QR_OK);

    /* A wall between the pawns prevents the jump entirely. */
    g.walls[4][3] = QR_WALL_H;              /* e4h: between e4/e5 and f4/f5 */
    qr_game_sync(&g);
    CHECK(check(&g, "e6") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "e5") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(count_pawn_moves(&g) == 3);
}

static void test_diagonal_jump_wall_behind(void)
{
    qr_game g;

    qr_game_init(&g);
    g.pawn[0] = sq("e4");
    g.pawn[1] = sq("e5");
    g.walls[4][4] = QR_WALL_H;              /* e5h: behind the opponent */
    qr_game_sync(&g);
    CHECK(check(&g, "e6") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "d5") == QR_OK);
    CHECK(check(&g, "f5") == QR_OK);
    CHECK(count_pawn_moves(&g) == 5);       /* d5 f5 d4 f4 e3 */

    /* Block one diagonal with a vertical wall between d5 and e5. */
    g.walls[3][4] = QR_WALL_V;              /* d5v: between d/e on ranks 5-6 */
    qr_game_sync(&g);
    CHECK(check(&g, "d5") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(check(&g, "f5") == QR_OK);
    CHECK(count_pawn_moves(&g) == 4);
}

static void test_diagonal_jump_edge_behind(void)
{
    qr_game g;

    qr_game_init(&g);
    g.pawn[0] = sq("e8");
    g.pawn[1] = sq("e9");
    CHECK(check(&g, "d9") == QR_OK);
    CHECK(check(&g, "f9") == QR_OK);
    CHECK(count_pawn_moves(&g) == 5);       /* d9 f9 d8 f8 e7 */

    /* Same from player 1's side, at the board corner. */
    g.pawn[0] = sq("a1");
    g.pawn[1] = sq("a2");
    g.to_move = 1;
    CHECK(check(&g, "b1") == QR_OK);
    CHECK(check(&g, "a3") == QR_OK);
    CHECK(check(&g, "b2") == QR_OK);
    CHECK(count_pawn_moves(&g) == 3);
}

static void test_win(void)
{
    qr_game g, before;
    qr_move moves[QR_MAX_MOVES];

    qr_game_init(&g);
    g.pawn[0] = sq("e8");
    g.pawn[1] = sq("a1");
    CHECK(play(&g, "e9") == QR_OK);
    CHECK(g.winner == 0);
    before = g;
    CHECK(play(&g, "a2") == QR_ERR_GAME_OVER);
    CHECK(play(&g, "a5h") == QR_ERR_GAME_OVER);
    CHECK(memcmp(&before, &g, sizeof g) == 0);
    CHECK(qr_legal_moves(&g, moves) == 0);

    qr_game_init(&g);
    g.pawn[1] = sq("c2");
    g.to_move = 1;
    CHECK(play(&g, "c1") == QR_OK);
    CHECK(g.winner == 1);
}

static void test_no_walls_left(void)
{
    qr_game g;
    qr_move moves[QR_MAX_MOVES];

    qr_game_init(&g);
    g.walls_left[0] = 0;
    CHECK(check(&g, "a1h") == QR_ERR_NO_WALLS_LEFT);
    CHECK(qr_legal_moves(&g, moves) == 3);
    CHECK(check(&g, "e2") == QR_OK);
}

static void test_failed_apply_leaves_state(void)
{
    qr_game g, before;

    qr_game_init(&g);
    CHECK(play(&g, "e1h") == QR_OK);
    before = g;
    CHECK(play(&g, "e1v") == QR_ERR_WALL_CROSSES);
    CHECK(play(&g, "e7") == QR_ERR_BAD_PAWN_MOVE);
    CHECK(memcmp(&before, &g, sizeof g) == 0);

    /* a wall turned down for blocking a path, of either orientation */
    qr_game_init(&g);
    CHECK(play(&g, "d1h") == QR_OK);
    CHECK(play(&g, "c1v") == QR_OK);
    before = g;
    CHECK(play(&g, "e1v") == QR_ERR_WALL_BLOCKS_PATH);
    CHECK(memcmp(&before, &g, sizeof g) == 0);

    qr_game_init(&g);
    CHECK(play(&g, "d1v") == QR_OK);
    CHECK(play(&g, "e1v") == QR_OK);
    before = g;
    CHECK(play(&g, "e2h") == QR_ERR_WALL_BLOCKS_PATH);
    CHECK(memcmp(&before, &g, sizeof g) == 0);
}

/* Walls written into the struct by hand count once qr_game_sync has run. */
static void test_sync(void)
{
    qr_game g, played;

    /* the same position by legal moves and by hand */
    qr_game_init(&played);
    CHECK(play(&played, "e3h") == QR_OK);
    CHECK(play(&played, "c7v") == QR_OK);

    qr_game_init(&g);
    g.walls[4][2] = QR_WALL_H;              /* e3h */
    g.walls[2][6] = QR_WALL_V;              /* c7v */
    g.walls_left[0] = g.walls_left[1] = 9;
    CHECK(memcmp(&g, &played, sizeof g) != 0);
    qr_game_sync(&g);
    CHECK(memcmp(&g, &played, sizeof g) == 0);

    /* taking a wall away by hand */
    g.walls[4][2] = QR_WALL_NONE;
    qr_game_sync(&g);
    CHECK(!qr_is_blocked(&g, sq("e3"), sq("e4")));
    CHECK(qr_is_blocked(&g, sq("c7"), sq("d7")));
    CHECK(check(&g, "e3h") == QR_OK);
    CHECK(check(&g, "c7v") == QR_ERR_WALL_OVERLAP);

    /* syncing a game that is in step changes nothing */
    played = g;
    qr_game_sync(&g);
    CHECK(memcmp(&g, &played, sizeof g) == 0);
}

/* Applies s and undoes it again; g must come back byte for byte. */
static void check_undo(qr_game *g, const char *s)
{
    qr_game before = *g;
    qr_move m = mv(s);
    qr_pos from = g->pawn[g->to_move];

    CHECK(qr_apply_move(g, &m) == QR_OK);
    CHECK(memcmp(g, &before, sizeof before) != 0);
    qr_undo_move(g, &m, from);
    CHECK(memcmp(g, &before, sizeof before) == 0);
}

static void test_undo(void)
{
    qr_game g;

    qr_game_init(&g);
    check_undo(&g, "e2");                /* step */
    check_undo(&g, "e3h");               /* wall */
    CHECK(play(&g, "e2") == QR_OK);
    check_undo(&g, "c7v");               /* wall by player 1 */
    check_undo(&g, "e8");                /* step by player 1 */

    /* straight jump */
    qr_game_init(&g);
    g.pawn[0] = sq("e5");
    g.pawn[1] = sq("e6");
    check_undo(&g, "e7");

    /* diagonal jump: wall behind the jumped pawn */
    CHECK(play(&g, "a1h") == QR_OK);
    CHECK(play(&g, "e6h") == QR_OK);
    g.to_move = 0;
    check_undo(&g, "d6");

    /* winning move: the winner is cleared again */
    qr_game_init(&g);
    g.pawn[0] = sq("e8");
    g.pawn[1] = sq("a5");
    check_undo(&g, "e9");
    CHECK(g.winner == -1);

    /* undoing several moves in reverse order */
    {
        qr_game start;
        qr_move m1 = mv("e2"), m2 = mv("d8v"), m3 = mv("e3");
        qr_pos f1, f2, f3;

        qr_game_init(&g);
        start = g;
        f1 = g.pawn[g.to_move];
        CHECK(qr_apply_move(&g, &m1) == QR_OK);
        f2 = g.pawn[g.to_move];
        CHECK(qr_apply_move(&g, &m2) == QR_OK);
        f3 = g.pawn[g.to_move];
        CHECK(qr_apply_move(&g, &m3) == QR_OK);
        qr_undo_move(&g, &m3, f3);
        qr_undo_move(&g, &m2, f2);
        qr_undo_move(&g, &m1, f1);
        CHECK(memcmp(&g, &start, sizeof start) == 0);
    }
}

static void test_legal_moves_agree_with_check(void)
{
    qr_game g;
    qr_move moves[QR_MAX_MOVES];
    int n, i;

    qr_game_init(&g);
    CHECK(play(&g, "e2") == QR_OK);
    CHECK(play(&g, "e5h") == QR_OK);
    CHECK(play(&g, "d4v") == QR_OK);
    n = qr_legal_moves(&g, moves);
    CHECK(n > 0);
    for (i = 0; i < n; i++)
        CHECK(qr_check_move(&g, &moves[i]) == QR_OK);
    /* 128 slots minus the 2 occupied, 2 crossing, 4 overlapping */
    CHECK(n == count_pawn_moves(&g) + 128 - 2 - 2 - 4);
}

static void test_notation(void)
{
    qr_game g;
    qr_move moves[QR_MAX_MOVES], back;
    char buf[4];
    int n, i;

    CHECK(qr_move_from_str("e2", &back) == 0);
    CHECK(back.type == QR_MOVE_PAWN && back.pos.col == 4 && back.pos.row == 1);
    CHECK(qr_move_from_str("E3H", &back) == 0);
    CHECK(back.type == QR_MOVE_WALL && back.pos.col == 4 && back.pos.row == 2 &&
          back.orient == QR_WALL_H);
    CHECK(qr_move_from_str("a8v", &back) == 0);
    CHECK(back.type == QR_MOVE_WALL && back.orient == QR_WALL_V);
    CHECK(qr_move_from_str("i9", &back) == 0);

    CHECK(qr_move_from_str("", &back) == -1);
    CHECK(qr_move_from_str("e", &back) == -1);
    CHECK(qr_move_from_str("j1", &back) == -1);
    CHECK(qr_move_from_str("e0", &back) == -1);
    CHECK(qr_move_from_str("e10", &back) == -1);
    CHECK(qr_move_from_str("e3x", &back) == -1);
    CHECK(qr_move_from_str("i3h", &back) == -1);
    CHECK(qr_move_from_str("e9v", &back) == -1);
    CHECK(qr_move_from_str("e3hh", &back) == -1);

    qr_game_init(&g);
    n = qr_legal_moves(&g, moves);
    for (i = 0; i < n; i++) {
        CHECK(qr_move_to_str(&moves[i], buf) == 0);
        CHECK(qr_move_from_str(buf, &back) == 0);
        CHECK(back.type == moves[i].type && back.orient == moves[i].orient &&
              back.pos.col == moves[i].pos.col && back.pos.row == moves[i].pos.row);
    }
    CHECK(qr_move_to_str(&moves[0], buf) == 0 && strcmp(buf, "e2") == 0);
}

int main(void)
{
    test_init();
    test_basic_pawn_moves();
    test_walls_block_steps();
    test_wall_placement_rules();
    test_wall_cannot_block_path();
    test_shortest_path();
    test_straight_jump();
    test_diagonal_jump_wall_behind();
    test_diagonal_jump_edge_behind();
    test_win();
    test_no_walls_left();
    test_failed_apply_leaves_state();
    test_sync();
    test_undo();
    test_legal_moves_agree_with_check();
    test_notation();

    if (failures) {
        fprintf(stderr, "%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("all %d checks passed\n", checks);
    return 0;
}
