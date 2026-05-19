#include "input.h"
#include "app.h"
#include <math.h>

static void nudge(size_t *v, long d, size_t minv, size_t maxv){ long nv=(long)(*v)+d; if(nv<(long)minv)nv=(long)minv; if(nv>(long)maxv)nv=(long)maxv; *v=(size_t)nv; }
static int gamepad_edit_target = 0;
static bool previous_buttons[SDL_GAMEPAD_BUTTON_COUNT];

static void clamp_view(App *app) {
    if(app->view.view_span<0.01) app->view.view_span=0.01;
    if(app->view.view_span>1.0) app->view.view_span=1.0;
    double half = app->view.view_span * 0.5;
    if(app->view.view_center<half) app->view.view_center=half;
    if(app->view.view_center>1.0-half) app->view.view_center=1.0-half;
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
    if(target == 0) {
        nudge(&app->clip.loop_start_frame, frames, 0, app->clip.loop_end_frame - 1);
    } else {
        nudge(&app->clip.loop_end_frame, frames, app->clip.loop_start_frame + 1, app->clip.frame_count);
    }
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
    SDL_Keymod mod = SDL_GetModState(); long step=(mod & SDL_KMOD_SHIFT)?5000:500;
    switch(e->key.key){
        case SDLK_ESCAPE: return false;
        case SDLK_SPACE: toggle_playing(app); break;
        case SDLK_M: app->transport.metronome_enabled=!app->transport.metronome_enabled; break;
        case SDLK_LEFT: app->view.view_center -= 0.03*app->view.view_span; break;
        case SDLK_RIGHT: app->view.view_center += 0.03*app->view.view_span; break;
        case SDLK_UP: app->view.view_span *= 0.9; break;
        case SDLK_DOWN: app->view.view_span *= 1.1; break;
        case SDLK_A: nudge(&app->clip.loop_start_frame,-step,0,app->clip.loop_end_frame-1); break;
        case SDLK_D: nudge(&app->clip.loop_start_frame,step,0,app->clip.loop_end_frame-1); break;
        case SDLK_J: nudge(&app->clip.loop_end_frame,-step,app->clip.loop_start_frame+1,app->clip.frame_count); break;
        case SDLK_L: nudge(&app->clip.loop_end_frame,step,app->clip.loop_start_frame+1,app->clip.frame_count); break;
        case SDLK_1: gamepad_edit_target=0; app_focus_loop_start(app); break;
        case SDLK_2: gamepad_edit_target=1; app_focus_loop_end(app); break;
        case SDLK_R: app->clip.loop_start_frame=0; app->clip.loop_end_frame=app->clip.frame_count; break;
        case SDLK_HOME: jump_to_loop_start(app); break;
    }
    clamp_view(app);
    return true;
}

void input_update_gamepad(App *app, double dt){
    if(!app->gamepad) return;

    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_SOUTH) || button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_START)) toggle_playing(app);
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_EAST)) jump_to_loop_start(app);
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK)) app->transport.metronome_enabled=!app->transport.metronome_enabled;
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_WEST) || button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        gamepad_edit_target=0;
        app_focus_loop_start(app);
    }
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_NORTH) || button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        gamepad_edit_target=1;
        app_focus_loop_end(app);
    }
    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) clip_reset_loop(&app->clip);

    double lx = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    double ly = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    double rx = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    double left_trigger = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    double right_trigger = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if(left_trigger < 0.0) left_trigger = 0.0;
    if(right_trigger < 0.0) right_trigger = 0.0;

    app->view.view_center += lx * app->view.view_span * dt * 0.9;
    app->view.view_span += ly * app->view.view_span * dt * 1.4;

    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app->view.view_span *= 1.0 - fmin(0.9, dt * 1.8);
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app->view.view_span *= 1.0 + dt * 1.8;

    double trim_speed = (double)app->clip.sample_rate * (0.25 + right_trigger * 3.0);
    trim_speed *= 1.0 - left_trigger * 0.8;
    long trim_frames = (long)(rx * trim_speed * dt);
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        gamepad_edit_target = 0;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        gamepad_edit_target = 1;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) trim_frames -= (long)(trim_speed * dt * 0.35);
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) trim_frames += (long)(trim_speed * dt * 0.35);
    if(trim_frames != 0) nudge_loop_edge(app, gamepad_edit_target, trim_frames);

    clamp_view(app);
}
