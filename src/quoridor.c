/*
 * quoridor.c - Quoridor rules.
 *
 * The game struct is a plain value that callers may copy and edit, so
 * nothing is cached in it. Wall legality, the expensive part, works on bit
 * masks rebuilt from the struct on each call: one mask per column, with bit
 * r standing for row r.
 *
 * A wall is illegal if it leaves a player without a path. Listing the legal
 * walls (legal_walls) searches for a path with a wall in place only when
 * the wall could matter:
 *   - it closes a loop of walls and board edge, the only way to cut
 *     squares off from each other;
 *   - and it cuts every path already found for the player.
 * Few walls pass both tests, so most calls search once per player.
 */
#include <ctype.h>
#include <string.h>

#include "quoridor.h"

/* Orthogonal directions: up, down, left, right (up = towards rank 9). */
static const int DIR_DC[4] = { 0, 0, -1, 1 };
static const int DIR_DR[4] = { 1, -1, 0, 0 };

static qr_pos make_pos(int col, int row)
{
    qr_pos p;
    p.col = col;
    p.row = row;
    return p;
}

static int on_board(qr_pos p)
{
    return p.col >= 0 && p.col < QR_BOARD_SIZE &&
           p.row >= 0 && p.row < QR_BOARD_SIZE;
}

static int pos_eq(qr_pos a, qr_pos b)
{
    return a.col == b.col && a.row == b.row;
}

void qr_game_init(qr_game *g)
{
    int c, r, p;

    for (c = 0; c < QR_WALL_GRID; c++)
        for (r = 0; r < QR_WALL_GRID; r++)
            g->walls[c][r] = QR_WALL_NONE;
    g->pawn[0] = make_pos(QR_BOARD_SIZE / 2, 0);
    g->pawn[1] = make_pos(QR_BOARD_SIZE / 2, QR_BOARD_SIZE - 1);
    for (p = 0; p < QR_NUM_PLAYERS; p++)
        g->walls_left[p] = QR_WALLS_PER_PLAYER;
    g->to_move = 0;
    g->winner = -1;
}

int qr_is_blocked(const qr_game *g, qr_pos a, qr_pos b)
{
    int c, r;

    if (!on_board(a) || !on_board(b))
        return 1;

    if (a.row == b.row && (a.col - b.col == 1 || b.col - a.col == 1)) {
        /* Horizontal step: look for a vertical wall between the columns. */
        c = a.col < b.col ? a.col : b.col;
        r = a.row;
        if (r < QR_WALL_GRID && g->walls[c][r] == QR_WALL_V)
            return 1;
        if (r > 0 && g->walls[c][r - 1] == QR_WALL_V)
            return 1;
        return 0;
    }

    if (a.col == b.col && (a.row - b.row == 1 || b.row - a.row == 1)) {
        /* Vertical step: look for a horizontal wall between the rows. */
        c = a.col;
        r = a.row < b.row ? a.row : b.row;
        if (c < QR_WALL_GRID && g->walls[c][r] == QR_WALL_H)
            return 1;
        if (c > 0 && g->walls[c - 1][r] == QR_WALL_H)
            return 1;
        return 0;
    }

    return 1;
}

int qr_goal_row(int player)
{
    return player == 0 ? QR_BOARD_SIZE - 1 : 0;
}

#define ROW_BITS  ((1u << QR_BOARD_SIZE) - 1)   /* the rows of a column */
#define SLOT_BITS ((1u << QR_WALL_GRID) - 1)    /* the wall slots of a column */

typedef struct {
    /* Wall anchors. h is padded with an empty column on each side: anchor
     * column c is at h[c + 1]. */
    unsigned h[QR_WALL_GRID + 2];
    unsigned v[QR_WALL_GRID];
    /* Open steps. up[c] bit r: between (c, r) and (c, r + 1).
     * side[x] bit r: between columns x - 1 and x in row r; side[0] and
     * side[QR_BOARD_SIZE] are the closed board edges. */
    unsigned up[QR_BOARD_SIZE];
    unsigned side[QR_BOARD_SIZE + 1];
} wall_masks;

/* One stage of a flood fill: the squares of column `col` that were reached
 * together, by stepping across from the squares of stage `parent` onto the
 * squares in `entry` and then spreading along the column. */
