/*
 * font.h - tiny embedded 5x7 bitmap font for SDL2.
 *
 * Covers A-Z, 0-9 and a little punctuation; lowercase is drawn as uppercase
 * and unknown characters as blanks. Glyphs are drawn with the renderer's
 * current draw color, each font pixel as a scale x scale square.
 */
#ifndef FONT_H
#define FONT_H

#include <SDL.h>

#define FONT_GLYPH_W 5
#define FONT_GLYPH_H 7
#define FONT_ADVANCE 6   /* glyph width + 1 column of spacing */

void font_draw(SDL_Renderer *r, int x, int y, int scale, const char *s);
int  font_text_width(const char *s, int scale);

#endif /* FONT_H */
