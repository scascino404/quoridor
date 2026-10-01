/*
 * ui.c - SDL2 rendering and input for Quoridor.
 *
 * Screen layout (top to bottom): player 1 info row, board with rank labels
 * on the left and file labels below, player 0 info row, status line, help.
 * Row 0 (rank 1) is drawn at the bottom of the board.
 *
 * While an AI seat is to move, a worker thread searches a snapshot of the
 * game and the board ignores the mouse. The worker's only contact with the
 * main thread is the user event it posts when done; anything that makes the
 * search pointless (undo, new game, seat change, quit) stops and joins it.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "font.h"
#include "ui.h"

static const SDL_Color COL_BG        = {   0,   0,   0, 255 };
static const SDL_Color COL_SQUARE    = {  20,  20,  24, 255 };
static const SDL_Color COL_GRID      = { 190, 190, 190, 255 };
static const SDL_Color COL_FRAME     = {  70,  70,  70, 255 };
static const SDL_Color COL_WALL      = { 240, 240, 240, 255 };
static const SDL_Color COL_WALL_USED = {  45,  45,  45, 255 };
static const SDL_Color COL_GHOST_OK  = { 200, 200, 200, 110 };
static const SDL_Color COL_GHOST_BAD = { 225,  50,  50, 150 };
static const SDL_Color COL_TEXT      = { 210, 210, 210, 255 };
static const SDL_Color COL_TEXT_DIM  = { 110, 110, 110, 255 };
static const SDL_Color COL_ERROR     = { 235,  80,  80, 255 };
static const SDL_Color COL_RING      = { 235, 235, 235, 255 };

static const SDL_Color PLAYER_COL[QR_NUM_PLAYERS]   = { { 150,  90, 220, 255 },
                                                        { 235, 195,  60, 255 } };
static const SDL_Color PLAYER_GOAL[QR_NUM_PLAYERS]  = { {  34,  22,  48, 255 },
                                                        {  42,  35,  12, 255 } };
static const SDL_Color PLAYER_HOVER[QR_NUM_PLAYERS] = { {  70,  45, 105, 255 },
                                                        {  95,  80,  25, 255 } };
static const char *PLAYER_NAME[QR_NUM_PLAYERS] = { "PURPLE", "YELLOW" };

/* ---- geometry ---------------------------------------------------------- */

static int at_least(int min, int v)
{
    return v < min ? min : v;
}

static int board_size_px(const ui_state *ui)
{
    return QR_BOARD_SIZE * ui->cell_px + (QR_BOARD_SIZE - 1) * ui->groove_px;
}

/* Blank space between the board and its rank and file labels. */
static int label_gap_px(const ui_state *ui)
{
    return ui->cell_px / 4;
}

static int square_x(const ui_state *ui, int col)
{
    return ui->board_x + col * (ui->cell_px + ui->groove_px);
}

static int square_y(const ui_state *ui, int row)
{
    return ui->board_y + (QR_BOARD_SIZE - 1 - row) * (ui->cell_px + ui->groove_px);
}

static SDL_Rect square_rect(const ui_state *ui, qr_pos p)
{
    SDL_Rect r;

    r.x = square_x(ui, p.col);
    r.y = square_y(ui, p.row);
    r.w = ui->cell_px;
    r.h = ui->cell_px;
    return r;
}

static SDL_Rect wall_rect(const ui_state *ui, qr_pos anchor, qr_orient o)
{
    SDL_Rect r;
    int inset = ui->groove_px / 6;

    if (o == QR_WALL_H) {
        /* groove above row anchor.row, spanning two columns */
        r.x = square_x(ui, anchor.col);
        r.y = square_y(ui, anchor.row) - ui->groove_px + inset;
        r.w = 2 * ui->cell_px + ui->groove_px;
        r.h = ui->groove_px - 2 * inset;
    } else {
        /* groove right of column anchor.col, spanning two rows */
        r.x = square_x(ui, anchor.col) + ui->cell_px + inset;
        r.y = square_y(ui, anchor.row + 1);
        r.w = ui->groove_px - 2 * inset;
        r.h = 2 * ui->cell_px + ui->groove_px;
    }
    return r;
}

