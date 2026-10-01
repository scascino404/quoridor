/*
 * ai.c - iterative-deepening alpha-beta search (principal variation search)
 * with a transposition table, null-move pruning and several threads that
 * search the same tree together (Lazy SMP).
 *
 * Moves searched. At the root every legal move is tried. Inside the tree a
 * player's walls are restricted to those that block a step of one of the
 * opponent's shortest paths: no other wall can make that path longer.
 *
 * Evaluation. Mostly the difference of the two shortest paths (pawns are
 * ignored), weighing more as the paths get short, plus the walls each side
 * has left (the first ones worth most, and all of them more while the
 * paths are long) and a bonus for the side to move.
 * Each side's threat counts too: the most one wall can add to the other's
 * path, if that side has a wall left. A side that moves first and can no
 * longer be walled in wins the race if its path is no longer than the
 * opponent's; such races score near a win.
 *
 * Threats. Only walls that block every shortest path at some distance
 * from the pawn are measured (a wall blocks two steps at most), each with
 * one breadth-first search. A threat depends only on the walls and the
 * pawn, so threats are kept in a cache shared by all searches that is
 * never cleared.
 *
 * Paths. Distances are worked out a whole breadth-first layer at a time on
 * the per-column bit masks of qr_masks (see quoridor.h), which the search
 * reads directly. One ply above the horizon the children are not visited:
 * their values follow from path lengths measured in place (frontier).
 *
 * Threads. Every thread runs its own iterative deepening on its own copy of
 * the game; they share the transposition table, whose entries are written
 * and read whole and checked against the position's key. The calling
 * thread's result is the one returned.
 *
 * The table. One table serves every search in the process, so that what a
 * search learns carries over to the next move (worth about 80 Elo). It is
 * allocated on first use and never freed. Since entries are checked against
 * the position and every search evaluates alike, searches running at the
 * same time may share it. qr_ai_init clears it; the threat cache (16 MB)
 * needs no clearing.
 */
#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ai.h"

/* --- Constants --- */

#define WIN       10000     /* a won game; less the plies needed to win it */
#define INF       30000
#define MATE_BAND 9000      /* scores beyond this are wins or losses */
#define MAX_PLY   64

/* Evaluation weights, in hundredths of a step. They come from a logistic
 * regression of game results on 335k positions of 6,000 self-play games,
 * scaled so that one step of path difference is worth 100, and were then
 * checked in timed matches against the previous evaluation (branch
 * better-eval, reports/better-eval.md). The wall weights (EVAL_CURVE,
 * EVAL_WALLFAR) were then raised in timed matches against that version,
 * which spent its walls too early (branch walls-traps). */
#define EVAL_STEP    100    /* one step of shortest-path difference */
#define EVAL_REL     50     /* ten times the path difference over the
                             * total path length */
#define EVAL_WALL    180    /* a wall in hand */
#define EVAL_CURVE   20     /* tenths: less for each further wall in hand */
#define EVAL_WALLFAR 60     /* a wall in hand, more per step of the two
                             * paths' total length over 16 */
#define EVAL_TEMPO   10     /* being the side to move */
#define EVAL_RACE    5000   /* a race that can no longer be lost */
#define EVAL_THREAT  65     /* per step one wall of mine can add to the
                             * opponent's path, me to move */
#define EVAL_THREAT2 46     /* the same, the opponent to move */

#define NULL_MOVE_MIN_DEPTH 3
#define THREAT_CAP 4        /* threat assumed at most when pruning walls */

#define TT_BITS  22         /* 2^22 entries, 64 MB with 64-bit longs */
#define TC_BITS  20         /* 2^20 threat cache entries, 16 MB */

#define NSQ       (QR_BOARD_SIZE * QR_BOARD_SIZE)
#define NSLOT     (QR_WALL_GRID * QR_WALL_GRID)
#define NCODE     (NSQ + 2 * NSLOT)     /* move codes, see move_code */
#define ROW_BITS  ((1u << QR_BOARD_SIZE) - 1)
#define SLOT_BITS ((1u << QR_WALL_GRID) - 1)

#define TT_EXACT 1
#define TT_LOWER 2
#define TT_UPPER 3
#define NO_MOVE  0xFF

/* --- Paths --- */

/* Breadth-first layers towards a player's goal row, ignoring pawns:
 * sq[d][c] has bit r set if square (c, r) is d steps from the goal row. */
typedef struct {
    unsigned sq[NSQ + 1][QR_BOARD_SIZE];
    int      n;      /* layers computed */
    int      pawn;   /* distance of the player's pawn, -1 if unreachable */
} layers;

/* Wall slots: bit r of h[c] (v[c]) is the horizontal (vertical) wall
 * anchored at (c, r). */
typedef struct {
    unsigned h[QR_WALL_GRID], v[QR_WALL_GRID];
} wall_set;

/* Fills L until the player's pawn is reached and `extra` layers beyond it,
 * or until the board is exhausted. Returns the pawn's distance. */