typedef struct {
    int      col, parent;
    unsigned entry, squares;
} flood_stage;

/* Every stage reaches at least one new square. */
typedef struct {
    flood_stage stage[QR_BOARD_SIZE * QR_BOARD_SIZE];
    int         n;
} flood_trace;

/* A set of wall slots: for each orientation, one mask of anchor rows per
 * anchor column. h is padded like wall_masks.h. */
typedef struct {
    unsigned h[QR_WALL_GRID + 2];
    unsigned v[QR_WALL_GRID];
} slot_set;

static const slot_set no_slots = { { 0 }, { 0 } };

/* masks_init reads a column of eight wall slots as two bits each,
 * horizontal and vertical. */
typedef char wall_codes_are_bits[QR_WALL_H == 1 && QR_WALL_V == 2 ? 1 : -1];
typedef char wall_grid_is_8[QR_WALL_GRID == 8 ? 1 : -1];

/* Gathers the even-numbered bits of a 16-bit value into the low 8. */
static unsigned even_bits(unsigned x)
{
    x &= 0x5555;
    x = (x | x >> 1) & 0x3333;
    x = (x | x >> 2) & 0x0F0F;
    x = (x | x >> 4) & 0x00FF;
    return x;
}

static void masks_init(wall_masks *m, const qr_game *g)
{
    const qr_orient *col;
    unsigned slots, h, v, left_h = 0;
    int c;

    m->h[0] = m->h[QR_WALL_GRID + 1] = 0;
    m->side[0] = m->side[QR_BOARD_SIZE] = 0;
    for (c = 0; c < QR_WALL_GRID; c++) {
        /* The column's slots, two bits each, then split by orientation. */
        col = g->walls[c];
        slots = (unsigned)col[0] | (unsigned)col[1] << 2 |
                (unsigned)col[2] << 4 | (unsigned)col[3] << 6 |
                (unsigned)col[4] << 8 | (unsigned)col[5] << 10 |
                (unsigned)col[6] << 12 | (unsigned)col[7] << 14;
        h = even_bits(slots);
        v = even_bits(slots >> 1);
        m->h[c + 1] = h;
        m->v[c] = v;
        m->up[c] = ~(left_h | h) & SLOT_BITS;
        m->side[c + 1] = ~(v | v << 1) & ROW_BITS;
        left_h = h;
    }
    m->up[QR_WALL_GRID] = ~left_h & SLOT_BITS;
}

/* Closes the steps that a wall at (c, r) blocks. Only up and side change;
 * callers that take the wall away again restore those themselves. */
static void masks_block(wall_masks *m, int c, int r, qr_orient o)
{
    if (o == QR_WALL_H) {
        m->up[c] &= ~(1u << r);
        m->up[c + 1] &= ~(1u << r);
    } else {
        m->side[c + 1] &= ~(3u << r);
    }
}

/* Extends the squares in s along their column through its open steps. */
static unsigned fill_column(unsigned s, unsigned up)
{
    unsigned open;

    /* Upwards: adding a square to the open steps above it carries through
     * to the top of its run. */
    s |= (up + (s & up)) ^ up;

    /* Downwards: double the reach each round; four rounds cover a column. */
    open = up;
    s |= open & s >> 1;
    open &= open >> 1;
    s |= open & s >> 2;
    open &= open >> 2;
    s |= open & s >> 4;
    open &= open >> 4;
    s |= open & s >> 8;
    return s;
}

/* Nonzero if a pawn on `from` can walk to the row whose bit is `goal`. The
 * stages of the search are left in `trace`, which doubles as its queue; on
 * success the last one is the one that reached the goal row. */
static int can_reach(const wall_masks *m, qr_pos from, unsigned goal,
                     flood_trace *trace)
{
    unsigned reach[QR_BOARD_SIZE + 2];      /* column c at [c + 1] */
    flood_stage *s, *next;
    unsigned add;
    int c, i;

    for (c = 0; c < QR_BOARD_SIZE + 2; c++)
        reach[c] = 0;
    s = &trace->stage[0];
    s->col = from.col;
    s->parent = 0;
    s->entry = 1u << from.row;
    s->squares = fill_column(s->entry, m->up[from.col]);
    reach[from.col + 1] = s->squares;
    trace->n = 1;
    if (s->squares & goal)
        return 1;

    for (i = 0; i < trace->n; i++) {
        s = &trace->stage[i];
        /* The column on each side. Past the board edge side[] is closed,
         * so nothing is added there. */
        for (c = s->col - 1; c <= s->col + 1; c += 2) {
            add = s->squares & m->side[c < s->col ? s->col : c] & ~reach[c + 1];
            if (!add)
                continue;
            next = &trace->stage[trace->n++];
            next->col = c;
            next->parent = i;
            next->entry = add;
            next->squares = fill_column(add, m->up[c]);
            reach[c + 1] |= next->squares;
            if (next->squares & goal)
                return 1;
        }
    }
    return 0;
}