static void update_layout(ui_state *ui)
{
    int w, h, cell, board_px, label_h, gap, line_h, total, top, p, pad;

    SDL_GetRendererOutputSize(ui->renderer, &w, &h);

    cell = w * 2 / 25;
    if (h / 16 < cell)
        cell = h / 16;
    cell = at_least(12, cell);

    ui->cell_px = cell;
    ui->groove_px = at_least(3, cell / 4);
    ui->text_scale = at_least(1, cell / 22);
    ui->info_h = cell * 3 / 4;
    board_px = board_size_px(ui);
    line_h = FONT_GLYPH_H * ui->text_scale;
    label_h = label_gap_px(ui) + line_h;
    gap = cell / 3;

    total = ui->info_h + gap + board_px + label_h + ui->info_h + gap + 7 * line_h / 2;
    top = (h - total) / 2;
    if (top < 0)
        top = 0;

    ui->top_info_y = top;
    ui->board_y = top + ui->info_h + gap;
    ui->board_x = (w - board_px) / 2;
    ui->bottom_info_y = ui->board_y + board_px + label_h;
    ui->status_y = ui->bottom_info_y + ui->info_h + gap;
    ui->help_y = ui->status_y + 5 * line_h / 2;

    /* Seat labels sit at the left of each info row; the clickable area is
     * sized for the longer label so it does not move when toggled. */
    pad = 2 * ui->text_scale;
    for (p = 0; p < QR_NUM_PLAYERS; p++) {
        ui->seat_rect[p].x = ui->board_x - pad;
        ui->seat_rect[p].y = (p == 0 ? ui->bottom_info_y : ui->top_info_y)
                             + ui->info_h / 2 - line_h / 2 - pad;
        ui->seat_rect[p].w = font_text_width("HUMAN", ui->text_scale) + 2 * pad;
        ui->seat_rect[p].h = line_h + 2 * pad;
    }
}

/* The seat whose HUMAN/AI label contains the point, or -1. */
static int seat_at(const ui_state *ui, int x, int y)
{
    const SDL_Rect *r;
    int p;

    for (p = 0; p < QR_NUM_PLAYERS; p++) {
        r = &ui->seat_rect[p];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
            return p;
    }
    return -1;
}

/* Nonzero while it is an AI seat's turn. */
static int ai_to_move(const ui_state *ui)
{
    return ui->game.winner < 0 && ui->player_is_ai[ui->game.to_move];
}

/* Nonzero while it is a human seat's turn: only then does the board take
 * the mouse. */
static int human_to_move(const ui_state *ui)
{
    return ui->game.winner < 0 && !ui->player_is_ai[ui->game.to_move];
}

/* Nonzero for the player to move or, once the game is over, the winner. */
static int player_is_active(const qr_game *g, int p)
{
    return g->winner < 0 ? g->to_move == p : g->winner == p;
}

/* Map a point (output pixels) to the move it would make: a square means a
 * pawn move, a groove means a wall. The wall's other half extends towards
 * whichever half of the neighbouring square the cursor is in. */
