#include "app.h"
#include "input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_strings(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return SDL_strcasecmp(*sa, *sb);
}

static void app_set_status(App *app, const char *text) {
    SDL_strlcpy(app->status_text, text, sizeof(app->status_text));
}

static const SDL_Color roster_palette[] = {
    { 255, 105, 120, 255 },
    {  80, 220, 230, 255 },
    { 255, 205,  95, 255 },
    { 140, 220, 120, 255 },
    { 185, 140, 255, 255 },
    { 255, 145,  80, 255 },
    { 120, 175, 255, 255 },
    { 235, 115, 190, 255 },
};

static void roster_clip_destroy(RosterClip *clip) {
    SDL_free(clip->samples);
    SDL_memset(clip, 0, sizeof(*clip));
}

static const char *path_basename(const char *path) {
    const char *base = path && path[0] ? path : "generated";
    for (const char *p = base; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base[0] ? base : "generated";
}

static void capture_base_name(const AudioClip *clip, char *out, size_t out_size) {
    const char *base = path_basename(clip->file_path);
    SDL_strlcpy(out, base, out_size);
    char *dot = SDL_strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
    if (!out[0]) SDL_strlcpy(out, "capture", out_size);
}

static int next_capture_number(const App *app, const char *base) {
    int max_seen = 0;
    size_t base_len = SDL_strlen(base);
    for (int i = 0; i < app->roster_clip_count; ++i) {
        const char *name = app->roster[i].name;
        if (SDL_strncmp(name, base, base_len) == 0 && name[base_len] == '#') {
            int number = atoi(name + base_len + 1);
            if (number > max_seen) max_seen = number;
        }
    }
    return max_seen + 1;
}

static int64_t beats_to_ticks(double beats, int ticks_per_beat) {
    double ticks = beats * (double)ticks_per_beat;
    if (ticks < 1.0) ticks = 1.0;
    if (ticks > 9000000000000000.0) ticks = 9000000000000000.0;
    return (int64_t)llround(ticks);
}

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int64_t clamp_i64(int64_t value, int64_t min_value, int64_t max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int64_t timeline_snap_ticks(const MasterTimeline *timeline) {
    return timeline->ticks_per_beat > 0 ? (int64_t)timeline->ticks_per_beat : 960;
}

static int64_t timeline_min_range_ticks(const MasterTimeline *timeline) {
    int64_t length = timeline->length_ticks > 0 ? timeline->length_ticks : 0;
    int64_t snap = timeline_snap_ticks(timeline);
    if (length > 0 && snap > length) return length;
    return snap > 0 ? snap : 1;
}

static const char *timeline_focus_label(TimelineFocusZone zone) {
    switch (zone) {
        case TIMELINE_FOCUS_TRANSPORT: return "TRANSPORT";
        case TIMELINE_FOCUS_RULER: return "RULER";
        case TIMELINE_FOCUS_PLAY_RANGE: return "PLAY RANGE";
        case TIMELINE_FOCUS_TRACK_AREA: return "TRACK AREA";
        case TIMELINE_FOCUS_ROSTER: return "ROSTER";
        case TIMELINE_FOCUS_COUNT:
        default: return "TIMELINE";
    }
}

static const char *timeline_edit_mode_label(TimelineEditMode mode) {
    switch (mode) {
        case TIMELINE_EDIT_MOVE_INSTANCE: return "MOVE INSTANCE";
        case TIMELINE_EDIT_PLACE_CLIP: return "PLACE CLIP";
        case TIMELINE_EDIT_NONE:
        default: return "";
    }
}

static void timeline_effective_play_range(const MasterTimeline *timeline, int64_t *start, int64_t *end) {
    int64_t length = timeline->length_ticks > 0 ? timeline->length_ticks : 0;
    if (length <= 0) {
        if (start) *start = 0;
        if (end) *end = 0;
        return;
    }

    int64_t s = timeline->play_range_start_tick;
    int64_t e = timeline->play_range_end_tick;
    if (s < 0 || e > length || e <= s) {
        s = 0;
        e = length;
    }
    s = clamp_i64(s, 0, length);
    e = clamp_i64(e, 0, length);
    if (e <= s) {
        s = 0;
        e = length;
    }

    if (start) *start = s;
    if (end) *end = e;
}

static void sync_timeline_play_range_no_lock(App *app) {
    MasterTimeline *timeline = &app->timeline;
    int64_t length = timeline->length_ticks > 0 ? timeline->length_ticks : 0;
    if (length <= 0) {
        timeline->play_range_start_tick = 0;
        timeline->play_range_end_tick = 0;
        timeline->timeline_cursor_tick = 0;
        timeline->playhead_tick = 0;
        return;
    }

    if (!timeline->play_range_custom) {
        timeline->play_range_start_tick = 0;
        timeline->play_range_end_tick = length;
    } else {
        int64_t min_len = timeline_min_range_ticks(timeline);
        if (min_len < 1) min_len = 1;
        timeline->play_range_start_tick = clamp_i64(timeline->play_range_start_tick, 0, length - 1);
        timeline->play_range_end_tick = clamp_i64(timeline->play_range_end_tick, timeline->play_range_start_tick + min_len, length);
        if (timeline->play_range_end_tick <= timeline->play_range_start_tick) {
            timeline->play_range_start_tick = 0;
            timeline->play_range_end_tick = length;
            timeline->play_range_custom = false;
        }
    }

    timeline->timeline_cursor_tick = clamp_i64(timeline->timeline_cursor_tick, 0, length);
    timeline->playhead_tick = clamp_i64(timeline->playhead_tick, 0, length);
}

static void sync_timeline_play_range(App *app) {
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    sync_timeline_play_range_no_lock(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
}

static int64_t timeline_clip_duration_ticks(const App *app, int roster_clip_index) {
    if (roster_clip_index < 0 || roster_clip_index >= app->roster_clip_count) return 0;
    const RosterClip *clip = &app->roster[roster_clip_index];
    return beats_to_ticks(clip->target_beats, app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat : 960);
}

static int64_t timeline_snap_tick_down(const App *app, int64_t tick) {
    int64_t snap = timeline_snap_ticks(&app->timeline);
    if (snap <= 1) return tick < 0 ? 0 : tick;
    if (tick < 0) tick = 0;
    return (tick / snap) * snap;
}

static bool timeline_range_overlaps_existing(const App *app, int64_t start, int64_t duration, int ignore_instance) {
    if (duration <= 0) return true;
    int64_t end = start + duration;
    if (start < 0 || end <= start) return true;

    for (int i = 0; i < app->timeline.instance_count; ++i) {
        if (i == ignore_instance) continue;
        const TimelineInstance *instance = &app->timeline.instances[i];
        if (instance->duration_ticks <= 0) continue;
        int64_t other_start = instance->start_tick;
        int64_t other_end = instance->start_tick + instance->duration_ticks;
        if (start < other_end && end > other_start) return true;
    }
    return false;
}

static bool timeline_ghost_is_valid(const App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return false;
    int ignore = app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE ? app->timeline_edit_instance_index : -1;
    return !timeline_range_overlaps_existing(app, app->timeline_edit_ghost_start_tick, app->timeline_edit_duration_ticks, ignore);
}

static void timeline_update_ghost_valid(App *app) {
    app->timeline_edit_ghost_valid = timeline_ghost_is_valid(app);
}

static void timeline_clamp_ghost_start(App *app) {
    if (app->timeline_edit_ghost_start_tick < 0) app->timeline_edit_ghost_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline_edit_ghost_start_tick);
}

static void timeline_enter_move_instance(App *app) {
    if (app->selected_timeline_instance < 0 || app->selected_timeline_instance >= app->timeline.instance_count) {
        app_set_status(app, "no instance selected");
        return;
    }
    TimelineInstance *instance = &app->timeline.instances[app->selected_timeline_instance];
    app->timeline_edit_mode = TIMELINE_EDIT_MOVE_INSTANCE;
    app->timeline_edit_instance_index = app->selected_timeline_instance;
    app->timeline_edit_roster_clip_index = instance->roster_clip_index;
    app->timeline_edit_original_start_tick = instance->start_tick;
    app->timeline_edit_ghost_start_tick = instance->start_tick;
    app->timeline_edit_duration_ticks = instance->duration_ticks;
    timeline_update_ghost_valid(app);
    app_set_status(app, "MOVE INSTANCE");
}

static void timeline_enter_place_clip(App *app) {
    if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
        app_set_status(app, "roster empty");
        return;
    }
    if (app->timeline.instance_count >= APP_MAX_TIMELINE_INSTANCES) {
        app_set_status(app, "timeline full");
        return;
    }
    int64_t duration = timeline_clip_duration_ticks(app, app->selected_roster_clip);
    if (duration <= 0 || app->timeline.length_ticks <= 0) {
        app_set_status(app, "timeline empty");
        return;
    }
    app->timeline_edit_mode = TIMELINE_EDIT_PLACE_CLIP;
    app->timeline_edit_instance_index = -1;
    app->timeline_edit_roster_clip_index = app->selected_roster_clip;
    app->timeline_edit_original_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    app->timeline_edit_duration_ticks = duration;
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    app_set_status(app, app->timeline_edit_ghost_valid ? "PLACE CLIP" : "Invalid placement");
}

static void clamp_timeline_view(App *app) {
    MasterTimeline *timeline = &app->timeline;
    double length = timeline->length_ticks > 0 ? (double)timeline->length_ticks :
                    (double)(timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat * 4 : 3840);
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
        double ghost_end = (double)(app->timeline_edit_ghost_start_tick + app->timeline_edit_duration_ticks);
        if (ghost_end > length) length = ghost_end;
    }
    if (length < 1.0) length = 1.0;
    if (timeline->view_span_ticks <= 0.0) timeline->view_span_ticks = length;
    if (timeline->view_span_ticks < (double)(timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat / 4 : 240)) {
        timeline->view_span_ticks = (double)(timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat / 4 : 240);
    }
    if (timeline->view_span_ticks > length) timeline->view_span_ticks = length;
    double half = timeline->view_span_ticks * 0.5;
    if (timeline->view_center_tick < half) timeline->view_center_tick = half;
    if (timeline->view_center_tick > length - half) timeline->view_center_tick = length - half;
}

