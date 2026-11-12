#ifndef TSW_INTRO_H
#define TSW_INTRO_H

/* Launch intro (timer4ontimer 0x482a98): open0.bmp with a shrinking hero
 * sprite.  The original creates Timer4 at form activate when the K5 option
 * is checked, runs 20 ticks, then calls gameover (the new game).  The timing
 * and sprite-index math here are SDL-free so they can be unit-tested.
 *
 * Even ticks draw Chtable tile 217 through mask 210; odd ticks draw tile 218
 * through mask 212.  ImageList1 is filled from the Chtable atlas by
 * syokidata0 (0x487d70), so image index n is Chtable tile n + 1.
 */

#include <stdbool.h>
#include <stdint.h>

#define INTRO_TICKS 20
#define INTRO_SPRITE_SIZE 32
#define INTRO_START_X 304
#define INTRO_START_Y 368

typedef struct {
    bool active;
    int tick;               /* 0 .. INTRO_TICKS-1 */
    uint64_t next_tick;
} LaunchIntro;

typedef struct {
    int sprite_tile;        /* 1-based Chtable tile */
    int mask_tile;          /* 1-based Chtable mask tile */
    float x, y;             /* destination top-left in the 640x400 playfield */
    float size;             /* destination width/height (32 - tick) */
} IntroFrame;

int intro_interval_ms(int tick);
void intro_start(LaunchIntro *intro, uint64_t now);
/* Advance to the next tick when due.  Returns true when the intro finished. */
bool intro_update(LaunchIntro *intro, uint64_t now);
/* kasanegaki runs while tick <= 0x12, so tick 19 is background-only. */
bool intro_draws_sprite(int tick);
void intro_frame(int tick, IntroFrame *out);

#endif