static ui_hover_kind pick_move(ui_state *ui, int x, int y, qr_move *out)
{
    int pitch = ui->cell_px + ui->groove_px;
    int board_px = board_size_px(ui);
    int lx = x - ui->board_x;
    int ly = y - ui->board_y;
    int ix, iy, fx, fy, in_x, in_y, row;

    if (lx < 0 || ly < 0 || lx >= board_px || ly >= board_px)
        return UI_HOVER_NONE;

    ix = lx / pitch;
    fx = lx % pitch;
    iy = ly / pitch;
    fy = ly % pitch;
    in_x = fx < ui->cell_px;
    in_y = fy < ui->cell_px;
    row = QR_BOARD_SIZE - 1 - iy;

    if (in_x && in_y) {
        out->type = QR_MOVE_PAWN;
        out->pos.col = ix;
        out->pos.row = row;
        out->orient = QR_WALL_NONE;
        return UI_HOVER_SQUARE;
    }

    out->type = QR_MOVE_WALL;
    if (!in_x && in_y) {
        /* vertical groove right of column ix */
        out->orient = QR_WALL_V;
        out->pos.col = ix;
        out->pos.row = fy < ui->cell_px / 2 ? row : row - 1;
    } else if (in_x && !in_y) {
        /* horizontal groove between rows row-1 and row */
        out->orient = QR_WALL_H;
        out->pos.col = fx < ui->cell_px / 2 ? ix - 1 : ix;
        out->pos.row = row - 1;
    } else {
        /* groove intersection: keep the orientation last hovered */
        out->orient = ui->last_orient;
        out->pos.col = ix;
        out->pos.row = row - 1;
    }

    if (out->pos.col < 0)
        out->pos.col = 0;
    if (out->pos.col > QR_WALL_GRID - 1)
        out->pos.col = QR_WALL_GRID - 1;
    if (out->pos.row < 0)
        out->pos.row = 0;
    if (out->pos.row > QR_WALL_GRID - 1)
        out->pos.row = QR_WALL_GRID - 1;

    ui->last_orient = out->orient;
    return UI_HOVER_WALL;
}

/* ---- drawing primitives ------------------------------------------------ */

