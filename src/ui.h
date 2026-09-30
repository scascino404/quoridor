/*
 * ui.h - SDL2 frontend for the Quoridor library. Each seat is played by a
 * human or by the AI; AI moves are computed on a worker thread, which
 * reports back by posting an SDL user event.
 *
 * All layout values are in renderer output pixels, which may differ from
 * window coordinates on high-DPI displays; mouse input is converted.
 */
#ifndef UI_H
#define UI_H

#include <SDL.h>

#include "ai.h"
#include "quoridor.h"

#define UI_MAX_HISTORY 512

typedef enum { UI_HOVER_NONE, UI_HOVER_SQUARE, UI_HOVER_WALL } ui_hover_kind;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    int           running;

    qr_game       game;
    qr_game       history[UI_MAX_HISTORY];     /* undo stack of snapshots */
    qr_move       history_move[UI_MAX_HISTORY]; /* move played from each snapshot */
    int           history_len;
    int           player_is_ai[QR_NUM_PLAYERS];

    /* AI worker; while ai_thread is non-NULL the thread owns ai (the main
     * thread may only call qr_ai_stop), ai_game, ai_move and ai_result */
    qr_ai         ai;
    SDL_Thread   *ai_thread;        /* non-NULL while a search is running */
    qr_game       ai_game;          /* private snapshot the worker searches */
    qr_move       ai_move;          /* worker's result, read only after join */
    int           ai_result;        /* qr_ai_choose_move's return value */
    Uint32        ai_event;         /* SDL user event posted when the search ends */
    int           ai_serial;        /* lets us drop a stale completion event */

    /* layout, recomputed on resize */
    int           cell_px, groove_px, board_x, board_y;
    int           text_scale, info_h;
    int           top_info_y, bottom_info_y, status_y, help_y;
    SDL_Rect      seat_rect[QR_NUM_PLAYERS];   /* clickable HUMAN/AI labels */

    /* interaction */
    SDL_Cursor   *cursor_arrow, *cursor_hand;  /* may be NULL */
    int           mouse_x, mouse_y;   /* last cursor position, output pixels */
    int           mouse_inside;
    int           hover_seat;       /* seat label under the cursor, or -1 */
    ui_hover_kind hover_kind;
    qr_move       hover_move;       /* move the cursor currently points at */
    qr_status     hover_status;     /* QR_OK -> ghost drawn gray, else red */
    qr_orient     last_orient;      /* used when hovering a groove intersection */
    qr_status     last_error;       /* shown in the status line */
} ui_state;

int  ui_init(ui_state *ui, int width, int height);   /* 0 on success */
void ui_shutdown(ui_state *ui);
void ui_handle_event(ui_state *ui, const SDL_Event *e);
void ui_render(ui_state *ui);

#endif /* UI_H */
