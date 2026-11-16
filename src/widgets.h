#ifndef TSW_WIDGETS_H
#define TSW_WIDGETS_H

/* The original's controls as Windows 95 drew them (Delphi 2 VCL, Ctl3D):
 * push buttons, sunken edits/memos, raised panels, form frames.  All the
 * windows use these so they share one look.  Rectangles are the form's
 * pixel bounds (form_layout.py). */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>

/* System colours (Windows 95 defaults). */
#define VCL_BTN_FACE      ((SDL_Color){ 192, 192, 192, 255 })
#define VCL_BTN_SHADOW    ((SDL_Color){ 128, 128, 128, 255 })
#define VCL_BTN_HIGHLIGHT ((SDL_Color){ 255, 255, 255, 255 })
#define VCL_DARK_SHADOW   ((SDL_Color){ 0, 0, 0, 255 })
#define VCL_GRAY_TEXT     ((SDL_Color){ 128, 128, 128, 255 })
#define VCL_WINDOW        ((SDL_Color){ 255, 255, 255, 255 })
#define VCL_BLACK         ((SDL_Color){ 0, 0, 0, 255 })
#define VCL_RED           ((SDL_Color){ 255, 0, 0, 255 })
#define VCL_CAPTION       ((SDL_Color){ 0, 0, 128, 255 })

enum {
    VCL_LEFT = 0,
    VCL_CENTER = 1,
    VCL_RIGHT = 2,
};

enum {
    VCL_FOCUSED = 1 << 0,      /* focus rectangle + default frame */
    VCL_DISABLED = 1 << 1,     /* etched caption */
};

void vcl_fill(SDL_Renderer *r, SDL_FRect rect, SDL_Color c);
/* A 1-pixel frame: top/left in `tl`, bottom/right in `br`. */
void vcl_frame(SDL_Renderer *r, SDL_FRect rect, SDL_Color tl, SDL_Color br);
float vcl_text_width(TTF_Font *font, const char *text);
void vcl_text(SDL_Renderer *r, TTF_Font *font, const char *text, float x,
              float y, SDL_Color c);
/* One line in `rect`, vertically centred, aligned VCL_LEFT/CENTER/RIGHT
 * with `pad` pixels at the aligned edge; clipped to the rectangle. */
void vcl_text_in(SDL_Renderer *r, TTF_Font *font, const char *text,
                 SDL_FRect rect, int align, float pad, SDL_Color c);
/* Word-wrapped text from the top-left of `rect`, clipped to it. */
void vcl_text_wrapped(SDL_Renderer *r, TTF_Font *font, const char *text,
                      SDL_FRect rect, SDL_Color c);
/* TButton: face, 2-pixel raised bevel, black default frame and dotted
 * focus rectangle when focused, etched caption when disabled. */
void vcl_button(SDL_Renderer *r, TTF_Font *font, SDL_FRect rect,
                const char *caption, int flags);
/* TEdit / TMemo / TRichEdit: 2-pixel sunken border around `bg`; returns
 * the client rectangle. */
SDL_FRect vcl_edit(SDL_Renderer *r, SDL_FRect rect, SDL_Color bg);
/* TPanel (bvRaised outer bevel): returns the client rectangle. */
SDL_FRect vcl_panel(SDL_Renderer *r, SDL_FRect rect, SDL_Color face);
/* A bsSingle / bsDialog form: frame, caption bar, face.  Returns the
 * client rectangle. */
SDL_FRect vcl_window(SDL_Renderer *r, TTF_Font *caption_font,
                     SDL_FRect rect, const char *caption);
/* A disabled TLabel: the caption etched (highlight under shadow). */
void vcl_label_disabled(SDL_Renderer *r, TTF_Font *font, const char *text,
                        SDL_FRect rect, int align);
/* The standard information icon (IDI_ASTERISK) at 32x32. */
void vcl_info_icon(SDL_Renderer *r, float x, float y, TTF_Font *font);

#endif