static void sync_transport_to_timeline(App *app) {
    app->transport.bpm = app->timeline.timeline_bpm > 0.0 ? app->timeline.timeline_bpm : 120.0;
    app->transport.beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    app->transport.beat_unit = app->timeline.timeline_beat_unit > 0 ? app->timeline.timeline_beat_unit : 4;
    app->transport.current_tick = app->timeline.playhead_tick > 0 ? (uint64_t)app->timeline.playhead_tick : 0;
    app->transport.current_seconds = app->timeline.ticks_per_beat > 0 && app->transport.bpm > 0.0 ?
        ((double)app->transport.current_tick / (double)app->timeline.ticks_per_beat) * 60.0 / app->transport.bpm : 0.0;
}

static TempoLockParams default_tempo_params(const App *app) {
    TempoLockParams params;
    params.bpm = app->clip.has_clip_metadata_bpm ? app->clip.clip_metadata_bpm : 120.0;
    params.downbeat_frame = app->clip.loop_start_frame;
    params.beats_per_bar = app->clip.beats_per_bar > 0 ? app->clip.beats_per_bar : 4;
    params.beat_unit = app->clip.beat_unit > 0 ? app->clip.beat_unit : 4;
    params.target_bars = app->clip.tempo_lock.target_bars > 0.0 ? app->clip.tempo_lock.target_bars : 4.0;
    return params;
}

static void sync_transport_from_app(App *app) {
    if (app->view_mode == APP_VIEW_TIMELINE) {
        sync_transport_to_timeline(app);
    } else if (app->tempo_lock_mode) {
        app->transport.bpm = app->tempo_lock_draft.bpm;
        app->transport.beats_per_bar = app->tempo_lock_draft.beats_per_bar;
        app->transport.beat_unit = app->tempo_lock_draft.beat_unit;
        app->transport.metronome_downbeat_frame = app->tempo_lock_draft.downbeat_frame;
    } else if (app->transport_bpm_manual) {
        app->transport.bpm = app->transport_bpm;
        app->transport.beats_per_bar = app->clip.beats_per_bar > 0 ? app->clip.beats_per_bar : 4;
        app->transport.beat_unit = app->clip.beat_unit > 0 ? app->clip.beat_unit : 4;
        app->transport.metronome_downbeat_frame = app->clip.loop_start_frame;
    } else if (app->clip.clip_tempo_locked) {
        app->transport_bpm = app->clip.tempo_lock.bpm;
        app->transport.bpm = app->clip.tempo_lock.bpm;
        app->transport.beats_per_bar = app->clip.tempo_lock.beats_per_bar;
        app->transport.beat_unit = app->clip.tempo_lock.beat_unit;
        app->transport.metronome_downbeat_frame = app->clip.tempo_lock.downbeat_frame;
    } else {
        app->transport.bpm = app->transport_bpm;
        app->transport.beats_per_bar = app->clip.beats_per_bar > 0 ? app->clip.beats_per_bar : 4;
        app->transport.beat_unit = app->clip.beat_unit > 0 ? app->clip.beat_unit : 4;
        app->transport.metronome_downbeat_frame = app->clip.downbeat_frame;
    }
}

static void set_transport_bpm_from_metadata(App *app) {
    app->transport_bpm_manual = false;
    app->transport_bpm = app->clip.has_clip_metadata_bpm ? app->clip.clip_metadata_bpm : 120.0;
    sync_transport_from_app(app);
}

static void clamp_tempo_params(App *app, TempoLockParams *params) {
    if (params->bpm < 30.0) params->bpm = 30.0;
    if (params->bpm > 300.0) params->bpm = 300.0;
    if (params->beats_per_bar < 1) params->beats_per_bar = 4;
    if (params->beat_unit < 1) params->beat_unit = 4;
    if (params->target_bars < 0.5) params->target_bars = 0.5;
    if (params->target_bars > 32.0) params->target_bars = 32.0;
    if (params->downbeat_frame >= app->clip.frame_count) {
        params->downbeat_frame = app->clip.frame_count > 0 ? app->clip.frame_count - 1 : 0;
    }
}

static void copy_tempo_params_to_clip(AudioClip *clip, const TempoLockParams *params) {
    clip->tempo_lock = *params;
    clip->clip_metadata_bpm = params->bpm;
    clip->has_clip_metadata_bpm = true;
    clip->source_bpm = params->bpm;
    clip->downbeat_frame = params->downbeat_frame;
    clip->beats_per_bar = params->beats_per_bar;
    clip->beat_unit = params->beat_unit;
}

BpmSource app_bpm_source(const App *app) {
    if (app->view_mode == APP_VIEW_TIMELINE) return BPM_SOURCE_MASTER_TIMELINE;
    if (app->tempo_lock_mode) return BPM_SOURCE_TEMPO_LOCKED;
    if (app->transport_bpm_manual) return BPM_SOURCE_TRANSPORT_MANUAL;
    if (app->clip.clip_tempo_locked) return BPM_SOURCE_TEMPO_LOCKED;
    if (app->clip.has_clip_metadata_bpm) return BPM_SOURCE_CLIP_METADATA;
    return BPM_SOURCE_DEFAULT_120;
}

const char *app_bpm_source_label(const App *app) {
    switch (app_bpm_source(app)) {
        case BPM_SOURCE_MASTER_TIMELINE: return "master timeline";
        case BPM_SOURCE_TEMPO_LOCKED: return "tempo locked";
        case BPM_SOURCE_TRANSPORT_MANUAL: return "transport/manual";
        case BPM_SOURCE_CLIP_METADATA: return "clip metadata";
        case BPM_SOURCE_DEFAULT_120:
        default: return "default 120";
    }
}

bool app_get_active_tempo_params(const App *app, TempoLockParams *params) {
    if (app->tempo_lock_mode) {
        if (params) *params = app->tempo_lock_draft;
        return true;
    }
    if (app->clip.clip_tempo_locked) {
        if (params) *params = app->clip.tempo_lock;
        return true;
    }
    if (app->has_retained_tempo_lock_params) {
        if (params) *params = app->retained_tempo_lock;
        return true;
    }
    if (app->clip.has_clip_metadata_bpm) {
        TempoLockParams metadata = default_tempo_params(app);
        metadata.bpm = app->clip.clip_metadata_bpm;
        if (params) *params = metadata;
        return true;
    }
    return false;
}

static TempoLockParams capture_tempo_params(const App *app) {
    TempoLockParams params;
    if (app_get_active_tempo_params(app, &params)) return params;

    params.bpm = app->transport.bpm > 0.0 ? app->transport.bpm : 120.0;
    params.downbeat_frame = app->clip.loop_start_frame;
    params.beats_per_bar = app->clip.beats_per_bar > 0 ? app->clip.beats_per_bar : 4;
    params.beat_unit = app->clip.beat_unit > 0 ? app->clip.beat_unit : 4;
    params.target_bars = 4.0;
    return params;
}