static int goal_layers(const qr_game *g, int player, int extra, layers *L)
{
    const qr_masks *m = &g->masks;
    unsigned seen[QR_BOARD_SIZE], n, any;
    const unsigned *cur;
    unsigned *next;
    qr_pos p = g->pawn[player];
    int c, d, goal = qr_goal_row(player), last = -1;

    for (c = 0; c < QR_BOARD_SIZE; c++)
        L->sq[0][c] = seen[c] = 1u << goal;
    L->pawn = -1;
    if (p.row == goal) {
        L->pawn = 0;
        last = extra;
    }
    for (d = 1; d <= NSQ && (last < 0 || d <= last); d++) {
        cur = L->sq[d - 1];
        next = L->sq[d];
        any = 0;
        for (c = 0; c < QR_BOARD_SIZE; c++) {
            n = ((cur[c] & m->up[c]) << 1) | ((cur[c] >> 1) & m->up[c]);
            if (c > 0)
                n |= cur[c - 1] & m->side[c];
            if (c < QR_BOARD_SIZE - 1)
                n |= cur[c + 1] & m->side[c + 1];
            n &= ~seen[c] & ROW_BITS;
            seen[c] |= n;
            next[c] = n;
            any |= n;
        }
        if (!any)
            break;
        if (last < 0 && (next[p.col] >> p.row & 1)) {
            L->pawn = d;
            last = d + extra;
        }
    }
    L->n = d;
    return L->pawn;
}

/* Distance of square (c, r) in L, or -1 if it lies beyond the layers. */
static int layer_dist(const layers *L, int c, int r)
{
    int d;

    for (d = 0; d < L->n; d++)
        if (L->sq[d][c] >> r & 1)
            return d;
    return -1;
}

/* Sets `out` to the walls that block a step of some shortest path of the
 * player from its pawn (L from goal_layers for that player). Walls outside
 * this set cannot make the player's path longer. Walks the paths back from
 * the pawn a layer at a time. */
static void path_walls(const qr_game *g, int player, const layers *L,
                       wall_set *out)
{
    const qr_masks *m = &g->masks;
    unsigned s[QR_BOARD_SIZE], t[QR_BOARD_SIZE], up, down, e, any;
    const unsigned *next;
    qr_pos p = g->pawn[player];
    int c, d;

    for (c = 0; c < QR_WALL_GRID; c++)
        out->h[c] = out->v[c] = 0;
    if (L->pawn <= 0)
        return;
    for (c = 0; c < QR_BOARD_SIZE; c++)
        s[c] = 0;
    s[p.col] = 1u << p.row;
    for (d = L->pawn; d > 0; d--) {
        next = L->sq[d - 1];
        for (c = 0; c < QR_BOARD_SIZE; c++)
            t[c] = 0;
        for (c = 0; c < QR_BOARD_SIZE; c++) {
            if (!s[c])
                continue;
            /* Up from row r, or down onto row r: either way blocked by
             * H at (c, r) and (c - 1, r). */
            up = s[c] & m->up[c] & next[c] >> 1;
            down = s[c] >> 1 & m->up[c] & next[c];
            t[c] |= up << 1 | down;
            if (c < QR_WALL_GRID)
                out->h[c] |= up | down;
            if (c > 0)
                out->h[c - 1] |= up | down;
            /* Right from row r: blocked by V at (c, r) and (c, r - 1). */
            if (c < QR_BOARD_SIZE - 1) {
                e = s[c] & m->side[c + 1] & next[c + 1];
                t[c + 1] |= e;
                out->v[c] |= (e | e >> 1) & SLOT_BITS;
            }
            /* Left from row r: blocked by V at (c - 1, r) and (c - 1, r - 1). */
            if (c > 0) {
                e = s[c] & m->side[c] & next[c - 1];
                t[c - 1] |= e;
                out->v[c - 1] |= (e | e >> 1) & SLOT_BITS;
            }
        }
        any = 0;
        for (c = 0; c < QR_BOARD_SIZE; c++) {
            s[c] = t[c];
            any |= t[c];
        }
        if (!any)
            break;
    }
}

static int in_wall_set(const wall_set *s, const qr_move *m)
{
    return (m->orient == QR_WALL_H ? s->h : s->v)[m->pos.col] >> m->pos.row & 1;
}

/* Nonzero if a wall fits at (c, r): its slot is free and it neither
 * crosses nor overlaps another wall. Whether it closes a path is not
 * checked. */
static int wall_fits(const qr_game *g, qr_orient o, int c, int r)
{
    const qr_masks *m = &g->masks;

    if (g->walls[c][r] != QR_WALL_NONE)
        return 0;
    if (o == QR_WALL_H)
        return !((m->h[c] | m->h[c + 2]) >> r & 1);
    return !((m->v[c] << 1 | m->v[c] >> 1) >> r & 1);
}

/* Number of bits set in x, counted up to 3. */
static int bits3(unsigned x)
{
    int n = 0;

    while (x && n < 3) {
        x &= x - 1;
        n++;
    }
    return n;
}

/* Sets `out` to the walls that block every step of the player's shortest
 * paths from one distance to the next (L from goal_layers for that player).
 * Since a wall blocks two steps at most, such a step holds one or two; a
 * wall that makes the path longer is nearly always one of these. */
