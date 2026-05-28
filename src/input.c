#include "input.h"
#include "app.h"
#include <math.h>

static void nudge(size_t *v, long d, size_t minv, size_t maxv){ long nv=(long)(*v)+d; if(nv<(long)minv)nv=(long)minv; if(nv>(long)maxv)nv=(long)maxv; *v=(size_t)nv; }
static int gamepad_edit_target = 0;
static bool previous_buttons[SDL_GAMEPAD_BUTTON_COUNT];
static Uint64 quit_confirm_until_ns = 0;
static int tempo_bpm_dpad_direction = 0;
static bool tempo_bpm_dpad_coarse = false;
static double tempo_bpm_dpad_repeat_timer = 0.0;
static int timeline_cursor_stick_direction = 0;
static double timeline_cursor_stick_repeat_timer = 0.0;
static double timeline_cursor_stick_held_seconds = 0.0;

static bool button_pressed(SDL_Gamepad *gamepad, SDL_GamepadButton button);

static const double TEMPO_LOCK_BPM_DPAD_NUDGE = 0.1;
static const double TEMPO_LOCK_BPM_DPAD_COARSE_NUDGE = 1.0;
static const double TEMPO_LOCK_BPM_DPAD_REPEAT_DELAY = 0.35;
static const double TEMPO_LOCK_BPM_DPAD_REPEAT_INTERVAL = 0.12;
static const double TEMPO_LOCK_BPM_DPAD_COARSE_REPEAT_INTERVAL = 0.07;
static const double TIMELINE_CURSOR_STICK_THRESHOLD = 0.28;
static const double TIMELINE_CURSOR_STICK_MAX_HELD = 3.0;
static const double WAVEFORM_FRAME_GRIP_L2_ARM_SECONDS = 0.12;

static void clamp_view_target(App *app) {
    if(app->view.target_span<0.000000001) app->view.target_span=0.000000001;
    if(app->view.target_span>1.0) app->view.target_span=1.0;
    double half = app->view.target_span * 0.5;
    if(app->view.target_center<half) app->view.target_center=half;
    if(app->view.target_center>1.0-half) app->view.target_center=1.0-half;
}

static void set_playing(App *app, bool playing) {
    if (playing && !app->audio.stream) {
        app->transport.playing = false;
        SDL_strlcpy(app->status_text, "Audio unavailable", sizeof(app->status_text));
        return;
    }
    app->transport.playing = playing;
}

static void toggle_playing(App *app) {
    set_playing(app, !app->transport.playing);
}

