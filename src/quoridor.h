/*
 * quoridor.h - Quoridor game library (rules, legality, notation).
 *
 * Pure C89, no I/O and no SDL. The whole game state is a plain struct that
 * can be copied by value. Its fields may be written directly to set up a
 * position, but after writing walls[][] call qr_game_sync.
 *
 * Coordinates: squares are (col, row) with col 0..8 = files a..i and
 * row 0..8 = ranks 1..9. Player 0 starts on e1 and must reach rank 9;
 * player 1 starts on e9 and must reach rank 1.
 *
 * Walls are two squares long and are identified by an anchor (c, r) with
 * c, r in 0..7: the square at the lower-left of the wall's centre point.
 *   QR_WALL_H at (c, r) blocks between rows r and r+1 for cols c and c+1
 *   QR_WALL_V at (c, r) blocks between cols c and c+1 for rows r and r+1
 * In notation these are written "e3h" / "e3v" (anchor square + orientation).
 */
#ifndef QUORIDOR_H
#define QUORIDOR_H

#define QR_BOARD_SIZE       9
#define QR_WALL_GRID        (QR_BOARD_SIZE - 1)   /* 8x8 wall anchor points */
#define QR_NUM_PLAYERS      2
#define QR_WALLS_PER_PLAYER 10
#define QR_MAX_PAWN_MOVES   6
#define QR_MAX_MOVES        (QR_MAX_PAWN_MOVES + 2 * QR_WALL_GRID * QR_WALL_GRID)

typedef struct { int col, row; } qr_pos;

typedef enum { QR_WALL_NONE = 0, QR_WALL_H, QR_WALL_V } qr_orient;

typedef enum { QR_MOVE_PAWN, QR_MOVE_WALL } qr_move_type;

typedef struct {
    qr_move_type type;
    qr_pos       pos;     /* pawn destination, or wall anchor */
    qr_orient    orient;  /* walls only; QR_WALL_NONE for pawn moves */
} qr_move;

/* Derived from walls[][]: one mask per column, bit r = row r. Kept up to
 * date by qr_game_init, qr_apply_move, qr_apply_unchecked and qr_undo_move.
 * After writing walls[][] directly, call qr_game_sync before any other
 * qr_* function. */
typedef struct {
    unsigned h[QR_WALL_GRID + 2];      /* H anchors, column c at h[c + 1] */
    unsigned v[QR_WALL_GRID];          /* V anchors */
    unsigned up[QR_BOARD_SIZE];        /* open steps (c, r) -> (c, r + 1) */
    unsigned side[QR_BOARD_SIZE + 1];  /* open steps between columns x - 1, x */
} qr_masks;

typedef struct {
    qr_pos    pawn[QR_NUM_PLAYERS];
    int       walls_left[QR_NUM_PLAYERS];
    qr_orient walls[QR_WALL_GRID][QR_WALL_GRID];  /* indexed [col][row] */
    int       to_move;   /* 0 or 1 */
    int       winner;    /* -1 while the game is in progress */
    qr_masks  masks;
} qr_game;

typedef enum {
    QR_OK = 0,
    QR_ERR_GAME_OVER,
    QR_ERR_BAD_PAWN_MOVE,
    QR_ERR_NO_WALLS_LEFT,
    QR_ERR_WALL_OUT_OF_BOUNDS,
    QR_ERR_WALL_OVERLAP,       /* same slot, or overlaps half of a collinear wall */
    QR_ERR_WALL_CROSSES,       /* H and V at the same anchor */
    QR_ERR_WALL_BLOCKS_PATH    /* would leave some player with no path to goal */
} qr_status;

void        qr_game_init(qr_game *g);
/* Rebuilds g->masks from g->walls. */
void        qr_game_sync(qr_game *g);

/* Queries */

/* Nonzero if a step from a to b is impossible: a wall is in the way, either
 * square is off the board, or the squares are not orthogonally adjacent. */
int         qr_is_blocked(const qr_game *g, qr_pos a, qr_pos b);
int         qr_goal_row(int player);
/* Number of steps to the goal row ignoring pawns, or -1 if unreachable. */
int         qr_shortest_path(const qr_game *g, int player);
/* Legal pawn destinations for the player to move (including jumps).
 * Returns the count. */
int         qr_pawn_moves(const qr_game *g, qr_pos out[QR_MAX_PAWN_MOVES]);
qr_status   qr_check_move(const qr_game *g, const qr_move *m);
/* All legal moves for the player to move: pawn moves first, then walls.
 * Returns the count (0 if the game is over). */
int         qr_legal_moves(const qr_game *g, qr_move out[QR_MAX_MOVES]);

/* Mutation: validates first; on error g is left unchanged. */
qr_status   qr_apply_move(qr_game *g, const qr_move *m);
/* Applies m without checking it. m must be legal in g: a move that
 * qr_legal_moves listed or qr_check_move accepted for this position. With
 * any other move the game is left in an undefined state. Undone with
 * qr_undo_move like any move. */
void        qr_apply_unchecked(qr_game *g, const qr_move *m);
/* Reverts m, the last move applied to g. `from` is the square the mover's
 * pawn stood on before the move (ignored for walls). m must be the move
 * most recently applied with qr_apply_move or qr_apply_unchecked; nothing
 * is validated. */
void        qr_undo_move(qr_game *g, const qr_move *m, qr_pos from);

/* Notation: "e2" (pawn), "e3h" / "e3v" (wall). Both return 0 on success,
 * -1 on malformed input. */
int         qr_move_to_str(const qr_move *m, char buf[4]);
int         qr_move_from_str(const char *s, qr_move *m);
const char *qr_status_str(qr_status s);

#endif /* QUORIDOR_H */