static void choke_walls(const qr_game *g, int player, const layers *L,
                        wall_set *out)
{
    const qr_masks *m = &g->masks;
    unsigned s[QR_BOARD_SIZE], t[QR_BOARD_SIZE], vs[QR_BOARD_SIZE], hs[QR_WALL_GRID];
    unsigned e, any;
    const unsigned *next;
    qr_pos p = g->pawn[player];
    int c, d, n, vc, hc, r;

    for (c = 0; c < QR_WALL_GRID; c++)
        out->h[c] = out->v[c] = 0;
    if (L->pawn <= 0)
        return;
    for (c = 0; c < QR_BOARD_SIZE; c++)
        s[c] = 0;
    s[p.col] = 1u << p.row;
    for (d = L->pawn; d > 0; d--) {
        next = L->sq[d - 1];
        /* vs[c] bit r: a step between rows r and r + 1 in column c;
         * hs[c] bit r: a step between columns c and c + 1 in row r. */
        for (c = 0; c < QR_BOARD_SIZE; c++)
            t[c] = vs[c] = 0;
        for (c = 0; c < QR_WALL_GRID; c++)
            hs[c] = 0;
        for (c = 0; c < QR_BOARD_SIZE; c++) {
            if (!s[c])
                continue;
            e = s[c] & m->up[c] & next[c] >> 1;
            t[c] |= e << 1;
            vs[c] |= e;
            e = s[c] >> 1 & m->up[c] & next[c];
            t[c] |= e;
            vs[c] |= e;
            if (c < QR_BOARD_SIZE - 1) {
                e = s[c] & m->side[c + 1] & next[c + 1];
                t[c + 1] |= e;
                hs[c] |= e;
            }
            if (c > 0) {
                e = s[c] & m->side[c] & next[c - 1];
                t[c - 1] |= e;
                hs[c - 1] |= e;
            }
        }
        n = 0;
        vc = hc = -1;
        any = 0;
        for (c = 0; c < QR_BOARD_SIZE && n < 3; c++) {
            if (vs[c]) {
                n += bits3(vs[c]);
                vc = vc < 0 ? c : vc;
            }
            if (c < QR_WALL_GRID && hs[c]) {
                n += bits3(hs[c]);
                hc = hc < 0 ? c : hc;
            }
        }
        if (n == 1 && vc >= 0) {
            for (r = 0; !(vs[vc] >> r & 1); r++)
                ;
            if (vc < QR_WALL_GRID)
                out->h[vc] |= 1u << r;
            if (vc > 0)
                out->h[vc - 1] |= 1u << r;
        } else if (n == 1) {
            out->v[hc] |= (hs[hc] | hs[hc] >> 1) & SLOT_BITS;
        } else if (n == 2 && vc >= 0 && hc < 0) {
            /* Two steps up or down: one wall if side by side. */
            if (vc < QR_WALL_GRID && vs[vc + 1] == vs[vc])
                out->h[vc] |= vs[vc];
        } else if (n == 2 && hc >= 0 && vc < 0) {
            /* Two steps across: one wall if one above the other. */
            e = hs[hc];
            if ((e & (e >> 1)) != 0)
                out->v[hc] |= e & e >> 1;
        }
        for (c = 0; c < QR_BOARD_SIZE; c++) {
            s[c] = t[c];
            any |= t[c];
        }
        if (!any)
            break;
    }
}

/* The most that one more wall, whoever places it, can add to the player's
 * path (L from goal_layers for that player). g is changed and restored. */
static int threat(qr_game *g, int player, const layers *L)
{
    wall_set cut;
    qr_move m;
    qr_pos from = g->pawn[g->to_move];
    unsigned bits;
    int c, r, d, best = 0;

    if (L->pawn <= 0)
        return 0;
    choke_walls(g, player, L, &cut);
    m.type = QR_MOVE_WALL;
    for (c = 0; c < QR_WALL_GRID; c++) {
        for (r = 0; r < QR_WALL_GRID; r++) {
            for (m.orient = QR_WALL_H; m.orient <= QR_WALL_V; m.orient++) {
                bits = m.orient == QR_WALL_H ? cut.h[c] : cut.v[c];
                if (!(bits >> r & 1) || !wall_fits(g, m.orient, c, r))
                    continue;
                m.pos.col = c;
                m.pos.row = r;
                qr_apply_unchecked(g, &m);
                d = qr_shortest_path(g, player);
                qr_undo_move(g, &m, from);
                if (d - L->pawn > best)
                    best = d - L->pawn;
            }
        }
    }
    return best;
}

/* --- Moves and keys --- */

/* Pawn moves are numbered by destination square, walls after them. */
static int move_code(const qr_move *m)
{
    if (m->type == QR_MOVE_PAWN)
        return m->pos.col * QR_BOARD_SIZE + m->pos.row;
    return NSQ + ((m->pos.col * QR_WALL_GRID + m->pos.row) << 1) +
           (m->orient == QR_WALL_V);
}

static void move_decode(int code, qr_move *m)
{
    if (code < NSQ) {
        m->type = QR_MOVE_PAWN;
        m->pos.col = code / QR_BOARD_SIZE;
        m->pos.row = code % QR_BOARD_SIZE;
        m->orient = QR_WALL_NONE;
    } else {
        code -= NSQ;
        m->type = QR_MOVE_WALL;
        m->orient = (code & 1) ? QR_WALL_V : QR_WALL_H;
        code >>= 1;
        m->pos.col = code / QR_WALL_GRID;
        m->pos.row = code % QR_WALL_GRID;
    }
}

/* A position's key is the XOR of a pseudo-random value for each of its
 * features (Zobrist hashing), in two 32-bit halves: `a` picks the table
 * entry and `b` checks it. The values are computed, not tabulated. */
typedef struct {
    unsigned long a, b;
} hkey;

#define KEY_PAWN(p, sq)  ((p) * NSQ + (sq))
#define KEY_WALL(code)   (2 * NSQ + (code) - NSQ)
#define KEY_WALLS_LEFT(p, n) \
    (2 * NSQ + 2 * NSLOT + (p) * (QR_WALLS_PER_PLAYER + 1) + (n))
