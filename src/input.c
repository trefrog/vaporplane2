#include "input.h"
#include "app.h"
#include <math.h>

static void nudge(size_t *v, long d, size_t minv, size_t maxv){ long nv=(long)(*v)+d; if(nv<(long)minv)nv=(long)minv; if(nv>(long)maxv)nv=(long)maxv; *v=(size_t)nv; }
static int gamepad_edit_target = 0;
static bool previous_buttons[SDL_GAMEPAD_BUTTON_COUNT];

static void clamp_view_target(App *app) {
    if(app->view.target_span<0.01) app->view.target_span=0.01;
    if(app->view.target_span>1.0) app->view.target_span=1.0;
    double half = app->view.target_span * 0.5;
    if(app->view.target_center<half) app->view.target_center=half;
    if(app->view.target_center>1.0-half) app->view.target_center=1.0-half;
}

static void set_playing(App *app, bool playing) {
    app->transport.playing = playing;
}

static void toggle_playing(App *app) {
    set_playing(app, !app->transport.playing);
}

static void jump_to_loop_start(App *app) {
    audio_engine_set_playhead(&app->audio, app->clip.loop_start_frame);
    transport_jump_to_seconds(&app->transport, 0);
}

static void nudge_loop_edge(App *app, int target, long frames) {
    if(app->clip.frame_count < 2) return;
    if(target == 0) {
        nudge(&app->clip.loop_start_frame, frames, 0, app->clip.loop_end_frame - 1);
    } else {
        nudge(&app->clip.loop_end_frame, frames, app->clip.loop_start_frame + 1, app->clip.frame_count);
    }
}

static size_t visible_frame_count(App *app) {
    size_t start = 0, end = 0;
    waveform_view_get_frame_bounds(&app->view, &app->clip, &start, &end);
    return end > start ? end - start : 1;
}

static long visible_fraction_frames(App *app, double fraction) {
    long frames = (long)((double)visible_frame_count(app) * fraction);
    if(frames < 1) frames = 1;
    return frames;
}

static void set_loop_to_visible(App *app) {
    size_t start = 0, end = 0;
    waveform_view_get_frame_bounds(&app->view, &app->clip, &start, &end);
    if(app->clip.frame_count < 2) return;
    if(end > app->clip.frame_count) end = app->clip.frame_count;
    if(end <= start) end = start + 1;
    if(end > app->clip.frame_count) {
        end = app->clip.frame_count;
        start = end > 1 ? end - 1 : 0;
    }
    app->clip.loop_start_frame = start;
    app->clip.loop_end_frame = end;
    clip_clamp_loop(&app->clip);
    jump_to_loop_start(app);
}

static bool button_pressed(SDL_Gamepad *gamepad, SDL_GamepadButton button) {
    bool down = SDL_GetGamepadButton(gamepad, button);
    bool pressed = down && !previous_buttons[button];
    previous_buttons[button] = down;
    return pressed;
}

static double axis_value(SDL_Gamepad *gamepad, SDL_GamepadAxis axis) {
    const double deadzone = 8000.0;
    double v = (double)SDL_GetGamepadAxis(gamepad, axis);
    if(fabs(v) < deadzone) return 0.0;
    return v / (v < 0.0 ? 32768.0 : 32767.0);
}