void app_capture_current_loop_to_roster(App *app) {
    if (audio_engine_timeline_is_playing(&app->audio)) {
        app_set_status(app, "Stop timeline before capture");
        return;
    }
    if (!app->clip.samples || app->clip.frame_count < 2) {
        app_set_status(app, "No clip to capture");
        return;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        app_set_status(app, "Roster full");
        return;
    }

    size_t start = app->clip.loop_start_frame;
    size_t end = app->clip.loop_end_frame;
    if (end > app->clip.frame_count) end = app->clip.frame_count;
    if (end <= start || end - start < APP_MIN_CAPTURE_FRAMES) {
        app_set_status(app, "Captured loop is too short");
        return;
    }

    size_t frame_count = end - start;
    if (frame_count > APP_MAX_CAPTURE_FRAMES) {
        app_set_status(app, "Clip too large to capture");
        return;
    }
    if (app->clip.channels <= 0 || app->clip.sample_rate <= 0) {
        app_set_status(app, "No clip to capture");
        return;
    }
    size_t channels = (size_t)app->clip.channels;
    if (frame_count > SIZE_MAX / channels) {
        app_set_status(app, "Clip too large to capture");
        return;
    }
    size_t sample_count = frame_count * channels;
    if (sample_count > SIZE_MAX / sizeof(float)) {
        app_set_status(app, "Clip too large to capture");
        return;
    }
    size_t byte_count = sample_count * sizeof(float);
    if (byte_count > APP_MAX_CAPTURE_BYTES) {
        app_set_status(app, "Clip too large to capture");
        return;
    }

    float *samples = (float *)SDL_malloc(byte_count);
    if (!samples) {
        app_set_status(app, "Memory allocation failed");
        return;
    }
    SDL_memcpy(samples, app->clip.samples + start * channels, byte_count);

    TempoLockParams tempo = capture_tempo_params(app);
    if (tempo.bpm <= 0.0) tempo.bpm = 120.0;
    if (tempo.beats_per_bar <= 0) tempo.beats_per_bar = 4;
    if (tempo.beat_unit <= 0) tempo.beat_unit = 4;
    if (tempo.target_bars <= 0.0) tempo.target_bars = 4.0;

    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        SDL_free(samples);
        app_set_status(app, "Could not lock audio stream");
        return;
    }
    if (app->timeline.playing) {
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        SDL_free(samples);
        app_set_status(app, "Stop timeline before capture");
        return;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        SDL_free(samples);
        app_set_status(app, "Roster full");
        return;
    }

    RosterClip next;
    SDL_memset(&next, 0, sizeof(next));
    char base[APP_ROSTER_CLIP_NAME_MAX];
    capture_base_name(&app->clip, base, sizeof(base));
    int number = next_capture_number(app, base);
    SDL_snprintf(next.name, sizeof(next.name), "%s#%03d", base, number);
    SDL_strlcpy(next.source_path, app->clip.file_path, sizeof(next.source_path));
    next.source_loop_start_frame = start;
    next.source_loop_end_frame = end;
    next.sample_rate = app->clip.sample_rate;
    next.channels = app->clip.channels;
    next.frame_count = frame_count;
    next.samples = samples;
    next.source_bpm = tempo.bpm;
    next.beats_per_bar = tempo.beats_per_bar;
    next.beat_unit = tempo.beat_unit;
    next.target_bars = tempo.target_bars;
    next.target_beats = tempo.target_bars * (double)tempo.beats_per_bar;
    next.midi_note = clamp_int(60 + (app->roster_clip_count % 36), 0, 127);
    next.midi_channel = 0;
    next.midi_velocity = 100;
    if (tempo.downbeat_frame > start) {
        size_t offset = tempo.downbeat_frame - start;
        next.downbeat_offset_frames = offset < frame_count ? offset : frame_count - 1;
    } else {
        next.downbeat_offset_frames = 0;
    }
    next.color = roster_palette[app->roster_clip_count % (int)(sizeof(roster_palette) / sizeof(roster_palette[0]))];

    int roster_index = app->roster_clip_count;
    app->roster[roster_index] = next;
    app->roster_clip_count++;
    app->selected_roster_clip = roster_index;
    app->selected_roster_clip_armed = false;

    if (!app->timeline.initialized) {
        app->timeline.initialized = true;
        app->timeline.timeline_bpm = next.source_bpm;
        app->timeline.timeline_beats_per_bar = next.beats_per_bar;
        app->timeline.timeline_beat_unit = next.beat_unit;
        app->timeline.ticks_per_beat = app->transport.ppqn > 0 ? app->transport.ppqn : 960;
        app->timeline.instance_count = 1;
        app->timeline.instances[0].roster_clip_index = roster_index;
        app->timeline.instances[0].start_tick = 0;
        app->timeline.instances[0].duration_ticks = beats_to_ticks(next.target_beats, app->timeline.ticks_per_beat);
        app->timeline.instances[0].midi_note = next.midi_note;
        app->timeline.instances[0].midi_channel = next.midi_channel;
        app->timeline.instances[0].midi_velocity = next.midi_velocity;
        app->timeline.length_ticks = app->timeline.instances[0].duration_ticks;
        app->timeline.playhead_tick = 0;
        app->timeline.timeline_cursor_tick = 0;
        app->timeline.play_range_start_tick = 0;
        app->timeline.play_range_end_tick = app->timeline.length_ticks;
        app->timeline.play_range_loop_enabled = false;
        app->timeline.play_range_custom = false;
        app->timeline.view_center_tick = (double)app->timeline.length_ticks * 0.5;
        app->timeline.view_span_ticks = (double)app->timeline.length_ticks;
        app->timeline_focus_zone = TIMELINE_FOCUS_RULER;
        app->timeline_play_range_handle = TIMELINE_RANGE_HANDLE_START;
        app->timeline_play_range_adjusting = false;
        app->selected_timeline_instance = 0;
    }
    sync_timeline_play_range_no_lock(app);
    clamp_timeline_view(app);

    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Captured %s to roster", app->roster[roster_index].name);
}

void app_toggle_view_mode(App *app) {
    if (app->view_mode == APP_VIEW_WAVEFORM) {
        app->transport.playing = false;
        audio_engine_set_playback_mode(&app->audio, AUDIO_PLAYBACK_TIMELINE);
        app->view_mode = APP_VIEW_TIMELINE;
        app->tempo_lock_mode = false;
        sync_transport_from_app(app);
    } else {
        audio_engine_stop_timeline(&app->audio, true);
        audio_engine_set_playback_mode(&app->audio, AUDIO_PLAYBACK_WAVEFORM);
        app->view_mode = APP_VIEW_WAVEFORM;
        app->transport.playing = false;
        sync_transport_from_app(app);
    }
}

void app_toggle_controls_legend(App *app) {
    app->controls_legend_open = !app->controls_legend_open;
}

void app_toggle_timeline_playback(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    sync_timeline_play_range(app);
    int64_t range_start = 0, range_end = 0;
    timeline_effective_play_range(&app->timeline, &range_start, &range_end);
    if (!app->timeline.initialized || app->timeline.instance_count <= 0 || range_end <= range_start) {
        app_set_status(app, "timeline empty");
        return;
    }
    if (audio_engine_timeline_is_playing(&app->audio)) {
        audio_engine_stop_timeline(&app->audio, true);
        sync_transport_from_app(app);
        app_set_status(app, "Timeline stopped");
    } else {
        sync_transport_from_app(app);
        audio_engine_start_timeline(&app->audio);
        app_set_status(app, "Timeline playing");
    }
}

void app_rewind_timeline(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    audio_engine_stop_timeline(&app->audio, true);
    int64_t range_start = 0;
    timeline_effective_play_range(&app->timeline, &range_start, NULL);
    app->timeline.timeline_cursor_tick = range_start;
    sync_transport_from_app(app);
    app_set_status(app, "Timeline rewound");
}

void app_timeline_jump_to_play_range_start(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    sync_timeline_play_range(app);
    int64_t range_start = 0;
    timeline_effective_play_range(&app->timeline, &range_start, NULL);
    audio_engine_set_timeline_playhead(&app->audio, range_start);
    app->timeline.timeline_cursor_tick = range_start;
    sync_transport_from_app(app);
    app_set_status(app, "Jumped to play range start");
}

void app_timeline_jump_playhead_to_cursor(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    sync_timeline_play_range(app);
    audio_engine_set_timeline_playhead(&app->audio, app->timeline.timeline_cursor_tick);
    sync_transport_from_app(app);
    app_set_status(app, "Playhead jumped to cursor");
}

void app_timeline_toggle_play_range_loop(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    app->timeline.play_range_loop_enabled = !app->timeline.play_range_loop_enabled;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    app_set_status(app, app->timeline.play_range_loop_enabled ? "Play range loop on" : "Play range loop off");
}

