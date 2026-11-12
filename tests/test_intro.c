#include "intro.h"

#include <assert.h>
#include <stdio.h>

static void test_intervals(void)
{
    for (int tick = 0; tick < 8; tick++)
        assert(intro_interval_ms(tick) == 350);
    for (int tick = 8; tick < 15; tick++)
        assert(intro_interval_ms(tick) == 250);
    for (int tick = 15; tick < INTRO_TICKS; tick++)
        assert(intro_interval_ms(tick) == 150);
}

static void test_tick_progression(void)
{
    LaunchIntro intro;
    intro_start(&intro, 1000);
    assert(intro.active);
    assert(intro.tick == 0);
    assert(intro.next_tick == 1350);

    assert(!intro_update(&intro, 1349));
    assert(intro.tick == 0);
    assert(!intro_update(&intro, 1350));
    assert(intro.tick == 1);
    assert(intro.next_tick == 1700);

    /* Frame 19 is shown for its 150 ms interval, then the intro finishes. */
    uint64_t now = intro.next_tick;
    int guard = 0;
    bool finished = false;
    while (!finished && guard++ < INTRO_TICKS + 4) {
        now = intro.next_tick;
        finished = intro_update(&intro, now);
        if (!finished)
            assert(intro.tick <= INTRO_TICKS - 1);
    }
    assert(finished);
    assert(!intro.active);
    assert(intro.tick == INTRO_TICKS - 1);
}

static void test_frame_geometry(void)
{
    IntroFrame frame;
    intro_frame(0, &frame);
    assert(frame.sprite_tile == 217);
    assert(frame.mask_tile == 210);
    assert(frame.x == 304.0f);
    assert(frame.y == 368.0f);
    assert(frame.size == 32.0f);

    intro_frame(1, &frame);
    assert(frame.sprite_tile == 218);
    assert(frame.mask_tile == 212);
    assert(frame.x == 305.0f);
    assert(frame.y == 369.0f);
    assert(frame.size == 31.0f);

    intro_frame(19, &frame);
    assert(frame.sprite_tile == 218);
    assert(frame.mask_tile == 212);
    assert(frame.x == 323.0f);
    assert(frame.y == 387.0f);
    assert(frame.size == 13.0f);

    /* kasanegaki is skipped at tick 19 (0x482dce: tick <= 0x12). */
    assert(intro_draws_sprite(0));
    assert(intro_draws_sprite(18));
    assert(!intro_draws_sprite(19));

    intro_frame(-5, &frame);
    assert(frame.x == 304.0f && frame.size == 32.0f);
    intro_frame(INTRO_TICKS + 5, &frame);
    assert(frame.x == 323.0f && frame.size == 13.0f);
}

int main(void)
{
    test_intervals();
    test_tick_progression();
    test_frame_geometry();
    printf("test_intro: ok\n");
    return 0;
}
