#include "input.h"
#include "app.h"
#include <math.h>

static void nudge(size_t *v, long d, size_t minv, size_t maxv){ long nv=(long)(*v)+d; if(nv<(long)minv)nv=(long)minv; if(nv>(long)maxv)nv=(long)maxv; *v=(size_t)nv; }
static int gamepad_edit_target = 0;
static bool previous_buttons[SDL_GAMEPAD_BUTTON_COUNT];
static Uint64 quit_confirm_until_ns = 0;
static int tempo_bpm_dpad_direction = 0;
static double tempo_bpm_dpad_repeat_timer = 0.0;

static const double TEMPO_LOCK_BPM_DPAD_NUDGE = 0.1;
static const double TEMPO_LOCK_BPM_DPAD_REPEAT_DELAY = 0.35;
static const double TEMPO_LOCK_BPM_DPAD_REPEAT_INTERVAL = 0.12;

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

static bool confirm_quit(App *app) {
    Uint64 now = SDL_GetTicksNS();
    if (quit_confirm_until_ns && now <= quit_confirm_until_ns) {
        quit_confirm_until_ns = 0;
        return true;
    }
    quit_confirm_until_ns = now + SDL_NS_PER_SECOND * 2;
    SDL_strlcpy(app->status_text, "Press Escape again to quit", sizeof(app->status_text));
    return false;
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
    app_note_loop_anchors_moved(app);
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
    app_note_loop_anchors_moved(app);
    jump_to_loop_start(app);
}

static long tempo_anchor_step(App *app, double fraction) {
    return visible_fraction_frames(app, fraction);
}

static void reset_tempo_bpm_dpad_repeat(void) {
    tempo_bpm_dpad_direction = 0;
    tempo_bpm_dpad_repeat_timer = 0.0;
}

static void update_tempo_bpm_dpad(App *app, double dt) {
    bool left = SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bool right = SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    int direction = (right ? 1 : 0) - (left ? 1 : 0);

    if(direction == 0) {
        reset_tempo_bpm_dpad_repeat();
        return;
    }

    if(direction != tempo_bpm_dpad_direction) {
        tempo_bpm_dpad_direction = direction;
        tempo_bpm_dpad_repeat_timer = TEMPO_LOCK_BPM_DPAD_REPEAT_DELAY;
        app_adjust_tempo_lock_bpm(app, (double)direction * TEMPO_LOCK_BPM_DPAD_NUDGE);
        return;
    }

    tempo_bpm_dpad_repeat_timer -= dt;
    if(tempo_bpm_dpad_repeat_timer <= 0.0) {
        app_adjust_tempo_lock_bpm(app, (double)direction * TEMPO_LOCK_BPM_DPAD_NUDGE);
        tempo_bpm_dpad_repeat_timer = TEMPO_LOCK_BPM_DPAD_REPEAT_INTERVAL;
    }
}

static bool handle_tempo_lock_key(App *app, SDL_Keycode key, SDL_Keymod mod) {
    switch(key) {
        case SDLK_ESCAPE: app_cancel_tempo_lock_mode(app); return true;
        case SDLK_RETURN: app_apply_tempo_lock(app); return true;
        case SDLK_U: app_clear_tempo_lock(app); return true;
        case SDLK_T:
            if(mod & SDL_KMOD_CTRL) app_clear_tempo_lock(app);
            else app_cancel_tempo_lock_mode(app);
            return true;
        case SDLK_LEFTBRACKET: app_adjust_tempo_lock_bpm(app, -0.5); return true;
        case SDLK_RIGHTBRACKET: app_adjust_tempo_lock_bpm(app, 0.5); return true;
        case SDLK_COMMA: app_cycle_tempo_lock_target_bars(app, -1); return true;
        case SDLK_PERIOD: app_cycle_tempo_lock_target_bars(app, 1); return true;
        case SDLK_B: app_adjust_tempo_lock_downbeat(app, -tempo_anchor_step(app, 0.0025)); return true;
        case SDLK_V: app_adjust_tempo_lock_downbeat(app, tempo_anchor_step(app, 0.0025)); return true;
        case SDLK_N: app_adjust_tempo_lock_downbeat(app, (mod & SDL_KMOD_SHIFT) ? tempo_anchor_step(app, 0.0025) : -tempo_anchor_step(app, 0.0025)); return true;
        case SDLK_M: app_cycle_tempo_lock_meter(app, (mod & SDL_KMOD_SHIFT) ? -1 : 1); return true;
        default: return false;
    }
}

