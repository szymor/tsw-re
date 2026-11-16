#include "widgets.h"

#include <string.h>

void vcl_fill(SDL_Renderer *r, SDL_FRect rect, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

void vcl_frame(SDL_Renderer *r, SDL_FRect rect, SDL_Color tl, SDL_Color br)
{
    if (rect.w <= 0 || rect.h <= 0)
        return;
    float x0 = rect.x, y0 = rect.y;
    float x1 = rect.x + rect.w - 1.0f, y1 = rect.y + rect.h - 1.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, tl.r, tl.g, tl.b, tl.a);
    SDL_FRect top = { x0, y0, rect.w - 1.0f, 1.0f };
    SDL_FRect left = { x0, y0, 1.0f, rect.h - 1.0f };
    SDL_RenderFillRect(r, &top);
    SDL_RenderFillRect(r, &left);
    SDL_SetRenderDrawColor(r, br.r, br.g, br.b, br.a);
    SDL_FRect bottom = { x0, y1, rect.w, 1.0f };
    SDL_FRect right = { x1, y0, 1.0f, rect.h };
    SDL_RenderFillRect(r, &bottom);
    SDL_RenderFillRect(r, &right);
}

static SDL_FRect inset(SDL_FRect rect, float d)
{
    return (SDL_FRect){ rect.x + d, rect.y + d, rect.w - 2.0f * d,
                        rect.h - 2.0f * d };
}

float vcl_text_width(TTF_Font *font, const char *text)
{
    int w = 0, h = 0;
    if (!font || !text || !*text || !TTF_GetStringSize(font, text, 0, &w, &h))
        return 0.0f;
    return (float)w;
}

static void blit_surface(SDL_Renderer *r, SDL_Surface *s, float x, float y)
{
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    if (t) {
        SDL_FRect dst = { x, y, (float)s->w, (float)s->h };
        SDL_RenderTexture(r, t, NULL, &dst);
        SDL_DestroyTexture(t);
    }
}

void vcl_text(SDL_Renderer *r, TTF_Font *font, const char *text, float x,
              float y, SDL_Color c)
{
    if (!r || !font || !text || !*text)
        return;
    SDL_Surface *s = TTF_RenderText_Blended(font, text, 0, c);
    if (!s)
        return;
    blit_surface(r, s, x, y);
    SDL_DestroySurface(s);
}