static void toggle_metronome(App *app) {
    if (!app->audio.stream) {
        SDL_strlcpy(app->status_text, "Audio unavailable", sizeof(app->status_text));
        return;
    }
    app->transport.metronome_enabled = !app->transport.metronome_enabled;
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

static size_t target_visible_frame_count(App *app) {
    if(!app || app->clip.frame_count < 1) return 1;
    double start = app->view.target_center - app->view.target_span * 0.5;
    double end = app->view.target_center + app->view.target_span * 0.5;
    if(start < 0.0) start = 0.0;
    if(end > 1.0) end = 1.0;
    size_t sf = (size_t)(start * (double)app->clip.frame_count);
    size_t ef = (size_t)ceil(end * (double)app->clip.frame_count);
    if(ef > app->clip.frame_count) ef = app->clip.frame_count;
    return ef > sf ? ef - sf : 1;
}

static bool waveform_frame_grip_available(const App *app) {
    return app &&
           app->view_mode == APP_VIEW_WAVEFORM &&
           app->clip.clip_tempo_locked &&
           !app->tempo_lock_mode &&
           app->clip.samples &&
           app->clip.frame_count > 1 &&
           app->clip.sample_rate > 0 &&
           app->clip.tempo_lock.bpm > 0.0;
}

static void set_view_to_exact_frames(App *app, size_t left, size_t right) {
    if(!app || app->clip.frame_count < 1) return;
    if(left >= app->clip.frame_count) left = app->clip.frame_count - 1;
    if(right > app->clip.frame_count) right = app->clip.frame_count;
    if(right <= left) right = left + 1 <= app->clip.frame_count ? left + 1 : app->clip.frame_count;
    double start = (double)left / (double)app->clip.frame_count;
    double end = (double)right / (double)app->clip.frame_count;
    double span = end - start;
    double center = start + span * 0.5;
    app->view.target_center = center;
    app->view.target_span = span;
    app->view.view_center = center;
    app->view.view_span = span;
    clamp_view_target(app);
}

static void waveform_frame_grip_set_right(App *app, size_t right, bool clear_snap) {
    if(!app || app->clip.frame_count < 2) return;
    size_t left = app->waveform_frame_grip_left_frame;
    if(left >= app->clip.frame_count) left = app->clip.frame_count - 1;
    if(right > app->clip.frame_count) right = app->clip.frame_count;
    if(right <= left) right = left + 1 <= app->clip.frame_count ? left + 1 : app->clip.frame_count;
    app->waveform_frame_grip_left_frame = left;
    app->waveform_frame_grip_right_frame = right;
    app->waveform_frame_grip_exact_valid = true;
    if(clear_snap) {
        app->waveform_frame_grip_snap_index = -1;
        app->waveform_frame_grip_snap_beats = 0.0;
    }
    set_view_to_exact_frames(app, left, right);
}

static void waveform_frame_grip_begin(App *app) {
    size_t left = app->clip.loop_start_frame;
    if(left >= app->clip.frame_count) left = app->clip.frame_count - 1;
    size_t length = target_visible_frame_count(app);
    if(length < 1) length = 1;
    size_t right = left + length;
    if(right > app->clip.frame_count) right = app->clip.frame_count;
    if(right <= left) right = left + 1 <= app->clip.frame_count ? left + 1 : app->clip.frame_count;
    app->waveform_frame_grip_active = true;
    app->waveform_frame_grip_snap_active = false;
    app->waveform_frame_grip_snap_index = -1;
    app->waveform_frame_grip_snap_beats = 0.0;
    app->waveform_frame_grip_left_frame = left;
    waveform_frame_grip_set_right(app, right, true);
    SDL_strlcpy(app->status_text, "FRAME GRIP", sizeof(app->status_text));
}

static double waveform_frame_grip_frames_per_beat(const App *app) {
    if(!app || app->clip.sample_rate <= 0 || app->clip.tempo_lock.bpm <= 0.0) return 0.0;
    return (60.0 / app->clip.tempo_lock.bpm) * (double)app->clip.sample_rate;
}

static const double waveform_frame_snap_beats[] = { 1.0, 2.0, 3.0, 4.0, 8.0, 16.0 };

static int waveform_frame_snap_count(void) {
    return (int)(sizeof(waveform_frame_snap_beats) / sizeof(waveform_frame_snap_beats[0]));
}

static bool waveform_frame_snap_right_for_index(const App *app, int index, size_t *right, double *beats) {
    int count = waveform_frame_snap_count();
    if(!app || index < 0 || index >= count) return false;
    double frames_per_beat = waveform_frame_grip_frames_per_beat(app);
    if(frames_per_beat <= 0.0) return false;
    double beat_count = waveform_frame_snap_beats[index];
    size_t length = (size_t)llround(frames_per_beat * beat_count);
    if(length < 1) length = 1;
    size_t left = app->waveform_frame_grip_left_frame;
    if(left >= app->clip.frame_count) return false;
    if(length > app->clip.frame_count - left) return false;
    if(right) *right = left + length;
    if(beats) *beats = beat_count;
    return true;
}

static int waveform_frame_nearest_snap_index(App *app) {
    int best = -1;
    double best_distance = 0.0;
    double current = (double)(app->waveform_frame_grip_right_frame - app->waveform_frame_grip_left_frame);
    int count = waveform_frame_snap_count();
    for(int i = 0; i < count; ++i) {
        size_t right = 0;
        double beats = 0.0;
        if(!waveform_frame_snap_right_for_index(app, i, &right, &beats)) continue;
        double distance = fabs((double)(right - app->waveform_frame_grip_left_frame) - current);
        if(best < 0 || distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

static int waveform_frame_next_snap_index(App *app, int direction) {
    int count = waveform_frame_snap_count();
    int start = app->waveform_frame_grip_snap_index;
    if(start < 0 || start >= count) start = waveform_frame_nearest_snap_index(app);
    if(start < 0) return -1;
    for(int step = 1; step <= count; ++step) {
        int index = (start + direction * step) % count;
        if(index < 0) index += count;
        size_t right = 0;
        double beats = 0.0;
        if(waveform_frame_snap_right_for_index(app, index, &right, &beats)) return index;
    }
    return start;
}

static void waveform_frame_apply_snap_index(App *app, int index) {
    size_t right = 0;
    double beats = 0.0;
    if(!waveform_frame_snap_right_for_index(app, index, &right, &beats)) {
        double frames_per_beat = waveform_frame_grip_frames_per_beat(app);
        right = app->clip.frame_count;
        beats = frames_per_beat > 0.0 ?
            (double)(right - app->waveform_frame_grip_left_frame) / frames_per_beat : 0.0;
        index = -1;
    }
    app->waveform_frame_grip_snap_index = index;
    app->waveform_frame_grip_snap_beats = beats;
    waveform_frame_grip_set_right(app, right, false);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "SNAP FRAME: %.0f beats", beats);
}

static void waveform_frame_grip_update(App *app, double ly, double dt, bool r2_shift) {
    if(!app->waveform_frame_grip_active) waveform_frame_grip_begin(app);

    if(r2_shift) {
        if(!app->waveform_frame_grip_snap_active) {
            app->waveform_frame_grip_snap_active = true;
            int index = waveform_frame_nearest_snap_index(app);
            waveform_frame_apply_snap_index(app, index);
        }
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            waveform_frame_apply_snap_index(app, waveform_frame_next_snap_index(app, -1));
        }
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            waveform_frame_apply_snap_index(app, waveform_frame_next_snap_index(app, 1));
        }
        return;
    }

    app->waveform_frame_grip_snap_active = false;
    size_t left = app->waveform_frame_grip_left_frame;
    size_t right = app->waveform_frame_grip_right_frame;
    double length = right > left ? (double)(right - left) : 1.0;
    bool changed = false;
    if(fabs(ly) > 0.0) {
        length += ly * length * dt * 1.4;
        changed = true;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
        length *= 1.0 - fmin(0.9, dt * 1.8);
        changed = true;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        length *= 1.0 + dt * 1.8;
        changed = true;
    }
    long dpad_step = (long)(length * 0.1 * dt);
    if(dpad_step < 1) dpad_step = 1;
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
        length -= (double)dpad_step;
        changed = true;
    }
    if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
        length += (double)dpad_step;
        changed = true;
    }
    if(length < 1.0) length = 1.0;
    if(left + (size_t)ceil(length) > app->clip.frame_count) {
        length = (double)(app->clip.frame_count - left);
    }
    if(changed) waveform_frame_grip_set_right(app, left + (size_t)llround(length), true);
    else set_view_to_exact_frames(app, left, right);
}

