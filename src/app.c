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

static void app_set_audio_unavailable_status(App *app) {
    app_set_status(app, "Audio unavailable");
}

static bool path_is_directory(const char *path) {
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static void path_join(char *out, size_t out_size, const char *base, const char *leaf) {
    if (!out || out_size == 0) return;
    if (!base || !base[0]) {
        SDL_strlcpy(out, leaf ? leaf : "", out_size);
        return;
    }
    size_t len = SDL_strlen(base);
    const char *separator = (len > 0 && (base[len - 1] == '/' || base[len - 1] == '\\')) ? "" : "/";
    SDL_snprintf(out, out_size, "%s%s%s", base, separator, leaf ? leaf : "");
}

static void app_resolve_sample_dir(App *app) {
    char candidate[CLIP_MAX_PATH];
    const char *base = SDL_GetBasePath();
    if (base && base[0]) {
        path_join(candidate, sizeof(candidate), base, "wav");
        if (path_is_directory(candidate)) {
            SDL_strlcpy(app->sample_dir, candidate, sizeof(app->sample_dir));
            return;
        }
    }
    SDL_strlcpy(app->sample_dir, "assets/samples", sizeof(app->sample_dir));
}

static void app_resolve_roster_export_dir(App *app) {
    char exports_dir[CLIP_MAX_PATH];
    const char *base = SDL_GetBasePath();
    if (base && base[0]) {
        path_join(exports_dir, sizeof(exports_dir), base, "exports");
        path_join(app->roster_export_dir, sizeof(app->roster_export_dir), exports_dir, "roster");
        if (SDL_CreateDirectory(app->roster_export_dir)) {
            app->roster_export_dir_is_base_path = true;
            return;
        }
    }

    path_join(exports_dir, sizeof(exports_dir), "exports", "roster");
    SDL_strlcpy(app->roster_export_dir, exports_dir, sizeof(app->roster_export_dir));
    app->roster_export_dir_is_base_path = false;
    SDL_CreateDirectory(app->roster_export_dir);
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

typedef struct {
    SDL_Color deep;
    SDL_Color pastel;
} LanePalette;

static const LanePalette lane_palettes[] = {
    { {  78,  22,  38, 255 }, { 255, 148, 166, 255 } },
    { {  16,  68,  76, 255 }, { 130, 238, 234, 255 } },
    { {  86,  62,  16, 255 }, { 255, 216, 132, 255 } },
    { {  28,  76,  42, 255 }, { 156, 236, 160, 255 } },
    { {  58,  38,  92, 255 }, { 196, 166, 255, 255 } },
    { {  88,  45,  20, 255 }, { 255, 172, 116, 255 } },
    { {  30,  55, 100, 255 }, { 146, 190, 255, 255 } },
    { {  86,  30,  72, 255 }, { 250, 144, 216, 255 } },
};

static int lane_palette_count(void) {
    return (int)(sizeof(lane_palettes) / sizeof(lane_palettes[0]));
}

static const LanePalette *lane_palette_for_index(int palette_index) {
    int count = lane_palette_count();
    if (count <= 0) return NULL;
    if (palette_index < 0) palette_index = 0;
    return &lane_palettes[palette_index % count];
}

static SDL_Color color_mix(SDL_Color a, SDL_Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SDL_Color out;
    out.r = (Uint8)lrintf((float)a.r + ((float)b.r - (float)a.r) * t);
    out.g = (Uint8)lrintf((float)a.g + ((float)b.g - (float)a.g) * t);
    out.b = (Uint8)lrintf((float)a.b + ((float)b.b - (float)a.b) * t);
    out.a = (Uint8)lrintf((float)a.a + ((float)b.a - (float)a.a) * t);
    return out;
}

static SDL_Color color_muted(SDL_Color color) {
    Uint8 grey = (Uint8)lrintf((float)color.r * 0.30f + (float)color.g * 0.59f + (float)color.b * 0.11f);
    SDL_Color flat = { grey, grey, grey, color.a };
    SDL_Color out = color_mix(color, flat, 0.72f);
    out = color_mix(out, (SDL_Color){ 22, 23, 30, color.a }, 0.36f);
    return out;
}

static void set_draw_color(SDL_Renderer *renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

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

static TimelineInstanceRef timeline_instance_ref_invalid(void) {
    TimelineInstanceRef ref = { -1, -1 };
    return ref;
}

static bool timeline_instance_ref_equal(TimelineInstanceRef a, TimelineInstanceRef b) {
    return a.lane_index == b.lane_index && a.instance_index == b.instance_index;
}

static bool timeline_lane_index_valid(int lane_index) {
    return lane_index >= 0 && lane_index < TIMELINE_MAX_LANES;
}

static bool timeline_instance_ref_valid(const MasterTimeline *timeline, TimelineInstanceRef ref) {
    if (!timeline || !timeline_lane_index_valid(ref.lane_index)) return false;
    const TimelineLane *lane = &timeline->lanes[ref.lane_index];
    return ref.instance_index >= 0 && ref.instance_index < lane->instance_count;
}

static TimelineInstance *timeline_instance_from_ref(MasterTimeline *timeline, TimelineInstanceRef ref) {
    if (!timeline_instance_ref_valid(timeline, ref)) return NULL;
    return &timeline->lanes[ref.lane_index].instances[ref.instance_index];
}

static const TimelineInstance *timeline_const_instance_from_ref(const MasterTimeline *timeline, TimelineInstanceRef ref) {
    if (!timeline_instance_ref_valid(timeline, ref)) return NULL;
    return &timeline->lanes[ref.lane_index].instances[ref.instance_index];
}

static int timeline_total_instance_count(const MasterTimeline *timeline) {
    if (!timeline) return 0;
    int total = 0;
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        total += timeline->lanes[lane_index].instance_count;
    }
    return total;
}

static bool timeline_has_instances(const MasterTimeline *timeline) {
    return timeline_total_instance_count(timeline) > 0;
}

static bool app_uses_timeline_transport(const App *app) {
    return app->view_mode == APP_VIEW_TIMELINE || app->view_mode == APP_VIEW_LANE_INSPECTOR;
}

static void reset_lane_analyzer_visual(App *app, int lane_index);

static void timeline_init_lanes(MasterTimeline *timeline) {
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &timeline->lanes[lane_index];
        lane->palette_index = lane_index % lane_palette_count();
        if (lane->gain <= 0.0f) lane->gain = 1.0f;
        if (lane->instance_count < 0) lane->instance_count = 0;
        if (lane->instance_count > APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
            lane->instance_count = APP_MAX_TIMELINE_INSTANCES_PER_LANE;
        }
    }
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
        case TIMELINE_FOCUS_LANE_INDEX: return "LANE INDEX";
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

static const char *timeline_edit_verb_label(TimelineEditMode mode) {
    switch (mode) {
        case TIMELINE_EDIT_MOVE_INSTANCE: return "MOVING";
        case TIMELINE_EDIT_PLACE_CLIP: return "PLACING";
        case TIMELINE_EDIT_NONE:
        default: return "";
    }
}

static const char *timeline_edit_clip_name(const App *app) {
    int index = app->timeline_edit_roster_clip_index;
    if (index >= 0 && index < app->roster_clip_count) return app->roster[index].name;
    return "clip";
}

static void app_set_timeline_edit_status(App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    SDL_snprintf(app->status_text, sizeof(app->status_text), "%s %s",
                 timeline_edit_verb_label(app->timeline_edit_mode),
                 timeline_edit_clip_name(app));
}

#define TIMELINE_CONTEXT_MAX_ITEMS 4

static void app_timeline_clear_context_menu(App *app) {
    app->timeline_context_menu_open = false;
    app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_NONE;
    app->timeline_context_menu_selected = 0;
    app->timeline_context_menu_instance = timeline_instance_ref_invalid();
    app->timeline_context_menu_roster_index = -1;
    app->timeline_context_menu_tick = 0;
}

static int timeline_bar_ticks(const MasterTimeline *timeline) {
    int ticks_per_beat = timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat : 960;
    int beats_per_bar = timeline->timeline_beats_per_bar > 0 ? timeline->timeline_beats_per_bar : 4;
    int64_t ticks = (int64_t)ticks_per_beat * (int64_t)beats_per_bar;
    if (ticks < 1) ticks = 1;
    if (ticks > 0x7fffffff) ticks = 0x7fffffff;
    return (int)ticks;
}

static int timeline_context_menu_items(const App *app,
                                       TimelineContextMenuItem *items,
                                       int max_items) {
    if (!app || !items || max_items <= 0 || !app->timeline_context_menu_open) return 0;
    int count = 0;
    switch (app->timeline_context_menu_scope) {
        case TIMELINE_CONTEXT_SCOPE_TIMELINE:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_INSERT_BAR;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_INSTANCE:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_INSERT_BAR;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_ROSTER:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_DELETE_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_NONE:
        default:
            break;
    }
    return count;
}

static const char *timeline_context_menu_title(TimelineContextMenuScope scope) {
    switch (scope) {
        case TIMELINE_CONTEXT_SCOPE_TIMELINE: return "TIMELINE MENU";
        case TIMELINE_CONTEXT_SCOPE_INSTANCE: return "INSTANCE MENU";
        case TIMELINE_CONTEXT_SCOPE_ROSTER: return "ROSTER MENU";
        case TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE: return "DELETE ROSTER CLIP?";
        case TIMELINE_CONTEXT_SCOPE_NONE:
        default: return "MENU";
    }
}

static const char *timeline_context_item_label(TimelineContextMenuItem item) {
    switch (item) {
        case TIMELINE_CONTEXT_ITEM_INSERT_BAR: return "Insert bar";
        case TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE: return "Remove instance";
        case TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER: return "Export WAV";
        case TIMELINE_CONTEXT_ITEM_DELETE_ROSTER: return "Delete roster clip";
        case TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_ROSTER: return "Delete clip and instances";
        case TIMELINE_CONTEXT_ITEM_CANCEL:
        default: return "Cancel";
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

static void recompute_timeline_length_no_lock(App *app) {
    int64_t length = 0;
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        for (int i = 0; i < lane->instance_count; ++i) {
            TimelineInstance *instance = &lane->instances[i];
            if (instance->duration_ticks <= 0) continue;
            int64_t end = instance->start_tick + instance->duration_ticks;
            if (end > length) length = end;
        }
    }
    app->timeline.length_ticks = length;
    sync_timeline_play_range_no_lock(app);
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

static bool timeline_range_overlaps_existing(const App *app,
                                             int lane_index,
                                             int64_t start,
                                             int64_t duration,
                                             TimelineInstanceRef ignore_instance) {
    if (duration <= 0) return true;
    int64_t end = start + duration;
    if (start < 0 || end <= start) return true;
    if (!timeline_lane_index_valid(lane_index)) return true;

    const TimelineLane *lane = &app->timeline.lanes[lane_index];
    for (int i = 0; i < lane->instance_count; ++i) {
        TimelineInstanceRef ref = { lane_index, i };
        if (timeline_instance_ref_equal(ref, ignore_instance)) continue;
        const TimelineInstance *instance = &lane->instances[i];
        if (instance->duration_ticks <= 0) continue;
        int64_t other_start = instance->start_tick;
        int64_t other_end = instance->start_tick + instance->duration_ticks;
        if (start < other_end && end > other_start) return true;
    }
    return false;
}

static bool timeline_ghost_is_valid(const App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return false;
    if (!timeline_lane_index_valid(app->timeline_edit_ghost_lane)) return false;
    const TimelineLane *ghost_lane = &app->timeline.lanes[app->timeline_edit_ghost_lane];
    if (app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP &&
        ghost_lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
        return false;
    }
    if (app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE &&
        app->timeline_edit_ghost_lane != app->timeline_edit_instance.lane_index &&
        ghost_lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
        return false;
    }
    TimelineInstanceRef ignore = app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE ?
        app->timeline_edit_instance : timeline_instance_ref_invalid();
    return !timeline_range_overlaps_existing(app,
                                             app->timeline_edit_ghost_lane,
                                             app->timeline_edit_ghost_start_tick,
                                             app->timeline_edit_duration_ticks,
                                             ignore);
}

static void timeline_update_ghost_valid(App *app) {
    app->timeline_edit_ghost_valid = timeline_ghost_is_valid(app);
}

static void timeline_clamp_ghost_start(App *app) {
    if (app->timeline_edit_ghost_start_tick < 0) app->timeline_edit_ghost_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline_edit_ghost_start_tick);
}

static void timeline_enter_move_instance(App *app) {
    if (!timeline_instance_ref_valid(&app->timeline, app->selected_timeline_instance)) {
        app_set_status(app, "no instance selected");
        return;
    }
    TimelineInstance *instance = timeline_instance_from_ref(&app->timeline, app->selected_timeline_instance);
    app->timeline_edit_mode = TIMELINE_EDIT_MOVE_INSTANCE;
    app->timeline_edit_instance = app->selected_timeline_instance;
    app->timeline_edit_roster_clip_index = instance->roster_clip_index;
    app->timeline_edit_original_lane = app->selected_timeline_instance.lane_index;
    app->timeline_edit_ghost_lane = app->selected_timeline_instance.lane_index;
    app->timeline_edit_original_start_tick = instance->start_tick;
    app->timeline_edit_ghost_start_tick = instance->start_tick;
    app->timeline_edit_duration_ticks = instance->duration_ticks;
    timeline_update_ghost_valid(app);
    app_set_timeline_edit_status(app);
}

static void timeline_enter_place_clip(App *app) {
    if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
        app_set_status(app, "roster empty");
        return;
    }
    int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    if (app->timeline.lanes[lane_index].instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
        app_set_status(app, "lane full");
        return;
    }
    int64_t duration = timeline_clip_duration_ticks(app, app->selected_roster_clip);
    if (duration <= 0) {
        app_set_status(app, "Invalid roster clip");
        return;
    }
    app->timeline_edit_mode = TIMELINE_EDIT_PLACE_CLIP;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_roster_clip_index = app->selected_roster_clip;
    app->timeline_edit_original_lane = lane_index;
    app->timeline_edit_ghost_lane = lane_index;
    app->timeline_edit_original_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    app->timeline_edit_duration_ticks = duration;
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    if (app->timeline_edit_ghost_valid) app_set_timeline_edit_status(app);
    else app_set_status(app, "overlap blocked");
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
    if (app_uses_timeline_transport(app)) {
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
    if (app_uses_timeline_transport(app)) return BPM_SOURCE_MASTER_TIMELINE;
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
        timeline_init_lanes(&app->timeline);
        TimelineLane *lane = &app->timeline.lanes[0];
        lane->instance_count = 1;
        lane->instances[0].roster_clip_index = roster_index;
        lane->instances[0].start_tick = 0;
        lane->instances[0].duration_ticks = beats_to_ticks(next.target_beats, app->timeline.ticks_per_beat);
        lane->instances[0].midi_note = next.midi_note;
        lane->instances[0].midi_channel = next.midi_channel;
        lane->instances[0].midi_velocity = next.midi_velocity;
        app->timeline.length_ticks = lane->instances[0].duration_ticks;
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
        app->selected_timeline_lane = 0;
        app->selected_timeline_instance = (TimelineInstanceRef){ 0, 0 };
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
        app_timeline_clear_context_menu(app);
        sync_transport_from_app(app);
    } else {
        audio_engine_set_active_lane_analyzer(&app->audio, -1);
        audio_engine_stop_timeline(&app->audio, true);
        audio_engine_set_playback_mode(&app->audio, AUDIO_PLAYBACK_WAVEFORM);
        app->view_mode = APP_VIEW_WAVEFORM;
        app->transport.playing = false;
        app_timeline_clear_context_menu(app);
        sync_transport_from_app(app);
    }
}

void app_toggle_controls_legend(App *app) {
    app->controls_legend_open = !app->controls_legend_open;
}

void app_toggle_timeline_playback(App *app) {
    if (!app_uses_timeline_transport(app)) return;
    if (!app->audio.stream) {
        app->timeline.playing = false;
        app->transport.playing = false;
        app_set_audio_unavailable_status(app);
        return;
    }
    sync_timeline_play_range(app);
    int64_t range_start = 0, range_end = 0;
    timeline_effective_play_range(&app->timeline, &range_start, &range_end);
    if (!app->timeline.initialized || !timeline_has_instances(&app->timeline) || range_end <= range_start) {
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
    if (!app_uses_timeline_transport(app)) return;
    audio_engine_stop_timeline(&app->audio, true);
    audio_engine_stop_preview(&app->audio);
    int64_t range_start = 0;
    timeline_effective_play_range(&app->timeline, &range_start, NULL);
    app->timeline.timeline_cursor_tick = range_start;
    sync_transport_from_app(app);
    app_set_status(app, "Timeline rewound");
}

void app_timeline_jump_to_play_range_start(App *app) {
    if (!app_uses_timeline_transport(app)) return;
    sync_timeline_play_range(app);
    int64_t range_start = 0;
    timeline_effective_play_range(&app->timeline, &range_start, NULL);
    audio_engine_set_timeline_playhead(&app->audio, range_start);
    app->timeline.timeline_cursor_tick = range_start;
    sync_transport_from_app(app);
    app_set_status(app, "Jumped to play range start");
}

void app_timeline_jump_playhead_to_cursor(App *app) {
    if (!app_uses_timeline_transport(app)) return;
    sync_timeline_play_range(app);
    audio_engine_set_timeline_playhead(&app->audio, app->timeline.timeline_cursor_tick);
    sync_transport_from_app(app);
    app_set_status(app, "Playhead jumped to cursor");
}

void app_timeline_toggle_play_range_loop(App *app) {
    if (!app_uses_timeline_transport(app)) return;
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
    app_timeline_clear_context_menu(app);
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

void app_timeline_move_cursor_by_bar(App *app, int direction) {
    if (direction == 0) return;
    sync_timeline_play_range(app);
    int64_t length = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
    int beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    int64_t bar_ticks = timeline_snap_ticks(&app->timeline) * (int64_t)beats_per_bar;
    if (bar_ticks < 1) bar_ticks = 1;
    app->timeline.timeline_cursor_tick = clamp_i64(app->timeline.timeline_cursor_tick + bar_ticks * direction, 0, length);
}

void app_timeline_select_play_range_handle(App *app, TimelineRangeHandle handle) {
    app->timeline_play_range_handle = handle;
    app->timeline_focus_zone = TIMELINE_FOCUS_PLAY_RANGE;
    app_timeline_clear_context_menu(app);
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

void app_timeline_select_lane_delta(App *app, int delta) {
    if (delta == 0) return;
    app->selected_timeline_lane = clamp_int(app->selected_timeline_lane + delta, 0, TIMELINE_MAX_LANES - 1);
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Track lane %d", app->selected_timeline_lane + 1);
}

void app_timeline_nudge_edit_ghost(App *app, int direction) {
    if (direction == 0 || app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    int64_t snap = timeline_snap_ticks(&app->timeline);
    app->timeline_edit_ghost_start_tick += snap * direction;
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    if (!app->timeline_edit_ghost_valid) app_set_status(app, "overlap blocked");
    else app_set_timeline_edit_status(app);
}

void app_timeline_nudge_edit_lane(App *app, int direction) {
    if (direction == 0 || app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    app->timeline_edit_ghost_lane = clamp_int(app->timeline_edit_ghost_lane + direction, 0, TIMELINE_MAX_LANES - 1);
    app->selected_timeline_lane = app->timeline_edit_ghost_lane;
    timeline_update_ghost_valid(app);
    if (!app->timeline_edit_ghost_valid) app_set_status(app, "overlap blocked");
    else app_set_timeline_edit_status(app);
}

static TimelineInstanceRef app_timeline_instance_at_cursor(App *app) {
    int64_t cursor = app->timeline.timeline_cursor_tick;
    TimelineInstanceRef best = timeline_instance_ref_invalid();
    int64_t tolerance = timeline_snap_ticks(&app->timeline);
    if (tolerance < 1) tolerance = 1;
    int64_t best_distance = tolerance + 1;

    int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    for (int i = 0; i < lane->instance_count; ++i) {
        TimelineInstance *instance = &lane->instances[i];
        int64_t start = instance->start_tick;
        int64_t end = instance->start_tick + instance->duration_ticks;
        if (cursor >= start && cursor < end) {
            TimelineInstanceRef ref = { lane_index, i };
            return ref;
        }
        int64_t distance = llabs(cursor - start);
        int64_t end_distance = llabs(cursor - end);
        if (end_distance < distance) distance = end_distance;
        if (distance <= tolerance && distance < best_distance) {
            best_distance = distance;
            best = (TimelineInstanceRef){ lane_index, i };
        }
    }
    return best;
}

static void app_timeline_select_instance_at_cursor(App *app) {
    TimelineInstanceRef ref = app_timeline_instance_at_cursor(app);
    if (timeline_instance_ref_valid(&app->timeline, ref)) {
        app->selected_timeline_instance = ref;
        app->selected_timeline_lane = ref.lane_index;
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Selected timeline instance");
        return;
    }
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    app_set_status(app, "no instance at cursor");
}

static void app_timeline_confirm_edit_mode(App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    timeline_update_ghost_valid(app);
    if (!app->timeline_edit_ghost_valid) {
        app_set_status(app, "overlap blocked");
        return;
    }

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    if (app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE) {
        TimelineInstance *instance = timeline_instance_from_ref(&app->timeline, app->timeline_edit_instance);
        if (!instance || !timeline_lane_index_valid(app->timeline_edit_ghost_lane)) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app->timeline_edit_mode = TIMELINE_EDIT_NONE;
            app_set_status(app, "Move cancelled");
            return;
        }
        TimelineInstance moved = *instance;
        moved.start_tick = app->timeline_edit_ghost_start_tick;
        app->timeline.timeline_cursor_tick = app->timeline_edit_ghost_start_tick;
        if (app->timeline_edit_ghost_lane == app->timeline_edit_instance.lane_index) {
            *instance = moved;
            app->selected_timeline_instance = app->timeline_edit_instance;
        } else {
            TimelineLane *from_lane = &app->timeline.lanes[app->timeline_edit_instance.lane_index];
            TimelineLane *to_lane = &app->timeline.lanes[app->timeline_edit_ghost_lane];
            if (to_lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
                if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
                app_set_status(app, "lane full");
                return;
            }
            int original_index = app->timeline_edit_instance.instance_index;
            for (int i = original_index; i + 1 < from_lane->instance_count; ++i) {
                from_lane->instances[i] = from_lane->instances[i + 1];
            }
            from_lane->instance_count--;
            int new_index = to_lane->instance_count++;
            to_lane->instances[new_index] = moved;
            app->selected_timeline_instance = (TimelineInstanceRef){ app->timeline_edit_ghost_lane, new_index };
        }
        app->selected_timeline_lane = app->timeline_edit_ghost_lane;
        recompute_timeline_length_no_lock(app);
        sync_timeline_play_range_no_lock(app);
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        app->timeline_edit_mode = TIMELINE_EDIT_NONE;
        app_set_status(app, "Instance moved");
        return;
    }

    if (app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP) {
        if (!timeline_lane_index_valid(app->timeline_edit_ghost_lane) ||
            app->timeline_edit_roster_clip_index < 0 ||
            app->timeline_edit_roster_clip_index >= app->roster_clip_count) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app_set_status(app, "Invalid placement");
            return;
        }
        TimelineLane *lane = &app->timeline.lanes[app->timeline_edit_ghost_lane];
        if (lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app_set_status(app, "lane full");
            return;
        }
        int index = lane->instance_count++;
        RosterClip *clip = &app->roster[app->timeline_edit_roster_clip_index];
        TimelineInstance *instance = &lane->instances[index];
        instance->roster_clip_index = app->timeline_edit_roster_clip_index;
        instance->start_tick = app->timeline_edit_ghost_start_tick;
        instance->duration_ticks = app->timeline_edit_duration_ticks;
        instance->midi_note = clip->midi_note;
        instance->midi_channel = clip->midi_channel;
        instance->midi_velocity = clip->midi_velocity;
        app->timeline.timeline_cursor_tick = app->timeline_edit_ghost_start_tick;
        app->selected_timeline_lane = app->timeline_edit_ghost_lane;
        app->selected_timeline_instance = (TimelineInstanceRef){ app->timeline_edit_ghost_lane, index };
        recompute_timeline_length_no_lock(app);
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
        app->selected_timeline_lane = app->timeline_edit_original_lane;
        app_set_status(app, "Move cancelled");
    } else {
        app_set_status(app, "Placement cancelled");
    }
    app->timeline_edit_mode = TIMELINE_EDIT_NONE;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_roster_clip_index = -1;
    app->timeline_edit_ghost_lane = 0;
    app->timeline_edit_ghost_valid = false;
}

void app_timeline_activate_focus(App *app) {
    app_timeline_clear_context_menu(app);
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
        case TIMELINE_FOCUS_LANE_INDEX:
            app_open_lane_inspector(app, app->selected_timeline_lane);
            break;
        case TIMELINE_FOCUS_PLAY_RANGE:
            app->timeline_play_range_adjusting = true;
            app_set_status(app, "Adjusting play range");
            break;
        case TIMELINE_FOCUS_TRACK_AREA:
        {
            TimelineInstanceRef under_cursor = app_timeline_instance_at_cursor(app);
            if (timeline_instance_ref_valid(&app->timeline, under_cursor)) {
                if (timeline_instance_ref_equal(under_cursor, app->selected_timeline_instance)) {
                    timeline_enter_move_instance(app);
                } else {
                    app->selected_timeline_instance = under_cursor;
                    app->selected_timeline_lane = under_cursor.lane_index;
                    app_timeline_clear_context_menu(app);
                    app_set_status(app, "Selected timeline instance");
                }
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
    if (app->timeline_context_menu_open) {
        app_timeline_close_context_menu(app);
        return;
    }
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

void app_timeline_open_context_menu(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE || app->timeline_play_range_adjusting) {
        app_set_status(app, "Finish current edit first");
        return;
    }
    app_timeline_clear_context_menu(app);
    if (app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) {
        if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
            app_set_status(app, "Select a roster clip first");
            return;
        }
        app->timeline_context_menu_open = true;
        app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_ROSTER;
        app->timeline_context_menu_roster_index = app->selected_roster_clip;
        app->timeline_context_menu_tick = app->timeline.timeline_cursor_tick;
        app_set_status(app, "Roster menu");
        return;
    }

    if (app->timeline_focus_zone == TIMELINE_FOCUS_RULER) {
        app->timeline_context_menu_open = true;
        app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_TIMELINE;
        app->timeline_context_menu_tick = app->timeline.timeline_cursor_tick;
        app_set_status(app, "Timeline menu");
        return;
    }

    if (app->timeline_focus_zone != TIMELINE_FOCUS_TRACK_AREA) {
        app_set_status(app, "No context menu here");
        return;
    }

    TimelineInstanceRef target = app_timeline_instance_at_cursor(app);
    if (!timeline_instance_ref_valid(&app->timeline, target) &&
        timeline_instance_ref_valid(&app->timeline, app->selected_timeline_instance)) {
        target = app->selected_timeline_instance;
    }
    app->timeline_context_menu_open = true;
    app->timeline_context_menu_scope = timeline_instance_ref_valid(&app->timeline, target) ?
        TIMELINE_CONTEXT_SCOPE_INSTANCE : TIMELINE_CONTEXT_SCOPE_TIMELINE;
    app->timeline_context_menu_instance = target;
    app->timeline_context_menu_tick = app->timeline.timeline_cursor_tick;
    app_set_status(app, app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_INSTANCE ?
                   "Instance menu" : "Timeline menu");
}

void app_timeline_close_context_menu(App *app) {
    TimelineContextMenuScope scope = app->timeline_context_menu_scope;
    if (scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE) {
        app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_ROSTER;
        app->timeline_context_menu_selected = 1;
        app_set_status(app, "Delete cancelled");
        return;
    }
    app_timeline_clear_context_menu(app);
    app_set_status(app, "Menu closed");
}

void app_timeline_context_menu_move(App *app, int delta) {
    if (!app->timeline_context_menu_open || delta == 0) return;
    TimelineContextMenuItem items[TIMELINE_CONTEXT_MAX_ITEMS];
    int count = timeline_context_menu_items(app, items, TIMELINE_CONTEXT_MAX_ITEMS);
    if (count <= 0) return;
    int selected = app->timeline_context_menu_selected + delta;
    while (selected < 0) selected += count;
    selected %= count;
    app->timeline_context_menu_selected = selected;
}

void app_timeline_context_menu_apply(App *app) {
    if (!app->timeline_context_menu_open) return;
    TimelineContextMenuItem items[TIMELINE_CONTEXT_MAX_ITEMS];
    int count = timeline_context_menu_items(app, items, TIMELINE_CONTEXT_MAX_ITEMS);
    if (count <= 0) {
        app_timeline_clear_context_menu(app);
        return;
    }
    int selected = clamp_int(app->timeline_context_menu_selected, 0, count - 1);
    TimelineContextMenuItem item = items[selected];
    switch (item) {
        case TIMELINE_CONTEXT_ITEM_INSERT_BAR:
            app_timeline_insert_bar_at_cursor(app);
            break;
        case TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE:
            if (timeline_instance_ref_valid(&app->timeline, app->timeline_context_menu_instance)) {
                app->selected_timeline_instance = app->timeline_context_menu_instance;
                app->selected_timeline_lane = app->timeline_context_menu_instance.lane_index;
            }
            app_timeline_remove_selected_instance(app);
            break;
        case TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER:
            if (app->timeline_context_menu_roster_index >= 0 &&
                app->timeline_context_menu_roster_index < app->roster_clip_count) {
                app->selected_roster_clip = app->timeline_context_menu_roster_index;
            }
            app_export_selected_roster_clip(app);
            break;
        case TIMELINE_CONTEXT_ITEM_DELETE_ROSTER:
            app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE;
            app->timeline_context_menu_selected = 0;
            app_set_status(app, "Confirm roster delete");
            break;
        case TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_ROSTER:
            if (app->timeline_context_menu_roster_index >= 0 &&
                app->timeline_context_menu_roster_index < app->roster_clip_count) {
                app->selected_roster_clip = app->timeline_context_menu_roster_index;
            }
            app_delete_selected_roster_clip(app);
            break;
        case TIMELINE_CONTEXT_ITEM_CANCEL:
        default:
            app_timeline_close_context_menu(app);
            break;
    }
}

void app_timeline_insert_bar_at_cursor(App *app) {
    if (!app->timeline.initialized) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "timeline empty");
        return;
    }

    int64_t bar_ticks = (int64_t)timeline_bar_ticks(&app->timeline);
    int64_t cursor = app->timeline_context_menu_open ? app->timeline_context_menu_tick : app->timeline.timeline_cursor_tick;
    if (cursor < 0) cursor = 0;
    int64_t insertion_tick = (cursor / bar_ticks) * bar_ticks;
    int64_t old_playhead = audio_engine_get_timeline_playhead_tick(&app->audio);
    audio_engine_stop_timeline(&app->audio, false);
    audio_engine_stop_preview(&app->audio);

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    int64_t old_length = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
    if (insertion_tick > old_length) insertion_tick = old_length;

    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        for (int i = 0; i < lane->instance_count; ++i) {
            TimelineInstance *instance = &lane->instances[i];
            if (instance->start_tick >= insertion_tick) instance->start_tick += bar_ticks;
        }
    }

    int64_t new_length_floor = old_length + bar_ticks;
    if (app->timeline.timeline_cursor_tick >= insertion_tick) app->timeline.timeline_cursor_tick += bar_ticks;
    int64_t new_playhead = old_playhead >= insertion_tick ? old_playhead + bar_ticks : old_playhead;
    app->timeline.playhead_tick = new_playhead;
    if (app->timeline.play_range_custom) {
        if (app->timeline.play_range_start_tick >= insertion_tick) app->timeline.play_range_start_tick += bar_ticks;
        if (app->timeline.play_range_end_tick >= insertion_tick) app->timeline.play_range_end_tick += bar_ticks;
    }
    recompute_timeline_length_no_lock(app);
    if (app->timeline.length_ticks < new_length_floor) app->timeline.length_ticks = new_length_floor;
    sync_timeline_play_range_no_lock(app);
    clamp_timeline_view(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    audio_engine_set_timeline_playhead(&app->audio, new_playhead);
    sync_transport_from_app(app);
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Inserted bar before tick %lld", (long long)insertion_tick);
}

void app_timeline_remove_selected_instance(App *app) {
    if (!timeline_instance_ref_valid(&app->timeline, app->selected_timeline_instance)) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No instance selected");
        return;
    }
    audio_engine_stop_timeline(&app->audio, false);
    audio_engine_stop_preview(&app->audio);
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    int lane_index = app->selected_timeline_instance.lane_index;
    int removed = app->selected_timeline_instance.instance_index;
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    for (int i = removed; i + 1 < lane->instance_count; ++i) {
        lane->instances[i] = lane->instances[i + 1];
    }
    lane->instance_count--;
    if (!timeline_has_instances(&app->timeline)) {
        app->selected_timeline_instance = timeline_instance_ref_invalid();
        app->timeline.timeline_cursor_tick = 0;
        app->timeline.playing = false;
        app->transport.playing = false;
        app->transport.metronome_env = 0.0f;
    } else if (lane->instance_count > 0) {
        if (removed >= lane->instance_count) removed = lane->instance_count - 1;
        app->selected_timeline_instance = (TimelineInstanceRef){ lane_index, removed };
    } else {
        app->selected_timeline_instance = timeline_instance_ref_invalid();
    }
    recompute_timeline_length_no_lock(app);
    int64_t playhead = app->timeline.playhead_tick;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    audio_engine_set_timeline_playhead(&app->audio, playhead);
    sync_transport_from_app(app);
    app_timeline_clear_context_menu(app);
    app_set_status(app, "Removed timeline instance");
}

static bool write_fourcc(SDL_IOStream *io, const char text[4]) {
    return SDL_WriteIO(io, text, 4) == 4;
}

static bool write_roster_clip_wav(const RosterClip *clip, const char *path) {
    if (!clip || !clip->samples || clip->frame_count == 0 ||
        clip->channels <= 0 || clip->sample_rate <= 0) {
        return false;
    }

    Uint64 sample_count = (Uint64)clip->frame_count * (Uint64)clip->channels;
    Uint64 data_size = sample_count * sizeof(Sint16);
    if (data_size > 0xffffffffu) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;

    bool ok = true;
    Uint32 byte_rate = (Uint32)clip->sample_rate * (Uint32)clip->channels * (Uint32)sizeof(Sint16);
    Uint16 block_align = (Uint16)(clip->channels * (int)sizeof(Sint16));

    ok = ok && write_fourcc(io, "RIFF");
    ok = ok && SDL_WriteU32LE(io, 36u + (Uint32)data_size);
    ok = ok && write_fourcc(io, "WAVE");
    ok = ok && write_fourcc(io, "fmt ");
    ok = ok && SDL_WriteU32LE(io, 16);
    ok = ok && SDL_WriteU16LE(io, 1);
    ok = ok && SDL_WriteU16LE(io, (Uint16)clip->channels);
    ok = ok && SDL_WriteU32LE(io, (Uint32)clip->sample_rate);
    ok = ok && SDL_WriteU32LE(io, byte_rate);
    ok = ok && SDL_WriteU16LE(io, block_align);
    ok = ok && SDL_WriteU16LE(io, 16);
    ok = ok && write_fourcc(io, "data");
    ok = ok && SDL_WriteU32LE(io, (Uint32)data_size);

    for (Uint64 i = 0; ok && i < sample_count; ++i) {
        float sample = clip->samples[i];
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        Sint16 pcm = (Sint16)lrintf(sample * 32767.0f);
        ok = ok && SDL_WriteS16LE(io, pcm);
    }

    ok = SDL_CloseIO(io) && ok;
    return ok;
}

static void roster_export_filename(const RosterClip *clip, char *out, size_t out_size) {
    char clean[APP_ROSTER_CLIP_NAME_MAX];
    const char *name = clip && clip->name[0] ? clip->name : "roster_clip";
    size_t write = 0;
    for (size_t read = 0; name[read] && write + 1 < sizeof(clean); ++read) {
        char c = name[read];
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '#';
        clean[write++] = keep ? c : '_';
    }
    if (write == 0) clean[write++] = 'c';
    clean[write] = '\0';
    SDL_snprintf(out, out_size, "%s.wav", clean);
}

static bool unique_roster_export_path(const App *app, const RosterClip *clip, char *out, size_t out_size) {
    char filename[APP_ROSTER_CLIP_NAME_MAX + 8];
    char stem[APP_ROSTER_CLIP_NAME_MAX];
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    roster_export_filename(clip, filename, sizeof(filename));
    SDL_strlcpy(stem, filename, sizeof(stem));
    char *dot = SDL_strrchr(stem, '.');
    if (dot) *dot = '\0';

    for (int suffix = 0; suffix < 1000; ++suffix) {
        if (suffix == 0) {
            path_join(out, out_size, app->roster_export_dir, filename);
        } else {
            SDL_snprintf(filename, sizeof(filename), "%s_%03d.wav", stem, suffix + 1);
            path_join(out, out_size, app->roster_export_dir, filename);
        }
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(out, &info)) return true;
    }
    out[0] = '\0';
    return false;
}

static void app_use_cwd_roster_export_dir(App *app) {
    char dir[CLIP_MAX_PATH];
    path_join(dir, sizeof(dir), "exports", "roster");
    SDL_strlcpy(app->roster_export_dir, dir, sizeof(app->roster_export_dir));
    app->roster_export_dir_is_base_path = false;
    SDL_CreateDirectory(app->roster_export_dir);
}

void app_export_selected_roster_clip(App *app) {
    if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No roster clip selected");
        return;
    }
    if (!SDL_CreateDirectory(app->roster_export_dir)) {
        if (app->roster_export_dir_is_base_path) app_use_cwd_roster_export_dir(app);
        if (!SDL_CreateDirectory(app->roster_export_dir)) {
            app_set_status(app, "Could not create roster export folder");
            return;
        }
    }

    const RosterClip *clip = &app->roster[app->selected_roster_clip];
    char path[CLIP_MAX_PATH];
    if (!unique_roster_export_path(app, clip, path, sizeof(path))) {
        app_set_status(app, "Could not create export path");
        return;
    }

    bool exported = write_roster_clip_wav(clip, path);
    if (!exported && app->roster_export_dir_is_base_path) {
        app_use_cwd_roster_export_dir(app);
        if (unique_roster_export_path(app, clip, path, sizeof(path))) {
            exported = write_roster_clip_wav(clip, path);
        }
    }
    if (!exported) {
        app_set_status(app, "Could not export roster WAV");
        return;
    }

    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Exported %s", path);
}

void app_delete_selected_roster_clip(App *app) {
    int delete_index = app->selected_roster_clip;
    if (delete_index < 0 || delete_index >= app->roster_clip_count) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No roster clip selected");
        return;
    }

    char deleted_name[APP_ROSTER_CLIP_NAME_MAX];
    SDL_strlcpy(deleted_name, app->roster[delete_index].name, sizeof(deleted_name));
    audio_engine_stop_timeline(&app->audio, false);
    audio_engine_stop_preview(&app->audio);

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        int write_index = 0;
        for (int read_index = 0; read_index < lane->instance_count; ++read_index) {
            TimelineInstance instance = lane->instances[read_index];
            if (instance.roster_clip_index == delete_index) continue;
            if (instance.roster_clip_index > delete_index) instance.roster_clip_index--;
            lane->instances[write_index++] = instance;
        }
        lane->instance_count = write_index;
    }

    roster_clip_destroy(&app->roster[delete_index]);
    for (int i = delete_index; i + 1 < app->roster_clip_count; ++i) {
        app->roster[i] = app->roster[i + 1];
    }
    app->roster_clip_count--;
    if (app->roster_clip_count >= 0) {
        SDL_memset(&app->roster[app->roster_clip_count], 0, sizeof(app->roster[app->roster_clip_count]));
    }

    if (app->roster_clip_count <= 0) {
        app->selected_roster_clip = -1;
        app->selected_roster_clip_armed = false;
    } else {
        app->selected_roster_clip = clamp_int(delete_index, 0, app->roster_clip_count - 1);
        app->selected_roster_clip_armed = false;
    }
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    if (!timeline_has_instances(&app->timeline)) {
        app->timeline.timeline_cursor_tick = 0;
        app->timeline.playhead_tick = 0;
        app->timeline.playing = false;
        app->transport.playing = false;
        app->transport.metronome_env = 0.0f;
    }
    recompute_timeline_length_no_lock(app);
    int64_t playhead = app->timeline.playhead_tick;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    audio_engine_set_timeline_playhead(&app->audio, playhead);
    sync_transport_from_app(app);
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Deleted %s", deleted_name);
}

