#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "clip.h"
#include "audio_engine.h"
#include "transport.h"
#include "waveform.h"

#define APP_MAX_SAMPLES 64
#define APP_SAMPLE_NAME_MAX 128

typedef struct {
    char path[CLIP_MAX_PATH];
    char name[APP_SAMPLE_NAME_MAX];
} SampleEntry;

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Gamepad *gamepad;
    SDL_JoystickID gamepad_id;
    bool running;

    AudioClip clip;
    Transport transport;
    AudioEngine audio;
    WaveformView view;

    SampleEntry samples[APP_MAX_SAMPLES];
    int sample_count;
    int selected_sample;
    bool sample_selector_open;
    char status_text[160];
} App;

bool app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);
void app_focus_loop_start(App *app);
void app_focus_loop_end(App *app);
void app_refresh_sample_list(App *app);
bool load_clip_from_path(App *app, const char *path);
bool app_load_selected_sample(App *app);
void app_select_sample_delta(App *app, int delta);