/* Follows the trace of a successful search back from the goal row to the
 * pawn, and sets `cut` to the wall slots that would close a step of that
 * path. Bits beyond SLOT_BITS may be set in cut->v. */
static void path_cuts(const wall_masks *m, const flood_trace *trace,
                      int goal_row, slot_set *cut)
{
    const flood_stage *s = &trace->stage[trace->n - 1], *next;
    unsigned up, rows;
    int r = goal_row, lo;

    *cut = no_slots;
    for (;;) {
        /* The path came into this column on an entry square that r can be
         * reached from: the nearest one below it, or else above it. */
        up = m->up[s->col];
        for (lo = r; !(s->entry >> lo & 1) && lo > 0 && (up >> (lo - 1) & 1); )
            lo--;
        if (!(s->entry >> lo & 1))
            for (lo = r + 1; !(s->entry >> lo & 1); )
                lo++;

        rows = lo < r ? (1u << r) - (1u << lo) : (1u << lo) - (1u << r);
        cut->h[s->col] |= rows;
        cut->h[s->col + 1] |= rows;
        if (s == trace->stage)
            break;

        r = lo;
        next = &trace->stage[s->parent];
        cut->v[s->col < next->col ? s->col : next->col] |= (3u << r) >> 1;
        s = next;
    }
}

int qr_shortest_path(const qr_game *g, int player)
{
    /* Breadth-first search, a whole layer at a time. cur and next hold
     * column c at [c + 1]. */
    wall_masks m;
    unsigned seen[QR_BOARD_SIZE];
    unsigned layer[2][QR_BOARD_SIZE + 2];
    unsigned *cur = layer[0], *next = layer[1], *swap;
    unsigned goal, n, any;
    qr_pos from;
    int c, dist;

    masks_init(&m, g);
    goal = 1u << qr_goal_row(player);
    from = g->pawn[player];

    for (c = 0; c < QR_BOARD_SIZE; c++)
        seen[c] = 0;
    for (c = 0; c < QR_BOARD_SIZE + 2; c++)
        cur[c] = next[c] = 0;
    cur[from.col + 1] = seen[from.col] = any = 1u << from.row;

    for (dist = 0; !(any & goal); dist++) {
        any = 0;
        for (c = 0; c < QR_BOARD_SIZE; c++) {
            n = ((cur[c + 1] & m.up[c]) << 1) | ((cur[c + 1] >> 1) & m.up[c]) |
                (cur[c] & m.side[c]) | (cur[c + 2] & m.side[c + 1]);
            n &= ~seen[c];
            seen[c] |= n;
            next[c + 1] = n;
            any |= n;
        }
        if (!any)
            return -1;
        swap = cur;
        cur = next;
        next = swap;
    }
    return dist;
}

int qr_pawn_moves(const qr_game *g, qr_pos out[QR_MAX_PAWN_MOVES])
{
    int n = 0, i, j;
    qr_pos me, opp, adj, jump, side;

    me = g->pawn[g->to_move];
    opp = g->pawn[1 - g->to_move];

    for (i = 0; i < 4; i++) {
        adj = make_pos(me.col + DIR_DC[i], me.row + DIR_DR[i]);
        if (qr_is_blocked(g, me, adj))
            continue;
        if (!pos_eq(adj, opp)) {
            out[n++] = adj;
            continue;
        }

        /* Opponent is adjacent: jump straight over if possible... */
        jump = make_pos(adj.col + DIR_DC[i], adj.row + DIR_DR[i]);
        if (!qr_is_blocked(g, adj, jump)) {
            out[n++] = jump;
            continue;
        }

        /* ...otherwise (wall or edge behind) step diagonally around it. */
        for (j = 0; j < 4; j++) {
            if (DIR_DC[i] * DIR_DC[j] + DIR_DR[i] * DIR_DR[j] != 0)
                continue;   /* not perpendicular */
            side = make_pos(adj.col + DIR_DC[j], adj.row + DIR_DR[j]);
            if (!qr_is_blocked(g, adj, side))
                out[n++] = side;
        }
    }
    return n;
}