void app_timeline_adjust_selected_instance_velocity(App *app, int delta) {
    if (!timeline_instance_ref_valid(&app->timeline, app->selected_timeline_instance)) {
        app_set_status(app, "No instance selected");
        return;
    }
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    TimelineInstance *instance = timeline_instance_from_ref(&app->timeline, app->selected_timeline_instance);
    int velocity = instance ? instance->midi_velocity : 100;
    velocity = clamp_int(velocity + delta, 1, 127);
    if (instance) instance->midi_velocity = velocity;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Velocity %d", velocity);
}

void app_preview_selected_roster_clip(App *app) {
    if (!app->audio.stream) {
        app_set_audio_unavailable_status(app);
        return;
    }
    if (app->roster_clip_count <= 0) {
        app->selected_roster_clip = -1;
        app->selected_roster_clip_armed = false;
        app_set_status(app, "roster empty");
        return;
    }
    if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
        app->selected_roster_clip = 0;
        app->selected_roster_clip_armed = false;
    }

    RosterClip *clip = &app->roster[app->selected_roster_clip];
    if (audio_engine_preview_roster_clip(&app->audio, app->selected_roster_clip)) {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Preview %s", clip->name);
    } else {
        app_set_status(app, "Preview unavailable");
    }
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

void app_open_lane_inspector(App *app, int lane_index) {
    if (!timeline_lane_index_valid(lane_index)) lane_index = clamp_int(lane_index, 0, TIMELINE_MAX_LANES - 1);
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE || app->timeline_play_range_adjusting) {
        app_set_status(app, "Finish current edit first");
        return;
    }
    app->selected_timeline_lane = lane_index;
    app->inspected_timeline_lane = lane_index;
    app_timeline_clear_context_menu(app);
    app->view_mode = APP_VIEW_LANE_INSPECTOR;
    reset_lane_analyzer_visual(app, lane_index);
    audio_engine_set_active_lane_analyzer(&app->audio, lane_index);
    sync_transport_from_app(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Lane %d inspector", lane_index + 1);
}

