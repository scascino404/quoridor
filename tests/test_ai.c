/*
 * test_ai.c - tests for the Quoridor AI.
 */
#include <string.h>
#include <time.h>

#include "ai.h"
#include "check.h"

#define MAX_PLIES 400

static int same_move(const qr_move *a, const qr_move *b)
{
    return a->type == b->type && a->orient == b->orient &&
           a->pos.col == b->pos.col && a->pos.row == b->pos.row;
}

/* An AI that searches a fixed depth on one thread, with no time budget:
 * deterministic, and fast at small depths. */
static void init_fixed(qr_ai *ai, unsigned long seed, int depth)
{
    qr_ai_init(ai, seed);
    ai->time_ms = 0;
    ai->threads = 1;
    ai->depth = depth;
}

/* Lets the AI play both sides from g for at most MAX_PLIES, recording the
 * moves in `record` unless it is NULL. Returns the number of plies played,
 * or -1 if the AI failed to produce a legal move while the game was still
 * in progress. */
static int self_play(qr_ai *ai, qr_game *g, qr_move record[MAX_PLIES])
{
    qr_move m;
    int ply = 0;

    while (g->winner < 0 && ply < MAX_PLIES) {
        if (qr_ai_choose_move(ai, g, &m) != 0 ||
            qr_apply_move(g, &m) != QR_OK)
            return -1;
        if (record != NULL)
            record[ply] = m;
        ply++;
    }
    return ply;
}

static void test_init(void)
{
    qr_ai ai;

    qr_ai_init(&ai, 42UL);
    CHECK(ai.time_ms == QR_AI_DEFAULT_TIME_MS);
    CHECK(ai.threads == QR_AI_DEFAULT_THREADS);
    CHECK(ai.depth == 0);
    CHECK(qr_atomic_load(&ai.stop) == 0);
}

static void test_evaluate(void)
{
    qr_game g;
    qr_move m;
    int before;

    /* Even, apart from the bonus for the player to move. */
    qr_game_init(&g);
    CHECK(qr_ai_evaluate(&g, 0) > 0);
    CHECK(qr_ai_evaluate(&g, 0) == -qr_ai_evaluate(&g, 1));

    /* Player 0 one step ahead outweighs player 1 being to move. */
    CHECK(qr_move_from_str("e2", &m) == 0);
    CHECK(qr_apply_move(&g, &m) == QR_OK);
    CHECK(qr_ai_evaluate(&g, 0) > 0);
    CHECK(qr_ai_evaluate(&g, 0) == -qr_ai_evaluate(&g, 1));

    /* A wall above a2/b2 sends player 0 round by the c file (9 steps
     * instead of 7) and leaves player 1's 8 steps down the e file alone. */
    qr_game_init(&g);
    g.pawn[0] = sq("a2");
    CHECK(qr_ai_evaluate(&g, 0) > 0);
    g.walls[0][1] = QR_WALL_H;              /* a2h */
    qr_game_sync(&g);
    CHECK(qr_ai_evaluate(&g, 0) < 0);
    CHECK(qr_ai_evaluate(&g, 1) > 0);

    /* Walls in hand count. */
    qr_game_init(&g);
    before = qr_ai_evaluate(&g, 0);
    g.walls_left[1] = QR_WALLS_PER_PLAYER - 1;
    CHECK(qr_ai_evaluate(&g, 0) > before);

    /* Moving first on an equal path against a player with no walls left
     * wins the race: nearly as good as a win. */
    qr_game_init(&g);
    g.walls_left[1] = 0;
    CHECK(qr_ai_evaluate(&g, 0) > 1000);
    CHECK(qr_ai_evaluate(&g, 1) < -1000);

    /* A won game dwarfs everything. */
    qr_game_init(&g);
    g.pawn[0] = sq("c8");
    CHECK(qr_move_from_str("c9", &m) == 0);
    CHECK(qr_apply_move(&g, &m) == QR_OK);
    CHECK(g.winner == 0);
    CHECK(qr_ai_evaluate(&g, 0) > 5000);
    CHECK(qr_ai_evaluate(&g, 1) < -5000);
}

static void test_takes_winning_move(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m;
    int depth;

    for (depth = 1; depth <= 3; depth++) {
        init_fixed(&ai, 1UL, depth);

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

/* The AI must see that the opponent is about to win and wall it off rather
 * than advance its own pawn. */
static void test_blocks_imminent_loss(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m;

    qr_game_init(&g);
    g.pawn[0] = sq("a3");
    g.pawn[1] = sq("e2");   /* one step from rank 1 */
    CHECK(qr_shortest_path(&g, 1) == 1);

    init_fixed(&ai, 7UL, 3);
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
    init_fixed(&ai, 1UL, 2);
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
    init_fixed(&ai, 1UL, 2);

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

/* Whole games at small depths (fast): every move legal, and the game
 * ends. */
static void test_self_play_finishes(void)
{
    qr_game g;
    qr_ai ai;
    unsigned long seed;
    int plies;

    for (seed = 1; seed <= 6; seed++) {
        qr_game_init(&g);
        init_fixed(&ai, seed, 1 + (int)(seed % 3));
        plies = self_play(&ai, &g, NULL);
        CHECK(plies > 0);
        CHECK(g.winner >= 0);
    }
}

/* One whole game on the clock with several threads, which also gets walls
 * placed; each move keeps roughly to its budget. */
static void test_timed_game(void)
{
    qr_game g;
    qr_ai ai;
    qr_move m;
    time_t t0;
    int plies;

    qr_game_init(&g);
    qr_ai_init(&ai, 3UL);
    ai.time_ms = 20;
    plies = self_play(&ai, &g, NULL);
    CHECK(plies > 0);
    CHECK(g.winner >= 0);
    CHECK(g.walls_left[0] < QR_WALLS_PER_PLAYER ||
          g.walls_left[1] < QR_WALLS_PER_PLAYER);

    /* 300 ms from the start, where the search would go on for long. */
    qr_game_init(&g);
    ai.time_ms = 300;
    t0 = time(NULL);
    CHECK(qr_ai_choose_move(&ai, &g, &m) == 0);
    CHECK(qr_check_move(&g, &m) == QR_OK);
    CHECK(difftime(time(NULL), t0) <= 2.0);
}

static void test_same_seed_same_game(void)
{
    static qr_move a[MAX_PLIES], b[MAX_PLIES];
    qr_game g;
    qr_ai ai;
    int na, nb, i;

    qr_game_init(&g);
    init_fixed(&ai, 1234UL, 3);
    na = self_play(&ai, &g, a);

    qr_game_init(&g);
    init_fixed(&ai, 1234UL, 3);
    nb = self_play(&ai, &g, b);

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
    test_timed_game();
    test_same_seed_same_game();

    return check_report();
}