qr_status qr_check_wall(const qr_game *g, qr_pos anchor, qr_orient o)
{
    wall_masks m;
    flood_trace trace;
    int c = anchor.col, r = anchor.row;
    int p;

    if (g->winner >= 0)
        return QR_ERR_GAME_OVER;
    if (g->walls_left[g->to_move] <= 0)
        return QR_ERR_NO_WALLS_LEFT;
    if (c < 0 || c >= QR_WALL_GRID || r < 0 || r >= QR_WALL_GRID ||
        (o != QR_WALL_H && o != QR_WALL_V))
        return QR_ERR_WALL_OUT_OF_BOUNDS;

    if (g->walls[c][r] == o)
        return QR_ERR_WALL_OVERLAP;
    if (g->walls[c][r] != QR_WALL_NONE)
        return QR_ERR_WALL_CROSSES;

    if (o == QR_WALL_H) {
        if ((c > 0 && g->walls[c - 1][r] == QR_WALL_H) ||
            (c < QR_WALL_GRID - 1 && g->walls[c + 1][r] == QR_WALL_H))
            return QR_ERR_WALL_OVERLAP;
    } else {
        if ((r > 0 && g->walls[c][r - 1] == QR_WALL_V) ||
            (r < QR_WALL_GRID - 1 && g->walls[c][r + 1] == QR_WALL_V))
            return QR_ERR_WALL_OVERLAP;
    }

    masks_init(&m, g);
    masks_block(&m, c, r, o);
    for (p = 0; p < QR_NUM_PLAYERS; p++)
        if (!can_reach(&m, g->pawn[p], 1u << qr_goal_row(p), &trace))
            return QR_ERR_WALL_BLOCKS_PATH;

    return QR_OK;
}

qr_status qr_check_move(const qr_game *g, const qr_move *m)
{
    qr_pos dest[QR_MAX_PAWN_MOVES];
    int n, i;

    if (g->winner >= 0)
        return QR_ERR_GAME_OVER;

    if (m->type == QR_MOVE_WALL)
        return qr_check_wall(g, m->pos, m->orient);

    if (m->type == QR_MOVE_PAWN) {
        n = qr_pawn_moves(g, dest);
        for (i = 0; i < n; i++)
            if (pos_eq(dest[i], m->pos))
                return QR_OK;
    }
    return QR_ERR_BAD_PAWN_MOVE;
}

/*
 * Grid points are the corners of squares: point (x, y), with x and y from
 * 0 to QR_BOARD_SIZE, is the lower-left corner of square (x, y). A wall
 * runs through three of them in a line. point_sets is a union-find over
 * them: two points are in one set when walls or the board edge join them.
 */
#define LINE_POINTS (QR_BOARD_SIZE + 1)
#define POINT(x, y) ((x) * LINE_POINTS + (y))

typedef struct {
    unsigned char parent[LINE_POINTS * LINE_POINTS];
} point_sets;

/* No walls: the points on the board edge are one set, the rest alone. */
#define EDGE_LINE  0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#define INNER_LINE(x) \
    0, POINT(x, 1), POINT(x, 2), POINT(x, 3), POINT(x, 4), \
    POINT(x, 5), POINT(x, 6), POINT(x, 7), POINT(x, 8), 0
static const point_sets no_walls = { {
    EDGE_LINE, INNER_LINE(1), INNER_LINE(2), INNER_LINE(3), INNER_LINE(4),
    INNER_LINE(5), INNER_LINE(6), INNER_LINE(7), INNER_LINE(8), EDGE_LINE
} };

static int set_of(point_sets *s, int p)
{
    while (s->parent[p] != p) {
        s->parent[p] = s->parent[s->parent[p]];
        p = s->parent[p];
    }
    return p;
}

