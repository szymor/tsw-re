#include "widgets.h"

#include "text_layout.h"

#include <stdlib.h>
#include <string.h>

void ui_fill(SDL_Renderer *r, SDL_FRect rect, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, c.a == 255 ? SDL_BLENDMODE_NONE
                                             : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

void ui_outline(SDL_Renderer *r, SDL_FRect rect, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderRect(r, &rect);
}

float ui_text_width(TTF_Font *font, const char *text)
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

void ui_text(SDL_Renderer *r, TTF_Font *font, const char *text, float x,
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

/* Clip drawing to `rect` until pop_clip. */
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

void ui_text_in(SDL_Renderer *r, TTF_Font *font, const char *text,
                SDL_FRect rect, int align, float pad, SDL_Color c)
{
    int w = 0, h = 0;
    if (!font || !text || !*text || !TTF_GetStringSize(font, text, 0, &w, &h))
        return;
    float x = rect.x + pad;
    if (align == UI_CENTER)
        x = rect.x + (rect.w - (float)w) / 2.0f;
    else if (align == UI_RIGHT)
        x = rect.x + rect.w - pad - (float)w;
    float y = rect.y + (rect.h - (float)h) / 2.0f;
    SDL_Rect saved;
    bool had = push_clip(r, rect, &saved);
    ui_text(r, font, text, (float)(int)x, (float)(int)y, c);
    pop_clip(r, had, &saved);
}

TTF_Font *ui_fit(TTF_Font *const *fonts, int count, const char *text,
                 float width)
{
    for (int i = 0; i < count; i++)
        if (fonts[i] && ui_text_width(fonts[i], text) <= width)
            return fonts[i];
    return fonts[count - 1];
}

void ui_text_wrapped(SDL_Renderer *r, TTF_Font *font, const char *text,
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

static int measure_font(void *ctx, const char *text, size_t len)
{
    int w = 0, h = 0;
    if (len == 0 || !TTF_GetStringSize((TTF_Font *)ctx, text, len, &w, &h))
        return 0;
    return w;
}

/* The paragraph, laid out and rendered once into one surface. */
static SDL_Surface *render_justified(TTF_Font *font, const char *text,
                                     int width, SDL_Color c)
{
    enum { MAX_WORDS = 1024 };
    static TextWord words[MAX_WORDS];
    int lines = 0;
    int n = text_layout(text, width, true, measure_font, font, words,
                        MAX_WORDS, &lines);
    if (n > MAX_WORDS)
        n = MAX_WORDS;
    int skip = TTF_GetFontLineSkip(font);
    if (n == 0 || lines == 0 || skip <= 0)
        return NULL;
    SDL_Surface *page = SDL_CreateSurface(width, lines * skip,
                                          SDL_PIXELFORMAT_ARGB8888);
    if (!page)
        return NULL;
    SDL_FillSurfaceRect(page, NULL, 0);
    for (int i = 0; i < n; i++) {
        SDL_Surface *w = TTF_RenderText_Blended(font, text + words[i].start,
                                                words[i].len, c);
        if (!w)
            continue;
        /* Copy, not blend: the page is transparent and words never overlap,
         * so the glyphs keep their own alpha. */
        SDL_SetSurfaceBlendMode(w, SDL_BLENDMODE_NONE);
        SDL_Rect at = { words[i].x, words[i].line * skip, w->w, w->h };
        SDL_BlitSurface(w, NULL, page, &at);
        SDL_DestroySurface(w);
    }
    return page;
}

void ui_text_justified(SDL_Renderer *r, TTF_Font *font, const char *text,
                       SDL_FRect rect, SDL_Color c)
{
    /* One cached paragraph: the message window redraws the same text every
     * frame.  The texture belongs to `r`; a different renderer starts over
     * (the old one freed its textures). */
    static struct {
        SDL_Renderer *r;
        TTF_Font *font;
        char *text;
        int width;
        SDL_Color c;
        SDL_Texture *tex;
    } cache;
    if (!r || !font || !text || !*text || rect.w < 1.0f)
        return;
    int width = (int)rect.w;
    bool same = cache.r == r && cache.font == font && cache.width == width
                && cache.text && strcmp(cache.text, text) == 0
                && memcmp(&cache.c, &c, sizeof c) == 0;
    if (!same) {
        if (cache.r == r && cache.tex)
            SDL_DestroyTexture(cache.tex);
        cache.tex = NULL;
        free(cache.text);
        cache.text = NULL;
        cache.r = r;
        cache.font = font;
        cache.width = width;
        cache.c = c;
        size_t len = strlen(text);
        cache.text = malloc(len + 1);
        if (cache.text)
            memcpy(cache.text, text, len + 1);
        SDL_Surface *page = render_justified(font, text, width, c);
        if (page) {
            cache.tex = SDL_CreateTextureFromSurface(r, page);
            SDL_DestroySurface(page);
        }
    }
    if (!cache.tex)
        return;
    float tw = 0.0f, th = 0.0f;
    SDL_GetTextureSize(cache.tex, &tw, &th);
    SDL_Rect saved;
    bool had = push_clip(r, rect, &saved);
    SDL_RenderTexture(r, cache.tex, NULL,
                      &(SDL_FRect){ rect.x, rect.y, tw, th });
    pop_clip(r, had, &saved);
}

void ui_option(SDL_Renderer *r, TTF_Font *font, SDL_FRect rect,
               const char *text, int flags, int align)
{
    bool hot = (flags & UI_FOCUSED) != 0;
    bool off = (flags & UI_DISABLED) != 0;
    if (hot) {
        ui_fill(r, rect, UI_BUTTON_HOT);
        ui_outline(r, rect, UI_HOT);
    }
    ui_text_in(r, font, text, rect, align, 6.0f,
               off ? UI_DIM : hot ? UI_HOT : UI_TEXT);
}

void ui_hint(SDL_Renderer *r, TTF_Font *font, const char *text,
             SDL_FRect rect, int align)
{
    ui_text_in(r, font, text, rect, align, 0.0f, UI_DIM);
}

void ui_triangle(SDL_Renderer *r, SDL_FRect rect, bool up, SDL_Color c)
{
    float cx = rect.x + rect.w / 2.0f, cy = rect.y + rect.h / 2.0f;
    float s = (rect.w < rect.h ? rect.w : rect.h) / 2.0f;
    SDL_FColor fc = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f };
    SDL_Vertex v[3] = {
        { { cx, up ? cy - s * 0.6f : cy + s * 0.6f }, fc, { 0, 0 } },
        { { cx - s, up ? cy + s * 0.5f : cy - s * 0.5f }, fc, { 0, 0 } },
        { { cx + s, up ? cy + s * 0.5f : cy - s * 0.5f }, fc, { 0, 0 } },
    };
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

SDL_FRect ui_panel(SDL_Renderer *r, SDL_FRect rect, float pad)
{
    ui_fill(r, rect, UI_PANEL);
    ui_outline(r, rect, UI_BORDER);
    return (SDL_FRect){ rect.x + pad, rect.y + pad, rect.w - 2.0f * pad,
                        rect.h - 2.0f * pad };
}

void ui_veil(SDL_Renderer *r, float w, float h)
{
    ui_fill(r, (SDL_FRect){ 0.0f, 0.0f, w, h }, (SDL_Color){ 0, 0, 0, 185 });
}

SDL_FRect ui_dialog(SDL_Renderer *r, TTF_Font *title_font, SDL_FRect rect,
                    const char *title, float screen_w, float screen_h)
{
    ui_veil(r, screen_w, screen_h);
    ui_panel(r, rect, 0.0f);
    SDL_FRect bar = { rect.x, rect.y + 6.0f, rect.w, 24.0f };
    ui_text_in(r, title_font, title, bar, UI_CENTER, 0.0f, UI_TITLE);
    return (SDL_FRect){ rect.x + 12.0f, rect.y + 34.0f, rect.w - 24.0f,
                        rect.h - 46.0f };
}