static bool handle_timeline_key(App *app, SDL_Keycode key, SDL_Keymod mod) {
    switch(key) {
        case SDLK_ESCAPE:
            if(app->timeline_play_range_adjusting) app_timeline_cancel_focus(app);
            else if(confirm_quit(app)) return false;
            return true;
        case SDLK_RETURN: app_timeline_activate_focus(app); return true;
        case SDLK_SPACE: app_toggle_timeline_playback(app); return true;
        case SDLK_M: app->transport.metronome_enabled=!app->transport.metronome_enabled; return true;
        case SDLK_HOME: app_rewind_timeline(app); return true;
        case SDLK_R:
            if(app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_reset_play_range(app);
            return true;
        case SDLK_1:
            if(app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_select_play_range_handle(app, TIMELINE_RANGE_HANDLE_START);
            return true;
        case SDLK_2:
            if(app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_select_play_range_handle(app, TIMELINE_RANGE_HANDLE_END);
            return true;
        case SDLK_LEFT:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, -0.12);
                else app_timeline_nudge_edit_ghost(app, -1);
            } else if(app->timeline_play_range_adjusting) app_timeline_nudge_play_range(app, -1);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER || app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, -0.12);
                else app_timeline_move_cursor(app, -1);
            }
            return true;
        case SDLK_RIGHT:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, 0.12);
                else app_timeline_nudge_edit_ghost(app, 1);
            } else if(app->timeline_play_range_adjusting) app_timeline_nudge_play_range(app, 1);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER || app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, 0.12);
                else app_timeline_move_cursor(app, 1);
            }
            return true;
        case SDLK_UP:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) app_zoom_timeline_view(app, 0.8);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) app_timeline_select_roster_delta(app, -1);
            else app_zoom_timeline_view(app, 0.8);
            return true;
        case SDLK_DOWN:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) app_zoom_timeline_view(app, 1.25);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) app_timeline_select_roster_delta(app, 1);
            else app_zoom_timeline_view(app, 1.25);
            return true;
        default: return true;
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
    if(e->key.key==SDLK_F1) {
        app_toggle_controls_legend(app);
        return true;
    }
    if(app->controls_legend_open) {
        if(e->key.key==SDLK_ESCAPE) app->controls_legend_open = false;
        return true;
    }
    if(app->sample_selector_open) {
        switch(e->key.key) {
            case SDLK_ESCAPE: app->sample_selector_open=false; break;
            case SDLK_TAB: app->sample_selector_open=false; break;
            case SDLK_UP: app_select_sample_delta(app, -1); break;
            case SDLK_DOWN: app_select_sample_delta(app, 1); break;
            case SDLK_RETURN: if(app_load_selected_sample(app)) app->sample_selector_open=false; break;
            case SDLK_R: app_refresh_sample_list(app); break;
            default: break;
        }
        return true;
    }
    SDL_Keymod mod = SDL_GetModState();
    if(e->key.key==SDLK_F2) {
        app_toggle_view_mode(app);
        return true;
    }
    if(e->key.key==SDLK_TAB) {
        if(app->view_mode == APP_VIEW_TIMELINE) app_timeline_cycle_focus(app, (mod & SDL_KMOD_SHIFT) ? -1 : 1);
        else app->sample_selector_open = !app->sample_selector_open;
        return true;
    }
    if(app->view_mode == APP_VIEW_TIMELINE) return handle_timeline_key(app, e->key.key, mod);
    if(app->tempo_lock_mode) return handle_tempo_lock_key(app, e->key.key, mod);
    if(e->key.key==SDLK_U || (e->key.key==SDLK_T && (mod & SDL_KMOD_CTRL))) {
        app_clear_tempo_lock(app);
        return true;
    }
    if(e->key.key==SDLK_T) {
        app_enter_tempo_lock_mode(app);
        return true;
    }
    long step=(mod & SDL_KMOD_SHIFT)?visible_fraction_frames(app, 0.05):visible_fraction_frames(app, 0.0025);
    switch(e->key.key){
        case SDLK_ESCAPE:
            if(app->controls_legend_open) {
                app->controls_legend_open = false;
                break;
            }
            if(confirm_quit(app)) return false;
            break;
        case SDLK_SPACE: toggle_playing(app); break;
        case SDLK_M: app->transport.metronome_enabled=!app->transport.metronome_enabled; break;
        case SDLK_LEFTBRACKET: app_adjust_transport_bpm(app, -0.5); break;
        case SDLK_RIGHTBRACKET: app_adjust_transport_bpm(app, 0.5); break;
        case SDLK_LEFT: app->view.target_center -= 0.12*app->view.target_span; break;
        case SDLK_RIGHT: app->view.target_center += 0.12*app->view.target_span; break;
        case SDLK_UP: app->view.target_span *= 0.8; break;
        case SDLK_DOWN: app->view.target_span *= 1.25; break;
        case SDLK_A: nudge_loop_edge(app, 0, -step); break;
        case SDLK_D: nudge_loop_edge(app, 0, step); break;
        case SDLK_J: nudge_loop_edge(app, 1, -step); break;
        case SDLK_L: nudge_loop_edge(app, 1, step); break;
        case SDLK_1: gamepad_edit_target=0; app_focus_loop_start(app); break;
        case SDLK_2: gamepad_edit_target=1; app_focus_loop_end(app); break;
        case SDLK_R: app->clip.loop_start_frame=0; app->clip.loop_end_frame=app->clip.frame_count; app_note_loop_anchors_moved(app); break;
        case SDLK_HOME: jump_to_loop_start(app); break;
    }
    clamp_view_target(app);
    return true;
}

