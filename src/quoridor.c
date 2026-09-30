/*
 * quoridor.c - Quoridor rules. Deliberately straightforward; the
 * representation will be revisited in a later performance pass.
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

int qr_shortest_path(const qr_game *g, int player)
{
    int dist[QR_BOARD_SIZE][QR_BOARD_SIZE];
    qr_pos queue[QR_BOARD_SIZE * QR_BOARD_SIZE];
    int head = 0, tail = 0;
    int c, r, i, goal;
    qr_pos p, n;

    for (c = 0; c < QR_BOARD_SIZE; c++)
        for (r = 0; r < QR_BOARD_SIZE; r++)
            dist[c][r] = -1;

    goal = qr_goal_row(player);
    p = g->pawn[player];
    dist[p.col][p.row] = 0;
    queue[tail++] = p;

    while (head < tail) {
        p = queue[head++];
        if (p.row == goal)
            return dist[p.col][p.row];
        for (i = 0; i < 4; i++) {
            n = make_pos(p.col + DIR_DC[i], p.row + DIR_DR[i]);
            if (!on_board(n) || dist[n.col][n.row] >= 0 || qr_is_blocked(g, p, n))
                continue;
            dist[n.col][n.row] = dist[p.col][p.row] + 1;
            queue[tail++] = n;
        }
    }
    return -1;
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
    qr_game tmp;
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

    tmp = *g;
    tmp.walls[c][r] = o;
    for (p = 0; p < QR_NUM_PLAYERS; p++)
        if (qr_shortest_path(&tmp, p) < 0)
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

int qr_legal_moves(const qr_game *g, qr_move out[QR_MAX_MOVES])
{
    qr_pos dest[QR_MAX_PAWN_MOVES];
    int n = 0, np, i, c, r, o;
    static const qr_orient orients[2] = { QR_WALL_H, QR_WALL_V };

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

    for (c = 0; c < QR_WALL_GRID; c++) {
        for (r = 0; r < QR_WALL_GRID; r++) {
            for (o = 0; o < 2; o++) {
                if (qr_check_wall(g, make_pos(c, r), orients[o]) != QR_OK)
                    continue;
                out[n].type = QR_MOVE_WALL;
                out[n].pos = make_pos(c, r);
                out[n].orient = orients[o];
                n++;
            }
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
