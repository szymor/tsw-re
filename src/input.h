#ifndef TSW_INPUT_H
#define TSW_INPUT_H

#include <SDL3/SDL.h>

struct App;

void input_handle_event(struct App *app, const SDL_Event *event);
/* Once a frame: a held arrow walks on at the NPCs' pace -- one cell per
 * speed interval (150 / 250 / 350 ms, the Timer2 tick that moves a
 * timeline's walker one cell), not at the keyboard's repeat rate. */
void input_update(struct App *app);

#endif