static long visible_fraction_frames(App *app, double fraction) {
    long frames = (long)((double)visible_frame_count(app) * fraction);
    if(frames < 1) frames = 1;
    return frames;
}

static void set_loop_to_visible(App *app) {
    size_t start = 0, end = 0;
    if(app->waveform_frame_grip_exact_valid) {
        start = app->waveform_frame_grip_left_frame;
        end = app->waveform_frame_grip_right_frame;
    } else {
        waveform_view_get_frame_bounds(&app->view, &app->clip, &start, &end);
    }
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
    if(app->clip.clip_tempo_locked && app->waveform_frame_grip_snap_beats > 0.0) {
        int beats_per_bar = app->clip.tempo_lock.beats_per_bar > 0 ? app->clip.tempo_lock.beats_per_bar : 4;
        app->clip.tempo_lock.target_bars = app->waveform_frame_grip_snap_beats / (double)beats_per_bar;
        app->has_retained_tempo_lock_params = true;
        app->retained_tempo_lock = app->clip.tempo_lock;
        app->retained_loop_start_frame = app->clip.loop_start_frame;
        app->retained_loop_end_frame = app->clip.loop_end_frame;
        app->retained_tempo_lock_stale = false;
    }
    app_note_loop_anchors_moved(app);
    app_clear_waveform_frame_grip(app);
    jump_to_loop_start(app);
}

static long tempo_anchor_step(App *app, double fraction) {
    return visible_fraction_frames(app, fraction);
}

