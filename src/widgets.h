#ifndef TSW_WIDGETS_H
#define TSW_WIDGETS_H

/* The remake's window style: plain dark panels with a thin outline, flat
 * buttons whose focus is gold, one sans face.  The windows sit where the
 * original puts its controls (19-windows.md); only the look is the
 * remake's.  All windows use these so they share one look. */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>

#define UI_PANEL      ((SDL_Color){ 34, 38, 52, 248 })
#define UI_PANEL_ALT  ((SDL_Color){ 44, 49, 66, 248 })
#define UI_BORDER     ((SDL_Color){ 145, 160, 190, 255 })
#define UI_LINE       ((SDL_Color){ 70, 78, 100, 255 })
#define UI_BUTTON     ((SDL_Color){ 52, 58, 76, 255 })
#define UI_BUTTON_HOT ((SDL_Color){ 62, 70, 92, 255 })
#define UI_TEXT       ((SDL_Color){ 225, 232, 245, 255 })
#define UI_HOT        ((SDL_Color){ 255, 230, 140, 255 })
#define UI_DIM        ((SDL_Color){ 120, 126, 140, 255 })
#define UI_TITLE      ((SDL_Color){ 240, 235, 205, 255 })
#define UI_WARN       ((SDL_Color){ 255, 120, 110, 255 })

enum {
    UI_LEFT = 0,
    UI_CENTER = 1,
    UI_RIGHT = 2,
};

enum {
    UI_FOCUSED = 1 << 0,       /* the highlight bar */
    UI_DISABLED = 1 << 1,      /* dim caption */
};

void ui_fill(SDL_Renderer *r, SDL_FRect rect, SDL_Color c);
void ui_outline(SDL_Renderer *r, SDL_FRect rect, SDL_Color c);
float ui_text_width(TTF_Font *font, const char *text);
void ui_text(SDL_Renderer *r, TTF_Font *font, const char *text, float x,
             float y, SDL_Color c);
/* One line in `rect`, vertically centred, aligned UI_LEFT/CENTER/RIGHT
 * with `pad` pixels at the aligned edge; clipped to the rectangle. */
void ui_text_in(SDL_Renderer *r, TTF_Font *font, const char *text,
                SDL_FRect rect, int align, float pad, SDL_Color c);
/* The first of `fonts` (largest first) whose line fits `width`. */
TTF_Font *ui_fit(TTF_Font *const *fonts, int count, const char *text,
                 float width);
/* Word-wrapped text from the top-left of `rect`, clipped to it. */
void ui_text_wrapped(SDL_Renderer *r, TTF_Font *font, const char *text,
                     SDL_FRect rect, SDL_Color c);
/* Like ui_text_wrapped, justified: full lines reach both edges, a
 * paragraph's last line stays left-aligned (text_layout.h).  The rendered
 * paragraph is cached until the text, font, width or colour changes. */
void ui_text_justified(SDL_Renderer *r, TTF_Font *font, const char *text,
                       SDL_FRect rect, SDL_Color c);
/* An option of a keyboard choice: plain text, or (UI_FOCUSED) a gold
 * highlight bar; UI_DISABLED dims it.  `align` places the text. */
void ui_option(SDL_Renderer *r, TTF_Font *font, SDL_FRect rect,
               const char *text, int flags, int align);
/* The keys a window takes, dim. */
void ui_hint(SDL_Renderer *r, TTF_Font *font, const char *text,
             SDL_FRect rect, int align);
/* A small filled triangle pointing up or down, centred in `rect`. */
void ui_triangle(SDL_Renderer *r, SDL_FRect rect, bool up, SDL_Color c);
/* A plain panel (fill + outline); returns the inside with `pad`. */
SDL_FRect ui_panel(SDL_Renderer *r, SDL_FRect rect, float pad);
/* The screen dimmed behind a separate dialog. */
void ui_veil(SDL_Renderer *r, float w, float h);
/* A separate dialog: veil, panel, centred title.  Returns the area under
 * the title. */
SDL_FRect ui_dialog(SDL_Renderer *r, TTF_Font *title_font, SDL_FRect rect,
                    const char *title, float screen_w, float screen_h);

#endif