bool input_handle_event(App *app, const SDL_Event *e){
    if(e->type==SDL_EVENT_QUIT) return false;
    if(e->type==SDL_EVENT_GAMEPAD_ADDED && !app->gamepad) {
        app->gamepad=SDL_OpenGamepad(e->gdevice.which);
        if(app->gamepad) app->gamepad_id=SDL_GetGamepadID(app->gamepad);
    }
    if(e->type==SDL_EVENT_GAMEPAD_REMOVED && app->gamepad && e->gdevice.which==app->gamepad_id) {
        SDL_CloseGamepad(app->gamepad);
        app->gamepad=NULL;
        app->gamepad_id=0;
        SDL_memset(previous_buttons, 0, sizeof(previous_buttons));
    }
    if(e->type!=SDL_EVENT_KEY_DOWN) return true;
    if(e->key.key==SDLK_TAB) {
        app->sample_selector_open = !app->sample_selector_open;
        return true;
    }
    if(app->sample_selector_open) {
        switch(e->key.key) {
            case SDLK_ESCAPE: app->sample_selector_open=false; break;
            case SDLK_UP: app_select_sample_delta(app, -1); break;
            case SDLK_DOWN: app_select_sample_delta(app, 1); break;
            case SDLK_RETURN: if(app_load_selected_sample(app)) app->sample_selector_open=false; break;
            case SDLK_R: app_refresh_sample_list(app); break;
            default: break;
        }
        return true;
    }
    SDL_Keymod mod = SDL_GetModState();
    long step=(mod & SDL_KMOD_SHIFT)?visible_fraction_frames(app, 0.05):visible_fraction_frames(app, 0.0025);
    switch(e->key.key){
        case SDLK_ESCAPE: return false;
        case SDLK_SPACE: toggle_playing(app); break;
        case SDLK_M: app->transport.metronome_enabled=!app->transport.metronome_enabled; break;
        case SDLK_LEFT: app->view.target_center -= 0.12*app->view.target_span; break;
        case SDLK_RIGHT: app->view.target_center += 0.12*app->view.target_span; break;
        case SDLK_UP: app->view.target_span *= 0.8; break;
        case SDLK_DOWN: app->view.target_span *= 1.25; break;
        case SDLK_A: nudge(&app->clip.loop_start_frame,-step,0,app->clip.loop_end_frame-1); break;
        case SDLK_D: nudge(&app->clip.loop_start_frame,step,0,app->clip.loop_end_frame-1); break;
        case SDLK_J: nudge(&app->clip.loop_end_frame,-step,app->clip.loop_start_frame+1,app->clip.frame_count); break;
        case SDLK_L: nudge(&app->clip.loop_end_frame,step,app->clip.loop_start_frame+1,app->clip.frame_count); break;
        case SDLK_1: gamepad_edit_target=0; app_focus_loop_start(app); break;
        case SDLK_2: gamepad_edit_target=1; app_focus_loop_end(app); break;
        case SDLK_R: app->clip.loop_start_frame=0; app->clip.loop_end_frame=app->clip.frame_count; break;
        case SDLK_HOME: jump_to_loop_start(app); break;
    }
    clamp_view_target(app);
    return true;
}

void input_update_gamepad(App *app, double dt){
    if(!app->gamepad) return;

    if(app->sample_selector_open) {
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_select_sample_delta(app, -1);
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_select_sample_delta(app, 1);
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_SOUTH) && app_load_selected_sample(app)) app->sample_selector_open=false;
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_EAST)) app->sample_selector_open=false;
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK)) app_refresh_sample_list(app);
        return;
    }

    double lx = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    double ly = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    double rx = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    double left_trigger = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    double right_trigger = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if(left_trigger < 0.0) left_trigger = 0.0;
    if(right_trigger < 0.0) right_trigger = 0.0;
    bool r2_shift = right_trigger > 0.65;

    bool south_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    bool east_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_EAST);
    bool west_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_WEST);
    bool north_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_NORTH);
    bool start_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_START);

    if(r2_shift) {
        if(south_pressed) set_loop_to_visible(app);
    } else {
        if(south_pressed || start_pressed) toggle_playing(app);
        if(east_pressed) jump_to_loop_start(app);
        if(west_pressed) {
            gamepad_edit_target=0;
            app_focus_loop_start(app);
        }
        if(north_pressed) {
            gamepad_edit_target=1;
            app_focus_loop_end(app);
        }
    }

    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) app->sample_selector_open=true;
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK)) app->transport.metronome_enabled=!app->transport.metronome_enabled;
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        gamepad_edit_target=0;
        app_focus_loop_start(app);
    }
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        gamepad_edit_target=1;
        app_focus_loop_end(app);
    }
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) clip_reset_loop(&app->clip);

    app->view.target_center += lx * app->view.target_span * dt * 0.9;
    app->view.target_span += ly * app->view.target_span * dt * 1.4;

    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app->view.target_span *= 1.0 - fmin(0.9, dt * 1.8);
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app->view.target_span *= 1.0 + dt * 1.8;

    double trim_speed = (double)visible_frame_count(app) * 0.9;
    trim_speed *= 1.0 - left_trigger * 0.8;
    long trim_frames = (long)(rx * trim_speed * dt);
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        gamepad_edit_target = 0;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        gamepad_edit_target = 1;
    }
    long dpad_step = (long)((double)visible_frame_count(app) * 0.1 * dt * (1.0 - left_trigger * 0.8));
    if(dpad_step < 1) dpad_step = 1;
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) trim_frames -= dpad_step;
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) trim_frames += dpad_step;
    if(trim_frames != 0) nudge_loop_edge(app, gamepad_edit_target, trim_frames);

    clamp_view_target(app);
}