void app_timeline_cycle_focus(App *app, int direction) {
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
        app_set_status(app, "Confirm or cancel edit first");
        return;
    }
    int zone = (int)app->timeline_focus_zone + direction;
    while (zone < 0) zone += (int)TIMELINE_FOCUS_COUNT;
    zone %= (int)TIMELINE_FOCUS_COUNT;
    app->timeline_focus_zone = (TimelineFocusZone)zone;
    if (app->timeline_focus_zone != TIMELINE_FOCUS_PLAY_RANGE) {
        app->timeline_play_range_adjusting = false;
    }
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Focus: %s", timeline_focus_label(app->timeline_focus_zone));
}

void app_timeline_move_cursor(App *app, int direction) {
    if (direction == 0) return;
    sync_timeline_play_range(app);
    int64_t length = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
    int64_t snap = timeline_snap_ticks(&app->timeline);
    app->timeline.timeline_cursor_tick = clamp_i64(app->timeline.timeline_cursor_tick + snap * direction, 0, length);
}

void app_timeline_select_play_range_handle(App *app, TimelineRangeHandle handle) {
    app->timeline_play_range_handle = handle;
    app->timeline_focus_zone = TIMELINE_FOCUS_PLAY_RANGE;
    app_set_status(app, handle == TIMELINE_RANGE_HANDLE_START ? "Play range start handle" : "Play range end handle");
}

void app_timeline_nudge_play_range(App *app, int direction) {
    if (direction == 0) return;
    int64_t length = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
    if (length <= 0) {
        app_set_status(app, "timeline empty");
        return;
    }

    int64_t snap = timeline_snap_ticks(&app->timeline);
    int64_t min_len = timeline_min_range_ticks(&app->timeline);
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    sync_timeline_play_range_no_lock(app);
    app->timeline.play_range_custom = true;
    if (app->timeline_play_range_handle == TIMELINE_RANGE_HANDLE_START) {
        int64_t max_start = app->timeline.play_range_end_tick - min_len;
        app->timeline.play_range_start_tick = clamp_i64(app->timeline.play_range_start_tick + snap * direction, 0, max_start);
        app->timeline.timeline_cursor_tick = app->timeline.play_range_start_tick;
    } else {
        int64_t min_end = app->timeline.play_range_start_tick + min_len;
        app->timeline.play_range_end_tick = clamp_i64(app->timeline.play_range_end_tick + snap * direction, min_end, length);
        app->timeline.timeline_cursor_tick = app->timeline.play_range_end_tick;
    }
    sync_timeline_play_range_no_lock(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
}

void app_timeline_reset_play_range(App *app) {
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    app->timeline.play_range_custom = false;
    app->timeline.play_range_start_tick = 0;
    app->timeline.play_range_end_tick = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
    app->timeline.timeline_cursor_tick = app->timeline.play_range_start_tick;
    sync_timeline_play_range_no_lock(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    app_set_status(app, "Play range reset");
}

void app_timeline_select_roster_delta(App *app, int delta) {
    if (app->roster_clip_count <= 0) {
        app->selected_roster_clip = -1;
        app->selected_roster_clip_armed = false;
        app_set_status(app, "roster empty");
        return;
    }
    if (app->selected_roster_clip < 0) app->selected_roster_clip = 0;
    app->selected_roster_clip += delta;
    if (app->selected_roster_clip < 0) app->selected_roster_clip = 0;
    if (app->selected_roster_clip >= app->roster_clip_count) app->selected_roster_clip = app->roster_clip_count - 1;
    app->selected_roster_clip_armed = false;
}

void app_timeline_nudge_edit_ghost(App *app, int direction) {
    if (direction == 0 || app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    int64_t snap = timeline_snap_ticks(&app->timeline);
    app->timeline_edit_ghost_start_tick += snap * direction;
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    if (!app->timeline_edit_ghost_valid) app_set_status(app, "Invalid placement: overlaps");
}

static void app_timeline_select_instance_at_cursor(App *app) {
    int64_t cursor = app->timeline.timeline_cursor_tick;
    app->selected_timeline_instance = -1;
    for (int i = 0; i < app->timeline.instance_count; ++i) {
        TimelineInstance *instance = &app->timeline.instances[i];
        int64_t start = instance->start_tick;
        int64_t end = instance->start_tick + instance->duration_ticks;
        if (cursor >= start && cursor < end) {
            app->selected_timeline_instance = i;
            app_set_status(app, "Selected timeline instance");
            return;
        }
    }
    app_set_status(app, "no instance at cursor");
}

static int app_timeline_instance_at_cursor(App *app) {
    int64_t cursor = app->timeline.timeline_cursor_tick;
    for (int i = 0; i < app->timeline.instance_count; ++i) {
        TimelineInstance *instance = &app->timeline.instances[i];
        int64_t start = instance->start_tick;
        int64_t end = instance->start_tick + instance->duration_ticks;
        if (cursor >= start && cursor < end) return i;
    }
    return -1;
}

static void app_timeline_confirm_edit_mode(App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    timeline_update_ghost_valid(app);
    if (!app->timeline_edit_ghost_valid) {
        app_set_status(app, "Invalid placement: overlaps");
        return;
    }

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    if (app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE) {
        if (app->timeline_edit_instance_index < 0 || app->timeline_edit_instance_index >= app->timeline.instance_count) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app->timeline_edit_mode = TIMELINE_EDIT_NONE;
            app_set_status(app, "Move cancelled");
            return;
        }
        app->timeline.instances[app->timeline_edit_instance_index].start_tick = app->timeline_edit_ghost_start_tick;
        app->timeline.timeline_cursor_tick = app->timeline_edit_ghost_start_tick;
        app->selected_timeline_instance = app->timeline_edit_instance_index;
        int64_t moved_end = app->timeline_edit_ghost_start_tick + app->timeline_edit_duration_ticks;
        if (moved_end > app->timeline.length_ticks) app->timeline.length_ticks = moved_end;
        sync_timeline_play_range_no_lock(app);
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        app->timeline_edit_mode = TIMELINE_EDIT_NONE;
        app_set_status(app, "Instance moved");
        return;
    }

    if (app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP) {
        if (app->timeline.instance_count >= APP_MAX_TIMELINE_INSTANCES ||
            app->timeline_edit_roster_clip_index < 0 ||
            app->timeline_edit_roster_clip_index >= app->roster_clip_count) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app_set_status(app, "Invalid placement");
            return;
        }
        int index = app->timeline.instance_count++;
        RosterClip *clip = &app->roster[app->timeline_edit_roster_clip_index];
        TimelineInstance *instance = &app->timeline.instances[index];
        instance->roster_clip_index = app->timeline_edit_roster_clip_index;
        instance->start_tick = app->timeline_edit_ghost_start_tick;
        instance->duration_ticks = app->timeline_edit_duration_ticks;
        instance->midi_note = clip->midi_note;
        instance->midi_channel = clip->midi_channel;
        instance->midi_velocity = clip->midi_velocity;
        app->timeline.timeline_cursor_tick = app->timeline_edit_ghost_start_tick;
        app->selected_timeline_instance = index;
        int64_t placed_end = app->timeline_edit_ghost_start_tick + app->timeline_edit_duration_ticks;
        if (placed_end > app->timeline.length_ticks) app->timeline.length_ticks = placed_end;
        sync_timeline_play_range_no_lock(app);
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        app->timeline_edit_mode = TIMELINE_EDIT_NONE;
        app_set_status(app, "Clip placed");
    }
}

static void app_timeline_cancel_edit_mode(App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    if (app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE) {
        app->timeline.timeline_cursor_tick = app->timeline_edit_original_start_tick;
        app_set_status(app, "Move cancelled");
    } else {
        app_set_status(app, "Placement cancelled");
    }
    app->timeline_edit_mode = TIMELINE_EDIT_NONE;
    app->timeline_edit_instance_index = -1;
    app->timeline_edit_roster_clip_index = -1;
    app->timeline_edit_ghost_valid = false;
}

void app_timeline_activate_focus(App *app) {
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
        app_timeline_confirm_edit_mode(app);
        return;
    }
    switch (app->timeline_focus_zone) {
        case TIMELINE_FOCUS_TRANSPORT:
            app_toggle_timeline_playback(app);
            break;
        case TIMELINE_FOCUS_RULER:
            app_timeline_jump_playhead_to_cursor(app);
            break;
        case TIMELINE_FOCUS_PLAY_RANGE:
            app->timeline_play_range_adjusting = true;
            app_set_status(app, "Adjusting play range");
            break;
        case TIMELINE_FOCUS_TRACK_AREA:
        {
            int under_cursor = app_timeline_instance_at_cursor(app);
            if (under_cursor >= 0 && under_cursor != app->selected_timeline_instance) {
                app->selected_timeline_instance = under_cursor;
                app_set_status(app, "Selected timeline instance");
            } else if (app->selected_timeline_instance >= 0) {
                timeline_enter_move_instance(app);
            } else {
                app_timeline_select_instance_at_cursor(app);
            }
            break;
        }
        case TIMELINE_FOCUS_ROSTER:
            if (app->selected_roster_clip < 0 && app->roster_clip_count > 0) {
                app->selected_roster_clip = 0;
                app->selected_roster_clip_armed = false;
            }
            if (app->selected_roster_clip >= 0 && app->selected_roster_clip < app->roster_clip_count) {
                if (!app->selected_roster_clip_armed) {
                    app->selected_roster_clip_armed = true;
                    SDL_snprintf(app->status_text, sizeof(app->status_text), "Selected %s", app->roster[app->selected_roster_clip].name);
                } else {
                    timeline_enter_place_clip(app);
                }
            } else {
                app_set_status(app, "roster empty");
            }
            break;
        case TIMELINE_FOCUS_COUNT:
        default:
            break;
    }
}