static void set_color(SDL_Renderer *r, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/* Fills a circle centred on (cx, cy), where pixel (x, y) spans [x, x+1) by
 * [y, y+1). Pixels on the edge are blended in proportion to how much of
 * them the circle covers (estimated from the distance of their centre), so
 * the outline is anti-aliased; runs of fully covered pixels are drawn as
 * lines. */
static void fill_circle(SDL_Renderer *r, double cx, double cy, double radius,
                        SDL_Color c)
{
    int x, y, x0, x1, y0, y1, run;
    double dx, dy, cover;

    x0 = (int)floor(cx - radius - 0.5);
    x1 = (int)ceil(cx + radius + 0.5);
    y0 = (int)floor(cy - radius - 0.5);
    y1 = (int)ceil(cy + radius + 0.5);
    for (y = y0; y < y1; y++) {
        dy = y + 0.5 - cy;
        run = x0;
        for (x = x0; x <= x1; x++) {
            dx = x + 0.5 - cx;
            cover = x < x1 ? radius + 0.5 - sqrt(dx * dx + dy * dy) : 0.0;
            if (cover >= 1.0)
                continue;
            if (run < x) {
                set_color(r, c);
                SDL_RenderDrawLine(r, run, y, x - 1, y);
            }
            run = x + 1;
            if (cover > 0.0) {
                SDL_SetRenderDrawColor(r, c.r, c.g, c.b, (Uint8)(c.a * cover + 0.5));
                SDL_RenderDrawPoint(r, x, y);
            }
        }
    }
}

static void draw_outline(SDL_Renderer *r, SDL_Rect rect, int thickness, SDL_Color c)
{
    int i;

    set_color(r, c);
    for (i = 0; i < thickness; i++) {
        SDL_RenderDrawRect(r, &rect);
        rect.x++;
        rect.y++;
        rect.w -= 2;
        rect.h -= 2;
    }
}

static void draw_text_centered(ui_state *ui, int cx, int y, const char *s, SDL_Color c)
{
    set_color(ui->renderer, c);
    font_draw(ui->renderer, cx - font_text_width(s, ui->text_scale) / 2, y,
              ui->text_scale, s);
}

/* ---- scene ------------------------------------------------------------- */

static void draw_board(ui_state *ui)
{
    const qr_game *g = &ui->game;
    const qr_game *prev;
    const qr_move *last;
    qr_pos dest[QR_MAX_PAWN_MOVES];
    int n = 0, i, p, board_px, half, thick;
    SDL_Rect rect, frame;
    qr_pos pos;

    board_px = board_size_px(ui);
    half = ui->groove_px / 2;
    frame.x = ui->board_x - half;
    frame.y = ui->board_y - half;
    frame.w = board_px + 2 * half;
    frame.h = board_px + 2 * half;
    set_color(ui->renderer, COL_FRAME);
    SDL_RenderDrawRect(ui->renderer, &frame);

    for (pos.col = 0; pos.col < QR_BOARD_SIZE; pos.col++) {
        for (pos.row = 0; pos.row < QR_BOARD_SIZE; pos.row++) {
            rect = square_rect(ui, pos);
            set_color(ui->renderer, COL_SQUARE);
            for (p = 0; p < QR_NUM_PLAYERS; p++)
                if (pos.row == qr_goal_row(p))
                    set_color(ui->renderer, PLAYER_GOAL[p]);
            SDL_RenderFillRect(ui->renderer, &rect);
        }
    }

    if (human_to_move(ui)) {
        n = qr_pawn_moves(g, dest);
        if (ui->hover_kind == UI_HOVER_SQUARE && ui->hover_status == QR_OK) {
            rect = square_rect(ui, ui->hover_move.pos);
            set_color(ui->renderer, PLAYER_HOVER[g->to_move]);
            SDL_RenderFillRect(ui->renderer, &rect);
        }
    }

    set_color(ui->renderer, COL_GRID);
    for (pos.col = 0; pos.col < QR_BOARD_SIZE; pos.col++) {
        for (pos.row = 0; pos.row < QR_BOARD_SIZE; pos.row++) {
            rect = square_rect(ui, pos);
            SDL_RenderDrawRect(ui->renderer, &rect);
        }
    }

    /* Each player's latest move, if it was a pawn move: outline the square
     * it left, in the mover's colour. The pawn itself marks the square it
     * reached. */
    thick = at_least(2, ui->cell_px / 16);
    for (i = ui->history_len - 1;
         i >= 0 && i >= ui->history_len - QR_NUM_PLAYERS; i--) {
        last = &ui->history_move[i];
        prev = &ui->history[i];
        if (last->type == QR_MOVE_PAWN)
            draw_outline(ui->renderer, square_rect(ui, prev->pawn[prev->to_move]),
                         thick, PLAYER_HOVER[prev->to_move]);
    }

    for (i = 0; i < n; i++) {
        rect = square_rect(ui, dest[i]);
        fill_circle(ui->renderer, rect.x + rect.w / 2.0, rect.y + rect.h / 2.0,
                    ui->cell_px / 9.0, PLAYER_COL[g->to_move]);
    }
}

static void draw_labels(ui_state *ui)
{
    char s[2];
    int i, line_h, board_px, gap;

    line_h = FONT_GLYPH_H * ui->text_scale;
    board_px = board_size_px(ui);
    gap = label_gap_px(ui);
    s[1] = '\0';
    set_color(ui->renderer, COL_TEXT_DIM);
    for (i = 0; i < QR_BOARD_SIZE; i++) {
        /* Files sit below the board and ranks to its left, each the same
         * gap away from the board's edge. */
        s[0] = (char)('a' + i);
        draw_text_centered(ui, square_x(ui, i) + ui->cell_px / 2,
                           ui->board_y + board_px + gap, s, COL_TEXT_DIM);
        s[0] = (char)('1' + i);
        font_draw(ui->renderer,
                  ui->board_x - gap - font_text_width(s, ui->text_scale),
                  square_y(ui, i) + (ui->cell_px - line_h) / 2,
                  ui->text_scale, s);
    }
}

static void draw_walls(ui_state *ui)
{
    const qr_move *last = NULL;
    SDL_Rect rect;
    qr_pos a;
    qr_orient o;

    if (ui->history_len > 0 &&
        ui->history_move[ui->history_len - 1].type == QR_MOVE_WALL)
        last = &ui->history_move[ui->history_len - 1];

    for (a.col = 0; a.col < QR_WALL_GRID; a.col++) {
        for (a.row = 0; a.row < QR_WALL_GRID; a.row++) {
            o = ui->game.walls[a.col][a.row];
            if (o == QR_WALL_NONE)
                continue;
            /* the wall placed last is drawn in the mover's colour */
            if (last && last->pos.col == a.col && last->pos.row == a.row)
                set_color(ui->renderer,
                          PLAYER_COL[ui->history[ui->history_len - 1].to_move]);
            else
                set_color(ui->renderer, COL_WALL);
            rect = wall_rect(ui, a, o);
            SDL_RenderFillRect(ui->renderer, &rect);
        }
    }

    if (ui->hover_kind == UI_HOVER_WALL) {
        rect = wall_rect(ui, ui->hover_move.pos, ui->hover_move.orient);
        set_color(ui->renderer, ui->hover_status == QR_OK ? COL_GHOST_OK : COL_GHOST_BAD);
        SDL_RenderFillRect(ui->renderer, &rect);
    }
}

static void draw_pawns(ui_state *ui)
{
    const qr_game *g = &ui->game;
    SDL_Rect rect;
    double cx, cy;
    int p, radius, ring;

    radius = ui->cell_px * 9 / 25;
    ring = at_least(2, ui->cell_px / 20);
    for (p = 0; p < QR_NUM_PLAYERS; p++) {
        rect = square_rect(ui, g->pawn[p]);
        cx = rect.x + rect.w / 2.0;
        cy = rect.y + rect.h / 2.0;
        if (player_is_active(g, p))
            fill_circle(ui->renderer, cx, cy, radius + ring, COL_RING);
        fill_circle(ui->renderer, cx, cy, radius, PLAYER_COL[p]);
    }
}

static void draw_player_info(ui_state *ui, int p, int y)
{
    const qr_game *g = &ui->game;
    char buf[8];
    SDL_Rect bar;
    int active, cy, line_h, i, bar_w, bar_gap, board_px;

    active = player_is_active(g, p);
    line_h = FONT_GLYPH_H * ui->text_scale;
    board_px = board_size_px(ui);
    cy = y + ui->info_h / 2;

    /* The seat label is a toggle; it lights up like a button when hovered. */
    if (ui->hover_seat == p) {
        set_color(ui->renderer, COL_FRAME);
        SDL_RenderFillRect(ui->renderer, &ui->seat_rect[p]);
        set_color(ui->renderer, COL_GRID);
        SDL_RenderDrawRect(ui->renderer, &ui->seat_rect[p]);
        set_color(ui->renderer, COL_RING);
    } else {
        set_color(ui->renderer, active ? COL_TEXT : COL_TEXT_DIM);
    }
    font_draw(ui->renderer, ui->board_x, cy - line_h / 2, ui->text_scale,
              ui->player_is_ai[p] ? "AI" : "HUMAN");

    /* Wall stock, right-aligned with the board: remaining walls bright. */
    bar_w = at_least(2, ui->groove_px * 2 / 3);
    bar_gap = bar_w;
    bar.w = bar_w;
    bar.h = ui->info_h * 3 / 4;
    bar.y = cy - bar.h / 2;
    for (i = 0; i < QR_WALLS_PER_PLAYER; i++) {
        bar.x = ui->board_x + board_px - (QR_WALLS_PER_PLAYER - i) * (bar_w + bar_gap) + bar_gap;
        set_color(ui->renderer, i < g->walls_left[p] ? COL_WALL : COL_WALL_USED);
        SDL_RenderFillRect(ui->renderer, &bar);
    }

    sprintf(buf, "%d", g->walls_left[p]);
    set_color(ui->renderer, active ? COL_TEXT : COL_TEXT_DIM);
    font_draw(ui->renderer,
              ui->board_x + board_px - QR_WALLS_PER_PLAYER * (bar_w + bar_gap)
                  - ui->cell_px / 4 - font_text_width(buf, ui->text_scale),
              cy - line_h / 2, ui->text_scale, buf);
}

static void draw_status(ui_state *ui)
{
    const qr_game *g = &ui->game;
    char buf[96], mv[4];
    int cx, vs_ai;
    SDL_Color c;

    cx = ui->board_x + board_size_px(ui) / 2;
    /* One human against the AI: address the human directly. */
    vs_ai = ui->player_is_ai[0] != ui->player_is_ai[1];

    if (g->winner >= 0) {
        if (vs_ai)
            strcpy(buf, ui->player_is_ai[g->winner] ? "AI WINS!" : "YOU WIN!");
        else
            sprintf(buf, "%s WINS!", PLAYER_NAME[g->winner]);
        c = PLAYER_COL[g->winner];
    } else if (ui->last_error != QR_OK) {
        strcpy(buf, qr_status_str(ui->last_error));
        c = COL_ERROR;
    } else {
        if (ai_to_move(ui))
            sprintf(buf, "%s THINKING...", vs_ai ? "AI" : PLAYER_NAME[g->to_move]);
        else if (vs_ai)
            strcpy(buf, "YOUR TURN");
        else
            sprintf(buf, "%s TO MOVE", PLAYER_NAME[g->to_move]);
        if (ui->history_len > 0 &&
            qr_move_to_str(&ui->history_move[ui->history_len - 1], mv) == 0) {
            strcat(buf, "   LAST: ");
            strcat(buf, mv);
        }
        c = COL_TEXT;
    }
    draw_text_centered(ui, cx, ui->status_y, buf, c);
    draw_text_centered(ui, cx, ui->help_y,
                       "1/2: TOGGLE AI  U: UNDO  N: NEW  ESC: QUIT", COL_TEXT_DIM);
}

/* ---- game actions ------------------------------------------------------ */

static void update_hover(ui_state *ui)
{
    SDL_Cursor *cursor;

    ui->hover_kind = UI_HOVER_NONE;
    ui->hover_seat = -1;
    if (ui->mouse_inside) {
        ui->hover_seat = seat_at(ui, ui->mouse_x, ui->mouse_y);
        if (ui->hover_seat < 0 && human_to_move(ui)) {
            ui->hover_kind = pick_move(ui, ui->mouse_x, ui->mouse_y, &ui->hover_move);
            if (ui->hover_kind != UI_HOVER_NONE)
                ui->hover_status = qr_check_move(&ui->game, &ui->hover_move);
        }
    }

    cursor = ui->hover_seat >= 0 ? ui->cursor_hand : ui->cursor_arrow;
    if (cursor)
        SDL_SetCursor(cursor);
}

static void push_history(ui_state *ui, const qr_game *before, const qr_move *m)
{
    if (ui->history_len == UI_MAX_HISTORY) {
        memmove(&ui->history[0], &ui->history[1],
                (UI_MAX_HISTORY - 1) * sizeof ui->history[0]);
        memmove(&ui->history_move[0], &ui->history_move[1],
                (UI_MAX_HISTORY - 1) * sizeof ui->history_move[0]);
        ui->history_len--;
    }
    ui->history[ui->history_len] = *before;
    ui->history_move[ui->history_len] = *m;
    ui->history_len++;
}

/* Runs on the worker thread. */
static int ai_worker(void *data)
{
    ui_state *ui = data;
    SDL_Event e;

    ui->ai_result = qr_ai_choose_move(&ui->ai, &ui->ai_game, &ui->ai_move);

    memset(&e, 0, sizeof e);
    e.type = ui->ai_event;
    e.user.code = ui->ai_serial;
    SDL_PushEvent(&e);
    return 0;
}

/* Starts a search if it is an AI seat's turn and none is running. */
static void ai_start(ui_state *ui)
{
    if (ui->ai_thread || !ai_to_move(ui))
        return;

    ui->ai_game = ui->game;
    qr_ai_clear_stop(&ui->ai);
    ui->ai_serial++;
    ui->ai_thread = SDL_CreateThread(ai_worker, "quoridor-ai", ui);
    if (!ui->ai_thread) {
        fprintf(stderr, "SDL_CreateThread: %s\n", SDL_GetError());
        ui->player_is_ai[ui->game.to_move] = 0;   /* let a human take over */
    }
}

/* Stops the running search, if any, and discards its result. Its completion
 * event may already be queued; the next ai_start makes its serial stale. */
static void ai_cancel(ui_state *ui)
{
    if (!ui->ai_thread)
        return;
    qr_ai_stop(&ui->ai);
    SDL_WaitThread(ui->ai_thread, NULL);
    ui->ai_thread = NULL;
}

/* Plays m for the player to move; if that hands the turn to an AI seat,
 * its search starts. */
static qr_status play_move(ui_state *ui, const qr_move *m)
{
    qr_game before;
    qr_status st;

    before = ui->game;
    st = qr_apply_move(&ui->game, m);
    if (st == QR_OK) {
        push_history(ui, &before, m);
        ai_start(ui);
    }
    update_hover(ui);
    return st;
}

/* The worker has posted its completion event: collect and play its move. */
static void ai_finish(ui_state *ui)
{
    SDL_WaitThread(ui->ai_thread, NULL);
    ui->ai_thread = NULL;
    if (ui->ai_result == 0)
        play_move(ui, &ui->ai_move);
}

/* Rewinds to the latest position in which a human was to move, so that
 * undoing against the AI also takes back its reply. Does nothing if there
 * is no such position (in particular when both seats are AI). */
static void undo(ui_state *ui)
{
    int n = ui->history_len - 1;

    while (n >= 0 && ui->player_is_ai[ui->history[n].to_move])
        n--;
    if (n < 0)
        return;

    ai_cancel(ui);
    ui->history_len = n;
    ui->game = ui->history[n];
    ui->last_error = QR_OK;
    update_hover(ui);
}

static void new_game(ui_state *ui)
{
    ai_cancel(ui);
    qr_game_init(&ui->game);
    ui->history_len = 0;
    ui->last_error = QR_OK;
    ai_start(ui);
    update_hover(ui);
}

static void toggle_seat(ui_state *ui, int p)
{
    /* A running search belongs to the seat to move. */
    if (ui->game.to_move == p)
        ai_cancel(ui);
    ui->player_is_ai[p] = !ui->player_is_ai[p];
    ui->last_error = QR_OK;
    ai_start(ui);
    update_hover(ui);
}

static void click(ui_state *ui, int x, int y)
{
    qr_move m;
    ui_hover_kind kind;
    int seat;

    seat = seat_at(ui, x, y);
    if (seat >= 0) {
        toggle_seat(ui, seat);
        return;
    }

    if (!human_to_move(ui))
        return;
    kind = pick_move(ui, x, y, &m);
    if (kind == UI_HOVER_NONE)
        return;
    if (kind == UI_HOVER_SQUARE &&
        m.pos.col == ui->game.pawn[ui->game.to_move].col &&
        m.pos.row == ui->game.pawn[ui->game.to_move].row)
        return;   /* clicking your own pawn does nothing */

    ui->last_error = play_move(ui, &m);
}

/* Window coordinates -> renderer output pixels (differ on high-DPI). */
static void to_output_coords(const ui_state *ui, int *x, int *y)
{
    int ww, wh, ow, oh;

    SDL_GetWindowSize(ui->window, &ww, &wh);
    SDL_GetRendererOutputSize(ui->renderer, &ow, &oh);
    if (ww > 0 && wh > 0) {
        *x = *x * ow / ww;
        *y = *y * oh / wh;
    }
}

/* ---- public API -------------------------------------------------------- */

int ui_init(ui_state *ui, int width, int height)
{
    memset(ui, 0, sizeof *ui);

    ui->window = SDL_CreateWindow("Quoridor",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  width, height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!ui->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    ui->renderer = SDL_CreateRenderer(ui->window, -1,
                                      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui->renderer)
        ui->renderer = SDL_CreateRenderer(ui->window, -1, SDL_RENDERER_SOFTWARE);
    if (!ui->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(ui->window);
        ui->window = NULL;
        return -1;
    }
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetWindowMinimumSize(ui->window, 360, 460);

    ui->ai_event = SDL_RegisterEvents(1);
    if (ui->ai_event == (Uint32)-1) {
        fprintf(stderr, "SDL_RegisterEvents: out of user events\n");
        ui_shutdown(ui);
        return -1;
    }
    ui->cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    ui->cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

    qr_game_init(&ui->game);
    qr_ai_init(&ui->ai, (unsigned long)time(NULL));
    ui->player_is_ai[1] = 1;   /* human (purple, moves first) vs AI */
    ui->running = 1;
    ui->hover_kind = UI_HOVER_NONE;
    ui->hover_seat = -1;
    ui->hover_status = QR_OK;
    ui->last_orient = QR_WALL_H;
    ui->last_error = QR_OK;
    update_layout(ui);
    ai_start(ui);
    return 0;
}

void ui_shutdown(ui_state *ui)
{
    ai_cancel(ui);
    if (ui->cursor_arrow)
        SDL_FreeCursor(ui->cursor_arrow);
    if (ui->cursor_hand)
        SDL_FreeCursor(ui->cursor_hand);
    ui->cursor_arrow = NULL;
    ui->cursor_hand = NULL;
    if (ui->renderer)
        SDL_DestroyRenderer(ui->renderer);
    if (ui->window)
        SDL_DestroyWindow(ui->window);
    ui->renderer = NULL;
    ui->window = NULL;
}

void ui_handle_event(ui_state *ui, const SDL_Event *e)
{
    int x, y;

    switch (e->type) {
    case SDL_QUIT:
        ui->running = 0;
        break;

    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            update_layout(ui);
            update_hover(ui);
        } else if (e->window.event == SDL_WINDOWEVENT_LEAVE) {
            ui->mouse_inside = 0;
            update_hover(ui);
        }
        break;

    case SDL_MOUSEMOTION:
        x = e->motion.x;
        y = e->motion.y;
        to_output_coords(ui, &x, &y);
        ui->mouse_x = x;
        ui->mouse_y = y;
        ui->mouse_inside = 1;
        update_hover(ui);
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button != SDL_BUTTON_LEFT)
            break;
        x = e->button.x;
        y = e->button.y;
        to_output_coords(ui, &x, &y);
        click(ui, x, y);
        break;

    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_q:
            ui->running = 0;
            break;
        case SDLK_u:
        case SDLK_BACKSPACE:
            undo(ui);
            break;
        case SDLK_n:
            new_game(ui);
            break;
        case SDLK_1:
            toggle_seat(ui, 0);
            break;
        case SDLK_2:
            toggle_seat(ui, 1);
            break;
        default:
            break;
        }
        break;

    default:
        /* the worker finished; ignore events from searches since cancelled */
        if (e->type == ui->ai_event && ui->ai_thread &&
            e->user.code == ui->ai_serial)
            ai_finish(ui);
        break;
    }
}

void ui_render(ui_state *ui)
{
    set_color(ui->renderer, COL_BG);
    SDL_RenderClear(ui->renderer);

    draw_board(ui);
    draw_labels(ui);
    draw_walls(ui);
    draw_pawns(ui);
    draw_player_info(ui, 1, ui->top_info_y);
    draw_player_info(ui, 0, ui->bottom_info_y);
    draw_status(ui);

    SDL_RenderPresent(ui->renderer);
}
