/*
 * test_ai.c - tests for the Quoridor AI. Same conventions as
 * test_quoridor.c: no framework, failures are printed and the process exits
 * nonzero.
 */
#include <stdio.h>
#include <string.h>

#include "ai.h"

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

#define MAX_PLIES 400

static qr_pos sq(const char *s)
{
    qr_pos p;
    p.col = s[0] - 'a';
    p.row = s[1] - '1';
    return p;
}

static int same_move(const qr_move *a, const qr_move *b)
{
    return a->type == b->type && a->orient == b->orient &&
           a->pos.col == b->pos.col && a->pos.row == b->pos.row;
}

/* Lets the AI play both sides from g for at most max_plies, recording the
 * moves. Returns the number of plies played, or -1 if the AI failed to
 * produce a legal move while the game was still in progress. */
static int self_play(qr_ai *ai, qr_game *g, int max_plies, qr_move *record)
{
    qr_move m;
    int ply = 0;

    while (g->winner < 0 && ply < max_plies) {
        if (qr_ai_choose_move(ai, g, &m) != 0)
            return -1;
        if (qr_check_move(g, &m) != QR_OK)
            return -1;
        qr_apply_move(g, &m);
        record[ply++] = m;
    }
    return ply;
}

static void test_init(void)
{
    qr_ai ai;

    qr_ai_init(&ai, 42UL);
    CHECK(ai.depth == QR_AI_DEFAULT_DEPTH);
    CHECK(qr_atomic_load(&ai.stop) == 0);
}

static void test_evaluate(void)
{
    qr_game g;
    qr_move m;

    qr_game_init(&g);
    CHECK(qr_ai_evaluate(&g, 0) == 0);
    CHECK(qr_ai_evaluate(&g, 1) == 0);

    /* Player 0 one step ahead. */
    CHECK(qr_move_from_str("e2", &m) == 0);
    CHECK(qr_apply_move(&g, &m) == QR_OK);
    CHECK(qr_ai_evaluate(&g, 0) == 1);
    CHECK(qr_ai_evaluate(&g, 1) == -1);

    /* A wall above a2/b2 sends player 0 round by the c file (9 steps
     * instead of 7) and leaves player 1's 8 steps down the e file alone. */
    qr_game_init(&g);
    g.pawn[0] = sq("a2");
    CHECK(qr_ai_evaluate(&g, 0) == 1);
    g.walls[0][1] = QR_WALL_H;              /* a2h */
    CHECK(qr_ai_evaluate(&g, 0) == -1);
    CHECK(qr_ai_evaluate(&g, 1) == 1);

    /* A won game dwarfs any path difference. */
    qr_game_init(&g);
    g.pawn[0] = sq("c8");
    CHECK(qr_move_from_str("c9", &m) == 0);
    CHECK(qr_apply_move(&g, &m) == QR_OK);
    CHECK(g.winner == 0);
    CHECK(qr_ai_evaluate(&g, 0) > 100);
    CHECK(qr_ai_evaluate(&g, 1) < -100);
}

static void test_takes_winning_move(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m;
    int depth;

    for (depth = 1; depth <= 2; depth++) {
        qr_ai_init(&ai, 1UL);
        ai.depth = depth;

        qr_game_init(&g);
        g.pawn[0] = sq("c8");
        CHECK(qr_ai_choose_move(&ai, &g, &m) == 0);
        CHECK(m.type == QR_MOVE_PAWN && m.pos.col == 2 && m.pos.row == 8);

        qr_game_init(&g);
        g.pawn[1] = sq("g2");
        g.to_move = 1;
        CHECK(qr_ai_choose_move(&ai, &g, &m) == 0);
        CHECK(m.type == QR_MOVE_PAWN && m.pos.col == 6 && m.pos.row == 0);
    }
}

/* Looking two plies ahead, the AI must see that the opponent is about to
 * win and wall it off rather than advance its own pawn. */