void app_timeline_cancel_focus(App *app) {
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
        app_timeline_cancel_edit_mode(app);
        return;
    }
    if (app->timeline_play_range_adjusting) {
        app->timeline_play_range_adjusting = false;
        app_set_status(app, "Play range adjust off");
        return;
    }
    app_set_status(app, "Timeline focus idle");
}

void app_pan_timeline_view(App *app, double fraction) {
    app->timeline.view_center_tick += app->timeline.view_span_ticks * fraction;
    clamp_timeline_view(app);
}

void app_zoom_timeline_view(App *app, double scale) {
    if (scale <= 0.0) return;
    app->timeline.view_span_ticks *= scale;
    clamp_timeline_view(app);
}

void app_enter_tempo_lock_mode(App *app) {
    if (app->tempo_lock_mode) {
        app_cancel_tempo_lock_mode(app);
        return;
    }
    if (app->clip.clip_tempo_locked) {
        app->tempo_lock_draft = app->clip.tempo_lock;
    } else if (app->has_retained_tempo_lock_params) {
        app->tempo_lock_draft = app->retained_tempo_lock;
    } else {
        app->tempo_lock_draft = default_tempo_params(app);
    }
    clamp_tempo_params(app, &app->tempo_lock_draft);
    app->tempo_lock_mode = true;
    app->sample_selector_open = false;
    sync_transport_from_app(app);
    app_set_status(app, "TEMPO LOCK MODE");
}

void app_cancel_tempo_lock_mode(App *app) {
    app->tempo_lock_mode = false;
    sync_transport_from_app(app);
    app_set_status(app, "Tempo lock cancelled");
}

void app_apply_tempo_lock(App *app) {
    clamp_tempo_params(app, &app->tempo_lock_draft);
    copy_tempo_params_to_clip(&app->clip, &app->tempo_lock_draft);
    app->clip.clip_tempo_locked = true;
    app->has_retained_tempo_lock_params = true;
    app->retained_tempo_lock = app->tempo_lock_draft;
    app->retained_loop_start_frame = app->clip.loop_start_frame;
    app->retained_loop_end_frame = app->clip.loop_end_frame;
    app->retained_tempo_lock_stale = false;
    app->transport_bpm_manual = false;
    app->transport_bpm = app->tempo_lock_draft.bpm;
    app->tempo_lock_mode = false;
    sync_transport_from_app(app);
    app_set_status(app, "Tempo lock applied");
}

void app_clear_tempo_lock(App *app) {
    if (app->clip.clip_tempo_locked) {
        app->has_retained_tempo_lock_params = true;
        app->retained_tempo_lock = app->clip.tempo_lock;
        app->retained_loop_start_frame = app->clip.loop_start_frame;
        app->retained_loop_end_frame = app->clip.loop_end_frame;
        app->retained_tempo_lock_stale = false;
    }
    app->clip.clip_tempo_locked = false;
    app->tempo_lock_mode = false;
    set_transport_bpm_from_metadata(app);
    app_set_status(app, "Tempo lock cleared");
}

void app_adjust_transport_bpm(App *app, double delta) {
    app->transport_bpm += delta;
    if (app->transport_bpm < 30.0) app->transport_bpm = 30.0;
    if (app->transport_bpm > 300.0) app->transport_bpm = 300.0;
    app->transport_bpm_manual = true;
    sync_transport_from_app(app);
}

void app_adjust_tempo_lock_bpm(App *app, double delta) {
    app->tempo_lock_draft.bpm += delta;
    clamp_tempo_params(app, &app->tempo_lock_draft);
    sync_transport_from_app(app);
}

void app_adjust_tempo_lock_downbeat(App *app, long frames) {
    long next = (long)app->tempo_lock_draft.downbeat_frame + frames;
    if (next < 0) next = 0;
    if (app->clip.frame_count > 0 && (size_t)next >= app->clip.frame_count) next = (long)app->clip.frame_count - 1;
    app->tempo_lock_draft.downbeat_frame = (size_t)next;
    sync_transport_from_app(app);
}

void app_cycle_tempo_lock_target_bars(App *app, int direction) {
    static const double options[] = {0.5, 1.0, 2.0, 4.0, 8.0};
    int count = (int)(sizeof(options) / sizeof(options[0]));
    int index = 0;
    double best = fabs(app->tempo_lock_draft.target_bars - options[0]);
    for (int i = 1; i < count; ++i) {
        double d = fabs(app->tempo_lock_draft.target_bars - options[i]);
        if (d < best) { best = d; index = i; }
    }
    index = (index + direction) % count;
    if (index < 0) index += count;
    app->tempo_lock_draft.target_bars = options[index];
}

void app_cycle_tempo_lock_meter(App *app, int direction) {
    static const TempoLockParams meters[] = {
        {.beats_per_bar = 3, .beat_unit = 4},
        {.beats_per_bar = 4, .beat_unit = 4},
        {.beats_per_bar = 6, .beat_unit = 8},
    };
    int count = (int)(sizeof(meters) / sizeof(meters[0]));
    int index = 1;
    for (int i = 0; i < count; ++i) {
        if (app->tempo_lock_draft.beats_per_bar == meters[i].beats_per_bar &&
            app->tempo_lock_draft.beat_unit == meters[i].beat_unit) {
            index = i;
            break;
        }
    }
    index = (index + direction) % count;
    if (index < 0) index += count;
    app->tempo_lock_draft.beats_per_bar = meters[index].beats_per_bar;
    app->tempo_lock_draft.beat_unit = meters[index].beat_unit;
    sync_transport_from_app(app);
}

void app_note_loop_anchors_moved(App *app) {
    if (!app->clip.clip_tempo_locked && app->has_retained_tempo_lock_params &&
        (app->clip.loop_start_frame != app->retained_loop_start_frame ||
         app->clip.loop_end_frame != app->retained_loop_end_frame)) {
        app->retained_tempo_lock_stale = true;
    }
    sync_transport_from_app(app);
}

void app_refresh_sample_list(App *app) {
    app->sample_count = 0;
    app->selected_sample = 0;

    int count = 0;
    char **names = SDL_GlobDirectory("assets/samples", "*.wav", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) return;
    qsort(names, (size_t)count, sizeof(char *), compare_strings);

    for (int i = 0; i < count && app->sample_count < APP_MAX_SAMPLES; ++i) {
        SampleEntry *entry = &app->samples[app->sample_count++];
        SDL_snprintf(entry->path, sizeof(entry->path), "assets/samples/%s", names[i]);
        SDL_strlcpy(entry->name, names[i], sizeof(entry->name));
    }
    SDL_free(names);
}

bool load_clip_from_path(App *app, const char *path) {
    AudioClip next;
    if (!clip_init_from_wav(&next, path)) {
        app_set_status(app, "Could not load WAV");
        return false;
    }

    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        clip_destroy(&next);
        app_set_status(app, "Could not lock audio stream");
        return false;
    }

    AudioClip old = app->clip;
    app->clip = next;
    clip_destroy(&old);
    app->tempo_lock_mode = false;
    app->has_retained_tempo_lock_params = false;
    app->retained_tempo_lock_stale = false;
    set_transport_bpm_from_metadata(app);
    audio_engine_set_playhead(&app->audio, app->clip.loop_start_frame);
    transport_jump_to_seconds(&app->transport, 0.0);
    waveform_view_init(&app->view);

    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }

    for (int i = 0; i < app->sample_count; ++i) {
        if (SDL_strcmp(app->samples[i].path, path) == 0) {
            app->selected_sample = i;
            break;
        }
    }
    app_set_status(app, path);
    return true;
}

bool app_load_selected_sample(App *app) {
    if (app->sample_count <= 0) {
        app_set_status(app, "No WAV files in assets/samples");
        return false;
    }
    return load_clip_from_path(app, app->samples[app->selected_sample].path);
}