/* The first of a wall's three points, and the step from one to the next. */
static int wall_point(int c, int r, qr_orient o)
{
    return o == QR_WALL_H ? POINT(c, r + 1) : POINT(c + 1, r);
}

static int wall_step(qr_orient o)
{
    return o == QR_WALL_H ? LINE_POINTS : 1;
}

static void join_wall(point_sets *s, int c, int r, qr_orient o)
{
    int p = wall_point(c, r, o), step = wall_step(o);
    unsigned char set = (unsigned char)set_of(s, p);

    s->parent[set_of(s, p + step)] = set;
    s->parent[set_of(s, p + 2 * step)] = set;
}

/* Nonzero if a new wall would join two points that are joined already. Only
 * such a wall closes a loop of walls, and only a loop can cut squares off
 * from each other. */
static int closes_loop(point_sets *s, int c, int r, qr_orient o)
{
    int p = wall_point(c, r, o), step = wall_step(o);
    int a = set_of(s, p), b = set_of(s, p + step), d = set_of(s, p + 2 * step);

    return a == b || b == d || a == d;
}

/* Bitwise majority: the bits set in at least two of a, b and c. */
static unsigned majority(unsigned a, unsigned b, unsigned c)
{
    return (a & b) | (a & c) | (b & c);
}

/* Searches for both players' paths with a wall at (c, r) in place; zero if
 * someone has none. A player is only searched for if the wall is in their
 * `need` set, the slots that cut every path found for them so far, and each
 * path found narrows that set further. */
static int wall_keeps_paths(wall_masks *m, const qr_game *g, int c, int r,
                            qr_orient o, slot_set need[QR_NUM_PLAYERS])
{
    flood_trace trace;
    slot_set cut;
    unsigned saved[3];
    int p, i, kept = 1;

    saved[0] = m->up[c];
    saved[1] = m->up[c + 1];
    saved[2] = m->side[c + 1];
    masks_block(m, c, r, o);
    for (p = 0; p < QR_NUM_PLAYERS; p++) {
        if (!((o == QR_WALL_H ? need[p].h[c + 1] : need[p].v[c]) >> r & 1))
            continue;
        if (!can_reach(m, g->pawn[p], 1u << qr_goal_row(p), &trace)) {
            kept = 0;
            break;
        }
        path_cuts(m, &trace, qr_goal_row(p), &cut);
        for (i = 0; i < QR_WALL_GRID; i++) {
            need[p].h[i + 1] &= cut.h[i + 1];
            need[p].v[i] &= cut.v[i];
        }
    }
    m->up[c] = saved[0];
    m->up[c + 1] = saved[1];
    m->side[c + 1] = saved[2];
    return kept;
}