void app_close_lane_inspector(App *app) {
    if (app->view_mode != APP_VIEW_LANE_INSPECTOR) return;
    audio_engine_set_active_lane_analyzer(&app->audio, -1);
    app->view_mode = APP_VIEW_TIMELINE;
    sync_transport_from_app(app);
    app_set_status(app, "Timeline");
}

void app_toggle_inspected_lane_mute(App *app) {
    int lane_index = clamp_int(app->inspected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    lane->muted = !lane->muted;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Lane %d %s", lane_index + 1, lane->muted ? "muted" : "unmuted");
}

void app_cycle_inspected_lane_palette(App *app, int direction) {
    if (direction == 0) return;
    int lane_index = clamp_int(app->inspected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    int count = lane_palette_count();
    if (count <= 0) return;
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    int palette_index = lane->palette_index + direction;
    while (palette_index < 0) palette_index += count;
    palette_index %= count;
    lane->palette_index = palette_index;
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

void app_apply_tempo_lock_and_capture(App *app) {
    app_apply_tempo_lock(app);

    int previous_count = app->roster_clip_count;
    app_capture_current_loop_to_roster(app);
    if (app->roster_clip_count > previous_count) {
        SDL_snprintf(app->status_text, sizeof(app->status_text),
                     "Tempo locked and captured %s to roster",
                     app->roster[app->roster_clip_count - 1].name);
        return;
    }

    char reason[sizeof(app->status_text)];
    SDL_strlcpy(reason, app->status_text, sizeof(reason));
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Tempo locked, capture failed: %s",
                 reason[0] ? reason : "unknown reason");
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

void app_snap_tempo_lock_downbeat_to_loop_start(App *app) {
    app->tempo_lock_draft.downbeat_frame = app->clip.loop_start_frame;
    clamp_tempo_params(app, &app->tempo_lock_draft);
    sync_transport_from_app(app);
    app_set_status(app, "Downbeat snapped to loop start");
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
    app_resolve_sample_dir(app);

    int count = 0;
    char **names = SDL_GlobDirectory(app->sample_dir, "*.wav", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) return;
    qsort(names, (size_t)count, sizeof(char *), compare_strings);

    for (int i = 0; i < count && app->sample_count < APP_MAX_SAMPLES; ++i) {
        SampleEntry *entry = &app->samples[app->sample_count++];
        path_join(entry->path, sizeof(entry->path), app->sample_dir, names[i]);
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
        SDL_snprintf(app->status_text, sizeof(app->status_text), "No WAV files in %s", app->sample_dir);
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
    SDL_FRect panel = { 36.0f, 88.0f, 700.0f, 466.0f };
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
    SDL_RenderDebugText(app->renderer, x, y, "Timeline: C/Start menu   Up/Down choose   South apply   East backs out"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Timeline stick: Left/Right pan   Up/Down zoom   L2 turbo"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Ruler: Left/Right cursor by beat   Shift+Left/Right pans"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Lane Index: Up/Down lane   South opens Lane Inspector"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Lane Inspector: L/R palette   South mute   East timeline   R2 transport"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Play Range: Enter/South adjust   1/2 or West/North choose handle"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Track: L/R cursor   U/D lane cursor   South select/move   [/] velocity"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Gamepad track: L2+stick X glide   L2+D-pad L/R bars   L2+D-pad U/D velocity"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Move/Place: D-pad L/R ticks   D-pad U/D lane   same-lane overlap blocked"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Roster: South arms/places   C/Start menu   Right stick previews"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Timeline R2: South play   East stop all   West jump start   North loop"); y += 22.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Gamepad waveform: South/Start play   Back metronome"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Waveform: D-pad L/R trim selected edge   D-pad U/D zoom"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "R2+South set loop to visible   L2+R2+South capture loop"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Waveform right stick picker   R2+Start timeline/waveform"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Waveform R2+North tempo lock"); y += 22.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Tempo Lock: South apply   L2+R2+South apply+roster   East/T cancel"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Tempo Lock: R2+North snap downbeat   R2+East clear"); y += 16.0f;
    SDL_RenderDebugText(app->renderer, x, y, "Tempo Lock: d-pad BPM/bars   L2+d-pad coarse BPM   sticks/bumpers downbeat");
}

static float timeline_x_for_tick(double tick, double view_start, double view_span, float x, float w) {
    return x + (float)((tick - view_start) / view_span) * w;
}

typedef enum {
    TIMELINE_BLOCK_NORMAL,
    TIMELINE_BLOCK_SELECTED,
    TIMELINE_BLOCK_ORIGIN,
    TIMELINE_BLOCK_GHOST_VALID,
    TIMELINE_BLOCK_GHOST_INVALID
} TimelineBlockStyle;

static void render_timeline_block(App *app,
                                  SDL_FRect block,
                                  SDL_Color clip_color,
                                  SDL_Color lane_color,
                                  const char *label,
                                  TimelineBlockStyle style,
                                  bool muted) {
    SDL_Renderer *renderer = app->renderer;
    SDL_FRect draw = block;
    if (style == TIMELINE_BLOCK_GHOST_VALID || style == TIMELINE_BLOCK_GHOST_INVALID) {
        SDL_FRect shadow = { block.x + 5.0f, block.y + 7.0f, block.w, block.h };
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 118);
        SDL_RenderFillRect(renderer, &shadow);
        draw.y -= 5.0f;
    }

    SDL_Color fill = muted ? color_muted(clip_color) : clip_color;
    SDL_Color border = muted ? color_muted(lane_color) : lane_color;
    SDL_Color cast = border;
    switch (style) {
        case TIMELINE_BLOCK_SELECTED:
            fill.a = muted ? 126 : 232;
            border = (SDL_Color){ 255, 255, 255, 255 };
            break;
        case TIMELINE_BLOCK_ORIGIN:
            fill.a = muted ? 34 : 54;
            border.a = muted ? 80 : 112;
            break;
        case TIMELINE_BLOCK_GHOST_VALID:
            fill.a = muted ? 88 : 138;
            border = (SDL_Color){ 245, 245, 255, 255 };
            break;
        case TIMELINE_BLOCK_GHOST_INVALID:
            fill = (SDL_Color){ 255, 72, 88, 132 };
            border = (SDL_Color){ 255, 80, 96, 255 };
            break;
        case TIMELINE_BLOCK_NORMAL:
        default:
            fill.a = muted ? 92 : 218;
            border.a = muted ? 118 : 230;
            break;
    }

    set_draw_color(renderer, fill);
    SDL_RenderFillRect(renderer, &draw);
    if (style != TIMELINE_BLOCK_GHOST_INVALID) {
        cast.a = muted ? 22 : 42;
        set_draw_color(renderer, cast);
        SDL_RenderFillRect(renderer, &draw);
    }
    set_draw_color(renderer, border);
    SDL_RenderRect(renderer, &draw);
    if (style == TIMELINE_BLOCK_SELECTED) {
        SDL_FRect inner = { draw.x + 2.0f, draw.y + 2.0f, draw.w - 4.0f, draw.h - 4.0f };
        if (inner.w > 0.0f && inner.h > 0.0f) SDL_RenderRect(renderer, &inner);
    }

    if (label && label[0]) {
        if (muted) SDL_SetRenderDrawColor(renderer, 168, 172, 180, 255);
        else SDL_SetRenderDrawColor(renderer, style == TIMELINE_BLOCK_ORIGIN ? 180 : 8,
                                    style == TIMELINE_BLOCK_ORIGIN ? 190 : 8,
                                    style == TIMELINE_BLOCK_ORIGIN ? 205 : 12,
                                    255);
        SDL_RenderDebugText(renderer, draw.x + 8.0f, draw.y + 10.0f, label);
    }
}

static void render_focus_outline(App *app, SDL_FRect rect, TimelineFocusZone zone) {
    if (app->timeline_focus_zone != zone) return;
    SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
    SDL_RenderRect(app->renderer, &rect);
    SDL_FRect inner = { rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f };
    if (inner.w > 0.0f && inner.h > 0.0f) SDL_RenderRect(app->renderer, &inner);
}

static void render_master_meter(App *app, SDL_FRect rect) {
    MasterMeterState meter;
    SDL_memset(&meter, 0, sizeof(meter));
    audio_engine_get_master_meter(&app->audio, &meter);

    SDL_SetRenderDrawColor(app->renderer, 13, 14, 21, 220);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_SetRenderDrawColor(app->renderer, 90, 98, 118, 255);
    SDL_RenderRect(app->renderer, &rect);
    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    SDL_RenderDebugText(app->renderer, rect.x + 8.0f, rect.y + 6.0f, "MASTER OUT");

    float peak_l = fminf(fabsf(meter.peak_l), 1.25f) / 1.25f;
    float peak_r = fminf(fabsf(meter.peak_r), 1.25f) / 1.25f;
    float bar_x = rect.x + 82.0f;
    float bar_w = rect.w - 112.0f;
    if (bar_w < 40.0f) bar_w = 40.0f;
    SDL_FRect bg_l = { bar_x, rect.y + 8.0f, bar_w, 8.0f };
    SDL_FRect bg_r = { bar_x, rect.y + 22.0f, bar_w, 8.0f };
    SDL_SetRenderDrawColor(app->renderer, 30, 32, 42, 255);
    SDL_RenderFillRect(app->renderer, &bg_l);
    SDL_RenderFillRect(app->renderer, &bg_r);
    SDL_FRect fill_l = bg_l;
    SDL_FRect fill_r = bg_r;
    fill_l.w *= peak_l;
    fill_r.w *= peak_r;
    SDL_SetRenderDrawColor(app->renderer, meter.clip_flash_seconds > 0.0f ? 255 : 115,
                           meter.clip_flash_seconds > 0.0f ? 80 : 225,
                           meter.clip_flash_seconds > 0.0f ? 82 : 145, 255);
    SDL_RenderFillRect(app->renderer, &fill_l);
    SDL_RenderFillRect(app->renderer, &fill_r);
    SDL_SetRenderDrawColor(app->renderer, 180, 188, 205, 255);
    SDL_RenderDebugText(app->renderer, rect.x + 8.0f, rect.y + 20.0f, "L/R");
    if (meter.clip_flash_seconds > 0.0f) {
        SDL_SetRenderDrawColor(app->renderer, 255, 80, 82, 255);
        SDL_RenderDebugTextFormat(app->renderer, rect.x + rect.w - 48.0f, rect.y + 20.0f, "CLIP %u", meter.clip_count);
    }
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
    float track_h = (float)h - timeline_y - 138.0f;
    if (track_h < 220.0f) track_h = 220.0f;
    if (track_h > 390.0f) track_h = 390.0f;
    float lane_h = track_h / (float)TIMELINE_MAX_LANES;
    SDL_FRect transport_rect = { 24.0f, 132.0f, timeline_w + 32.0f, 72.0f };
    SDL_FRect ruler_rect = { timeline_x, timeline_y - 58.0f, timeline_w, 42.0f };
    SDL_FRect play_range_rect = { timeline_x, timeline_y - 12.0f, timeline_w, 18.0f };
    SDL_FRect track_rect = { timeline_x, timeline_y + 8.0f, timeline_w, track_h };
    SDL_FRect lane_index_rect = { timeline_x - 34.0f, track_rect.y, 28.0f, track_h };

    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    SDL_RenderDebugText(app->renderer, 24, 132, "MASTER TIMELINE");
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE) {
        SDL_RenderDebugTextFormat(app->renderer, 24, 196, "focus: %s / %s %s",
                                  timeline_focus_label(app->timeline_focus_zone),
                                  timeline_edit_verb_label(app->timeline_edit_mode),
                                  timeline_edit_clip_name(app));
    } else {
        SDL_RenderDebugTextFormat(app->renderer, 24, 196, "focus: %s%s",
                                  timeline_focus_label(app->timeline_focus_zone),
                                  app->timeline_play_range_adjusting ? " / adjusting" : "");
    }
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
    SDL_FRect meter_rect = { timeline_x + timeline_w - 260.0f, 134.0f, 248.0f, 42.0f };
    if (meter_rect.x > 320.0f) render_master_meter(app, meter_rect);

    clamp_timeline_view(app);
    double view_start = app->timeline.view_center_tick - app->timeline.view_span_ticks * 0.5;
    double view_end = app->timeline.view_center_tick + app->timeline.view_span_ticks * 0.5;
    if (view_end <= view_start) view_end = view_start + 1.0;
    double view_span = view_end - view_start;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 14, 14, 24, 255);
    SDL_FRect rail = { timeline_x, timeline_y - 58.0f, timeline_w, track_h + 64.0f };
    SDL_RenderFillRect(app->renderer, &rail);
    SDL_SetRenderDrawColor(app->renderer, 85, 90, 112, 255);
    SDL_RenderRect(app->renderer, &rail);

    SDL_SetRenderDrawColor(app->renderer, 24, 24, 36, 255);
    SDL_RenderFillRect(app->renderer, &track_rect);
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        const LanePalette *palette = lane_palette_for_index(lane->palette_index);
        SDL_Color deep = palette ? palette->deep : (SDL_Color){ 48, 52, 70, 255 };
        SDL_Color pastel = palette ? palette->pastel : (SDL_Color){ 180, 188, 205, 255 };
        if (lane->muted) {
            deep = color_muted(deep);
            pastel = color_muted(pastel);
        }
        float y = track_rect.y + (float)lane_index * lane_h;
        SDL_FRect lane_rect = { track_rect.x, y, track_rect.w, lane_h };
        SDL_Color base = color_mix(deep, (SDL_Color){ 18, 18, 28, 255 }, lane_index == app->selected_timeline_lane ? 0.38f : 0.68f);
        base.a = lane->muted ? 105 : (lane_index == app->selected_timeline_lane ? 174 : 128);
        set_draw_color(app->renderer, base);
        SDL_RenderFillRect(app->renderer, &lane_rect);
        if (lane_index == app->selected_timeline_lane) {
            SDL_Color selected_frame = pastel;
            selected_frame.a = lane->muted ? 118 : 190;
            set_draw_color(app->renderer, selected_frame);
            SDL_RenderRect(app->renderer, &lane_rect);
        }
        SDL_Color divider = color_mix(pastel, (SDL_Color){ 60, 64, 82, 255 }, 0.58f);
        divider.a = lane->muted ? 78 : 132;
        set_draw_color(app->renderer, divider);
        SDL_RenderLine(app->renderer, track_rect.x, y, track_rect.x + track_rect.w, y);
        SDL_FRect number_rect = { lane_index_rect.x + 3.0f, y + 4.0f, lane_index_rect.w - 6.0f, lane_h - 8.0f };
        if (lane_index == app->selected_timeline_lane) {
            SDL_Color number_bg = deep;
            number_bg.a = app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX ? 212 : 128;
            set_draw_color(app->renderer, number_bg);
            SDL_RenderFillRect(app->renderer, &number_rect);
            SDL_Color number_border = pastel;
            number_border.a = 255;
            set_draw_color(app->renderer, number_border);
            SDL_RenderRect(app->renderer, &number_rect);
        }
        SDL_Color number_color = lane->muted ? color_muted(pastel) : pastel;
        number_color.a = lane->muted ? 160 : 255;
        set_draw_color(app->renderer, number_color);
        SDL_RenderDebugTextFormat(app->renderer, lane_index_rect.x + 10.0f, y + 5.0f, "%d", lane_index + 1);
    }
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
        SDL_RenderLine(app->renderer, x, timeline_y - 58.0f, x, timeline_y + track_h + 8.0f);
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
            SDL_FRect range_rect = { range_x0, timeline_y - 12.0f, range_x1 - range_x0, track_h + 20.0f };
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
            SDL_RenderLine(app->renderer, x, timeline_y - 22.0f, x, timeline_y + track_h + 12.0f);
            SDL_RenderDebugText(app->renderer, x + 4.0f, timeline_y + track_h + 10.0f, "end");
        }
    }

    float cursor_x = timeline_x_for_tick((double)app->timeline.timeline_cursor_tick, view_start, view_span, timeline_x, timeline_w);
    if (cursor_x >= timeline_x && cursor_x <= timeline_x + timeline_w) {
        SDL_SetRenderDrawColor(app->renderer, 250, 250, 255, 255);
        SDL_RenderLine(app->renderer, cursor_x, timeline_y - 62.0f, cursor_x, timeline_y + track_h + 20.0f);
        SDL_RenderDebugText(app->renderer, cursor_x + 5.0f, timeline_y + track_h + 22.0f, "cursor");
        if (app->timeline_focus_zone == TIMELINE_FOCUS_TRACK_AREA) {
            int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
            const LanePalette *palette = lane_palette_for_index(app->timeline.lanes[lane_index].palette_index);
            SDL_Color lane_accent = palette ? palette->pastel : (SDL_Color){ 255, 220, 120, 255 };
            if (app->timeline.lanes[lane_index].muted) lane_accent = color_muted(lane_accent);
            float lane_y = track_rect.y + (float)lane_index * lane_h;
            SDL_FRect lane_cursor = { track_rect.x, lane_y, track_rect.w, lane_h };
            set_draw_color(app->renderer, lane_accent);
            SDL_RenderRect(app->renderer, &lane_cursor);
            SDL_FRect cursor_tab = { cursor_x - 5.0f, lane_y + 2.0f, 10.0f, lane_h - 4.0f };
            lane_accent.a = 92;
            set_draw_color(app->renderer, lane_accent);
            SDL_RenderFillRect(app->renderer, &cursor_tab);
            lane_accent.a = 255;
            set_draw_color(app->renderer, lane_accent);
            SDL_RenderRect(app->renderer, &cursor_tab);
        }
    }

    int64_t playhead_tick = audio_engine_get_timeline_playhead_tick(&app->audio);
    float playhead_x = timeline_x_for_tick((double)playhead_tick, view_start, view_span, timeline_x, timeline_w);
    if (playhead_x >= timeline_x && playhead_x <= timeline_x + timeline_w) {
        SDL_SetRenderDrawColor(app->renderer, 180, 255, 120, 255);
        SDL_RenderLine(app->renderer, playhead_x, timeline_y - 62.0f, playhead_x, timeline_y + track_h + 20.0f);
    }

    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        const LanePalette *palette = lane_palette_for_index(lane->palette_index);
        SDL_Color lane_color = palette ? palette->pastel : (SDL_Color){ 180, 188, 205, 255 };
        for (int i = 0; i < lane->instance_count; ++i) {
            TimelineInstanceRef ref = { lane_index, i };
            TimelineInstance *instance = &lane->instances[i];
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
            float lane_y = track_rect.y + (float)lane_index * lane_h;
            SDL_FRect block = { x + 2.0f, lane_y + 5.0f, block_w - 4.0f, lane_h - 10.0f };
            if (block.w < 8.0f) block.w = 8.0f;
            TimelineBlockStyle style = timeline_instance_ref_equal(ref, app->selected_timeline_instance) ? TIMELINE_BLOCK_SELECTED : TIMELINE_BLOCK_NORMAL;
            if (app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE &&
                timeline_instance_ref_equal(ref, app->timeline_edit_instance)) {
                style = TIMELINE_BLOCK_ORIGIN;
            }
            char label[APP_ROSTER_CLIP_NAME_MAX + 16];
            SDL_snprintf(label, sizeof(label), "%s v%d", clip->name, instance->midi_velocity);
            render_timeline_block(app, block, clip->color, lane_color, label, style, lane->muted);
        }
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
            int ghost_lane = clamp_int(app->timeline_edit_ghost_lane, 0, TIMELINE_MAX_LANES - 1);
            float lane_y = track_rect.y + (float)ghost_lane * lane_h;
            SDL_FRect ghost = { x + 2.0f, lane_y + 5.0f, block_w - 4.0f, lane_h - 10.0f };
            if (ghost.w < 8.0f) ghost.w = 8.0f;
            char ghost_label[APP_ROSTER_CLIP_NAME_MAX + 16];
            if (app->timeline_edit_ghost_valid) {
                SDL_snprintf(ghost_label, sizeof(ghost_label), "%s %s",
                             app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE ? "moving" : "placing",
                             clip->name);
            } else {
                SDL_strlcpy(ghost_label, "overlap", sizeof(ghost_label));
            }
            TimelineLane *ghost_timeline_lane = &app->timeline.lanes[ghost_lane];
            const LanePalette *ghost_palette = lane_palette_for_index(ghost_timeline_lane->palette_index);
            SDL_Color ghost_lane_color = ghost_palette ? ghost_palette->pastel : (SDL_Color){ 180, 188, 205, 255 };
            render_timeline_block(app,
                                  ghost,
                                  clip->color,
                                  ghost_lane_color,
                                  ghost_label,
                                  app->timeline_edit_ghost_valid ? TIMELINE_BLOCK_GHOST_VALID : TIMELINE_BLOCK_GHOST_INVALID,
                                  ghost_timeline_lane->muted);
        }
    }

    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    if (timeline_w >= 220.0f) {
        SDL_RenderDebugText(app->renderer, timeline_x, timeline_y + track_h + 24.0f, "00:00:00");
    }

    render_focus_outline(app, transport_rect, TIMELINE_FOCUS_TRANSPORT);
    render_focus_outline(app, ruler_rect, TIMELINE_FOCUS_RULER);
    render_focus_outline(app, lane_index_rect, TIMELINE_FOCUS_LANE_INDEX);
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
                if (app->selected_roster_clip_armed) SDL_SetRenderDrawColor(app->renderer, 86, 78, 38, 230);
                else SDL_SetRenderDrawColor(app->renderer, 64, 72, 96, 210);
                SDL_RenderFillRect(app->renderer, &row);
                if (app->selected_roster_clip_armed) {
                    SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
                    SDL_RenderRect(app->renderer, &row);
                }
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

    if (app->timeline_context_menu_open) {
        TimelineContextMenuItem items[TIMELINE_CONTEXT_MAX_ITEMS];
        int item_count = timeline_context_menu_items(app, items, TIMELINE_CONTEXT_MAX_ITEMS);
        const char *title = timeline_context_menu_title(app->timeline_context_menu_scope);
        const char *name = "timeline";
        if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_ROSTER ||
            app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE) {
            int roster_index = app->timeline_context_menu_roster_index;
            if (roster_index >= 0 && roster_index < app->roster_clip_count) name = app->roster[roster_index].name;
        } else if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_INSTANCE &&
                   timeline_instance_ref_valid(&app->timeline, app->timeline_context_menu_instance)) {
            const TimelineInstance *selected = timeline_const_instance_from_ref(&app->timeline,
                                                                                app->timeline_context_menu_instance);
            if (selected && selected->roster_clip_index >= 0 && selected->roster_clip_index < app->roster_clip_count) {
                name = app->roster[selected->roster_clip_index].name;
            } else {
                name = "instance";
            }
        }
        float menu_w = app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE ? 384.0f : 320.0f;
        float warning_h = app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE ? 34.0f : 0.0f;
        SDL_FRect menu = {
            (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_ROSTER ||
             app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE) ? roster_x + 6.0f : timeline_x + 18.0f,
            timeline_y + track_h + 42.0f,
            menu_w,
            58.0f + warning_h + (float)item_count * 22.0f
        };
        SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 238);
        SDL_RenderFillRect(app->renderer, &menu);
        SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
        SDL_RenderRect(app->renderer, &menu);
        SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);
        SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 10.0f, title);
        SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 28.0f, name);
        float item_y = menu.y + 50.0f;
        if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE) {
            SDL_SetRenderDrawColor(app->renderer, 255, 160, 150, 255);
            SDL_RenderDebugText(app->renderer, menu.x + 12.0f, item_y, "Also deletes its timeline instances.");
            item_y += 30.0f;
        }
        for (int i = 0; i < item_count; ++i) {
            SDL_FRect row = { menu.x + 8.0f, item_y - 4.0f, menu.w - 16.0f, 20.0f };
            bool selected = i == app->timeline_context_menu_selected;
            if (selected) {
                SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 58);
                SDL_RenderFillRect(app->renderer, &row);
                SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
                SDL_RenderRect(app->renderer, &row);
            }
            SDL_SetRenderDrawColor(app->renderer, selected ? 255 : 210, selected ? 238 : 218, selected ? 178 : 226, 255);
            SDL_RenderDebugText(app->renderer, menu.x + 18.0f, item_y, timeline_context_item_label(items[i]));
            item_y += 22.0f;
        }
    }
}