static void reset_tempo_bpm_dpad_repeat(void) {
    tempo_bpm_dpad_direction = 0;
    tempo_bpm_dpad_coarse = false;
    tempo_bpm_dpad_repeat_timer = 0.0;
}

static void reset_timeline_cursor_stick_repeat(void) {
    timeline_cursor_stick_direction = 0;
    timeline_cursor_stick_repeat_timer = 0.0;
    timeline_cursor_stick_held_seconds = 0.0;
}

static double timeline_cursor_stick_interval(double strength, double held_seconds) {
    if(strength < 0.0) strength = 0.0;
    if(strength > 1.0) strength = 1.0;
    if(held_seconds > TIMELINE_CURSOR_STICK_MAX_HELD) held_seconds = TIMELINE_CURSOR_STICK_MAX_HELD;
    double steps_per_second = 3.0 + strength * 6.0 + held_seconds * 5.0;
    if(steps_per_second > 24.0) steps_per_second = 24.0;
    return 1.0 / steps_per_second;
}

static void update_timeline_cursor_stick(App *app, double x_axis, double dt) {
    double strength = fabs(x_axis);
    int direction = 0;
    if(strength >= TIMELINE_CURSOR_STICK_THRESHOLD) direction = x_axis > 0.0 ? 1 : -1;
    if(direction == 0) {
        reset_timeline_cursor_stick_repeat();
        return;
    }

    if(direction != timeline_cursor_stick_direction) {
        timeline_cursor_stick_direction = direction;
        timeline_cursor_stick_held_seconds = 0.0;
        app_timeline_move_cursor(app, direction);
        timeline_cursor_stick_repeat_timer = timeline_cursor_stick_interval(strength, timeline_cursor_stick_held_seconds);
        return;
    }

    timeline_cursor_stick_held_seconds += dt;
    timeline_cursor_stick_repeat_timer -= dt;
    int safety = 0;
    while(timeline_cursor_stick_repeat_timer <= 0.0 && safety < 8) {
        app_timeline_move_cursor(app, direction);
        timeline_cursor_stick_repeat_timer += timeline_cursor_stick_interval(strength, timeline_cursor_stick_held_seconds);
        safety++;
    }
}