void app_select_sample_delta(App *app, int delta) {
    if (app->sample_count <= 0) return;
    app->selected_sample = (app->selected_sample + delta) % app->sample_count;
    if (app->selected_sample < 0) app->selected_sample += app->sample_count;
}

static void app_render_controls_legend(App *app) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_FRect panel = { 36.0f, 88.0f, 660.0f, 326.0f };
    if (panel.w > (float)w - 72.0f) panel.w = (float)w - 72.0f;
    if (panel.h > (float)h - 112.0f) panel.h = (float)h - 112.0f;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 9, 10, 16, 232);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 120, 220, 235, 255);
    SDL_RenderRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);

    float x = panel.x + 16.0f;
    float y = panel.y + 14.0f;
    SDL_RenderDebugText(app->renderer, x, y, "CONTROLS"); y += 22.0f;
    SDL_RenderDebugText(app->renderer, x, y, "F1 legend   F2 timeline/waveform   Tab sample picker in waveform"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Waveform: Space play   M metronome   [/] BPM   T tempo lock"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "A/D loop start   J/L loop end   Shift = larger step"); y += 22.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Timeline: Tab/Shift+Tab or bumpers cycle focus zones"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Timeline: Space play/pause   Enter/South activate focus   East cancel"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Ruler: Left/Right cursor by beat   Shift+Left/Right or stick pan"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Play Range: Enter/South adjust   1/2 or West/North choose handle"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Track/Roster: South selects, South again moves/places, East cancels"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Timeline R2: South play   East stop   West jump start   North loop"); y += 22.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Gamepad waveform: South/Start play   Back metronome"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Waveform: D-pad L/R trim selected edge   D-pad U/D zoom"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "R2+South set loop to visible   L2+R2+South capture loop"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Waveform R2+North tempo lock   R2+Start timeline/waveform"); y += 22.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Tempo Lock: South apply   East cancel   R2+East clear"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Tempo Lock: d-pad fine BPM/bars   sticks/bumpers downbeat");
}

static float timeline_x_for_tick(double tick, double view_start, double view_span, float x, float w) {
    return x + (float)((tick - view_start) / view_span) * w;
}

static void render_focus_outline(App *app, SDL_FRect rect, TimelineFocusZone zone) {
    if (app->timeline_focus_zone != zone) return;
    SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
    SDL_RenderRect(app->renderer, &rect);
    SDL_FRect inner = { rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f };
    if (inner.w > 0.0f && inner.h > 0.0f) SDL_RenderRect(app->renderer, &inner);
}

