#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>

typedef struct App App;

bool input_handle_event(App *app, const SDL_Event *e);
void input_handle_gamepad(App *app, float dt);
