#ifndef TSW_PICTURES_H
#define TSW_PICTURES_H

/* The words on the original's pictures, in the player's language.
 *
 * Four of the intro's and ending's pictures had English baked in: open2
 * (the intro's verse), Image21 (the ending's verse), Image33 ("The End",
 * the credits, "See you again!") and Image34 (the "See you again!"
 * button).  The remake writes those words (itext's picture.* entries)
 * over the same art without them -- open1, End26 and the cleaned Image33
 * and Image34 (tools/make_clean_pictures.py) -- into the textures (and
 * open2's surface) the rest of the game draws, so the intro's reveal,
 * the ending's row-by-row reveal and the button stay as they were.  It
 * runs again whenever the language changes.  The lettered originals are
 * not shipped (tools/README.md); without the art or the fonts a picture
 * is left out.
 *
 * The title picture's credits (render.c) are written at draw time, from
 * the same list. */

#include "assets.h"

typedef struct {
    const char *years;
    const char *who;
} PictureCredit;

#define PICTURE_CREDIT_COUNT 3
/* The original's two lines and the remake's (not translated). */
extern const PictureCredit picture_credits[PICTURE_CREDIT_COUNT];

/* Write the current language's words over the pictures; `cjk` uses the
 * Japanese face (the whole interface does, assets_use_cjk). */
void pictures_bake(Assets *assets, SDL_Renderer *renderer, bool cjk);

#endif