/* Sets `ok` to the legal wall slots for a player with walls left. */
static void legal_walls(const qr_game *g, slot_set *ok)
{
    wall_masks m;
    flood_trace trace;
    slot_set risky, cut, need[QR_NUM_PLAYERS];
    point_sets sets;
    /* Grid points (corners of squares) that a wall or the board edge
     * touches: x and y run from 0 to QR_BOARD_SIZE, column x at touch[x],
     * bit y. */
    unsigned touch[QR_BOARD_SIZE + 1];
    unsigned occupied, t, any_risky, any_needed, bit;
    int c, r, p;

    masks_init(&m, g);

    /* Slots that are empty and do not overlap a wall in the same line. */
    for (c = 0; c < QR_WALL_GRID; c++) {
        occupied = m.h[c + 1] | m.v[c];
        ok->h[c + 1] = ~(occupied | m.h[c] | m.h[c + 2]) & SLOT_BITS;
        ok->v[c] = ~(occupied | m.v[c] << 1 | m.v[c] >> 1) & SLOT_BITS;
    }

    /* A wall runs through three grid points. It can only close a loop if
     * at least two of them already touch a wall or the edge. */
    touch[0] = touch[QR_BOARD_SIZE] = (2u << QR_BOARD_SIZE) - 1;
    for (c = 1; c < QR_BOARD_SIZE; c++)
        touch[c] = 1u | 1u << QR_BOARD_SIZE |
                   (m.h[c - 1] | m.h[c] | m.h[c + 1]) << 1 |
                   m.v[c - 1] | m.v[c - 1] << 1 | m.v[c - 1] << 2;
    any_risky = 0;
    for (c = 0; c < QR_WALL_GRID; c++) {
        t = touch[c + 1];
        risky.h[c + 1] = majority(touch[c], t, touch[c + 2]) >> 1 & ok->h[c + 1];
        risky.v[c] = majority(t, t >> 1, t >> 2) & ok->v[c];
        any_risky |= risky.h[c + 1] | risky.v[c];
    }

    /* Both players must have a path to begin with (always true in a
     * position reached by legal play). A risky wall then only matters to a
     * player if it cuts the path found here: `need`. */
    any_needed = 0;
    for (p = 0; p < QR_NUM_PLAYERS; p++) {
        if (!can_reach(&m, g->pawn[p], 1u << qr_goal_row(p), &trace)) {
            *ok = no_slots;
            return;
        }
        if (!any_risky)
            continue;
        path_cuts(&m, &trace, qr_goal_row(p), &cut);
        for (c = 0; c < QR_WALL_GRID; c++) {
            need[p].h[c + 1] = risky.h[c + 1] & cut.h[c + 1];
            need[p].v[c] = risky.v[c] & cut.v[c];
            any_needed |= need[p].h[c + 1] | need[p].v[c];
        }
    }
    if (!any_needed)
        return;

    /* Few walls are left, so look at them one at a time: search for paths
     * only if the wall really closes a loop. */
    sets = no_walls;
    for (c = 0; c < QR_WALL_GRID; c++) {
        for (r = 0; (m.h[c + 1] | m.v[c]) >> r; r++) {
            if (m.h[c + 1] >> r & 1)
                join_wall(&sets, c, r, QR_WALL_H);
            if (m.v[c] >> r & 1)
                join_wall(&sets, c, r, QR_WALL_V);
        }
    }
    for (c = 0; c < QR_WALL_GRID; c++) {
        for (r = 0; (need[0].h[c + 1] | need[1].h[c + 1] |
                     need[0].v[c] | need[1].v[c]) >> r; r++) {
            bit = 1u << r;
            if (((need[0].h[c + 1] | need[1].h[c + 1]) & bit) &&
                closes_loop(&sets, c, r, QR_WALL_H) &&
                !wall_keeps_paths(&m, g, c, r, QR_WALL_H, need))
                ok->h[c + 1] &= ~bit;
            if (((need[0].v[c] | need[1].v[c]) & bit) &&
                closes_loop(&sets, c, r, QR_WALL_V) &&
                !wall_keeps_paths(&m, g, c, r, QR_WALL_V, need))
                ok->v[c] &= ~bit;
        }
    }
}

/* Every wall move, in the order qr_legal_moves lists them. */
#define SLOT_MOVES(c, r) \
    { QR_MOVE_WALL, { c, r }, QR_WALL_H }, { QR_MOVE_WALL, { c, r }, QR_WALL_V }
#define COLUMN_MOVES(c) \
    SLOT_MOVES(c, 0), SLOT_MOVES(c, 1), SLOT_MOVES(c, 2), SLOT_MOVES(c, 3), \
    SLOT_MOVES(c, 4), SLOT_MOVES(c, 5), SLOT_MOVES(c, 6), SLOT_MOVES(c, 7)
static const qr_move wall_moves[QR_WALL_GRID][2 * QR_WALL_GRID] = {
    { COLUMN_MOVES(0) }, { COLUMN_MOVES(1) }, { COLUMN_MOVES(2) },
    { COLUMN_MOVES(3) }, { COLUMN_MOVES(4) }, { COLUMN_MOVES(5) },
    { COLUMN_MOVES(6) }, { COLUMN_MOVES(7) }
};

