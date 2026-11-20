#include "pictures.h"

#include "itext.h"

#include <stdio.h>
#include <string.h>

/* Newest first: the remake, the Windows version, the PC-98 original. */
const PictureCredit picture_credits[PICTURE_CREDIT_COUNT] = {
    { "(C) 2026", "vamastah" },
    { "(C) 1998 - 2000", "Oz & Kenichi" },
    { "(C) 1996", "N.W" },
};

typedef enum { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT } Align;

typedef struct {
    SDL_Surface *dst;
    TTF_Font *font;
    SDL_Color color;
} Pen;

/* The height of a capital above the baseline, for placing a line by the
 * top of its capitals as the originals' were measured. */
static int cap_height(TTF_Font *font)
{
    int maxy = 0;
    if (!TTF_GetGlyphMetrics(font, 'H', NULL, NULL, NULL, &maxy, NULL))
        return TTF_GetFontAscent(font) * 7 / 10;
    return maxy;
}

/* One line with the top of its capitals at `cap_top`; `x` is its left
 * edge, centre or right edge.  Wider than `max_w`, it is squeezed
 * horizontally to fit (a long translation). */
static void put_line(const Pen *pen, const char *text, float x, float cap_top,
                     Align align, float max_w)
{
    if (!text || !*text)
        return;
    SDL_Surface *line = TTF_RenderText_Blended(pen->font, text, 0,
                                               pen->color);
    if (!line)
        return;
    float w = (float)line->w;
    if (max_w > 0.0f && w > max_w)
        w = max_w;
    float left = align == ALIGN_LEFT ? x
               : align == ALIGN_CENTRE ? x - w / 2.0f : x - w;
    float top = cap_top - (float)(TTF_GetFontAscent(pen->font) -
                                  cap_height(pen->font));
    SDL_Rect rect = { (int)(left + 0.5f), (int)(top + 0.5f), (int)(w + 0.5f),
                      line->h };
    SDL_SetSurfaceBlendMode(line, SDL_BLENDMODE_BLEND);
    SDL_BlitSurfaceScaled(line, NULL, pen->dst, &rect, SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(line);
}

/* A verse ("\n" between lines, "\n\n" between stanzas), each line centred
 * on `cx`, the lines' capitals `pitch` apart and `stanza` apart across a
 * stanza break, the whole centred on the capitals' middle `cy`.  A verse
 * taller than `max_h` is drawn with its spacing shrunk to fit. */
static void put_verse(const Pen *pen, const char *verse, float cx, float cy,
                      float pitch, float stanza, float max_w, float max_h)
{
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", verse ? verse : "");
    const char *lines[64];
    float at[64];
    int n = 0;
    float y = 0.0f;
    bool gap = false;
    for (char *s = buf; n < 64;) {
        char *end = strchr(s, '\n');
        if (end)
            *end = '\0';
        if (*s) {
            if (n > 0)
                y += gap ? stanza : pitch;
            lines[n] = s;
            at[n++] = y;
            gap = false;
        } else if (n > 0) {
            gap = true;
        }
        if (!end)
            break;
        s = end + 1;
    }
    if (!n)
        return;
    float span = at[n - 1];
    float scale = span > max_h && span > 0.0f ? max_h / span : 1.0f;
    for (int i = 0; i < n; i++)
        put_line(pen, lines[i], cx, cy + (at[i] - span / 2.0f) * scale,
                 ALIGN_CENTRE, max_w);
}

/* `base` with words written by `draw` into the texture `*tex` (and, if
 * asked, the surface `*surf`), replacing what was there. */
static void bake(Assets *assets, SDL_Renderer *renderer,
                 const SDL_Surface *base, SDL_Texture **tex,
                 SDL_Surface **surf, bool cjk,
                 void (*draw)(Assets *, SDL_Surface *, bool))
{
    if (!base)
        return;
    SDL_Surface *out = SDL_ConvertSurface((SDL_Surface *)base,
                                          SDL_PIXELFORMAT_RGBA32);
    if (!out)
        return;
    draw(assets, out, cjk);
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, out);
    if (!t) {
        SDL_DestroySurface(out);
        return;
    }
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    if (*tex)
        SDL_DestroyTexture(*tex);
    *tex = t;
    if (surf) {
        if (*surf)
            SDL_DestroySurface(*surf);
        *surf = out;
    } else {
        SDL_DestroySurface(out);
    }
}

