#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "audio_engine.h"
#include "clip.h"
#include "transport.h"
#include "waveform.h"

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Gamepad *gamepad;

    AudioClip clip;
    Transport transport;
    AudioEngine audio;
    WaveformView view;

    bool running;
} App;

bool app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);
