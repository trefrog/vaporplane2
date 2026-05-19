#include "input.h"
#include "app.h"

static void nudge(size_t *v, long d, size_t minv, size_t maxv){ long nv=(long)(*v)+d; if(nv<(long)minv)nv=(long)minv; if(nv>(long)maxv)nv=(long)maxv; *v=(size_t)nv; }

bool input_handle_event(App *app, const SDL_Event *e){
    if(e->type==SDL_EVENT_QUIT) return false;
    if(e->type==SDL_EVENT_GAMEPAD_ADDED && !app->gamepad) app->gamepad=SDL_OpenGamepad(e->gdevice.which);
    if(e->type!=SDL_EVENT_KEY_DOWN) return true;
    SDL_Keymod mod = SDL_GetModState(); long step=(mod & SDL_KMOD_SHIFT)?5000:500;
    switch(e->key.key){
        case SDLK_ESCAPE: return false;
        case SDLK_SPACE: app->transport.playing=!app->transport.playing; break;
        case SDLK_M: app->transport.metronome_enabled=!app->transport.metronome_enabled; break;
        case SDLK_LEFT: app->view.view_center -= 0.03*app->view.view_span; break;
        case SDLK_RIGHT: app->view.view_center += 0.03*app->view.view_span; break;
        case SDLK_UP: app->view.view_span *= 0.9; break;
        case SDLK_DOWN: app->view.view_span *= 1.1; break;
        case SDLK_A: nudge(&app->clip.loop_start_frame,-step,0,app->clip.loop_end_frame-1); break;
        case SDLK_D: nudge(&app->clip.loop_start_frame,step,0,app->clip.loop_end_frame-1); break;
        case SDLK_J: nudge(&app->clip.loop_end_frame,-step,app->clip.loop_start_frame+1,app->clip.frame_count); break;
        case SDLK_L: nudge(&app->clip.loop_end_frame,step,app->clip.loop_start_frame+1,app->clip.frame_count); break;
        case SDLK_1: app_focus_loop_start(app); break;
        case SDLK_2: app_focus_loop_end(app); break;
        case SDLK_R: app->clip.loop_start_frame=0; app->clip.loop_end_frame=app->clip.frame_count; break;
        case SDLK_HOME: audio_engine_set_playhead(&app->audio, app->clip.loop_start_frame); transport_jump_to_seconds(&app->transport,0); break;
    }
    if(app->view.view_span<0.01) app->view.view_span=0.01; if(app->view.view_span>1.0) app->view.view_span=1.0;
    return true;
}

void input_update_gamepad(App *app, double dt){ (void)dt; if(!app->gamepad) return; if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) app->transport.playing=true; }