static TTF_Font *pic_font(const Assets *assets, int which, bool cjk)
{
    return cjk && assets->pic_font_cjk[which] ? assets->pic_font_cjk[which]
                                              : assets->pic_font[which];
}

/* open2: the verse in white inside open1's ring (the original's lines'
 * capitals at y 106 .. 250, 48 apart, centred near x 328). */
static void draw_intro(Assets *assets, SDL_Surface *dst, bool cjk)
{
    Pen pen = { dst, pic_font(assets, PIC_FONT_INTRO, cjk),
                { 255, 255, 255, 255 } };
    put_verse(&pen, tx(ITX_PICTURE_INTRO), 328.0f, 178.0f, 48.0f, 48.0f,
              430.0f, 200.0f);
}

/* Image21: the verse in black on white, four stanzas of two lines (the
 * original's capitals at y 19 .. 348, 37 apart in a stanza, 60 across). */
static void draw_ending(Assets *assets, SDL_Surface *dst, bool cjk)
{
    Pen pen = { dst, pic_font(assets, PIC_FONT_ENDING, cjk),
                { 0, 0, 0, 255 } };
    put_verse(&pen, tx(ITX_PICTURE_ENDING), 320.0f, 184.0f, 37.0f, 60.0f,
              620.0f, 340.0f);
}

/* Image33: "The End" centred over the credits (the original's spans x
 * 347 .. 615 over credits at 381 .. 596), capitals from y 67; the
 * credits in two columns at x 381 and 503 (18 apart from y 150), their
 * middle at x 484; "See you again!" right-aligned at x 625 (capitals
 * from y 377). */
static void draw_the_end(Assets *assets, SDL_Surface *dst, bool cjk)
{
    SDL_Color white = { 255, 255, 255, 255 };
    Pen big = { dst, pic_font(assets, PIC_FONT_THE_END, cjk), white };
    put_line(&big, tx(ITX_PICTURE_THE_END), 484.0f, 67.0f, ALIGN_CENTRE,
             280.0f);
    /* The credits are the same in every language: always Liberation. */
    Pen small = { dst, assets->pic_font[PIC_FONT_CREDITS], white };
    for (int i = 0; i < PICTURE_CREDIT_COUNT; i++) {
        float y = 150.0f + 18.0f * (float)i;
        put_line(&small, picture_credits[i].years, 381.0f, y, ALIGN_LEFT,
                 118.0f);
        put_line(&small, picture_credits[i].who, 503.0f, y, ALIGN_LEFT,
                 130.0f);
    }
    Pen bye = { dst, pic_font(assets, PIC_FONT_SEE_YOU, cjk), white };
    put_line(&bye, tx(ITX_PICTURE_SEE_YOU), 625.0f, 377.0f, ALIGN_RIGHT,
             300.0f);
}

/* Image34 (180x36): "See you again!" centred (capitals from y 13). */
static void draw_button(Assets *assets, SDL_Surface *dst, bool cjk)
{
    Pen bye = { dst, pic_font(assets, PIC_FONT_SEE_YOU, cjk),
                { 255, 255, 255, 255 } };
    put_line(&bye, tx(ITX_PICTURE_SEE_YOU), 88.0f, 13.0f, ALIGN_CENTRE,
             168.0f);
}

void pictures_bake(Assets *assets, SDL_Renderer *renderer, bool cjk)
{
    if (!assets || !renderer)
        return;
    for (int i = 0; i < PIC_FONT_COUNT; i++)
        if (!assets->pic_font[i])
            return;
    bake(assets, renderer, assets->open_surface[1], &assets->open2,
         &assets->open_surface[2], cjk, draw_intro);
    bake(assets, renderer, assets->image21_base, &assets->image21, NULL,
         cjk, draw_ending);
    bake(assets, renderer, assets->image33_clean, &assets->image33, NULL,
         cjk, draw_the_end);
    bake(assets, renderer, assets->image34_clean, &assets->image34, NULL,
         cjk, draw_button);
}