int qr_legal_moves(const qr_game *g, qr_move out[QR_MAX_MOVES])
{
    qr_pos dest[QR_MAX_PAWN_MOVES];
    slot_set ok;
    const qr_move *wall;
    int n = 0, np, i, c, r;

    if (g->winner >= 0)
        return 0;

    np = qr_pawn_moves(g, dest);
    for (i = 0; i < np; i++) {
        out[n].type = QR_MOVE_PAWN;
        out[n].pos = dest[i];
        out[n].orient = QR_WALL_NONE;
        n++;
    }

    if (g->walls_left[g->to_move] <= 0)
        return n;

    legal_walls(g, &ok);
    for (c = 0; c < QR_WALL_GRID; c++) {
        wall = wall_moves[c];
        if ((ok.h[c + 1] & ok.v[c]) == SLOT_BITS) {
            memcpy(&out[n], wall, sizeof wall_moves[c]);
            n += 2 * QR_WALL_GRID;
            continue;
        }
        /* Write each move where the next one goes, and step past it only
         * if it is legal. The slot written last is always inside out:
         * fewer than QR_MAX_MOVES moves come before any one move. */
        for (r = 0; r < QR_WALL_GRID; r++) {
            out[n] = *wall++;
            n += (int)(ok.h[c + 1] >> r & 1);
            out[n] = *wall++;
            n += (int)(ok.v[c] >> r & 1);
        }
    }
    return n;
}

qr_status qr_apply_move(qr_game *g, const qr_move *m)
{
    qr_status st;

    st = qr_check_move(g, m);
    if (st != QR_OK)
        return st;

    if (m->type == QR_MOVE_PAWN) {
        g->pawn[g->to_move] = m->pos;
        if (m->pos.row == qr_goal_row(g->to_move))
            g->winner = g->to_move;
    } else {
        g->walls[m->pos.col][m->pos.row] = m->orient;
        g->walls_left[g->to_move]--;
    }
    g->to_move = 1 - g->to_move;
    return QR_OK;
}

void qr_undo_move(qr_game *g, const qr_move *m, qr_pos from)
{
    g->to_move = 1 - g->to_move;
    if (m->type == QR_MOVE_PAWN) {
        g->pawn[g->to_move] = from;
        g->winner = -1;
    } else {
        g->walls[m->pos.col][m->pos.row] = QR_WALL_NONE;
        g->walls_left[g->to_move]++;
    }
}

int qr_move_to_str(const qr_move *m, char buf[4])
{
    if (m->type == QR_MOVE_PAWN) {
        if (!on_board(m->pos))
            return -1;
        buf[0] = (char)('a' + m->pos.col);
        buf[1] = (char)('1' + m->pos.row);
        buf[2] = '\0';
        return 0;
    }
    if (m->type == QR_MOVE_WALL) {
        if (m->pos.col < 0 || m->pos.col >= QR_WALL_GRID ||
            m->pos.row < 0 || m->pos.row >= QR_WALL_GRID ||
            (m->orient != QR_WALL_H && m->orient != QR_WALL_V))
            return -1;
        buf[0] = (char)('a' + m->pos.col);
        buf[1] = (char)('1' + m->pos.row);
        buf[2] = m->orient == QR_WALL_H ? 'h' : 'v';
        buf[3] = '\0';
        return 0;
    }
    return -1;
}

int qr_move_from_str(const char *s, qr_move *m)
{
    size_t len;
    int col, row, limit;
    char o;

    len = strlen(s);
    if (len != 2 && len != 3)
        return -1;

    col = tolower((unsigned char)s[0]) - 'a';
    row = s[1] - '1';
    limit = len == 2 ? QR_BOARD_SIZE : QR_WALL_GRID;
    if (col < 0 || col >= limit || row < 0 || row >= limit)
        return -1;

    if (len == 2) {
        m->type = QR_MOVE_PAWN;
        m->orient = QR_WALL_NONE;
    } else {
        o = (char)tolower((unsigned char)s[2]);
        if (o != 'h' && o != 'v')
            return -1;
        m->type = QR_MOVE_WALL;
        m->orient = o == 'h' ? QR_WALL_H : QR_WALL_V;
    }
    m->pos = make_pos(col, row);
    return 0;
}

const char *qr_status_str(qr_status s)
{
    switch (s) {
    case QR_OK:                     return "ok";
    case QR_ERR_GAME_OVER:          return "the game is over";
    case QR_ERR_BAD_PAWN_MOVE:      return "illegal pawn move";
    case QR_ERR_NO_WALLS_LEFT:      return "no walls left";
    case QR_ERR_WALL_OUT_OF_BOUNDS: return "wall out of bounds";
    case QR_ERR_WALL_OVERLAP:       return "wall overlaps another wall";
    case QR_ERR_WALL_CROSSES:       return "wall crosses another wall";
    case QR_ERR_WALL_BLOCKS_PATH:   return "wall would block a path to goal";
    }
    return "unknown error";
}
