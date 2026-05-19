#include "input.h"
#include "app.h"

static void move_loop(App *app, bool start, int dir, bool coarse){ int64_t d = coarse ? 2048 : 256; audio_engine_nudge_loop(&app->audio,start,dir*d);} 

bool input_handle_event(App *app, const SDL_Event *e){
    if(e->type==SDL_EVENT_QUIT) return false;
    if(e->type==SDL_EVENT_GAMEPAD_ADDED && !app->gamepad) app->gamepad = SDL_OpenGamepad(e->gdevice.which);
    if(e->type!=SDL_EVENT_KEY_DOWN) return true;
    SDL_Keymod mod = SDL_GetModState(); bool coarse = (mod & SDL_KMOD_SHIFT) != 0;
    switch(e->key.key){
        case SDLK_ESCAPE: return false;
        case SDLK_SPACE: transport_toggle_play(&app->transport); break;
        case SDLK_M: audio_engine_toggle_metronome(&app->audio); break;
        case SDLK_LEFT: waveform_pan(&app->view,-0.04); break;
        case SDLK_RIGHT: waveform_pan(&app->view,0.04); break;
        case SDLK_UP: waveform_zoom(&app->view,1.15); break;
        case SDLK_DOWN: waveform_zoom(&app->view,1.0/1.15); break;
        case SDLK_A: move_loop(app,true,-1,coarse); break;
        case SDLK_D: move_loop(app,true,1,coarse); break;
        case SDLK_J: move_loop(app,false,-1,coarse); break;
        case SDLK_L: move_loop(app,false,1,coarse); break;
        case SDLK_1: waveform_focus(&app->view, (double)app->clip.loop_start_frame / (double)(app->clip.frame_count-1)); break;
        case SDLK_2: waveform_focus(&app->view, (double)app->clip.loop_end_frame / (double)(app->clip.frame_count-1)); break;
        case SDLK_R: clip_reset_loop(&app->clip); break;
        case SDLK_HOME: audio_engine_set_playhead(&app->audio, app->clip.loop_start_frame); break;
        default: break;
    }
    return true;
}

void input_handle_gamepad(App *app, float dt){ (void)dt; if(!app->gamepad) return; if(SDL_GetGamepadButton(app->gamepad,SDL_GAMEPAD_BUTTON_SOUTH)) transport_set_playing(&app->transport,!app->transport.playing); }