#define KEY_SIDE         (2 * NSQ + 2 * NSLOT + 2 * (QR_WALLS_PER_PLAYER + 1))

static unsigned long mix32(unsigned long x)
{
    x &= 0xFFFFFFFFUL;
    x = ((x >> 16) ^ x) * 0x45D9F3BUL & 0xFFFFFFFFUL;
    x = ((x >> 16) ^ x) * 0x45D9F3BUL & 0xFFFFFFFFUL;
    return (x >> 16) ^ x;
}

static void key_toggle(hkey *k, int feature)
{
    unsigned long f = (unsigned long)feature;

    k->a ^= mix32(2 * f + 0x9E37UL);
    k->b ^= mix32(2 * f + 0x79B9UL + 1);
}

/* Key of the walls on the board alone. */
static hkey walls_key(const qr_game *g)
{
    hkey k;
    qr_move m;
    int c, r;

    k.a = k.b = 0;
    m.type = QR_MOVE_WALL;
    for (c = 0; c < QR_WALL_GRID; c++) {
        for (r = 0; r < QR_WALL_GRID; r++) {
            if (g->walls[c][r] == QR_WALL_NONE)
                continue;
            m.pos.col = c;
            m.pos.row = r;
            m.orient = g->walls[c][r];
            key_toggle(&k, KEY_WALL(move_code(&m)));
        }
    }
    return k;
}

static hkey key_of(const qr_game *g)
{
    hkey k = walls_key(g);
    int p;

    for (p = 0; p < QR_NUM_PLAYERS; p++) {
        key_toggle(&k, KEY_PAWN(p, g->pawn[p].col * QR_BOARD_SIZE + g->pawn[p].row));
        key_toggle(&k, KEY_WALLS_LEFT(p, g->walls_left[p]));
    }
    if (g->to_move)
        key_toggle(&k, KEY_SIDE);
    return k;
}

/* Updates k for m played in g (g as it was before the move). */
static void key_move(hkey *k, const qr_game *g, const qr_move *m)
{
    int p = g->to_move;

    if (m->type == QR_MOVE_PAWN) {
        key_toggle(k, KEY_PAWN(p, g->pawn[p].col * QR_BOARD_SIZE + g->pawn[p].row));
        key_toggle(k, KEY_PAWN(p, move_code(m)));
    } else {
        key_toggle(k, KEY_WALL(move_code(m)));
        key_toggle(k, KEY_WALLS_LEFT(p, g->walls_left[p]));
        key_toggle(k, KEY_WALLS_LEFT(p, g->walls_left[p] - 1));
    }
    key_toggle(k, KEY_SIDE);
}

/* --- Search state --- */

/* A table entry holds the check half of the key XORed with the data, so
 * that an entry torn by two threads writing at once fails the check. Data:
 * bits 0-7 best move code, 8-13 depth, 14-15 bound, 16-31 score + 32768. */
typedef struct {
    qr_atomic_ulong check, data;
} tt_entry;

#define TT_SIZE (1UL << TT_BITS)

#define TC_SIZE (1UL << TC_BITS)

/* The threat cache: a player's threat depends only on the walls and that
 * player's pawn, so entries never go stale and the cache is never cleared.
 * An entry holds the threat + 1 as data, checked like the table's. */
static tt_entry      *table, *threats;
static pthread_once_t table_once = PTHREAD_ONCE_INIT;

static void table_alloc(void)
{
    table = calloc(TT_SIZE, sizeof *table);
    threats = calloc(TC_SIZE, sizeof *threats);
}

typedef struct {
    tt_entry     *tt;
    unsigned long tt_mask;
    qr_atomic_int halt;      /* set once the search must end */
    qr_ai        *ai;        /* for its stop flag */
    double        deadline;  /* monotonic ms; 0 = none */
    int           max_depth;
} shared;

typedef struct {
    shared       *sh;
    qr_game       g;
    hkey          key;
    hkey          wkey;         /* key of the walls alone */
    int           id;
    int           after_null;   /* the last move was a null move */
    long          nodes;
    int           killers[MAX_PLY][2];
    int           history[QR_NUM_PLAYERS][NCODE];
    /* Main thread only: the result so far. */
    int           best_code, best_score;
} worker;

/* A move with its ordering score. */
typedef struct {
    qr_move m;
    int     code, score;
} smove;