static void app_render_timeline(App *app) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_SetRenderDrawColor(app->renderer, 8, 7, 13, 255);
    SDL_RenderClear(app->renderer);

    float roster_w = 300.0f;
    float roster_x = (float)w - roster_w - 24.0f;
    if (roster_x < 560.0f) roster_x = (float)w * 0.62f;
    float timeline_x = 56.0f;
    float timeline_y = 258.0f;
    float timeline_w = roster_x - timeline_x - 28.0f;
    if (timeline_w < 220.0f) timeline_w = (float)w - 96.0f;
    float lane_h = 72.0f;
    SDL_FRect transport_rect = { 24.0f, 132.0f, timeline_w + 32.0f, 72.0f };
    SDL_FRect ruler_rect = { timeline_x, timeline_y - 58.0f, timeline_w, 42.0f };
    SDL_FRect play_range_rect = { timeline_x, timeline_y - 12.0f, timeline_w, 18.0f };
    SDL_FRect track_rect = { timeline_x, timeline_y + 8.0f, timeline_w, lane_h - 16.0f };

    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    SDL_RenderDebugText(app->renderer, 24, 132, "MASTER TIMELINE");
    SDL_RenderDebugTextFormat(app->renderer, 24, 196, "focus: %s%s%s%s",
                              timeline_focus_label(app->timeline_focus_zone),
                              app->timeline_play_range_adjusting ? " / adjusting" : "",
                              app->timeline_edit_mode != TIMELINE_EDIT_NONE ? " / " : "",
                              timeline_edit_mode_label(app->timeline_edit_mode));
    if (!app->timeline.initialized) {
        SDL_RenderDebugText(app->renderer, 24, 158, "No captured loops yet.");
        SDL_RenderDebugText(app->renderer, 24, 176, "Use L2+R2+South to capture the selected loop into the roster.");
        render_focus_outline(app, transport_rect, TIMELINE_FOCUS_TRANSPORT);
        return;
    }

    SDL_RenderDebugTextFormat(app->renderer, 24, 158, "timeline bpm: %.2f  meter: %d/%d  roster: %d",
                              app->timeline.timeline_bpm,
                              app->timeline.timeline_beats_per_bar,
                              app->timeline.timeline_beat_unit,
                              app->roster_clip_count);
    SDL_RenderDebugTextFormat(app->renderer, 24, 176, "length: %lld ticks  playhead: %lld  %s",
                              (long long)app->timeline.length_ticks,
                              (long long)audio_engine_get_timeline_playhead_tick(&app->audio),
                              audio_engine_timeline_is_playing(&app->audio) ? "playing" : "stopped");
    int64_t range_start = 0, range_end = 0;
    timeline_effective_play_range(&app->timeline, &range_start, &range_end);
    SDL_RenderDebugTextFormat(app->renderer, 24, 214, "cursor: %lld  range: %lld..%lld  loop: %s",
                              (long long)app->timeline.timeline_cursor_tick,
                              (long long)range_start,
                              (long long)range_end,
                              app->timeline.play_range_loop_enabled ? "on" : "off");

    clamp_timeline_view(app);
    double view_start = app->timeline.view_center_tick - app->timeline.view_span_ticks * 0.5;
    double view_end = app->timeline.view_center_tick + app->timeline.view_span_ticks * 0.5;
    if (view_end <= view_start) view_end = view_start + 1.0;
    double view_span = view_end - view_start;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 14, 14, 24, 255);
    SDL_FRect rail = { timeline_x, timeline_y - 58.0f, timeline_w, lane_h + 64.0f };
    SDL_RenderFillRect(app->renderer, &rail);
    SDL_SetRenderDrawColor(app->renderer, 85, 90, 112, 255);
    SDL_RenderRect(app->renderer, &rail);

    SDL_SetRenderDrawColor(app->renderer, 34, 34, 48, 255);
    SDL_RenderFillRect(app->renderer, &track_rect);
    SDL_SetRenderDrawColor(app->renderer, 85, 90, 112, 255);
    SDL_RenderRect(app->renderer, &track_rect);

    int64_t beat_ticks = timeline_snap_ticks(&app->timeline);
    int64_t bar_ticks = (int64_t)app->timeline.ticks_per_beat * app->timeline.timeline_beats_per_bar;
    if (bar_ticks < 1) bar_ticks = beat_ticks;
    int64_t grid_step = beat_ticks > 0 ? beat_ticks : 960;
    while (grid_step > 0 && view_span / (double)grid_step > 160.0) grid_step *= 2;
    int64_t first_grid = (int64_t)floor(view_start / (double)grid_step) * grid_step;
    if (first_grid < 0) first_grid = 0;
    for (int64_t tick = first_grid; grid_step > 0 && (double)tick <= view_end; tick += grid_step) {
        float x = timeline_x_for_tick((double)tick, view_start, view_span, timeline_x, timeline_w);
        bool bar = bar_ticks > 0 && tick % bar_ticks == 0;
        if (bar) SDL_SetRenderDrawColor(app->renderer, 130, 135, 170, 180);
        else SDL_SetRenderDrawColor(app->renderer, 70, 72, 96, 120);
        SDL_RenderLine(app->renderer, x, timeline_y - 58.0f, x, timeline_y + lane_h + 8.0f);
        if (bar && x < timeline_x + timeline_w - 28.0f) {
            int64_t bar_index = bar_ticks > 0 ? tick / bar_ticks + 1 : 1;
            SDL_SetRenderDrawColor(app->renderer, 190, 198, 210, 255);
            SDL_RenderDebugTextFormat(app->renderer, x + 4.0f, timeline_y - 52.0f, "bar %lld", (long long)bar_index);
        } else if (view_span / (double)grid_step < 24.0 && x < timeline_x + timeline_w - 18.0f) {
            int64_t beat_index = beat_ticks > 0 ? tick / beat_ticks + 1 : 1;
            SDL_SetRenderDrawColor(app->renderer, 150, 158, 176, 210);
            SDL_RenderDebugTextFormat(app->renderer, x + 4.0f, timeline_y - 38.0f, "%lld", (long long)beat_index);
        }
    }

    if (range_end > range_start) {
        float range_x0 = timeline_x_for_tick((double)range_start, view_start, view_span, timeline_x, timeline_w);
        float range_x1 = timeline_x_for_tick((double)range_end, view_start, view_span, timeline_x, timeline_w);
        if (range_x0 < timeline_x) range_x0 = timeline_x;
        if (range_x1 > timeline_x + timeline_w) range_x1 = timeline_x + timeline_w;
        if (range_x1 > range_x0) {
            SDL_FRect range_rect = { range_x0, timeline_y - 12.0f, range_x1 - range_x0, lane_h + 20.0f };
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, app->timeline.play_range_loop_enabled ? 56 : 34);
            SDL_RenderFillRect(app->renderer, &range_rect);
        }
        float start_x = timeline_x_for_tick((double)range_start, view_start, view_span, timeline_x, timeline_w);
        float end_x = timeline_x_for_tick((double)range_end, view_start, view_span, timeline_x, timeline_w);
        if (start_x >= timeline_x && start_x <= timeline_x + timeline_w) {
            SDL_FRect tab = { start_x - 5.0f, timeline_y - 20.0f, 10.0f, 18.0f };
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
            SDL_RenderFillRect(app->renderer, &tab);
            if (app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE && app->timeline_play_range_handle == TIMELINE_RANGE_HANDLE_START) SDL_RenderRect(app->renderer, &tab);
        }
        if (end_x >= timeline_x && end_x <= timeline_x + timeline_w) {
            SDL_FRect tab = { end_x - 5.0f, timeline_y - 20.0f, 10.0f, 18.0f };
            SDL_SetRenderDrawColor(app->renderer, 255, 180, 100, 255);
            SDL_RenderFillRect(app->renderer, &tab);
            if (app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE && app->timeline_play_range_handle == TIMELINE_RANGE_HANDLE_END) SDL_RenderRect(app->renderer, &tab);
        }
    }

    if (app->timeline.length_ticks > 0) {
        float x = timeline_x_for_tick((double)app->timeline.length_ticks, view_start, view_span, timeline_x, timeline_w);
        if (x >= timeline_x && x <= timeline_x + timeline_w) {
            SDL_SetRenderDrawColor(app->renderer, 255, 215, 110, 230);
            SDL_RenderLine(app->renderer, x, timeline_y - 22.0f, x, timeline_y + lane_h + 12.0f);
            SDL_RenderDebugText(app->renderer, x + 4.0f, timeline_y + lane_h + 10.0f, "end");
        }
    }

    float cursor_x = timeline_x_for_tick((double)app->timeline.timeline_cursor_tick, view_start, view_span, timeline_x, timeline_w);
    if (cursor_x >= timeline_x && cursor_x <= timeline_x + timeline_w) {
        SDL_SetRenderDrawColor(app->renderer, 250, 250, 255, 255);
        SDL_RenderLine(app->renderer, cursor_x, timeline_y - 62.0f, cursor_x, timeline_y + lane_h + 20.0f);
        SDL_RenderDebugText(app->renderer, cursor_x + 5.0f, timeline_y + lane_h + 22.0f, "cursor");
    }

    int64_t playhead_tick = audio_engine_get_timeline_playhead_tick(&app->audio);
    float playhead_x = timeline_x_for_tick((double)playhead_tick, view_start, view_span, timeline_x, timeline_w);
    if (playhead_x >= timeline_x && playhead_x <= timeline_x + timeline_w) {
        SDL_SetRenderDrawColor(app->renderer, 180, 255, 120, 255);
        SDL_RenderLine(app->renderer, playhead_x, timeline_y - 62.0f, playhead_x, timeline_y + lane_h + 20.0f);
    }

    for (int i = 0; i < app->timeline.instance_count; ++i) {
        TimelineInstance *instance = &app->timeline.instances[i];
        if (instance->roster_clip_index < 0 || instance->roster_clip_index >= app->roster_clip_count) continue;
        RosterClip *clip = &app->roster[instance->roster_clip_index];
        double instance_start = (double)instance->start_tick;
        double instance_end = (double)(instance->start_tick + instance->duration_ticks);
        if (instance_end < view_start || instance_start > view_end) continue;
        float x = timeline_x_for_tick(instance_start, view_start, view_span, timeline_x, timeline_w);
        float end_x = timeline_x_for_tick(instance_end, view_start, view_span, timeline_x, timeline_w);
        if (x < timeline_x) x = timeline_x;
        if (end_x > timeline_x + timeline_w) end_x = timeline_x + timeline_w;
        float block_w = end_x - x;
        if (block_w < 8.0f) block_w = 8.0f;
        SDL_FRect block = { x + 2.0f, track_rect.y + 8.0f, block_w - 4.0f, track_rect.h - 16.0f };
        SDL_SetRenderDrawColor(app->renderer, clip->color.r, clip->color.g, clip->color.b, 218);
        SDL_RenderFillRect(app->renderer, &block);
        if (i == app->selected_timeline_instance) SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        else SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 220);
        SDL_RenderRect(app->renderer, &block);
        SDL_SetRenderDrawColor(app->renderer, 8, 8, 12, 255);
        SDL_RenderDebugText(app->renderer, block.x + 8.0f, block.y + 10.0f, clip->name);
    }

    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE &&
        app->timeline_edit_roster_clip_index >= 0 &&
        app->timeline_edit_roster_clip_index < app->roster_clip_count &&
        app->timeline_edit_duration_ticks > 0) {
        RosterClip *clip = &app->roster[app->timeline_edit_roster_clip_index];
        double ghost_start = (double)app->timeline_edit_ghost_start_tick;
        double ghost_end = (double)(app->timeline_edit_ghost_start_tick + app->timeline_edit_duration_ticks);
        if (ghost_end >= view_start && ghost_start <= view_end) {
            float x = timeline_x_for_tick(ghost_start, view_start, view_span, timeline_x, timeline_w);
            float end_x = timeline_x_for_tick(ghost_end, view_start, view_span, timeline_x, timeline_w);
            if (x < timeline_x) x = timeline_x;
            if (end_x > timeline_x + timeline_w) end_x = timeline_x + timeline_w;
            float block_w = end_x - x;
            if (block_w < 8.0f) block_w = 8.0f;
            SDL_FRect ghost = { x + 2.0f, track_rect.y + 8.0f, block_w - 4.0f, track_rect.h - 16.0f };
            if (app->timeline_edit_ghost_valid) {
                SDL_SetRenderDrawColor(app->renderer, clip->color.r, clip->color.g, clip->color.b, 106);
            } else {
                SDL_SetRenderDrawColor(app->renderer, 255, 72, 88, 116);
            }
            SDL_RenderFillRect(app->renderer, &ghost);
            SDL_SetRenderDrawColor(app->renderer,
                                   app->timeline_edit_ghost_valid ? 245 : 255,
                                   app->timeline_edit_ghost_valid ? 245 : 80,
                                   app->timeline_edit_ghost_valid ? 255 : 96,
                                   255);
            SDL_RenderRect(app->renderer, &ghost);
            SDL_RenderDebugText(app->renderer, ghost.x + 8.0f, ghost.y + 10.0f,
                                app->timeline_edit_ghost_valid ? "ghost" : "overlap");
        }
    }

    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    if (timeline_w >= 220.0f) {
        SDL_RenderDebugText(app->renderer, timeline_x, timeline_y + lane_h + 24.0f, "00:00:00");
    }

    render_focus_outline(app, transport_rect, TIMELINE_FOCUS_TRANSPORT);
    render_focus_outline(app, ruler_rect, TIMELINE_FOCUS_RULER);
    render_focus_outline(app, play_range_rect, TIMELINE_FOCUS_PLAY_RANGE);
    render_focus_outline(app, track_rect, TIMELINE_FOCUS_TRACK_AREA);

    if (roster_x + roster_w < (float)w) {
        SDL_FRect roster_panel = { roster_x, 132.0f, roster_w, (float)h - 162.0f };
        SDL_SetRenderDrawColor(app->renderer, 14, 15, 22, 220);
        SDL_RenderFillRect(app->renderer, &roster_panel);
        SDL_SetRenderDrawColor(app->renderer, 80, 90, 110, 255);
        SDL_RenderRect(app->renderer, &roster_panel);
        SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
        SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 14.0f, "ROSTER");
        int visible = ((int)roster_panel.h - 52) / 18;
        if (visible > app->roster_clip_count) visible = app->roster_clip_count;
        for (int i = 0; i < visible; ++i) {
            RosterClip *clip = &app->roster[i];
            float y = roster_panel.y + 42.0f + (float)i * 18.0f;
            if (i == app->selected_roster_clip) {
                SDL_FRect row = { roster_panel.x + 10.0f, y - 3.0f, roster_panel.w - 20.0f, 16.0f };
                SDL_SetRenderDrawColor(app->renderer, 64, 72, 96, 210);
                SDL_RenderFillRect(app->renderer, &row);
            }
            SDL_FRect swatch = { roster_panel.x + 14.0f, y - 1.0f, 12.0f, 12.0f };
            SDL_SetRenderDrawColor(app->renderer, clip->color.r, clip->color.g, clip->color.b, 255);
            SDL_RenderFillRect(app->renderer, &swatch);
            SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
            SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + 34.0f, y, "%s  %.1fb %.2fbpm",
                                      clip->name, clip->target_beats, clip->source_bpm);
        }
        render_focus_outline(app, roster_panel, TIMELINE_FOCUS_ROSTER);
    }
}

