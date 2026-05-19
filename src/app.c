#include "app.h"
#include "input.h"
#include <stdio.h>

bool app_init(App *app){
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){ fprintf(stderr,"SDL init failed: %s\n",SDL_GetError()); return false; }
    app->window=SDL_CreateWindow("vaporplane",1280,720,SDL_WINDOW_RESIZABLE); if(!app->window) return false;
    app->renderer=SDL_CreateRenderer(app->window,NULL); if(!app->renderer) return false;
    app->gamepad=NULL; app->running=true;
    if(!clip_init_from_wav(&app->clip,"assets/samples/demo_loop.wav")) clip_init_generated(&app->clip, 48000, 2.0f);
    transport_init(&app->transport, 120.0, 960, 4, 4);
    waveform_view_init(&app->view);
    if(!audio_engine_init(&app->audio,&app->clip,&app->transport)) return false;
    return true;
}
void app_focus_loop_start(App *app){ app->view.view_center=(double)app->clip.loop_start_frame/(double)app->clip.frame_count; }
void app_focus_loop_end(App *app){ app->view.view_center=(double)app->clip.loop_end_frame/(double)app->clip.frame_count; }

void app_run(App *app){
    Uint64 prev=SDL_GetTicksNS();
    while(app->running){
        SDL_Event e; while(SDL_PollEvent(&e)) if(!input_handle_event(app,&e)) app->running=false;
        Uint64 now=SDL_GetTicksNS(); double dt=(double)(now-prev)/1e9; prev=now; input_update_gamepad(app,dt);
        waveform_render(app->renderer,&app->clip,&app->view,audio_engine_get_playhead_frame(&app->audio));
        SDL_RenderPresent(app->renderer);
    }
}
void app_shutdown(App *app){ audio_engine_shutdown(&app->audio); clip_destroy(&app->clip); if(app->gamepad) SDL_CloseGamepad(app->gamepad); if(app->renderer) SDL_DestroyRenderer(app->renderer); if(app->window) SDL_DestroyWindow(app->window); SDL_Quit(); }