static double now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static unsigned long rng_next(qr_ai *ai)
{
    ai->rng = (ai->rng * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (ai->rng >> 16) & 0x7FFFUL;
}

static int halted(const worker *w)
{
    return qr_atomic_load(&w->sh->halt);
}

/* Counts a node; every 256 nodes also checks the clock and the stop flag. */
static int count_node(worker *w)
{
    shared *sh = w->sh;

    if ((++w->nodes & 255) == 0 &&
        ((sh->deadline > 0 && now_ms() >= sh->deadline) ||
         qr_atomic_load(&sh->ai->stop)))
        qr_atomic_store(&sh->halt, 1);
    return halted(w);
}

/* --- Transposition table --- */

/* Win scores are stored relative to the node, not the root. */
static int score_to_tt(int s, int ply)
{
    return s > MATE_BAND ? s + ply : s < -MATE_BAND ? s - ply : s;
}

static int score_from_tt(int s, int ply)
{
    return s > MATE_BAND ? s - ply : s < -MATE_BAND ? s + ply : s;
}

static tt_entry *tt_slot(const worker *w)
{
    return &w->sh->tt[w->key.a & w->sh->tt_mask];
}

/* Returns nonzero and fills the fields if the position is in the table. */
static int tt_probe(const worker *w, int *code, int *depth, int *bound,
                    int *score)
{
    tt_entry *t = tt_slot(w);
    unsigned long data = qr_atomic_load_ulong(&t->data);

    if ((qr_atomic_load_ulong(&t->check) ^ data) != w->key.b)
        return 0;
    *code = (int)(data & 0xFF);
    *depth = (int)(data >> 8 & 0x3F);
    *bound = (int)(data >> 14 & 3);
    *score = (int)(data >> 16 & 0xFFFF) - 32768;
    return 1;
}

static void tt_store(const worker *w, int code, int depth, int bound, int score)
{
    tt_entry *t = tt_slot(w);
    unsigned long data = qr_atomic_load_ulong(&t->data);

    /* Keep a deeper entry for the same position, unless this one is exact. */
    if ((qr_atomic_load_ulong(&t->check) ^ data) == w->key.b &&
        (int)(data >> 8 & 0x3F) > depth && bound != TT_EXACT)
        return;
    data = (unsigned long)(code < 0 ? NO_MOVE : code) |
           (unsigned long)depth << 8 | (unsigned long)bound << 14 |
           ((unsigned long)(score + 32768) & 0xFFFF) << 16;
    qr_atomic_store_ulong(&t->data, data);
    qr_atomic_store_ulong(&t->check, data ^ w->key.b);
}

/* --- Evaluation --- */

/* Value of n walls in hand. */
static int wall_value(int n)
{
    return EVAL_WALL * n - EVAL_CURVE * n * n / 10;
}

/* Value for the side to move (me) from both path lengths and the walls in
 * hand. */
static int eval_terms(int dm, int dop, int wm, int wo)
{
    /* Moving first, I win a race the opponent cannot wall if dm <= dop,
     * and lose one I cannot wall if dop < dm. */
    if (wo == 0 && dm <= dop)
        return EVAL_RACE - EVAL_STEP * dm + 10 * dop;
    if (wm == 0 && dop < dm)
        return -EVAL_RACE + EVAL_STEP * dop - 10 * dm;
    return EVAL_STEP * (dop - dm) + EVAL_REL * 10 * (dop - dm) / (dop + dm + 1) +
           wall_value(wm) - wall_value(wo) +
           EVAL_WALLFAR * (wm - wo) * (dm + dop) / 16 + EVAL_TEMPO;
}

/* Threat terms for the side to move: tm is what one wall of mine can add
 * to the opponent's path, to what one of the opponent's can add to mine
 * (each 0 for a side without walls). */
static int threat_terms(int tm, int to)
{
    return EVAL_THREAT * tm - EVAL_THREAT2 * to;
}

/* threat, through the threat cache if w (for the key of its walls) is not
 * NULL. */
static int threat_of(qr_game *g, worker *w, int player)
{
    layers L;
    hkey k;
    tt_entry *e = NULL;
    unsigned long data;
    int v;

    if (w && threats) {
        k = w->wkey;
        key_toggle(&k, KEY_PAWN(player, g->pawn[player].col * QR_BOARD_SIZE +
                                        g->pawn[player].row));
        e = &threats[k.a & (TC_SIZE - 1)];
        data = qr_atomic_load_ulong(&e->data);
        if (data && (qr_atomic_load_ulong(&e->check) ^ data) == k.b)
            return (int)data - 1;
    }
    goal_layers(g, player, 0, &L);
    v = threat(g, player, &L);
    if (e) {
        data = (unsigned long)v + 1;
        qr_atomic_store_ulong(&e->data, data);
        qr_atomic_store_ulong(&e->check, data ^ k.b);
    }
    return v;
}

/* Static value of g for the side to move; w (or NULL) supplies a cache. */
static int evaluate(qr_game *g, worker *w)
{
    int me = g->to_move, op = 1 - me, wm = g->walls_left[me], wo = g->walls_left[op];

    return eval_terms(qr_shortest_path(g, me), qr_shortest_path(g, op), wm, wo) +
           threat_terms(wm > 0 ? threat_of(g, w, op) : 0,
                        wo > 0 ? threat_of(g, w, me) : 0);
}

int qr_ai_evaluate(const qr_game *g, int player)
{
    qr_game t = *g;
    int v;

    if (g->winner >= 0)
        return g->winner == player ? WIN : -WIN;
    v = evaluate(&t, NULL);
    return player == g->to_move ? v : -v;
}

/* --- Move generation and ordering --- */

/* The moves searched in the tree: pawn moves, and the walls that cut a
 * shortest path of the opponent (all legal walls if `all`). */
static int gen_moves(const qr_game *g, smove *out, int all)
{
    qr_move legal[QR_MAX_MOVES];
    layers L;
    wall_set cut;
    int n, i, k = 0, me = g->to_move;

    n = qr_legal_moves(g, legal);
    if (!all && g->walls_left[me] > 0) {
        goal_layers(g, 1 - me, 0, &L);
        path_walls(g, 1 - me, &L, &cut);
    }
    for (i = 0; i < n; i++) {
        if (!all && legal[i].type == QR_MOVE_WALL && !in_wall_set(&cut, &legal[i]))
            continue;
        out[k].m = legal[i];
        out[k].code = move_code(&legal[i]);
        out[k].score = 0;
        k++;
    }
    return k;
}

/* Ordering: the table's move, then the killers, then pawn moves that get
 * closer to the goal, then walls by history. */
static void score_moves(worker *w, smove *mv, int n, int tt_code, int ply)
{
    const qr_game *g = &w->g;
    int me = g->to_move, dm, d, i;
    layers L;
    smove *s;

    dm = goal_layers(g, me, 2, &L);
    for (i = 0; i < n; i++) {
        s = &mv[i];
        if (s->code == tt_code) {
            s->score = 1 << 30;
        } else if (s->code == w->killers[ply][0]) {
            s->score = 1 << 29;
        } else if (s->code == w->killers[ply][1]) {
            s->score = (1 << 29) - 1;
        } else if (s->m.type == QR_MOVE_PAWN) {
            /* Steps towards the goal first; others go after the walls. */
            d = layer_dist(&L, s->m.pos.col, s->m.pos.row);
            s->score = d < dm ? (1 << 20) + 1000 * (dm - d) : -(1 << 20) - d;
        } else {
            s->score = w->history[me][s->code] / 64 - 1000;
        }
    }
}

/* Swaps the best-ordered of mv[i..n) into mv[i]. */
static void pick(smove *mv, int n, int i)
{
    int j, b = i;
    smove t;

    for (j = i + 1; j < n; j++)
        if (mv[j].score > mv[b].score)
            b = j;
    t = mv[i];
    mv[i] = mv[b];
    mv[b] = t;
}

/* --- Search --- */

static void make(worker *w, const qr_move *m)
{
    key_move(&w->key, &w->g, m);
    if (m->type == QR_MOVE_WALL)
        key_toggle(&w->wkey, KEY_WALL(move_code(m)));
    qr_apply_unchecked(&w->g, m);
}

static void unmake(worker *w, const qr_move *m, qr_pos from, hkey key)
{
    qr_undo_move(&w->g, m, from);
    if (m->type == QR_MOVE_WALL)
        key_toggle(&w->wkey, KEY_WALL(move_code(m)));
    w->key = key;
}

/* A node one ply above the horizon. Every child would only be evaluated,
 * so the children's values are computed here from path lengths: pawn moves
 * from the mover's layers, walls by measuring the opponent's new path and,
 * only if the wall could still be the best move, the mover's. */
static int frontier(worker *w, int alpha, int beta, int ply)
{
    qr_move legal[QR_MAX_MOVES];
    qr_pos dest[QR_MAX_PAWN_MOVES];
    layers lm, lo;
    wall_set cut;
    qr_game *g = &w->g;
    const qr_move *m;
    int me = g->to_move, op = 1 - me;
    int wm = g->walls_left[me], wo = g->walls_left[op];
    int dm, dop, n, i, j, k, v, d1, d2, bound, t_me, t_op, pv[QR_MAX_PAWN_MOVES];
    int best = -INF, best_code = -1, alpha0 = alpha;
    qr_pos from = g->pawn[me];

    dm = goal_layers(g, me, 2, &lm);
    dop = wm > 0 ? goal_layers(g, op, 0, &lo) : qr_shortest_path(g, op);
    /* In the children the opponent is to move: t_me is what one of its
     * walls can add to my path, t_op what one of mine can add to its. */
    t_op = wm > 0 ? threat_of(g, w, op) : 0;
    n = qr_pawn_moves(g, dest);
    /* The threat against me can only lower a pawn move's value, so pawn
     * moves are taken best first without it, and the threat is measured
     * only while a move can still beat the best. */
    for (i = 0; i < n; i++)
        pv[i] = -eval_terms(dop, layer_dist(&lm, dest[i].col, dest[i].row), wo, wm) -
                threat_terms(0, t_op);
    for (k = 0; k < n; k++) {
        for (j = -1, i = 0; i < n; i++)
            if (pv[i] > -INF && (j < 0 || pv[i] > pv[j]))
                j = i;
        if (pv[j] <= best)
            break;
        v = pv[j];
        pv[j] = -INF;
        if (wo > 0) {
            g->pawn[me] = dest[j];
            v -= EVAL_THREAT * threat_of(g, w, me);
            g->pawn[me] = from;
        }
        if (v > best) {
            best = v;
            best_code = dest[j].col * QR_BOARD_SIZE + dest[j].row;
        }
    }
    w->nodes += n;
    if (best > alpha)
        alpha = best;
    if (alpha < beta && wm > 0) {
        path_walls(g, op, &lo, &cut);
        n = qr_legal_moves(g, legal);
        for (i = 0; i < n; i++) {
            m = &legal[i];
            if (m->type != QR_MOVE_WALL || !in_wall_set(&cut, m))
                continue;
            w->nodes++;
            qr_apply_unchecked(g, m);
            key_toggle(&w->wkey, KEY_WALL(move_code(m)));
            d1 = qr_shortest_path(g, op);
            /* A wall that would fall short of alpha even with a threat of
             * THREAT_CAP is not measured further. */
            t_op = 0;
            if (wm > 1 &&
                -eval_terms(d1, dm, wo, wm - 1) - threat_terms(0, THREAT_CAP) > alpha)
                t_op = threat_of(g, w, op);
            /* The mover's path can only have grown, so dm, with no threat
             * against it, gives a bound. */
            v = -eval_terms(d1, dm, wo, wm - 1) - threat_terms(0, t_op);
            if (v > alpha) {
                d2 = qr_shortest_path(g, me);
                t_me = wo > 0 ? threat_of(g, w, me) : 0;
                v = -eval_terms(d1, d2, wo, wm - 1) - threat_terms(t_me, t_op);
            }
            key_toggle(&w->wkey, KEY_WALL(move_code(m)));
            qr_undo_move(g, m, from);
            if (v > best) {
                best = v;
                best_code = move_code(m);
            }
            if (v > alpha) {
                alpha = v;
                if (alpha >= beta)
                    break;
            }
        }
    }
    bound = best <= alpha0 ? TT_UPPER : best >= beta ? TT_LOWER : TT_EXACT;
    tt_store(w, best_code, 1, bound, score_to_tt(best, ply));
    return best;
}

/* Value of w->g for the side to move, searched `depth` more plies, within
 * the window (alpha, beta). w->g is left as it was found. Meaningless once
 * the search is halted; callers check. */
static int search(worker *w, int depth, int alpha, int beta, int ply)
{
    smove mv[QR_MAX_MOVES];
    qr_pos dest[QR_MAX_PAWN_MOVES];
    qr_game *g = &w->g;
    hkey key = w->key;
    qr_pos from;
    int n, i, r, score, best = -INF, best_code = -1, bound, alpha0 = alpha;
    int tcode = -1, tdepth, tbound, tscore;

    /* The previous move won. Nearer wins score higher. */
    if (g->winner >= 0)
        return -(WIN - ply);
    if (count_node(w))
        return 0;

    if (tt_probe(w, &tcode, &tdepth, &tbound, &tscore)) {
        if (tcode == NO_MOVE)
            tcode = -1;
        if (tdepth >= depth) {
            tscore = score_from_tt(tscore, ply);
            if (tbound == TT_EXACT ||
                (tbound == TT_LOWER && tscore >= beta) ||
                (tbound == TT_UPPER && tscore <= alpha))
                return tscore;
        }
    }
    if (depth <= 0 || ply >= MAX_PLY - 1)
        return evaluate(g, w);

    n = qr_pawn_moves(g, dest);
    for (i = 0; i < n; i++)
        if (dest[i].row == qr_goal_row(g->to_move))
            return WIN - ply - 1;

    if (depth == 1)
        return frontier(w, alpha, beta, ply);

    /* Null move: if passing still holds beta at reduced depth, assume a
     * real move does too. Never twice in a row or in the principal
     * variation. */
    if (depth >= NULL_MOVE_MIN_DEPTH && beta - alpha == 1 && !w->after_null &&
        beta < MATE_BAND && evaluate(g, w) >= beta) {
        r = depth >= 6 ? 3 : 2;
        g->to_move = 1 - g->to_move;
        key_toggle(&w->key, KEY_SIDE);
        w->after_null = 1;
        score = -search(w, depth - 1 - r, -beta, -beta + 1, ply + 1);
        w->after_null = 0;
        g->to_move = 1 - g->to_move;
        w->key = key;
        if (halted(w))
            return 0;
        if (score >= beta)
            return score;
    }

    n = gen_moves(g, mv, 0);
    if (n == 0)
        return evaluate(g, w);
    score_moves(w, mv, n, tcode, ply);

    from = g->pawn[g->to_move];
    for (i = 0; i < n; i++) {
        pick(mv, n, i);
        make(w, &mv[i].m);
        if (i == 0) {
            score = -search(w, depth - 1, -beta, -alpha, ply + 1);
        } else {
            score = -search(w, depth - 1, -alpha - 1, -alpha, ply + 1);
            if (score > alpha && score < beta)
                score = -search(w, depth - 1, -beta, -alpha, ply + 1);
        }
        unmake(w, &mv[i].m, from, key);
        if (halted(w))
            return 0;
        if (score > best) {
            best = score;
            best_code = mv[i].code;
        }
        if (score > alpha)
            alpha = score;
        if (alpha >= beta) {
            if (w->killers[ply][0] != mv[i].code) {
                w->killers[ply][1] = w->killers[ply][0];
                w->killers[ply][0] = mv[i].code;
            }
            w->history[g->to_move][mv[i].code] += depth * depth;
            break;
        }
    }
    bound = best <= alpha0 ? TT_UPPER : best >= beta ? TT_LOWER : TT_EXACT;
    tt_store(w, best_code, depth, bound, score_to_tt(best, ply));
    return best;
}

/* One iteration over the root moves. Returns 0 if halted before the first
 * move was searched. The best move found is moved to the front of mv and,
 * if the iteration finished, the others are left with their scores. */
static int search_root(worker *w, smove *mv, int n, int depth)
{
    hkey key = w->key;
    qr_pos from = w->g.pawn[w->g.to_move];
    int i, score, alpha = -INF, best_i = -1;
    smove t;

    for (i = 0; i < n; i++)
        mv[i].score = -INF;
    for (i = 0; i < n; i++) {
        make(w, &mv[i].m);
        if (i == 0) {
            score = -search(w, depth - 1, -INF, INF, 1);
        } else {
            score = -search(w, depth - 1, -alpha - 1, -alpha, 1);
            if (score > alpha && !halted(w))
                score = -search(w, depth - 1, -INF, -alpha, 1);
        }
        unmake(w, &mv[i].m, from, key);
        if (halted(w))
            break;
        mv[i].score = score;
        if (score > alpha) {
            alpha = score;
            best_i = i;
            w->best_code = mv[i].code;
            w->best_score = score;
        }
    }
    if (best_i < 0)
        return 0;
    t = mv[best_i];
    for (i = best_i; i > 0; i--)
        mv[i] = mv[i - 1];
    mv[0] = t;
    return 1;
}

static int by_score(const void *a, const void *b)
{
    const smove *x = a, *y = b;

    return (y->score > x->score) - (y->score < x->score);
}

/* Iterative deepening on worker w, from `first` up to the shared maximum
 * depth. The calling thread's worker (id 0) also decides when to stop:
 * after a win or loss is found, or once half the time budget is gone, as
 * the next iteration would then likely not finish. */
static void deepen(worker *w, smove *mv, int n, int first, double start)
{
    shared *sh = w->sh;
    int d;

    for (d = first; d <= sh->max_depth; d++) {
        if (!search_root(w, mv, n, d) || halted(w))
            break;
        qsort(mv + 1, (size_t)(n - 1), sizeof mv[0], by_score);
        if (w->id > 0)
            continue;
        if (w->best_score > MATE_BAND || w->best_score < -MATE_BAND)
            break;
        if (sh->deadline > 0 && now_ms() - start > (sh->deadline - start) / 2)
            break;
    }
}

typedef struct {
    worker *w;
    smove   mv[QR_MAX_MOVES];
    int     n;
    double  start;
} helper_arg;

/* Helper threads: odd ones start a ply deeper, so that the threads spread
 * over two depths. */
static void *helper(void *arg)
{
    helper_arg *h = arg;

    deepen(h->w, h->mv, h->n, 1 + (h->w->id & 1), h->start);
    return NULL;
}

/* --- Public functions --- */

void qr_ai_init(qr_ai *ai, unsigned long seed)
{
    unsigned long i;

    pthread_once(&table_once, table_alloc);
    for (i = 0; table && i < TT_SIZE; i++) {
        qr_atomic_store_ulong(&table[i].check, 0);
        qr_atomic_store_ulong(&table[i].data, 0);
    }
    ai->rng = seed & 0xFFFFFFFFUL;
    ai->time_ms = QR_AI_DEFAULT_TIME_MS;
    ai->depth = 0;
    ai->threads = QR_AI_DEFAULT_THREADS;
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

int qr_ai_choose_move(qr_ai *ai, const qr_game *g, qr_move *out)
{
    shared sh;
    worker *ws;
    helper_arg *ha;
    pthread_t tid[QR_AI_MAX_THREADS];
    smove root[QR_MAX_MOVES], t;
    int n, i, j, threads, started = 1;
    double start = now_ms();
    long time_ms = ai->time_ms;

    if (qr_atomic_load(&ai->stop))
        return -1;
    n = gen_moves(g, root, 1);
    if (n == 0)
        return -1;
    if (n == 1) {
        *out = root[0].m;
        return 0;
    }

    threads = ai->threads < 1 ? 1 :
              ai->threads > QR_AI_MAX_THREADS ? QR_AI_MAX_THREADS : ai->threads;
    if (time_ms <= 0 && ai->depth <= 0)
        time_ms = QR_AI_DEFAULT_TIME_MS;
    pthread_once(&table_once, table_alloc);
    sh.tt = table;
    sh.tt_mask = TT_SIZE - 1;
    ws = malloc(threads * sizeof *ws);
    ha = malloc(threads * sizeof *ha);
    if (!sh.tt || !ws || !ha) {
        free(ws);
        free(ha);
        return -1;
    }
    qr_atomic_store(&sh.halt, 0);
    sh.ai = ai;
    sh.deadline = time_ms > 0 ? start + time_ms : 0;
    sh.max_depth = ai->depth > 0 && ai->depth < MAX_PLY - 1 ? ai->depth : MAX_PLY - 2;

    /* Root order: shuffled, so that equal moves vary from game to game,
     * then by the usual ordering. */
    for (i = n - 1; i > 0; i--) {
        j = (int)(rng_next(ai) % (unsigned long)(i + 1));
        t = root[i];
        root[i] = root[j];
        root[j] = t;
    }
    for (i = 0; i < threads; i++) {
        worker *w = &ws[i];
        memset(w, 0, sizeof *w);
        w->sh = &sh;
        w->g = *g;
        w->key = key_of(g);
        w->wkey = walls_key(g);
        w->id = i;
        for (j = 0; j < MAX_PLY; j++)
            w->killers[j][0] = w->killers[j][1] = -1;
        if (i == 0)
            score_moves(w, root, n, -1, 0);
    }
    qsort(root, (size_t)n, sizeof root[0], by_score);
    ws[0].best_code = root[0].code;
    ws[0].best_score = 0;

    for (i = 1; i < threads; i++) {
        ha[i].w = &ws[i];
        ha[i].n = n;
        ha[i].start = start;
        memcpy(ha[i].mv, root, n * sizeof root[0]);
        if (pthread_create(&tid[i], NULL, helper, &ha[i]) != 0)
            break;
        started++;
    }
    deepen(&ws[0], root, n, 1, start);
    qr_atomic_store(&sh.halt, 1);
    for (i = 1; i < started; i++)
        pthread_join(tid[i], NULL);

    move_decode(ws[0].best_code, out);
    free(ws);
    free(ha);
    if (qr_atomic_load(&ai->stop))
        return -1;
    return 0;
}