static void app_render_overlay(App *app) {
    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    const char *current = app->clip.file_path[0] ? app->clip.file_path : "generated";
    SDL_RenderDebugTextFormat(app->renderer, 12, 10, "sample: %s", current);
    if (app->status_text[0]) SDL_RenderDebugText(app->renderer, 12, 24, app->status_text);
    SDL_RenderDebugTextFormat(app->renderer, 12, 38, "bpm: %.2f (%s)", app->transport.bpm, app_bpm_source_label(app));
    SDL_RenderDebugTextFormat(app->renderer, 12, 52, "tempo lock: %s%s%s",
                              app->clip.clip_tempo_locked ? "active" : "off",
                              app->has_retained_tempo_lock_params ? " retained" : "",
                              app->retained_tempo_lock_stale ? " stale" : "");

    TempoLockParams params;
    if (app_get_active_tempo_params(app, &params)) {
        double target_beats = params.target_bars * (double)params.beats_per_bar;
        double loop_duration = app->clip.sample_rate > 0 ? (double)(app->clip.loop_end_frame - app->clip.loop_start_frame) / (double)app->clip.sample_rate : 0.0;
        double target_duration = params.bpm > 0.0 ? target_beats * 60.0 / params.bpm : 0.0;
        double diff = loop_duration - target_duration;
        if (app->tempo_lock_mode) SDL_RenderDebugText(app->renderer, 12, 66, "TEMPO LOCK MODE");
        SDL_RenderDebugTextFormat(app->renderer, 12, 80, "meter: %d/%d  bars: %.1f  beats: %.1f",
                                  params.beats_per_bar, params.beat_unit, params.target_bars, target_beats);
        SDL_RenderDebugTextFormat(app->renderer, 12, 94, "loop: %.3fs  target: %.3fs  diff: %+.3fs",
                                  loop_duration, target_duration, diff);
    } else if (app->tempo_lock_mode) {
        SDL_RenderDebugText(app->renderer, 12, 66, "TEMPO LOCK MODE");
    }
    SDL_RenderDebugTextFormat(app->renderer, 12, 108, "view: %s  roster: %d",
                              app->view_mode == APP_VIEW_TIMELINE ? "timeline" : "waveform",
                              app->roster_clip_count);

    if (app->sample_selector_open) {

        int w = 0, h = 0;
        SDL_GetRenderOutputSize(app->renderer, &w, &h);
        SDL_FRect panel = { 24.0f, 48.0f, (float)w - 48.0f, (float)h - 96.0f };
        SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app->renderer, 12, 12, 18, 225);
        SDL_RenderFillRect(app->renderer, &panel);
        SDL_SetRenderDrawColor(app->renderer, 100, 230, 240, 255);
        SDL_RenderRect(app->renderer, &panel);

        SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 14.0f, "assets/samples/");
        SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 30.0f, "Up/Down select   Return load   Tab close");

        if (app->sample_count <= 0) {
            SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 58.0f, "No WAV files found.");
            return;
        }

        int visible_rows = ((int)panel.h - 82) / 16;
        if (visible_rows < 1) visible_rows = 1;
        int first = app->selected_sample - visible_rows / 2;
        if (first < 0) first = 0;
        if (first + visible_rows > app->sample_count) first = app->sample_count - visible_rows;
        if (first < 0) first = 0;

        for (int row = 0; row < visible_rows && first + row < app->sample_count; ++row) {
            int index = first + row;
            float y = panel.y + 62.0f + (float)row * 16.0f;
            if (index == app->selected_sample) {
                SDL_FRect highlight = { panel.x + 10.0f, y - 2.0f, panel.w - 20.0f, 14.0f };
                SDL_SetRenderDrawColor(app->renderer, 65, 85, 100, 210);
                SDL_RenderFillRect(app->renderer, &highlight);
                SDL_SetRenderDrawColor(app->renderer, 255, 220, 130, 255);
            } else {
                SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
            }
            SDL_RenderDebugTextFormat(app->renderer, panel.x + 16.0f, y, "%c %s", index == app->selected_sample ? '>' : ' ', app->samples[index].name);
        }
        return;
    }

    if (app->controls_legend_open) app_render_controls_legend(app);
}

bool app_init(App *app){
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){ fprintf(stderr,"SDL init failed: %s\n",SDL_GetError()); return false; }
    app->window=SDL_CreateWindow("vaporplane",1280,720,SDL_WINDOW_RESIZABLE); if(!app->window) return false;
    app->renderer=SDL_CreateRenderer(app->window,NULL); if(!app->renderer) return false;
    app->gamepad=NULL; app->gamepad_id=0; app->running=true;
    int gamepad_count = 0;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&gamepad_count);
    if (gamepads && gamepad_count > 0) {
        app->gamepad = SDL_OpenGamepad(gamepads[0]);
        if (app->gamepad) app->gamepad_id = SDL_GetGamepadID(app->gamepad);
    }
    SDL_free(gamepads);
    app_refresh_sample_list(app);
    clip_init_generated(&app->clip, 48000, 2.0f);
    transport_init(&app->transport, 120.0, 960, 4, 4);
    app->transport_bpm = 120.0;
    app->transport_bpm_manual = false;
    app->view_mode = APP_VIEW_WAVEFORM;
    app->controls_legend_open = false;
    app->timeline.ticks_per_beat = app->transport.ppqn;
    app->timeline.timeline_bpm = 120.0;
    app->timeline.timeline_beats_per_bar = 4;
    app->timeline.timeline_beat_unit = 4;
    app->timeline.timeline_cursor_tick = 0;
    app->timeline.play_range_start_tick = 0;
    app->timeline.play_range_end_tick = 0;
    app->timeline.play_range_loop_enabled = false;
    app->timeline.play_range_custom = false;
    app->timeline.view_center_tick = (double)app->timeline.ticks_per_beat * 2.0;
    app->timeline.view_span_ticks = (double)app->timeline.ticks_per_beat * 4.0;
    app->timeline_focus_zone = TIMELINE_FOCUS_RULER;
    app->timeline_play_range_handle = TIMELINE_RANGE_HANDLE_START;
    app->timeline_play_range_adjusting = false;
    app->selected_roster_clip = -1;
    app->selected_roster_clip_armed = false;
    app->selected_timeline_instance = -1;
    waveform_view_init(&app->view);
    if(!audio_engine_init(&app->audio,&app->clip,&app->transport)) return false;
    audio_engine_set_timeline(&app->audio, app->roster, &app->roster_clip_count, &app->timeline);
    if(app->sample_count > 0) app_load_selected_sample(app);
    return true;
}
void app_focus_loop_start(App *app){ app->view.target_center=(double)app->clip.loop_start_frame/(double)app->clip.frame_count; }
void app_focus_loop_end(App *app){ app->view.target_center=(double)app->clip.loop_end_frame/(double)app->clip.frame_count; }

void app_run(App *app){
    Uint64 prev=SDL_GetTicksNS();
    while(app->running){
        SDL_Event e; while(SDL_PollEvent(&e)) if(!input_handle_event(app,&e)) app->running=false;
        Uint64 now=SDL_GetTicksNS(); double dt=(double)(now-prev)/1e9; prev=now; input_update_gamepad(app,dt);
        waveform_view_update(&app->view, dt);
        if (app->view_mode == APP_VIEW_TIMELINE) {
            app_render_timeline(app);
        } else {
            TempoLockParams guide_params;
            TempoLockParams *guide = app_get_active_tempo_params(app, &guide_params) ? &guide_params : NULL;
            waveform_render(app->renderer,&app->clip,&app->view,audio_engine_get_playhead_frame(&app->audio),guide);
        }
        app_render_overlay(app);
        SDL_RenderPresent(app->renderer);
    }
}
void app_shutdown(App *app){
    audio_engine_shutdown(&app->audio);
    clip_destroy(&app->clip);
    for (int i = 0; i < app->roster_clip_count; ++i) roster_clip_destroy(&app->roster[i]);
    if(app->gamepad) SDL_CloseGamepad(app->gamepad);
    if(app->renderer) SDL_DestroyRenderer(app->renderer);
    if(app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}
