#include "app.h"
#include "input.h"
#include <stdio.h>

bool app_init(App *app){
    *app = (App){0}; app->running = true;
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){ fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return false; }
    app->window = SDL_CreateWindow("vaporplane",1280,720,SDL_WINDOW_RESIZABLE);
    app->renderer = app->window ? SDL_CreateRenderer(app->window,NULL) : NULL;
    if(!app->renderer) return false;
    if(!clip_load_or_generate(&app->clip,"assets/samples/demo.wav")) return false;
    transport_init(&app->transport,120.0,480,4,4);
    waveform_init_view(&app->view);
    if(!audio_engine_init(&app->audio,&app->clip,&app->transport)) return false;
    return true;
}

void app_run(App *app){
    uint64_t prev = SDL_GetTicks();
    while(app->running){
        SDL_Event e; while(SDL_PollEvent(&e)){ if(!input_handle_event(app,&e)) app->running=false; }
        uint64_t now = SDL_GetTicks(); float dt=(float)((now-prev)/1000.0); prev=now;
        input_handle_gamepad(app,dt);
        SDL_SetRenderDrawColor(app->renderer,12,8,20,255); SDL_RenderClear(app->renderer);
        SDL_FRect rect={60,140,1160,420};
        waveform_draw(app->renderer,&rect,&app->clip,audio_engine_get_playhead_frame(&app->audio),&app->view);
        SDL_RenderPresent(app->renderer);
    }
}

void app_shutdown(App *app){
    if(app->gamepad) SDL_CloseGamepad(app->gamepad);
    audio_engine_shutdown(&app->audio);
    clip_free(&app->clip);
    if(app->renderer) SDL_DestroyRenderer(app->renderer);
    if(app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}
