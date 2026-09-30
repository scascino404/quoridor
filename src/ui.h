/*
 * ui.h - SDL2 frontend for the Quoridor library (human vs human).
 *
 * All layout values are in renderer output pixels, which may differ from
 * window coordinates on high-DPI displays; mouse input is converted.
 */
#ifndef UI_H
#define UI_H

#include <SDL.h>

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

    /* layout, recomputed on resize */
    int           cell_px, groove_px, board_x, board_y;
    int           text_scale, info_h;
    int           top_info_y, bottom_info_y, status_y, help_y;

    /* interaction */
    int           mouse_x, mouse_y;   /* last cursor position, output pixels */
    int           mouse_inside;
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
