#pragma once
#include "audio_engine.h"
#include "clip.h"
#include "transport.h"
#include "waveform.h"
#include <stdbool.h>

typedef struct {
    bool running;
    SDL_Window *window;
    SDL_Renderer *renderer;
    Transport transport;
    AudioClip clip;
    AudioEngine audio;
    WaveformView view;
} App;

bool app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);