static void render_debug_text_scaled(SDL_Renderer *renderer, float x, float y, float scale, const char *text) {
    if (!text || scale <= 0.0f) return;
    float old_x = 1.0f, old_y = 1.0f;
    SDL_GetRenderScale(renderer, &old_x, &old_y);
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_RenderDebugText(renderer, x / scale, y / scale, text);
    SDL_SetRenderScale(renderer, old_x, old_y);
}

static void fft_1024(float real[LANE_ANALYZER_WINDOW_SIZE], float imag[LANE_ANALYZER_WINDOW_SIZE]) {
    int j = 0;
    for (int i = 1; i < LANE_ANALYZER_WINDOW_SIZE; ++i) {
        int bit = LANE_ANALYZER_WINDOW_SIZE >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float tr = real[i];
            real[i] = real[j];
            real[j] = tr;
            float ti = imag[i];
            imag[i] = imag[j];
            imag[j] = ti;
        }
    }

    for (int len = 2; len <= LANE_ANALYZER_WINDOW_SIZE; len <<= 1) {
        float angle = -6.28318530717958647692f / (float)len;
        float wlen_r = cosf(angle);
        float wlen_i = sinf(angle);
        for (int i = 0; i < LANE_ANALYZER_WINDOW_SIZE; i += len) {
            float wr = 1.0f;
            float wi = 0.0f;
            int half = len >> 1;
            for (int k = 0; k < half; ++k) {
                int even = i + k;
                int odd = even + half;
                float ur = real[even];
                float ui = imag[even];
                float vr = real[odd] * wr - imag[odd] * wi;
                float vi = real[odd] * wi + imag[odd] * wr;
                real[even] = ur + vr;
                imag[even] = ui + vi;
                real[odd] = ur - vr;
                imag[odd] = ui - vi;

                float next_wr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
            }
        }
    }
}

