#ifndef TSW_INPUT_H
#define TSW_INPUT_H

#include <SDL3/SDL.h>

struct App;

void input_handle_event(struct App *app, const SDL_Event *event);

#endif