static void test_blocks_imminent_loss(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m;

    qr_game_init(&g);
    g.pawn[0] = sq("a3");
    g.pawn[1] = sq("e2");   /* one step from rank 1 */
    CHECK(qr_shortest_path(&g, 1) == 1);

    qr_ai_init(&ai, 7UL);
    CHECK(qr_ai_choose_move(&ai, &g, &m) == 0);
    CHECK(m.type == QR_MOVE_WALL);
    CHECK(qr_apply_move(&g, &m) == QR_OK);
    CHECK(qr_shortest_path(&g, 1) > 1);
}

static void test_no_move_when_game_over(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m, untouched;

    qr_game_init(&g);
    g.winner = 1;
    qr_ai_init(&ai, 1UL);
    memset(&m, 0x5A, sizeof m);
    untouched = m;
    CHECK(qr_ai_choose_move(&ai, &g, &m) == -1);
    CHECK(memcmp(&m, &untouched, sizeof m) == 0);
}

static void test_stop(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m;

    qr_game_init(&g);
    qr_ai_init(&ai, 1UL);

    /* A stop requested before the search starts is honoured... */
    qr_ai_stop(&ai);
    CHECK(qr_atomic_load(&ai.stop) != 0);
    CHECK(qr_ai_choose_move(&ai, &g, &m) == -1);
    /* ...and stays in force until cleared. */
    CHECK(qr_ai_choose_move(&ai, &g, &m) == -1);
    qr_ai_clear_stop(&ai);
    CHECK(qr_atomic_load(&ai.stop) == 0);
    CHECK(qr_ai_choose_move(&ai, &g, &m) == 0);
    CHECK(qr_check_move(&g, &m) == QR_OK);
}

/* Whole games at depth 1 (fast): every move legal, and the game ends. */
static void test_self_play_finishes(void)
{
    static qr_move record[MAX_PLIES];
    qr_game g;
    qr_ai ai;
    unsigned long seed;
    int plies;

    for (seed = 1; seed <= 5; seed++) {
        qr_game_init(&g);
        qr_ai_init(&ai, seed);
        ai.depth = 1;
        plies = self_play(&ai, &g, MAX_PLIES, record);
        CHECK(plies > 0);
        CHECK(g.winner >= 0);
    }
}

/* One whole game at the default depth, which also gets walls placed. */
static void test_default_depth_game(void)
{
    static qr_move record[MAX_PLIES];
    qr_game g;
    qr_ai ai;

    qr_game_init(&g);
    qr_ai_init(&ai, 3UL);
    CHECK(self_play(&ai, &g, MAX_PLIES, record) > 0);
    CHECK(g.winner >= 0);
    CHECK(g.walls_left[0] < QR_WALLS_PER_PLAYER ||
          g.walls_left[1] < QR_WALLS_PER_PLAYER);
}

static void test_same_seed_same_game(void)
{
    static qr_move a[MAX_PLIES], b[MAX_PLIES];
    qr_game g;
    qr_ai ai;
    int na, nb, i;

    qr_game_init(&g);
    qr_ai_init(&ai, 1234UL);
    ai.depth = 1;
    na = self_play(&ai, &g, MAX_PLIES, a);

    qr_game_init(&g);
    qr_ai_init(&ai, 1234UL);
    ai.depth = 1;
    nb = self_play(&ai, &g, MAX_PLIES, b);

    CHECK(na > 0 && na == nb);
    for (i = 0; i < na && i < nb; i++)
        CHECK(same_move(&a[i], &b[i]));
}

int main(void)
{
    test_init();
    test_evaluate();
    test_takes_winning_move();
    test_blocks_imminent_loss();
    test_no_move_when_game_over();
    test_stop();
    test_self_play_finishes();
    test_default_depth_game();
    test_same_seed_same_game();

    if (failures) {
        fprintf(stderr, "%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("all %d checks passed\n", checks);
    return 0;
}