static void reset_lane_analyzer_visual(App *app, int lane_index) {
    app->lane_analyzer_visual_lane = lane_index;
    SDL_memset(app->lane_analyzer_bars, 0, sizeof(app->lane_analyzer_bars));
    SDL_memset(app->lane_analyzer_peaks, 0, sizeof(app->lane_analyzer_peaks));
}

static void update_lane_analyzer_visual(App *app, int lane_index) {
    if (app->lane_analyzer_visual_lane != lane_index) reset_lane_analyzer_visual(app, lane_index);

    float samples[LANE_ANALYZER_WINDOW_SIZE];
    int sample_rate = 0;
    bool active = false;
    audio_engine_get_lane_analyzer_snapshot(&app->audio,
                                            lane_index,
                                            samples,
                                            LANE_ANALYZER_WINDOW_SIZE,
                                            &sample_rate,
                                            &active);

    float target[LANE_ANALYZER_BUCKETS] = {0};
    if (active && sample_rate > 0) {
        float mean = 0.0f;
        for (int i = 0; i < LANE_ANALYZER_WINDOW_SIZE; ++i) mean += samples[i];
        mean /= (float)LANE_ANALYZER_WINDOW_SIZE;

        float real[LANE_ANALYZER_WINDOW_SIZE];
        float imag[LANE_ANALYZER_WINDOW_SIZE];
        for (int i = 0; i < LANE_ANALYZER_WINDOW_SIZE; ++i) {
            float window = 0.5f - 0.5f * cosf(6.28318530717958647692f * (float)i / (float)(LANE_ANALYZER_WINDOW_SIZE - 1));
            real[i] = (samples[i] - mean) * window;
            imag[i] = 0.0f;
        }
        fft_1024(real, imag);

        float nyquist = (float)sample_rate * 0.5f;
        float min_hz = 38.0f;
        float max_hz = nyquist < 16000.0f ? nyquist : 16000.0f;
        if (max_hz <= min_hz) max_hz = min_hz + 1.0f;
        float log_min = logf(min_hz);
        float log_span = logf(max_hz) - log_min;

        for (int bar = 0; bar < LANE_ANALYZER_BUCKETS; ++bar) {
            float f0 = (float)bar / (float)LANE_ANALYZER_BUCKETS;
            float f1 = (float)(bar + 1) / (float)LANE_ANALYZER_BUCKETS;
            float hz0 = expf(log_min + log_span * f0);
            float hz1 = expf(log_min + log_span * f1);
            int bin0 = (int)floorf(hz0 * (float)LANE_ANALYZER_WINDOW_SIZE / (float)sample_rate);
            int bin1 = (int)ceilf(hz1 * (float)LANE_ANALYZER_WINDOW_SIZE / (float)sample_rate);
            if (bin0 < 1) bin0 = 1;
            if (bin1 <= bin0) bin1 = bin0 + 1;
            if (bin1 > LANE_ANALYZER_WINDOW_SIZE / 2) bin1 = LANE_ANALYZER_WINDOW_SIZE / 2;

            float sum = 0.0f;
            float max_mag = 0.0f;
            int count = 0;
            for (int bin = bin0; bin < bin1; ++bin) {
                float mag = sqrtf(real[bin] * real[bin] + imag[bin] * imag[bin]);
                sum += mag;
                if (mag > max_mag) max_mag = mag;
                count++;
            }
            float avg = count > 0 ? sum / (float)count : 0.0f;
            float energy = avg * 0.62f + max_mag * 0.38f;
            float lift = 0.78f + 0.58f * sqrtf((float)bar / (float)(LANE_ANALYZER_BUCKETS - 1));
            float value = log10f(1.0f + energy * 0.55f * lift) / log10f(18.0f);
            if (value < 0.0f) value = 0.0f;
            if (value > 1.0f) value = 1.0f;
            target[bar] = value;
        }
    }

    for (int i = 0; i < LANE_ANALYZER_BUCKETS; ++i) {
        float current = app->lane_analyzer_bars[i];
        float amount = target[i] > current ? 0.48f : 0.12f;
        current += (target[i] - current) * amount;
        if (current < 0.002f) current = 0.0f;
        app->lane_analyzer_bars[i] = current;

        if (current > app->lane_analyzer_peaks[i]) {
            app->lane_analyzer_peaks[i] = current;
        } else {
            app->lane_analyzer_peaks[i] -= 0.010f;
            if (app->lane_analyzer_peaks[i] < current) app->lane_analyzer_peaks[i] = current;
            if (app->lane_analyzer_peaks[i] < 0.0f) app->lane_analyzer_peaks[i] = 0.0f;
        }
    }
}

