#include "app.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_strings(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return SDL_strcasecmp(*sa, *sb);
}

static void app_set_status(App *app, const char *text) {
    SDL_strlcpy(app->status_text, text, sizeof(app->status_text));
}

void app_refresh_sample_list(App *app) {
    app->sample_count = 0;
    app->selected_sample = 0;

    int count = 0;
    char **names = SDL_GlobDirectory("assets/samples", "*.wav", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) return;
    qsort(names, (size_t)count, sizeof(char *), compare_strings);

    for (int i = 0; i < count && app->sample_count < APP_MAX_SAMPLES; ++i) {
        SampleEntry *entry = &app->samples[app->sample_count++];
        SDL_snprintf(entry->path, sizeof(entry->path), "assets/samples/%s", names[i]);
        SDL_strlcpy(entry->name, names[i], sizeof(entry->name));
    }
    SDL_free(names);
}

bool load_clip_from_path(App *app, const char *path) {
    AudioClip next;
    if (!clip_init_from_wav(&next, path)) {
        app_set_status(app, "Could not load WAV");
        return false;
    }

    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        clip_destroy(&next);
        app_set_status(app, "Could not lock audio stream");
        return false;
    }

    AudioClip old = app->clip;
    app->clip = next;
    clip_destroy(&old);
    audio_engine_set_playhead(&app->audio, app->clip.loop_start_frame);
    transport_jump_to_seconds(&app->transport, 0.0);
    waveform_view_init(&app->view);

    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }

    for (int i = 0; i < app->sample_count; ++i) {
        if (SDL_strcmp(app->samples[i].path, path) == 0) {
            app->selected_sample = i;
            break;
        }
    }
    app_set_status(app, path);
    return true;
}

bool app_load_selected_sample(App *app) {
    if (app->sample_count <= 0) {
        app_set_status(app, "No WAV files in assets/samples");
        return false;
    }
    return load_clip_from_path(app, app->samples[app->selected_sample].path);
}

void app_select_sample_delta(App *app, int delta) {
    if (app->sample_count <= 0) return;
    app->selected_sample = (app->selected_sample + delta) % app->sample_count;
    if (app->selected_sample < 0) app->selected_sample += app->sample_count;
}

static void app_render_overlay(App *app) {
    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    const char *current = app->clip.file_path[0] ? app->clip.file_path : "generated";
    SDL_RenderDebugTextFormat(app->renderer, 12, 10, "sample: %s", current);
    if (app->status_text[0]) SDL_RenderDebugText(app->renderer, 12, 24, app->status_text);

    if (!app->sample_selector_open) return;

    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_FRect panel = { 24.0f, 48.0f, (float)w - 48.0f, (float)h - 96.0f };
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 12, 12, 18, 225);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 100, 230, 240, 255);
    SDL_RenderRect(app->renderer, &panel);

    SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 14.0f, "assets/samples/");
    SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 30.0f, "Up/Down select   Return load   Tab close");

    if (app->sample_count <= 0) {
        SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 58.0f, "No WAV files found.");
        return;
    }

    int visible_rows = ((int)panel.h - 82) / 16;
    if (visible_rows < 1) visible_rows = 1;
    int first = app->selected_sample - visible_rows / 2;
    if (first < 0) first = 0;
    if (first + visible_rows > app->sample_count) first = app->sample_count - visible_rows;
    if (first < 0) first = 0;

    for (int row = 0; row < visible_rows && first + row < app->sample_count; ++row) {
        int index = first + row;
        float y = panel.y + 62.0f + (float)row * 16.0f;
        if (index == app->selected_sample) {
            SDL_FRect highlight = { panel.x + 10.0f, y - 2.0f, panel.w - 20.0f, 14.0f };
            SDL_SetRenderDrawColor(app->renderer, 65, 85, 100, 210);
            SDL_RenderFillRect(app->renderer, &highlight);
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 130, 255);
        } else {
            SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
        }
        SDL_RenderDebugTextFormat(app->renderer, panel.x + 16.0f, y, "%c %s", index == app->selected_sample ? '>' : ' ', app->samples[index].name);
    }
}

bool app_init(App *app){
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){ fprintf(stderr,"SDL init failed: %s\n",SDL_GetError()); return false; }
    app->window=SDL_CreateWindow("vaporplane",1280,720,SDL_WINDOW_RESIZABLE); if(!app->window) return false;
    app->renderer=SDL_CreateRenderer(app->window,NULL); if(!app->renderer) return false;
    app->gamepad=NULL; app->gamepad_id=0; app->running=true;
    int gamepad_count = 0;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&gamepad_count);
    if (gamepads && gamepad_count > 0) {
        app->gamepad = SDL_OpenGamepad(gamepads[0]);
        if (app->gamepad) app->gamepad_id = SDL_GetGamepadID(app->gamepad);
    }
    SDL_free(gamepads);
    app_refresh_sample_list(app);
    clip_init_generated(&app->clip, 48000, 2.0f);
    transport_init(&app->transport, 120.0, 960, 4, 4);
    waveform_view_init(&app->view);
    if(!audio_engine_init(&app->audio,&app->clip,&app->transport)) return false;
    if(app->sample_count > 0) app_load_selected_sample(app);
    return true;
}
void app_focus_loop_start(App *app){ app->view.target_center=(double)app->clip.loop_start_frame/(double)app->clip.frame_count; }
void app_focus_loop_end(App *app){ app->view.target_center=(double)app->clip.loop_end_frame/(double)app->clip.frame_count; }

void app_run(App *app){
    Uint64 prev=SDL_GetTicksNS();
    while(app->running){
        SDL_Event e; while(SDL_PollEvent(&e)) if(!input_handle_event(app,&e)) app->running=false;
        Uint64 now=SDL_GetTicksNS(); double dt=(double)(now-prev)/1e9; prev=now; input_update_gamepad(app,dt);
        waveform_view_update(&app->view, dt);
        waveform_render(app->renderer,&app->clip,&app->view,audio_engine_get_playhead_frame(&app->audio));
        app_render_overlay(app);
        SDL_RenderPresent(app->renderer);
    }
}
void app_shutdown(App *app){ audio_engine_shutdown(&app->audio); clip_destroy(&app->clip); if(app->gamepad) SDL_CloseGamepad(app->gamepad); if(app->renderer) SDL_DestroyRenderer(app->renderer); if(app->window) SDL_DestroyWindow(app->window); SDL_Quit(); }