static void update_tempo_bpm_dpad(App *app, double dt, bool coarse) {
    bool left = SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bool right = SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    int direction = (right ? 1 : 0) - (left ? 1 : 0);
    double nudge = coarse ? TEMPO_LOCK_BPM_DPAD_COARSE_NUDGE : TEMPO_LOCK_BPM_DPAD_NUDGE;
    double repeat_interval = coarse ? TEMPO_LOCK_BPM_DPAD_COARSE_REPEAT_INTERVAL : TEMPO_LOCK_BPM_DPAD_REPEAT_INTERVAL;

    if(direction == 0) {
        reset_tempo_bpm_dpad_repeat();
        return;
    }

    if(direction != tempo_bpm_dpad_direction || coarse != tempo_bpm_dpad_coarse) {
        tempo_bpm_dpad_direction = direction;
        tempo_bpm_dpad_coarse = coarse;
        tempo_bpm_dpad_repeat_timer = TEMPO_LOCK_BPM_DPAD_REPEAT_DELAY;
        app_adjust_tempo_lock_bpm(app, (double)direction * nudge);
        return;
    }

    tempo_bpm_dpad_repeat_timer -= dt;
    if(tempo_bpm_dpad_repeat_timer <= 0.0) {
        app_adjust_tempo_lock_bpm(app, (double)direction * nudge);
        tempo_bpm_dpad_repeat_timer = repeat_interval;
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
    if(app->timeline_context_menu_open) {
        switch(key) {
            case SDLK_ESCAPE:
                app_timeline_close_context_menu(app);
                return true;
            case SDLK_RETURN:
                app_timeline_context_menu_apply(app);
                return true;
            case SDLK_UP:
                app_timeline_context_menu_move(app, -1);
                return true;
            case SDLK_DOWN:
                app_timeline_context_menu_move(app, 1);
                return true;
            default:
                return true;
        }
    }
    switch(key) {
        case SDLK_ESCAPE:
            if(app->timeline_play_range_adjusting) app_timeline_cancel_focus(app);
            else if(confirm_quit(app)) return false;
            return true;
        case SDLK_RETURN: app_timeline_activate_focus(app); return true;
        case SDLK_SPACE: app_toggle_timeline_playback(app); return true;
        case SDLK_M: toggle_metronome(app); return true;
        case SDLK_HOME: app_rewind_timeline(app); return true;
        case SDLK_C: app_timeline_open_context_menu(app); return true;
        case SDLK_LEFTBRACKET:
            if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER) app_timeline_adjust_tempo_event_at_cursor(app, -0.5);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT) app_timeline_adjust_tape_control(app, -1, 1.0);
            else app_timeline_adjust_selected_instance_velocity(app, -5);
            return true;
        case SDLK_RIGHTBRACKET:
            if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER) app_timeline_adjust_tempo_event_at_cursor(app, 0.5);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT) app_timeline_adjust_tape_control(app, 1, 1.0);
            else app_timeline_adjust_selected_instance_velocity(app, 5);
            return true;
        case SDLK_T:
            if(app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT) app_timeline_toggle_tape_control_mode(app);
            return true;
        case SDLK_0:
            if(mod & SDL_KMOD_SHIFT) app_timeline_fit_play_range_view(app);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT) app_timeline_reset_tape_speed(app);
            return true;
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
            } else if(app->timeline_play_range_adjusting) app_timeline_nudge_play_range(app, -1, false);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER ||
                    app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA ||
                    app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, -0.12);
                else app_timeline_move_cursor(app, -1);
            }
            return true;
        case SDLK_RIGHT:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, 0.12);
                else app_timeline_nudge_edit_ghost(app, 1);
            } else if(app->timeline_play_range_adjusting) app_timeline_nudge_play_range(app, 1, false);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER ||
                    app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA ||
                    app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) {
                if(mod & SDL_KMOD_SHIFT) app_pan_timeline_view(app, 0.12);
                else app_timeline_move_cursor(app, 1);
            }
            return true;
        case SDLK_UP:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) app_timeline_nudge_edit_lane(app, -1);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) app_timeline_select_roster_delta(app, -1);
            else if((app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA ||
                     app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) && !(mod & SDL_KMOD_SHIFT)) app_timeline_select_lane_delta(app, -1);
            else app_zoom_timeline_view(app, 0.8);
            return true;
        case SDLK_DOWN:
            if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) app_timeline_nudge_edit_lane(app, 1);
            else if(app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) app_timeline_select_roster_delta(app, 1);
            else if((app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA ||
                     app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) && !(mod & SDL_KMOD_SHIFT)) app_timeline_select_lane_delta(app, 1);
            else app_zoom_timeline_view(app, 1.25);
            return true;
        default: return true;
    }
}

static bool handle_lane_inspector_key(App *app, SDL_Keycode key, SDL_Keymod mod) {
    (void)mod;
    switch(key) {
        case SDLK_ESCAPE: app_close_lane_inspector(app); return true;
        case SDLK_RETURN: app_toggle_inspected_lane_mute(app); return true;
        case SDLK_LEFT: app_cycle_inspected_lane_palette(app, -1); return true;
        case SDLK_RIGHT: app_cycle_inspected_lane_palette(app, 1); return true;
        case SDLK_SPACE: app_toggle_timeline_playback(app); return true;
        case SDLK_HOME: app_rewind_timeline(app); return true;
        case SDLK_M: toggle_metronome(app); return true;
        default: return true;
    }
}