static void render_lane_inspector_analyzer(App *app,
                                           SDL_FRect rect,
                                           const float bars[LANE_ANALYZER_BUCKETS],
                                           const float peaks[LANE_ANALYZER_BUCKETS],
                                           SDL_Color deep,
                                           SDL_Color pastel,
                                           bool muted) {
    SDL_Color bg = color_mix(deep, (SDL_Color){ 8, 9, 14, 255 }, muted ? 0.78f : 0.60f);
    bg.a = 245;
    set_draw_color(app->renderer, bg);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_Color border = muted ? color_muted(pastel) : pastel;
    border.a = muted ? 128 : 230;
    set_draw_color(app->renderer, border);
    SDL_RenderRect(app->renderer, &rect);

    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, muted ? 20 : 30);
    for (int i = 1; i < 4; ++i) {
        float y = rect.y + rect.h * (float)i / 4.0f;
        SDL_RenderLine(app->renderer, rect.x, y, rect.x + rect.w, y);
    }

    float pad = 14.0f;
    float gap = rect.w > 620.0f ? 2.0f : 1.0f;
    float usable_w = rect.w - pad * 2.0f;
    float usable_h = rect.h - pad * 2.0f;
    float bar_w = (usable_w - gap * (float)(LANE_ANALYZER_BUCKETS - 1)) / (float)LANE_ANALYZER_BUCKETS;
    if (bar_w < 2.0f) bar_w = 2.0f;
    SDL_Color fill = muted ? color_muted(pastel) : pastel;
    fill.a = muted ? 112 : 222;
    for (int i = 0; i < LANE_ANALYZER_BUCKETS; ++i) {
        float value = bars ? bars[i] : 0.0f;
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        float h = usable_h * value;
        SDL_FRect bar = {
            rect.x + pad + (float)i * (bar_w + gap),
            rect.y + rect.h - pad - h,
            bar_w,
            h
        };
        if (bar.h < 1.0f) bar.h = 1.0f;
        SDL_Color body = fill;
        body.a = (Uint8)(muted ? 92 + (int)(value * 48.0f) : 150 + (int)(value * 90.0f));
        set_draw_color(app->renderer, body);
        SDL_RenderFillRect(app->renderer, &bar);

        float peak_value = peaks ? peaks[i] : value;
        if (peak_value < value) peak_value = value;
        if (peak_value > 1.0f) peak_value = 1.0f;
        float cap_y = rect.y + rect.h - pad - usable_h * peak_value;
        SDL_FRect cap = { bar.x, cap_y, bar.w, 2.0f };
        SDL_Color cap_color = muted ? color_muted(pastel) : color_mix(pastel, (SDL_Color){ 255, 248, 232, 255 }, 0.34f);
        cap_color.a = muted ? 120 : 245;
        set_draw_color(app->renderer, cap_color);
        SDL_RenderFillRect(app->renderer, &cap);
    }
}