/* Clip drawing to `rect` until the returned state is restored. */
static bool push_clip(SDL_Renderer *r, SDL_FRect rect, SDL_Rect *saved)
{
    bool had = SDL_RenderClipEnabled(r);
    if (had)
        SDL_GetRenderClipRect(r, saved);
    SDL_Rect clip = { (int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h };
    if (had) {
        SDL_Rect both;
        if (!SDL_GetRectIntersection(saved, &clip, &both))
            both = (SDL_Rect){ 0, 0, 0, 0 };
        clip = both;
    }
    SDL_SetRenderClipRect(r, &clip);
    return had;
}

static void pop_clip(SDL_Renderer *r, bool had, const SDL_Rect *saved)
{
    SDL_SetRenderClipRect(r, had ? saved : NULL);
}

void vcl_text_in(SDL_Renderer *r, TTF_Font *font, const char *text,
                 SDL_FRect rect, int align, float pad, SDL_Color c)
{
    int w = 0, h = 0;
    if (!font || !text || !*text || !TTF_GetStringSize(font, text, 0, &w, &h))
        return;
    float x = rect.x + pad;
    if (align == VCL_CENTER)
        x = rect.x + (rect.w - (float)w) / 2.0f;
    else if (align == VCL_RIGHT)
        x = rect.x + rect.w - pad - (float)w;
    float y = rect.y + (rect.h - (float)h) / 2.0f;
    SDL_Rect saved;
    bool had = push_clip(r, rect, &saved);
    vcl_text(r, font, text, (float)(int)x, (float)(int)y, c);
    pop_clip(r, had, &saved);
}

void vcl_text_wrapped(SDL_Renderer *r, TTF_Font *font, const char *text,
                      SDL_FRect rect, SDL_Color c)
{
    if (!r || !font || !text || !*text || rect.w < 1.0f)
        return;
    SDL_Surface *s = TTF_RenderText_Blended_Wrapped(font, text, 0, c,
                                                    (int)rect.w);
    if (!s)
        return;
    SDL_Rect saved;
    bool had = push_clip(r, rect, &saved);
    blit_surface(r, s, rect.x, rect.y);
    pop_clip(r, had, &saved);
    SDL_DestroySurface(s);
}

static void dotted_rect(SDL_Renderer *r, SDL_FRect rect)
{
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    int x0 = (int)rect.x, y0 = (int)rect.y;
    int x1 = (int)(rect.x + rect.w) - 1, y1 = (int)(rect.y + rect.h) - 1;
    for (int x = x0; x <= x1; x += 2) {
        SDL_RenderPoint(r, (float)x, (float)y0);
        SDL_RenderPoint(r, (float)x, (float)y1);
    }
    for (int y = y0; y <= y1; y += 2) {
        SDL_RenderPoint(r, (float)x0, (float)y);
        SDL_RenderPoint(r, (float)x1, (float)y);
    }
}

void vcl_button(SDL_Renderer *r, TTF_Font *font, SDL_FRect rect,
                const char *caption, int flags)
{
    SDL_FRect b = rect;
    vcl_fill(r, b, VCL_BTN_FACE);
    if (flags & VCL_FOCUSED) {
        vcl_frame(r, b, VCL_DARK_SHADOW, VCL_DARK_SHADOW);
        b = inset(b, 1.0f);
    }
    vcl_frame(r, b, VCL_BTN_HIGHLIGHT, VCL_DARK_SHADOW);
    vcl_frame(r, inset(b, 1.0f), VCL_BTN_FACE, VCL_BTN_SHADOW);
    SDL_FRect face = inset(b, 2.0f);
    if (flags & VCL_DISABLED) {
        SDL_FRect hi = { face.x + 1.0f, face.y + 1.0f, face.w, face.h };
        vcl_text_in(r, font, caption, hi, VCL_CENTER, 0.0f, VCL_BTN_HIGHLIGHT);
        vcl_text_in(r, font, caption, face, VCL_CENTER, 0.0f, VCL_BTN_SHADOW);
    } else {
        vcl_text_in(r, font, caption, face, VCL_CENTER, 0.0f, VCL_BLACK);
    }
    if ((flags & VCL_FOCUSED) && face.w > 6.0f && face.h > 6.0f)
        dotted_rect(r, inset(face, 1.0f));
}

SDL_FRect vcl_edit(SDL_Renderer *r, SDL_FRect rect, SDL_Color bg)
{
    vcl_fill(r, rect, bg);
    vcl_frame(r, rect, VCL_BTN_SHADOW, VCL_BTN_HIGHLIGHT);
    vcl_frame(r, inset(rect, 1.0f), VCL_DARK_SHADOW, VCL_BTN_FACE);
    return inset(rect, 2.0f);
}

SDL_FRect vcl_panel(SDL_Renderer *r, SDL_FRect rect, SDL_Color face)
{
    vcl_fill(r, rect, face);
    vcl_frame(r, rect, VCL_BTN_HIGHLIGHT, VCL_BTN_SHADOW);
    return inset(rect, 1.0f);
}

SDL_FRect vcl_window(SDL_Renderer *r, TTF_Font *caption_font,
                     SDL_FRect rect, const char *caption)
{
    vcl_fill(r, rect, VCL_BTN_FACE);
    vcl_frame(r, rect, VCL_BTN_FACE, VCL_DARK_SHADOW);
    vcl_frame(r, inset(rect, 1.0f), VCL_BTN_HIGHLIGHT, VCL_BTN_SHADOW);
    SDL_FRect in = inset(rect, 3.0f);
    SDL_FRect bar = { in.x, in.y, in.w, 18.0f };
    vcl_fill(r, bar, VCL_CAPTION);
    vcl_text_in(r, caption_font, caption, bar, VCL_LEFT, 3.0f, VCL_WINDOW);
    return (SDL_FRect){ in.x, in.y + 19.0f, in.w, in.h - 19.0f };
}

void vcl_label_disabled(SDL_Renderer *r, TTF_Font *font, const char *text,
                        SDL_FRect rect, int align)
{
    SDL_FRect hi = { rect.x + 1.0f, rect.y + 1.0f, rect.w, rect.h };
    vcl_text_in(r, font, text, hi, align, 0.0f, VCL_BTN_HIGHLIGHT);
    vcl_text_in(r, font, text, rect, align, 0.0f, VCL_BTN_SHADOW);
}

void vcl_info_icon(SDL_Renderer *r, float x, float y, TTF_Font *font)
{
    /* A white disc with a black outline and a blue "i". */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (int dy = 0; dy < 32; dy++)
        for (int dx = 0; dx < 32; dx++) {
            float fx = (float)dx - 15.5f, fy = (float)dy - 15.5f;
            float d2 = fx * fx + fy * fy;
            if (d2 > 15.5f * 15.5f)
                continue;
            if (d2 > 14.0f * 14.0f)
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            else
                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            SDL_RenderPoint(r, x + (float)dx, y + (float)dy);
        }
    SDL_FRect box = { x, y, 32.0f, 32.0f };
    vcl_text_in(r, font, "i", box, VCL_CENTER, 0.0f,
                (SDL_Color){ 0, 0, 255, 255 });
}
