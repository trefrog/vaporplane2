#include "input.h"

void input_handle_event(App *app, const SDL_Event *e) {
    if (e->type == SDL_EVENT_QUIT) app->running = false;
    if (e->type != SDL_EVENT_KEY_DOWN) return;
    bool shift = (e->key.mod & SDL_KMOD_SHIFT) != 0;
    int64_t fine = shift ? 2048 : 256;
    switch (e->key.key) {
        case SDLK_ESCAPE: app->running = false; break;
        case SDLK_SPACE: transport_toggle_playing(&app->transport); audio_engine_set_playing(&app->audio, app->transport.playing); break;
        case SDLK_M: transport_toggle_metronome(&app->transport); break;
        case SDLK_LEFT: waveform_pan(&app->view, -app->view.zoom_frames * 0.1, &app->clip); break;
        case SDLK_RIGHT: waveform_pan(&app->view, app->view.zoom_frames * 0.1, &app->clip); break;
        case SDLK_UP: waveform_zoom(&app->view, 0.8, &app->clip); break;
        case SDLK_DOWN: waveform_zoom(&app->view, 1.25, &app->clip); break;
        case SDLK_A: clip_move_loop_start(&app->clip, -fine); break;
        case SDLK_D: clip_move_loop_start(&app->clip, fine); break;
        case SDLK_J: clip_move_loop_end(&app->clip, -fine); break;
        case SDLK_L: clip_move_loop_end(&app->clip, fine); break;
        case SDLK_1: waveform_focus(&app->view, app->clip.loop_start_frame, &app->clip); break;
        case SDLK_2: waveform_focus(&app->view, app->clip.loop_end_frame, &app->clip); break;
        case SDLK_R: clip_reset_loop(&app->clip); break;
        case SDLK_HOME: audio_engine_jump_to_loop_start(&app->audio); transport_seek_seconds(&app->transport, 0.0); break;
        default: break;
    }
}
