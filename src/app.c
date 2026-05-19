#include "app.h"
#include "input.h"
#include <SDL3/SDL.h>
#include <stdio.h>

bool app_init(App *app) {
    SDL_zero(*app);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return false;
    app->window = SDL_CreateWindow("vaporplane", 1280, 720, SDL_WINDOW_RESIZABLE);
    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->window || !app->renderer) return false;

    transport_init(&app->transport, 120.0, 480, 4, 4);
    if (!clip_load_or_generate(&app->clip, "assets/samples/demo.wav")) return false;
    waveform_init(&app->view, &app->clip);
    if (!audio_engine_init(&app->audio, &app->clip, &app->transport)) return false;
    app->running = true;
    return true;
}

void app_run(App *app) {
    Uint64 prev = SDL_GetTicksNS();
    while (app->running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) input_handle_event(app, &e);
        Uint64 now = SDL_GetTicksNS();
        double dt = (double)(now - prev) / 1e9; prev = now;
        transport_update(&app->transport, dt);

        SDL_SetRenderDrawColor(app->renderer, 12, 8, 18, 255);
        SDL_RenderClear(app->renderer);
        waveform_draw(app->renderer, &app->clip, &app->view, audio_engine_playhead_frame(&app->audio));
        SDL_RenderPresent(app->renderer);
    }
}

void app_shutdown(App *app) {
    audio_engine_shutdown(&app->audio);
    clip_destroy(&app->clip);
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}
