/*
 * main.c - Quoridor: window and event loop.
 */
#include <stdio.h>

#include <SDL.h>

#include "ui.h"

int main(int argc, char *argv[])
{
    static ui_state ui;   /* large (undo history); keep it off the stack */
    SDL_Event e;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (ui_init(&ui, 680, 840) != 0) {
        SDL_Quit();
        return 1;
    }

    ui_render(&ui);
    while (ui.running) {
        if (!SDL_WaitEvent(&e))
            break;
        ui_handle_event(&ui, &e);
        while (SDL_PollEvent(&e))
            ui_handle_event(&ui, &e);
        ui_render(&ui);
    }

    ui_shutdown(&ui);
    SDL_Quit();
    return 0;
}
