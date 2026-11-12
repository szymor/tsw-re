#include "intro.h"

int intro_interval_ms(int tick)
{
    if (tick < 8)
        return 350;
    if (tick < 15)
        return 250;
    return 150;
}

void intro_start(LaunchIntro *intro, uint64_t now)
{
    if (!intro) return;
    intro->active = true;
    intro->tick = 0;
    intro->next_tick = now + (uint64_t)intro_interval_ms(0);
}

bool intro_update(LaunchIntro *intro, uint64_t now)
{
    if (!intro || !intro->active)
        return false;
    if (now < intro->next_tick)
        return false;
    if (intro->tick >= INTRO_TICKS - 1) {
        intro->active = false;
        return true;
    }
    intro->tick++;
    intro->next_tick = now + (uint64_t)intro_interval_ms(intro->tick);
    return false;
}

bool intro_draws_sprite(int tick)
{
    return tick >= 0 && tick < 19;
}

void intro_frame(int tick, IntroFrame *out)
{
    if (!out) return;
    if (tick < 0) tick = 0;
    if (tick >= INTRO_TICKS) tick = INTRO_TICKS - 1;
    bool odd = (tick & 1) != 0;
    out->sprite_tile = odd ? 218 : 217;
    out->mask_tile = odd ? 212 : 210;
    out->x = (float)(INTRO_START_X + tick);
    out->y = (float)(INTRO_START_Y + tick);
    out->size = (float)(INTRO_SPRITE_SIZE - tick);
}