static void render_lane_inspector_peak(App *app,
                                       SDL_FRect rect,
                                       const LaneMonitorState *monitor,
                                       SDL_Color pastel,
                                       bool muted) {
    SDL_SetRenderDrawColor(app->renderer, 13, 14, 21, 238);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_SetRenderDrawColor(app->renderer, 82, 88, 108, 255);
    SDL_RenderRect(app->renderer, &rect);

    float peak_l = monitor ? fminf(fabsf(monitor->peak_l), 1.25f) / 1.25f : 0.0f;
    float peak_r = monitor ? fminf(fabsf(monitor->peak_r), 1.25f) / 1.25f : 0.0f;
    SDL_Color fill = muted ? color_muted(pastel) : pastel;
    fill.a = muted ? 120 : 235;
    float pad = 5.0f;
    float bar_w = (rect.w - pad * 3.0f) * 0.5f;
    if (bar_w < 3.0f) bar_w = 3.0f;
    float usable_h = rect.h - pad * 2.0f;
    SDL_FRect bars[2] = {
        { rect.x + pad, rect.y + rect.h - pad - usable_h * peak_l, bar_w, usable_h * peak_l },
        { rect.x + pad * 2.0f + bar_w, rect.y + rect.h - pad - usable_h * peak_r, bar_w, usable_h * peak_r }
    };
    set_draw_color(app->renderer, fill);
    for (int i = 0; i < 2; ++i) {
        if (bars[i].h < 1.0f) bars[i].h = 1.0f;
        SDL_RenderFillRect(app->renderer, &bars[i]);
    }
    SDL_SetRenderDrawColor(app->renderer, 255, 92, 94, 190);
    float clip_y = rect.y + pad + usable_h * 0.20f;
    SDL_RenderLine(app->renderer, rect.x + 3.0f, clip_y, rect.x + rect.w - 3.0f, clip_y);
}