void input_update_gamepad(App *app, double dt){
    if(!app->gamepad) return;
    if(!app->tempo_lock_mode) reset_tempo_bpm_dpad_repeat();

    if(app->controls_legend_open) {
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_EAST) ||
           button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK)) {
            app->controls_legend_open = false;
        }
        return;
    }

    if(app->sample_selector_open) {
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_select_sample_delta(app, -1);
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_select_sample_delta(app, 1);
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_SOUTH) && app_load_selected_sample(app)) app->sample_selector_open=false;
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_EAST)) app->sample_selector_open=false;
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK)) app->transport.metronome_enabled=!app->transport.metronome_enabled;
        return;
    }

    double lx = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    double ly = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    double rx = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    double ry = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    double left_trigger = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    double right_trigger = axis_value(app->gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if(left_trigger < 0.0) left_trigger = 0.0;
    if(right_trigger < 0.0) right_trigger = 0.0;
    bool l2_shift = left_trigger > 0.65;
    bool r2_shift = right_trigger > 0.65;

    bool south_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    bool east_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_EAST);
    bool west_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_WEST);
    bool north_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_NORTH);
    bool start_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_START);
    bool back_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK);
    bool left_shoulder_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    bool right_shoulder_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    bool left_stick_pressed = button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);

    if(r2_shift && start_pressed) {
        app_toggle_view_mode(app);
        return;
    }

    if(app->view_mode == APP_VIEW_TIMELINE) {
        if(r2_shift) {
            if(south_pressed) app_toggle_timeline_playback(app);
            if(east_pressed) app_rewind_timeline(app);
            if(west_pressed) app_timeline_jump_to_play_range_start(app);
            if(north_pressed) app_timeline_toggle_play_range_loop(app);
            if(south_pressed || east_pressed || west_pressed || north_pressed) return;
        }

        if(back_pressed) app->transport.metronome_enabled=!app->transport.metronome_enabled;
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) app->sample_selector_open=true;
        if(left_shoulder_pressed) app_timeline_cycle_focus(app, -1);
        if(right_shoulder_pressed) app_timeline_cycle_focus(app, 1);

        if(south_pressed) app_timeline_activate_focus(app);
        if(east_pressed) app_timeline_cancel_focus(app);
        if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
            app_pan_timeline_view(app, lx * dt * 0.9);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_timeline_nudge_edit_ghost(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_timeline_nudge_edit_ghost(app, 1);
            if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_zoom_timeline_view(app, 1.0 - fmin(0.9, dt * 1.8));
            if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_zoom_timeline_view(app, 1.0 + dt * 1.8);
            return;
        }
        if(left_stick_pressed && app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_reset_play_range(app);
        if(west_pressed && app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_select_play_range_handle(app, TIMELINE_RANGE_HANDLE_START);
        if(north_pressed && app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_select_play_range_handle(app, TIMELINE_RANGE_HANDLE_END);

        if(app->timeline_play_range_adjusting) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_timeline_nudge_play_range(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_timeline_nudge_play_range(app, 1);
            return;
        }

        if(app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_select_roster_delta(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_select_roster_delta(app, 1);
            return;
        }

        if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER || app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
            app_pan_timeline_view(app, lx * dt * 0.9);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_timeline_move_cursor(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_timeline_move_cursor(app, 1);
            if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_zoom_timeline_view(app, 1.0 - fmin(0.9, dt * 1.8));
            if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_zoom_timeline_view(app, 1.0 + dt * 1.8);
            return;
        }

        return;
    }

    if(app->tempo_lock_mode) {
        if(r2_shift && north_pressed) app_cancel_tempo_lock_mode(app);
        else if(r2_shift && east_pressed) app_clear_tempo_lock(app);
        else if(south_pressed) app_apply_tempo_lock(app);
        else if(east_pressed) app_cancel_tempo_lock_mode(app);
        else if(north_pressed) app_cycle_tempo_lock_meter(app, 1);
        else if(west_pressed) app_cycle_tempo_lock_meter(app, -1);
        if(back_pressed) app->transport.metronome_enabled=!app->transport.metronome_enabled;

        update_tempo_bpm_dpad(app, dt);
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_cycle_tempo_lock_target_bars(app, 1);
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_cycle_tempo_lock_target_bars(app, -1);

        long anchor_frames = (long)(lx * (double)visible_frame_count(app) * 0.35 * dt);
        long bumper_frames = (long)((double)visible_frame_count(app) * 0.08 * dt);
        if(bumper_frames < 1) bumper_frames = 1;
        if(left_shoulder_pressed || SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) anchor_frames -= bumper_frames;
        if(right_shoulder_pressed || SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) anchor_frames += bumper_frames;
        if(anchor_frames != 0) app_adjust_tempo_lock_downbeat(app, anchor_frames);

        app->view.target_center += rx * app->view.target_span * dt * 0.9;
        app->view.target_span += ry * app->view.target_span * dt * 1.4;
        clamp_view_target(app);
        return;
    }

    if(r2_shift) {
        if(l2_shift && south_pressed) app_capture_current_loop_to_roster(app);
        else if(south_pressed) set_loop_to_visible(app);
        if(north_pressed) app_enter_tempo_lock_mode(app);
        if(east_pressed) app_clear_tempo_lock(app);
        if(south_pressed || north_pressed || east_pressed) return;
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
    if(back_pressed) app->transport.metronome_enabled=!app->transport.metronome_enabled;
    if(left_shoulder_pressed) {
        gamepad_edit_target=0;
        app_focus_loop_start(app);
    }
    if(right_shoulder_pressed) {
        gamepad_edit_target=1;
        app_focus_loop_end(app);
    }
    if(left_stick_pressed) { clip_reset_loop(&app->clip); app_note_loop_anchors_moved(app); }

    app->view.target_center += lx * app->view.target_span * dt * 0.9;
    app->view.target_span += ly * app->view.target_span * dt * 1.4;

    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app->view.target_span *= 1.0 - fmin(0.9, dt * 1.8);
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app->view.target_span *= 1.0 + dt * 1.8;

    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        gamepad_edit_target = 0;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        gamepad_edit_target = 1;
    }
    long dpad_step = (long)((double)visible_frame_count(app) * 0.1 * dt * (1.0 - left_trigger * 0.8));
    if(dpad_step < 1) dpad_step = 1;
    long trim_frames = 0;
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) trim_frames -= dpad_step;
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) trim_frames += dpad_step;
    if(trim_frames != 0) nudge_loop_edge(app, gamepad_edit_target, trim_frames);

    clamp_view_target(app);
}
