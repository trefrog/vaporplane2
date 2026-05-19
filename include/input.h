#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct App App;

bool input_handle_event(App *app, const SDL_Event *e);
void input_update_gamepad(App *app, double dt);