static void app_render_lane_inspector(App *app) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_SetRenderDrawColor(app->renderer, 8, 7, 13, 255);
    SDL_RenderClear(app->renderer);

    int lane_index = clamp_int(app->inspected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    const LanePalette *palette = lane_palette_for_index(lane->palette_index);
    SDL_Color deep = palette ? palette->deep : (SDL_Color){ 48, 52, 70, 255 };
    SDL_Color pastel = palette ? palette->pastel : (SDL_Color){ 180, 188, 205, 255 };
    bool muted = lane->muted;
    SDL_Color visible_deep = muted ? color_muted(deep) : deep;
    SDL_Color visible_pastel = muted ? color_muted(pastel) : pastel;

    LaneMonitorState monitor;
    audio_engine_get_lane_monitor(&app->audio, lane_index, &monitor);
    update_lane_analyzer_visual(app, lane_index);

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect panel = { 42.0f, 92.0f, (float)w - 84.0f, (float)h - 140.0f };
    if (panel.w < 360.0f) panel.w = 360.0f;
    if (panel.h < 360.0f) panel.h = 360.0f;
    SDL_Color panel_bg = color_mix(visible_deep, (SDL_Color){ 10, 11, 17, 255 }, 0.70f);
    panel_bg.a = 238;
    set_draw_color(app->renderer, panel_bg);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_Color panel_border = visible_pastel;
    panel_border.a = 245;
    set_draw_color(app->renderer, panel_border);
    SDL_RenderRect(app->renderer, &panel);

    float inset = 28.0f;
    float meter_w = 28.0f;
    float clip_w = 42.0f;
    float right_stack_w = meter_w + clip_w + 22.0f;
    float left_w = panel.w * 0.27f;
    if (left_w < 236.0f) left_w = 236.0f;
    if (left_w > 332.0f) left_w = 332.0f;
    float max_left_w = panel.w - 420.0f;
    if (left_w > max_left_w) left_w = max_left_w;
    if (left_w < 196.0f) left_w = 196.0f;

    SDL_FRect settings = { panel.x + inset, panel.y + inset, left_w, panel.h - inset * 2.0f };
    SDL_Color settings_bg = color_mix(visible_deep, (SDL_Color){ 7, 8, 13, 255 }, 0.62f);
    settings_bg.a = 190;
    set_draw_color(app->renderer, settings_bg);
    SDL_RenderFillRect(app->renderer, &settings);
    SDL_Color settings_border = visible_pastel;
    settings_border.a = 180;
    set_draw_color(app->renderer, settings_border);
    SDL_RenderRect(app->renderer, &settings);

    char title[32];
    SDL_snprintf(title, sizeof(title), "LANE %d", lane_index + 1);
    set_draw_color(app->renderer, visible_pastel);
    render_debug_text_scaled(app->renderer, settings.x + 18.0f, settings.y + 18.0f, 2.7f, title);

    SDL_FRect swatch = { settings.x + 20.0f, settings.y + 78.0f, (settings.w - 52.0f) * 0.5f, 18.0f };
    set_draw_color(app->renderer, visible_deep);
    SDL_RenderFillRect(app->renderer, &swatch);
    SDL_FRect swatch_pastel = { swatch.x + swatch.w + 12.0f, swatch.y, swatch.w, swatch.h };
    set_draw_color(app->renderer, visible_pastel);
    SDL_RenderFillRect(app->renderer, &swatch_pastel);

    float palette_y = swatch.y + 36.0f;
    SDL_SetRenderDrawColor(app->renderer, 222, 230, 234, 220);
    SDL_RenderDebugTextFormat(app->renderer, settings.x + 20.0f, palette_y, "PALETTE %d/%d", (lane->palette_index % lane_palette_count()) + 1, lane_palette_count());
    float small_gap = 5.0f;
    float small_w = (settings.w - 40.0f - small_gap * (float)(lane_palette_count() - 1)) / (float)lane_palette_count();
    if (small_w < 12.0f) small_w = 12.0f;
    for (int i = 0; i < lane_palette_count(); ++i) {
        const LanePalette *p = lane_palette_for_index(i);
        SDL_FRect chip = { settings.x + 20.0f + (float)i * (small_w + small_gap), palette_y + 18.0f, small_w, 16.0f };
        SDL_Color chip_color = p ? p->pastel : visible_pastel;
        if (muted) chip_color = color_muted(chip_color);
        set_draw_color(app->renderer, chip_color);
        SDL_RenderFillRect(app->renderer, &chip);
        if (i == lane->palette_index % lane_palette_count()) {
            SDL_SetRenderDrawColor(app->renderer, 255, 248, 230, 255);
            SDL_RenderRect(app->renderer, &chip);
        }
    }

    SDL_FRect mute_button = { settings.x + 20.0f, palette_y + 60.0f, settings.w - 40.0f, 42.0f };
    SDL_Color mute_fill = muted ? (SDL_Color){ 122, 36, 44, 236 } : color_mix(visible_deep, (SDL_Color){ 18, 20, 28, 255 }, 0.36f);
    set_draw_color(app->renderer, mute_fill);
    SDL_RenderFillRect(app->renderer, &mute_button);
    set_draw_color(app->renderer, muted ? (SDL_Color){ 255, 128, 132, 255 } : visible_pastel);
    SDL_RenderRect(app->renderer, &mute_button);
    SDL_SetRenderDrawColor(app->renderer, 236, 242, 245, 255);
    SDL_RenderDebugTextFormat(app->renderer, mute_button.x + 16.0f, mute_button.y + 14.0f, "MUTE %s", muted ? "ON" : "OFF");

    float peak_abs = fmaxf(fabsf(monitor.peak_l), fabsf(monitor.peak_r));
    SDL_SetRenderDrawColor(app->renderer, 222, 230, 234, 220);
    SDL_RenderDebugTextFormat(app->renderer, settings.x + 20.0f, mute_button.y + 64.0f, "PEAK %.2f", peak_abs);
    SDL_SetRenderDrawColor(app->renderer,
                           monitor.clip_hold_seconds > 0.0f ? 255 : 178,
                           monitor.clip_hold_seconds > 0.0f ? 92 : 184,
                           monitor.clip_hold_seconds > 0.0f ? 106 : 194,
                           230);
    SDL_RenderDebugText(app->renderer, settings.x + 20.0f, mute_button.y + 84.0f,
                        monitor.clip_hold_seconds > 0.0f ? "CLIP HOLD" : "CLIP CLEAR");

    SDL_SetRenderDrawColor(app->renderer, 190, 198, 210, 205);
    SDL_RenderDebugText(app->renderer, settings.x + 20.0f, settings.y + settings.h - 44.0f, "Left/Right palette");
    SDL_RenderDebugText(app->renderer, settings.x + 20.0f, settings.y + settings.h - 24.0f, "South mute");

    float analyzer_x = settings.x + settings.w + 22.0f;
    float analyzer_y = panel.y + 44.0f;
    SDL_FRect analyzer = {
        analyzer_x,
        analyzer_y,
        panel.x + panel.w - inset - right_stack_w - analyzer_x,
        panel.h - 88.0f
    };
    if (analyzer.w < 220.0f) analyzer.w = 220.0f;
    if (analyzer.h < 160.0f) analyzer.h = 160.0f;
    SDL_FRect peak = { analyzer.x + analyzer.w + 12.0f, analyzer.y, meter_w, analyzer.h };
    SDL_FRect clip = { peak.x + peak.w + 10.0f, analyzer.y, 42.0f, analyzer.h };

    render_lane_inspector_analyzer(app,
                                   analyzer,
                                   app->lane_analyzer_bars,
                                   app->lane_analyzer_peaks,
                                   visible_deep,
                                   visible_pastel,
                                   muted);
    render_lane_inspector_peak(app, peak, &monitor, visible_pastel, muted);

    SDL_Color clip_fill = monitor.clip_hold_seconds > 0.0f ? (SDL_Color){ 255, 28, 46, 245 } : (SDL_Color){ 54, 18, 24, 230 };
    set_draw_color(app->renderer, clip_fill);
    SDL_RenderFillRect(app->renderer, &clip);
    SDL_SetRenderDrawColor(app->renderer, 255, 92, 106, 255);
    SDL_RenderRect(app->renderer, &clip);
    SDL_SetRenderDrawColor(app->renderer, 255, 220, 222, monitor.clip_hold_seconds > 0.0f ? 255 : 150);
    SDL_RenderDebugText(app->renderer, clip.x + 6.0f, clip.y + 12.0f, "CLIP");
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
    const char *view_label = app->view_mode == APP_VIEW_LANE_INSPECTOR ? "lane inspector" :
                             (app->view_mode == APP_VIEW_TIMELINE ? "timeline" : "waveform");
    SDL_RenderDebugTextFormat(app->renderer, 12, 108, "view: %s  roster: %d",
                              view_label,
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

        SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 14.0f, app->sample_dir);
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
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)){ fprintf(stderr,"SDL init failed: %s\n",SDL_GetError()); return false; }
    app->window=SDL_CreateWindow("vaporplane",1280,720,SDL_WINDOW_RESIZABLE); if(!app->window){ fprintf(stderr,"SDL_CreateWindow failed: %s\n",SDL_GetError()); return false; }
    app->renderer=SDL_CreateRenderer(app->window,NULL); if(!app->renderer){ fprintf(stderr,"SDL_CreateRenderer failed: %s\n",SDL_GetError()); return false; }
    app->gamepad=NULL; app->gamepad_id=0; app->running=true;
    int gamepad_count = 0;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&gamepad_count);
    if (gamepads && gamepad_count > 0) {
        app->gamepad = SDL_OpenGamepad(gamepads[0]);
        if (app->gamepad) app->gamepad_id = SDL_GetGamepadID(app->gamepad);
    }
    SDL_free(gamepads);
    app_resolve_roster_export_dir(app);
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
    timeline_init_lanes(&app->timeline);
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
    app_timeline_clear_context_menu(app);
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_original_lane = 0;
    app->timeline_edit_ghost_lane = 0;
    app->selected_roster_clip = -1;
    app->selected_roster_clip_armed = false;
    app->selected_timeline_lane = 0;
    app->inspected_timeline_lane = 0;
    app->lane_analyzer_visual_lane = -1;
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    waveform_view_init(&app->view);
    bool audio_subsystem_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
    bool audio_ok = false;
    char audio_unavailable_status[sizeof(app->status_text)] = {0};
    if(audio_subsystem_ok) audio_ok = audio_engine_init(&app->audio,&app->clip,&app->transport);
    if(!audio_ok){
        const char *audio_error = SDL_GetError();
        fprintf(stderr,"%s failed: %s\n",
                audio_subsystem_ok ? "audio_engine_init" : "SDL audio init",
                audio_error);
        SDL_snprintf(audio_unavailable_status, sizeof(audio_unavailable_status),
                     "Audio unavailable: %s",
                     audio_error && audio_error[0] ? audio_error : "unknown SDL audio error");
        audio_engine_shutdown(&app->audio);
        app->audio.clip = &app->clip;
        app->audio.transport = &app->transport;
        app->audio.master_gain = 0.9f;
        app->audio.playback_mode = AUDIO_PLAYBACK_WAVEFORM;
        app->audio.active_analyzer_lane = -1;
    }
    audio_engine_set_timeline(&app->audio, app->roster, &app->roster_clip_count, &app->timeline);
    if(app->sample_count > 0) app_load_selected_sample(app);
    if(!audio_ok) app_set_status(app, audio_unavailable_status);
    return true;
}

void app_close_gamepad(App *app) {
    if (!app->gamepad) return;
    SDL_CloseGamepad(app->gamepad);
    app->gamepad = NULL;
    app->gamepad_id = 0;
}

void app_focus_loop_start(App *app){ app->view.target_center=(double)app->clip.loop_start_frame/(double)app->clip.frame_count; }
void app_focus_loop_end(App *app){ app->view.target_center=(double)app->clip.loop_end_frame/(double)app->clip.frame_count; }

void app_run(App *app){
    Uint64 prev=SDL_GetTicksNS();
    while(app->running){
        SDL_Event e; while(SDL_PollEvent(&e)) if(!input_handle_event(app,&e)) app->running=false;
        Uint64 now=SDL_GetTicksNS(); double dt=(double)(now-prev)/1e9; prev=now; input_update_gamepad(app,dt);
        waveform_view_update(&app->view, dt);
        if (app->view_mode == APP_VIEW_LANE_INSPECTOR) {
            app_render_lane_inspector(app);
        } else if (app->view_mode == APP_VIEW_TIMELINE) {
            app_render_timeline(app);
        } else {
            TempoLockParams guide_params;
            TempoLockParams *guide = app_get_active_tempo_params(app, &guide_params) ? &guide_params : NULL;
            waveform_render(app->renderer,&app->clip,&app->view,audio_engine_get_playhead_frame(&app->audio),guide);
        }
        if (app->view_mode != APP_VIEW_LANE_INSPECTOR) app_render_overlay(app);
        SDL_RenderPresent(app->renderer);
    }
}
void app_shutdown(App *app){
    audio_engine_shutdown(&app->audio);
    clip_destroy(&app->clip);
    for (int i = 0; i < app->roster_clip_count; ++i) roster_clip_destroy(&app->roster[i]);
    app_close_gamepad(app);
    if(app->renderer) SDL_DestroyRenderer(app->renderer);
    if(app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}