static bool handle_master_mix_key(App *app, SDL_Keycode key, SDL_Keymod mod) {
    bool fine = (mod & SDL_KMOD_SHIFT) != 0;
    switch(key) {
        case SDLK_ESCAPE: return true;
        case SDLK_TAB: app_master_mix_cycle_focus(app, (mod & SDL_KMOD_SHIFT) ? -1 : 1); return true;
        case SDLK_UP:
            if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) app_master_reverb_select_param_delta(app, -1);
            else app_master_mix_cycle_focus(app, -1);
            return true;
        case SDLK_DOWN:
            if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) app_master_reverb_select_param_delta(app, 1);
            else app_master_mix_cycle_focus(app, 1);
            return true;
        case SDLK_LEFT:
            if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) app_master_reverb_adjust_param(app, -1, fine);
            return true;
        case SDLK_RIGHT:
            if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) app_master_reverb_adjust_param(app, 1, fine);
            return true;
        case SDLK_RETURN:
            if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) app_master_reverb_activate_selected(app);
            return true;
        case SDLK_R:
            if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) app_master_reverb_clear_tail(app);
            return true;
        case SDLK_SPACE: app_toggle_timeline_playback(app); return true;
        case SDLK_M: toggle_metronome(app); return true;
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
        app_close_gamepad(app);
        SDL_memset(previous_buttons, 0, sizeof(previous_buttons));
    }
    if(e->type!=SDL_EVENT_KEY_DOWN) return true;
    if(e->key.key==SDLK_F12) {
        app_toggle_debug_overlay(app);
        return true;
    }
    if(e->key.key==SDLK_F1) {
        app_toggle_controls_legend(app);
        return true;
    }
    if(app->controls_legend_open) {
        if(e->key.key==SDLK_ESCAPE) app->controls_legend_open = false;
        return true;
    }
    if(app->waveform_sidecar_confirm_open) {
        switch(e->key.key) {
            case SDLK_RETURN: app_confirm_write_tempo_sidecar(app); break;
            case SDLK_ESCAPE: app_cancel_write_tempo_sidecar(app); break;
            default: break;
        }
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
        else if(app->view_mode == APP_VIEW_MASTER_MIX) app_master_mix_cycle_focus(app, (mod & SDL_KMOD_SHIFT) ? -1 : 1);
        else if(app->view_mode == APP_VIEW_LANE_INSPECTOR) return true;
        else {
            if(!app->sample_selector_open) app_refresh_sample_list(app);
            app->sample_selector_open = !app->sample_selector_open;
        }
        return true;
    }
    if(app->view_mode == APP_VIEW_LANE_INSPECTOR) return handle_lane_inspector_key(app, e->key.key, mod);
    if(app->view_mode == APP_VIEW_MASTER_MIX) return handle_master_mix_key(app, e->key.key, mod);
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
        case SDLK_M: toggle_metronome(app); break;
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
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_BACK)) toggle_metronome(app);
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

    if(app->waveform_sidecar_confirm_open) {
        if(south_pressed) app_confirm_write_tempo_sidecar(app);
        if(east_pressed) app_cancel_write_tempo_sidecar(app);
        return;
    }

    if(r2_shift && start_pressed && !app->waveform_frame_grip_active) {
        app_toggle_view_mode(app);
        return;
    }

    if(app->view_mode == APP_VIEW_MASTER_MIX) {
        if(r2_shift && south_pressed) {
            app_toggle_timeline_playback(app);
            return;
        }
        if(east_pressed) {
            app_master_mix_return_to_timeline(app);
            return;
        }
        if(back_pressed) toggle_metronome(app);
        if(left_shoulder_pressed) app_master_mix_cycle_focus(app, -1);
        if(right_shoulder_pressed) app_master_mix_cycle_focus(app, 1);
        if(app->master_mix_focus == MASTER_MIX_FOCUS_REVERB) {
            if(south_pressed) app_master_reverb_activate_selected(app);
            if(left_stick_pressed) app_master_reverb_clear_tail(app);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_master_reverb_select_param_delta(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_master_reverb_select_param_delta(app, 1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_master_reverb_adjust_param(app, -1, l2_shift);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_master_reverb_adjust_param(app, 1, l2_shift);
        } else {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_master_mix_cycle_focus(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_master_mix_cycle_focus(app, 1);
        }
        return;
    }

    if(app->view_mode == APP_VIEW_LANE_INSPECTOR) {
        if(r2_shift) {
            if(south_pressed) app_toggle_timeline_playback(app);
            if(east_pressed) app_rewind_timeline(app);
            if(west_pressed) app_timeline_jump_to_play_range_start(app);
            if(north_pressed) app_timeline_toggle_play_range_loop(app);
            if(south_pressed || east_pressed || west_pressed || north_pressed) return;
        }
        if(back_pressed) toggle_metronome(app);
        if(east_pressed) {
            app_close_lane_inspector(app);
            return;
        }
        if(south_pressed) {
            app_toggle_inspected_lane_mute(app);
            return;
        }
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            app_cycle_inspected_lane_palette(app, -1);
            return;
        }
        if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            app_cycle_inspected_lane_palette(app, 1);
            return;
        }
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

        if(app->timeline_context_menu_open) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_context_menu_move(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_context_menu_move(app, 1);
            if(south_pressed) app_timeline_context_menu_apply(app);
            if(east_pressed || start_pressed) app_timeline_close_context_menu(app);
            return;
        }

        if(l2_shift && r2_shift && left_stick_pressed) {
            app_timeline_fit_play_range_view(app);
            return;
        }

        if(start_pressed) {
            app_timeline_open_context_menu(app);
            return;
        }

        if(back_pressed) toggle_metronome(app);
        if(left_shoulder_pressed) app_timeline_cycle_focus(app, -1);
        if(right_shoulder_pressed) app_timeline_cycle_focus(app, 1);

        bool l2_track_cursor_grab = l2_shift &&
                                    app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA &&
                                    app->timeline_edit_mode == TIMELINE_EDIT_NONE &&
                                    !app->timeline_play_range_adjusting;
        if(l2_track_cursor_grab) update_timeline_cursor_stick(app, lx, dt);
        else reset_timeline_cursor_stick_repeat();

        double timeline_view_speed = l2_shift ? 3.0 : 1.0;
        app_pan_timeline_view(app, (l2_track_cursor_grab ? 0.0 : lx) * dt * 0.9 * timeline_view_speed);
        app_zoom_timeline_view(app, 1.0 + (l2_track_cursor_grab ? 0.0 : ly) * dt * 1.4 * timeline_view_speed);

        if(south_pressed) app_timeline_activate_focus(app);
        if(east_pressed) app_timeline_cancel_focus(app);
        if(app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
                if(r2_shift) app_timeline_nudge_edit_ghost_by_bar(app, -1);
                else app_timeline_nudge_edit_ghost(app, -1);
            }
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                if(r2_shift) app_timeline_nudge_edit_ghost_by_bar(app, 1);
                else app_timeline_nudge_edit_ghost(app, 1);
            }
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_nudge_edit_lane(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_nudge_edit_lane(app, 1);
            return;
        }
        if(l2_shift && app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_adjust_selected_instance_velocity(app, 5);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_adjust_selected_instance_velocity(app, -5);
            if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
               SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) return;
        }
        if(l2_shift && r2_shift &&
           app->timeline_focus_zone == TIMELINE_FOCUS_RULER &&
           app_timeline_cursor_on_tempo_event(app)) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_adjust_tempo_event_at_cursor(app, 1.0);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_adjust_tempo_event_at_cursor(app, -1.0);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_timeline_adjust_tempo_event_at_cursor(app, 0.1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_timeline_adjust_tempo_event_at_cursor(app, -0.1);
            if(SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
               SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
               SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
               SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) return;
        }
        if(left_stick_pressed && app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_reset_play_range(app);
        if(west_pressed && app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_select_play_range_handle(app, TIMELINE_RANGE_HANDLE_START);
        if(north_pressed && app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE) app_timeline_select_play_range_handle(app, TIMELINE_RANGE_HANDLE_END);

        if(app->timeline_play_range_adjusting) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_timeline_nudge_play_range(app, -1, r2_shift);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_timeline_nudge_play_range(app, 1, r2_shift);
            return;
        }

        if(app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT) {
            if(left_stick_pressed) app_timeline_reset_tape_speed(app);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
                app_timeline_set_tape_control_mode(app, TIMELINE_TAPE_CONTROL_PITCH);
            }
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
                app_timeline_set_tape_control_mode(app, TIMELINE_TAPE_CONTROL_BPM);
            }
            double bpm_step = r2_shift && app->timeline_tape_control_mode == TIMELINE_TAPE_CONTROL_BPM ? 0.01 : 1.0;
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) app_timeline_adjust_tape_control(app, -1, bpm_step);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) app_timeline_adjust_tape_control(app, 1, bpm_step);
            return;
        }

        if(app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
                app_preview_selected_roster_clip(app);
                return;
            }
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_select_roster_delta(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_select_roster_delta(app, 1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
                if(r2_shift) app_timeline_move_cursor_by_bar(app, -1);
                else app_timeline_move_cursor(app, -1);
            }
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                if(r2_shift) app_timeline_move_cursor_by_bar(app, 1);
                else app_timeline_move_cursor(app, 1);
            }
            return;
        }

        if(app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_select_lane_delta(app, -1);
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_select_lane_delta(app, 1);
            return;
        }

        if(app->timeline_focus_zone == TIMELINE_FOCUS_RULER || app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
                if(l2_shift || r2_shift) {
                    app_timeline_move_cursor_by_bar(app, -1);
                }
                else app_timeline_move_cursor(app, -1);
            }
            if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                if(l2_shift || r2_shift) {
                    app_timeline_move_cursor_by_bar(app, 1);
                }
                else app_timeline_move_cursor(app, 1);
            }
            if(app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
                if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) app_timeline_select_lane_delta(app, -1);
                if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) app_timeline_select_lane_delta(app, 1);
            }
            return;
        }

        return;
    }

    if(app->tempo_lock_mode) {
        if(l2_shift && r2_shift && south_pressed) app_apply_tempo_lock_and_capture(app);
        else if(l2_shift && r2_shift && back_pressed) app_request_write_tempo_sidecar(app);
        else if(r2_shift && north_pressed) app_snap_tempo_lock_downbeat_to_loop_start(app);
        else if(r2_shift && east_pressed) app_clear_tempo_lock(app);
        else if(south_pressed) app_apply_tempo_lock(app);
        else if(east_pressed) app_cancel_tempo_lock_mode(app);
        else if(north_pressed) app_cycle_tempo_lock_meter(app, 1);
        else if(west_pressed) app_cycle_tempo_lock_meter(app, -1);
        if(l2_shift && r2_shift && back_pressed) return;
        if(back_pressed) toggle_metronome(app);

        update_tempo_bpm_dpad(app, dt, l2_shift);
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

    if(waveform_frame_grip_available(app)) {
        if(l2_shift && app->waveform_frame_grip_active) {
            waveform_frame_grip_update(app, ly, dt, r2_shift);
            return;
        }
        if(l2_shift && !r2_shift) {
            app->waveform_frame_grip_l2_seconds += dt;
            if(app->waveform_frame_grip_l2_seconds >= WAVEFORM_FRAME_GRIP_L2_ARM_SECONDS) {
                waveform_frame_grip_update(app, ly, dt, false);
            } else {
                SDL_strlcpy(app->status_text, "Frame grip arming", sizeof(app->status_text));
            }
            return;
        }
        if(app->waveform_frame_grip_active) {
            app->waveform_frame_grip_active = false;
            app->waveform_frame_grip_snap_active = false;
            app->waveform_frame_grip_l2_seconds = 0.0;
            SDL_strlcpy(app->status_text, "Frame grip ready: R2+South commits", sizeof(app->status_text));
        } else if(!l2_shift) {
            app->waveform_frame_grip_l2_seconds = 0.0;
        }
    } else {
        app_clear_waveform_frame_grip(app);
    }

    if(r2_shift) {
        if(l2_shift && south_pressed) app_capture_current_loop_to_roster(app);
        else if(l2_shift && back_pressed) app_request_write_tempo_sidecar(app);
        else if(south_pressed) set_loop_to_visible(app);
        if(north_pressed) app_enter_tempo_lock_mode(app);
        if(east_pressed) app_clear_tempo_lock(app);
        if(south_pressed || north_pressed || east_pressed || back_pressed) return;
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

    if(button_pressed(app->gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
        app_refresh_sample_list(app);
        app->sample_selector_open=true;
    }
    if(back_pressed) toggle_metronome(app);
    if(left_shoulder_pressed) {
        gamepad_edit_target=0;
        app_focus_loop_start(app);
    }
    if(right_shoulder_pressed) {
        gamepad_edit_target=1;
        app_focus_loop_end(app);
    }
    if(left_stick_pressed) {
        app_clear_waveform_frame_grip(app);
        clip_reset_loop(&app->clip);
        app_note_loop_anchors_moved(app);
    }

    bool normal_view_or_trim_motion =
        fabs(lx) > 0.0 || fabs(ly) > 0.0 ||
        SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
        SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
        SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
        SDL_GetGamepadButton(app->gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    if(normal_view_or_trim_motion) app_clear_waveform_frame_grip(app);

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
