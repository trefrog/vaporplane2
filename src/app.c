#include "app.h"
#include "input.h"
#include "project_format.h"
#include "project_validation.h"
#include "stretch_soundtouch.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
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

static double bytes_to_mib(size_t bytes) {
    return (double)bytes / (1024.0 * 1024.0);
}

static bool path_is_directory(const char *path) {
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static bool app_sample_extension_supported(const char *name) {
    const char *dot = name ? SDL_strrchr(name, '.') : NULL;
    if (!dot) return false;
    if (SDL_strcasecmp(dot, ".wav") == 0) return true;
#ifdef VAPORPLANE_HAVE_SNDFILE
    return SDL_strcasecmp(dot, ".flac") == 0 ||
           SDL_strcasecmp(dot, ".mp3") == 0 ||
           SDL_strcasecmp(dot, ".aiff") == 0 ||
           SDL_strcasecmp(dot, ".aif") == 0;
#else
    return false;
#endif
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

static bool path_exists_any(const char *path);

static bool app_bundle_resources_dir(char *out, size_t out_size) {
    const char *base = SDL_GetBasePath();
    if (!base || !base[0]) return false;
    path_join(out, out_size, base, "../Resources");
    if (path_is_directory(out)) return true;
    path_join(out, out_size, base, "resources");
    return path_is_directory(out);
}

static bool app_copy_file_if_missing(const char *src, const char *dst) {
    if (path_exists_any(dst)) return true;
    size_t size = 0;
    void *bytes = SDL_LoadFile(src, &size);
    if (!bytes) return false;
    bool ok = SDL_SaveFile(dst, bytes, size);
    SDL_free(bytes);
    return ok;
}

static bool app_should_skip_packaged_data_entry(const char *name) {
    if (!name || !name[0]) return true;
    if (SDL_strcmp(name, ".") == 0 || SDL_strcmp(name, "..") == 0) return true;
    if (SDL_strcmp(name, ".DS_Store") == 0 || SDL_strcmp(name, ".gitkeep") == 0) return true;
    return name[0] == '.' && name[1] == '_';
}

static bool app_copy_directory_entries_if_missing(const char *src_dir, const char *dst_dir) {
    if (!path_is_directory(src_dir)) return false;
    if (!SDL_CreateDirectory(dst_dir) && !path_is_directory(dst_dir)) return false;

    int count = 0;
    char **names = SDL_GlobDirectory(src_dir, "*", 0, &count);
    if (!names) return true;

    bool ok = true;
    qsort(names, (size_t)count, sizeof(char *), compare_strings);
    for (int i = 0; i < count; ++i) {
        if (app_should_skip_packaged_data_entry(names[i])) continue;

        char src_path[CLIP_MAX_PATH];
        char dst_path[CLIP_MAX_PATH];
        path_join(src_path, sizeof(src_path), src_dir, names[i]);
        path_join(dst_path, sizeof(dst_path), dst_dir, names[i]);

        if (path_is_directory(src_path)) {
            ok = app_copy_directory_entries_if_missing(src_path, dst_path) && ok;
        } else {
            ok = app_copy_file_if_missing(src_path, dst_path) && ok;
        }
    }
    SDL_free(names);
    return ok;
}

static bool app_resolve_user_child_dir(const App *app, const char *leaf, char *out, size_t out_size) {
    path_join(out, out_size, app->user_data_dir, leaf);
    return SDL_CreateDirectory(out) || path_is_directory(out);
}

static bool app_seed_packaged_starter_sample(App *app, const char *resources_dir) {
    static const char *starter_wav = "[demo] Makaih Beats - Vibration.wav";
    static const char *starter_json = "[demo] Makaih Beats - Vibration.wav.json";

    char sentinel[CLIP_MAX_PATH];
    path_join(sentinel, sizeof(sentinel), app->sample_dir, ".makaih_vibration_demo_seeded");
    if (path_exists_any(sentinel)) return true;

    char bundled_wav_dir[CLIP_MAX_PATH];
    char source_wav[CLIP_MAX_PATH];
    char source_json[CLIP_MAX_PATH];
    char dest_wav[CLIP_MAX_PATH];
    char dest_json[CLIP_MAX_PATH];
    path_join(bundled_wav_dir, sizeof(bundled_wav_dir), resources_dir, "wav");
    path_join(source_wav, sizeof(source_wav), bundled_wav_dir, starter_wav);
    path_join(source_json, sizeof(source_json), bundled_wav_dir, starter_json);
    path_join(dest_wav, sizeof(dest_wav), app->sample_dir, starter_wav);
    path_join(dest_json, sizeof(dest_json), app->sample_dir, starter_json);

    if (!path_exists_any(source_wav) || !path_exists_any(source_json)) return false;
    if (!app_copy_file_if_missing(source_wav, dest_wav)) return false;
    if (!app_copy_file_if_missing(source_json, dest_json)) return false;

    static const char seeded[] = "makaih vibration demo seeded\n";
    return SDL_SaveFile(sentinel, seeded, sizeof(seeded) - 1);
}

static bool app_seed_packaged_drum_packs(App *app, const char *resources_dir) {
    char sentinel[CLIP_MAX_PATH];
    path_join(sentinel, sizeof(sentinel), app->drum_pack_dir, ".cc0_starter_drum_packs_seeded");
    if (path_exists_any(sentinel)) return true;

    char bundled_drum_packs[CLIP_MAX_PATH];
    path_join(bundled_drum_packs, sizeof(bundled_drum_packs), resources_dir, "drum_packs");
    if (!path_is_directory(bundled_drum_packs)) return false;
    if (!app_copy_directory_entries_if_missing(bundled_drum_packs, app->drum_pack_dir)) return false;

    static const char seeded[] = "cc0 starter drum packs seeded\n";
    return SDL_SaveFile(sentinel, seeded, sizeof(seeded) - 1);
}

static bool app_resolve_user_data_dirs(App *app, const char *resources_dir) {
    char *pref = SDL_GetPrefPath("Vaporplane", "Vaporplane");
    if (!pref || !pref[0]) {
        if (pref) SDL_free(pref);
        return false;
    }

    SDL_strlcpy(app->user_data_dir, pref, sizeof(app->user_data_dir));
    SDL_free(pref);

    bool ok = true;
    ok = ok && app_resolve_user_child_dir(app, "samples", app->sample_dir, sizeof(app->sample_dir));
    ok = ok && app_resolve_user_child_dir(app, "drum_packs", app->drum_pack_dir, sizeof(app->drum_pack_dir));

    char exports_dir[CLIP_MAX_PATH];
    path_join(exports_dir, sizeof(exports_dir), app->user_data_dir, "exports");
    ok = ok && (SDL_CreateDirectory(exports_dir) || path_is_directory(exports_dir));
    path_join(app->roster_export_dir, sizeof(app->roster_export_dir), exports_dir, "roster");
    path_join(app->project_export_dir, sizeof(app->project_export_dir), exports_dir, "projects");
    path_join(app->render_export_dir, sizeof(app->render_export_dir), exports_dir, "renders");
    ok = ok && (SDL_CreateDirectory(app->roster_export_dir) || path_is_directory(app->roster_export_dir));
    ok = ok && (SDL_CreateDirectory(app->project_export_dir) || path_is_directory(app->project_export_dir));
    ok = ok && (SDL_CreateDirectory(app->render_export_dir) || path_is_directory(app->render_export_dir));
    if (!ok) return false;

    app->use_user_data_dirs = true;
    app->roster_export_dir_is_base_path = false;
    if (!app_seed_packaged_starter_sample(app, resources_dir)) {
        app_set_status(app, "Could not seed starter sample");
    }
    if (!app_seed_packaged_drum_packs(app, resources_dir)) {
        app_set_status(app, "Could not seed starter drum packs");
    }
    return true;
}

static void app_resolve_data_dirs(App *app) {
    app->use_user_data_dirs = false;
    app->user_data_dir[0] = '\0';
    app->sample_dir[0] = '\0';
    app->drum_pack_dir[0] = '\0';
    app->roster_export_dir[0] = '\0';
    app->project_export_dir[0] = '\0';
    app->render_export_dir[0] = '\0';
    app->roster_export_dir_is_base_path = false;

    char resources_dir[CLIP_MAX_PATH];
    resources_dir[0] = '\0';
    bool packaged_build = app_bundle_resources_dir(resources_dir, sizeof(resources_dir));
    if (packaged_build &&
        app_resolve_user_data_dirs(app, resources_dir)) {
        return;
    }

    const char *base = SDL_GetBasePath();
    char exports_dir[CLIP_MAX_PATH];

    if (base && base[0]) {
        path_join(app->sample_dir, sizeof(app->sample_dir), base, "wav");
        if (!path_is_directory(app->sample_dir)) {
            SDL_strlcpy(app->sample_dir, "assets/samples", sizeof(app->sample_dir));
        }
        path_join(app->drum_pack_dir, sizeof(app->drum_pack_dir), base, "assets/drum_packs");
        if (!path_is_directory(app->drum_pack_dir)) {
            SDL_strlcpy(app->drum_pack_dir, "assets/drum_packs", sizeof(app->drum_pack_dir));
        }
        path_join(exports_dir, sizeof(exports_dir), base, "exports");
        path_join(app->roster_export_dir, sizeof(app->roster_export_dir), exports_dir, "roster");
        path_join(app->project_export_dir, sizeof(app->project_export_dir), exports_dir, "projects");
        path_join(app->render_export_dir, sizeof(app->render_export_dir), exports_dir, "renders");
        app->roster_export_dir_is_base_path = true;
    } else {
        SDL_strlcpy(app->sample_dir, "assets/samples", sizeof(app->sample_dir));
        SDL_strlcpy(app->drum_pack_dir, "assets/drum_packs", sizeof(app->drum_pack_dir));
        path_join(exports_dir, sizeof(exports_dir), "exports", "");
        path_join(app->roster_export_dir, sizeof(app->roster_export_dir), exports_dir, "roster");
        path_join(app->project_export_dir, sizeof(app->project_export_dir), exports_dir, "projects");
        path_join(app->render_export_dir, sizeof(app->render_export_dir), exports_dir, "renders");
    }

    SDL_CreateDirectory(exports_dir);
    SDL_CreateDirectory(app->roster_export_dir);
    SDL_CreateDirectory(app->project_export_dir);
    SDL_CreateDirectory(app->render_export_dir);
    if (packaged_build) app_set_status(app, "Using local data folders");
}

static void app_resolve_sample_dir(App *app) {
    if (!app || !app->sample_dir[0]) return;
}

static void app_resolve_roster_export_dir(App *app) {
    if (!app || !app->roster_export_dir[0]) return;
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

static int roster_palette_count(void) {
    return (int)(sizeof(roster_palette) / sizeof(roster_palette[0]));
}

static SDL_Color roster_color_for_index(int index) {
    int count = roster_palette_count();
    if (count <= 0) return (SDL_Color){ 255, 255, 255, 255 };
    if (index < 0) index = 0;
    return roster_palette[index % count];
}

static int app_next_available_midi_note(const App *app) {
    bool used[128] = {0};
    if (app) {
        for (int i = 0; i < app->roster_clip_count; ++i) {
            if (app->roster[i].midi_channel == 0 &&
                app->roster[i].midi_note >= 0 &&
                app->roster[i].midi_note < 128) {
                used[app->roster[i].midi_note] = true;
            }
        }
    }
    for (int note = 60; note < 128; ++note) {
        if (!used[note]) return note;
    }
    for (int note = 0; note < 60; ++note) {
        if (!used[note]) return note;
    }
    return 60;
}

static bool colors_equal(SDL_Color a, SDL_Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void generate_stable_id(const char *prefix, char *out, size_t out_size) {
    static Uint32 counter = 0;
    Uint32 bits = SDL_rand_bits();
    Uint64 ticks = SDL_GetTicksNS();
    counter++;
    SDL_snprintf(out,
                 out_size,
                 "%s_%08x",
                 prefix ? prefix : "id",
                 (unsigned int)(bits ^ (Uint32)ticks ^ (Uint32)(ticks >> 32) ^ counter));
}

static SDL_Color roster_color_avoiding_neighbors(SDL_Color preferred, const SDL_Color *prev, const SDL_Color *next) {
    int count = roster_palette_count();
    if (count <= 0) return preferred;
    if ((!prev || !colors_equal(preferred, *prev)) &&
        (!next || !colors_equal(preferred, *next))) {
        return preferred;
    }
    for (int i = 0; i < count; ++i) {
        SDL_Color candidate = roster_palette[i];
        if (prev && colors_equal(candidate, *prev)) continue;
        if (next && colors_equal(candidate, *next)) continue;
        return candidate;
    }
    return preferred;
}

static SDL_Color roster_color_for_append(const App *app) {
    int index = app ? app->roster_clip_count : 0;
    SDL_Color preferred = roster_color_for_index(index);
    const SDL_Color *prev = (app && app->roster_clip_count > 0) ? &app->roster[app->roster_clip_count - 1].color : NULL;
    return roster_color_avoiding_neighbors(preferred, prev, NULL);
}

static void app_repair_roster_adjacent_colors(App *app) {
    if (!app) return;
    for (int i = 1; i < app->roster_clip_count; ++i) {
        SDL_Color prev = app->roster[i - 1].color;
        if (!colors_equal(app->roster[i].color, prev)) continue;
        const SDL_Color *next = i + 1 < app->roster_clip_count ? &app->roster[i + 1].color : NULL;
        app->roster[i].color = roster_color_avoiding_neighbors(roster_color_for_index(i), &prev, next);
    }
}

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

static void drum_kit_destroy(DrumKit *kit) {
    if (!kit) return;
    for (int i = 0; i < kit->pad_count; ++i) {
        clip_destroy(&kit->pads[i].clip);
    }
    SDL_memset(kit, 0, sizeof(*kit));
}

static void app_clear_drum_kits(App *app) {
    if (!app) return;
    for (int i = 0; i < app->drum_kit_count; ++i) {
        drum_kit_destroy(&app->drum_kits[i]);
    }
    app->drum_kit_count = 0;
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

static bool roster_clip_name_exists(const App *app, const char *name) {
    if (!app || !name || !name[0]) return false;
    for (int i = 0; i < app->roster_clip_count; ++i) {
        if (SDL_strcmp(app->roster[i].name, name) == 0) return true;
    }
    return false;
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

static void app_clamp_roster_scroll(App *app, int visible_rows) {
    if (!app) return;
    if (visible_rows < 1) visible_rows = 1;
    app->roster_visible_rows = visible_rows;
    if (app->roster_clip_count <= 0) {
        app->selected_roster_clip = -1;
        app->selected_roster_clip_armed = false;
        app->roster_scroll_offset = 0;
        return;
    }
    if (app->selected_roster_clip < 0) app->selected_roster_clip = 0;
    if (app->selected_roster_clip >= app->roster_clip_count) {
        app->selected_roster_clip = app->roster_clip_count - 1;
    }
    int max_scroll = app->roster_clip_count - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    app->roster_scroll_offset = clamp_int(app->roster_scroll_offset, 0, max_scroll);
    if (app->selected_roster_clip < app->roster_scroll_offset) {
        app->roster_scroll_offset = app->selected_roster_clip;
    }
    if (app->selected_roster_clip >= app->roster_scroll_offset + visible_rows) {
        app->roster_scroll_offset = app->selected_roster_clip - visible_rows + 1;
    }
    app->roster_scroll_offset = clamp_int(app->roster_scroll_offset, 0, max_scroll);
}

static int64_t clamp_i64(int64_t value, int64_t min_value, int64_t max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static double clamp_double(double value, double min_value, double max_value) {
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
    return app->view_mode == APP_VIEW_TIMELINE ||
           app->view_mode == APP_VIEW_MASTER_MIX ||
           app->view_mode == APP_VIEW_LANE_INSPECTOR ||
           app->view_mode == APP_VIEW_DRUM_MACHINE;
}

static const char *master_mix_focus_label(MasterMixFocusSection section) {
    switch (section) {
        case MASTER_MIX_FOCUS_MASTER: return "MASTER";
        case MASTER_MIX_FOCUS_REVERB: return "REVERB";
        case MASTER_MIX_FOCUS_FX_CHAIN: return "FX CHAIN";
        case MASTER_MIX_FOCUS_MIDI_CONTROL: return "MIDI / CONTROL";
        case MASTER_MIX_FOCUS_COUNT:
        default: return "MASTER";
    }
}

static void reset_lane_analyzer_visual(App *app, int lane_index);
static void clamp_timeline_view(App *app);

static void timeline_init_lanes(MasterTimeline *timeline) {
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &timeline->lanes[lane_index];
        if (lane->type != TIMELINE_LANE_DRUMS) lane->type = TIMELINE_LANE_AUDIO;
        lane->midi_channel = lane->type == TIMELINE_LANE_DRUMS ? DRUM_MIDI_CHANNEL : 0;
        if (lane->type == TIMELINE_LANE_AUDIO) {
            lane->drum_kit_index = -1;
            lane->drum_kit_id[0] = '\0';
        }
        if (lane->drum_step_resolution <= 0) lane->drum_step_resolution = 16;
        lane->palette_index = lane_index % lane_palette_count();
        if (lane->gain <= 0.0f) lane->gain = 1.0f;
        if (lane->instance_count < 0) lane->instance_count = 0;
        if (lane->instance_count > APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
            lane->instance_count = APP_MAX_TIMELINE_INSTANCES_PER_LANE;
        }
        for (int i = 0; i < lane->instance_count; ++i) {
            TimelineInstance *instance = &lane->instances[i];
            if (instance->kind != TIMELINE_INSTANCE_DRUM_PATTERN) {
                instance->kind = TIMELINE_INSTANCE_AUDIO_CLIP;
                instance->pattern_index = -1;
            } else {
                instance->roster_clip_index = -1;
            }
        }
    }
}

static bool timeline_lane_is_drum(const TimelineLane *lane) {
    return lane && lane->type == TIMELINE_LANE_DRUMS;
}

static bool app_selected_timeline_lane_is_drum(const App *app) {
    if (!app) return false;
    int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    return timeline_lane_is_drum(&app->timeline.lanes[lane_index]);
}

static bool app_pattern_index_valid(const App *app, int index) {
    return app && index >= 0 && index < app->drum_pattern_count;
}

static bool app_kit_index_valid(const App *app, int index) {
    return app && index >= 0 && index < app->drum_kit_count;
}

static const char *timeline_lane_type_label(TimelineLaneType type) {
    return type == TIMELINE_LANE_DRUMS ? "drums" : "audio";
}

static int64_t timeline_snap_ticks(const MasterTimeline *timeline) {
    return timeline->ticks_per_beat > 0 ? (int64_t)timeline->ticks_per_beat : 960;
}

static int64_t timeline_snap_tick_down_to_grid(int64_t tick, int64_t snap) {
    if (snap <= 1) return tick < 0 ? 0 : tick;
    if (tick < 0) tick = 0;
    return (tick / snap) * snap;
}

static int timeline_bar_ticks(const MasterTimeline *timeline) {
    int ticks_per_beat = timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat : 960;
    int beats_per_bar = timeline->timeline_beats_per_bar > 0 ? timeline->timeline_beats_per_bar : 4;
    int64_t ticks = (int64_t)ticks_per_beat * (int64_t)beats_per_bar;
    if (ticks < 1) ticks = 1;
    if (ticks > 0x7fffffff) ticks = 0x7fffffff;
    return (int)ticks;
}

static double timeline_visible_bars(const App *app) {
    if (!app) return 0.0;
    int bar_ticks = timeline_bar_ticks(&app->timeline);
    double span = app->timeline.view_span_ticks;
    if (span <= 0.0) span = app->timeline.length_ticks > 0 ? (double)app->timeline.length_ticks : (double)bar_ticks;
    if (span <= 0.0) span = (double)bar_ticks;
    return span / (double)bar_ticks;
}

static int64_t timeline_edit_snap_ticks(const App *app) {
    if (!app) return 1;
    int64_t beat = timeline_snap_ticks(&app->timeline);
    int beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    double bars = timeline_visible_bars(app);
    int64_t snap = beat;
    if (bars < 3.0) snap = beat / 4;
    else if (bars < 6.0) snap = beat / 2;
    else if (bars < 12.0) snap = beat;
    else if (bars < 20.0) snap = beat * 2;
    else snap = beat * (int64_t)beats_per_bar;
    return snap > 0 ? snap : 1;
}

static const char *timeline_edit_snap_label(const App *app) {
    if (!app) return "snap";
    double bars = timeline_visible_bars(app);
    if (bars < 3.0) return "1/16";
    if (bars < 6.0) return "1/8";
    if (bars < 12.0) return "1/4";
    if (bars < 20.0) return "1/2";
    return "1 bar";
}

static int64_t timeline_snap_tick_down(const App *app, int64_t tick) {
    if (!app) return tick < 0 ? 0 : tick;
    if (tick < 0) tick = 0;
    if (timeline_tempo_event_index_at_tick(&app->timeline, tick) >= 0) return tick;
    return timeline_snap_tick_down_to_grid(tick, timeline_edit_snap_ticks(app));
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
    if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
        int index = app->timeline_edit_pattern_index;
        if (app_pattern_index_valid(app, index)) return app->drum_patterns[index].name;
        return "pattern";
    }
    int index = app->timeline_edit_roster_clip_index;
    if (index >= 0 && index < app->roster_clip_count) return app->roster[index].name;
    return "clip";
}

static const char *timeline_placement_verb_label(TimelinePlacementMode mode) {
    switch (mode) {
        case TIMELINE_PLACE_PULSE: return "PLACING PULSE";
        case TIMELINE_INSERT_PULSE: return "INSERTING PULSE";
        case TIMELINE_PLACE_FREE:
        default: return "PLACING";
    }
}

static void app_set_timeline_edit_status(App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    const char *verb = app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP ?
        timeline_placement_verb_label(app->timeline_edit_placement_mode) :
        timeline_edit_verb_label(app->timeline_edit_mode);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "%s %s",
                 verb,
                 timeline_edit_clip_name(app));
}

#define TIMELINE_CONTEXT_MAX_ITEMS 12

static void app_timeline_clear_context_menu(App *app) {
    app->timeline_context_menu_open = false;
    app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_NONE;
    app->timeline_context_menu_selected = 0;
    app->timeline_context_menu_instance = timeline_instance_ref_invalid();
    app->timeline_context_menu_roster_index = -1;
    app->timeline_context_menu_pattern_index = -1;
    app->timeline_context_menu_tick = 0;
}

static void timeline_ensure_tempo_anchor_no_lock(MasterTimeline *timeline) {
    if (!timeline) return;

    double bpm = timeline_base_bpm(timeline);
    int count = timeline_valid_tempo_event_count(timeline);
    int anchor_index = timeline_tempo_event_index_at_tick(timeline, 0);
    if (anchor_index < 0) {
        if (count >= TIMELINE_MAX_TEMPO_EVENTS) count = TIMELINE_MAX_TEMPO_EVENTS - 1;
        for (int i = count; i > 0; --i) {
            timeline->tempo_events[i] = timeline->tempo_events[i - 1];
        }
        timeline->tempo_event_count = count + 1;
        anchor_index = 0;
    } else if (anchor_index > 0) {
        TimelineTempoEvent anchor = timeline->tempo_events[anchor_index];
        for (int i = anchor_index; i > 0; --i) {
            timeline->tempo_events[i] = timeline->tempo_events[i - 1];
        }
        timeline->tempo_events[0] = anchor;
        anchor_index = 0;
    }

    timeline->tempo_events[anchor_index].tick = 0;
    timeline->tempo_events[anchor_index].bpm = timeline_clamp_bpm(bpm);
    timeline->timeline_bpm = timeline->tempo_events[anchor_index].bpm;
}

static bool timeline_set_tempo_event_no_lock(MasterTimeline *timeline, int64_t tick, double bpm) {
    if (!timeline) return false;
    if (tick < 0) tick = 0;
    bpm = timeline_clamp_bpm(bpm);
    timeline_ensure_tempo_anchor_no_lock(timeline);

    int count = timeline_valid_tempo_event_count(timeline);
    int existing = timeline_tempo_event_index_at_tick(timeline, tick);
    if (existing >= 0) {
        timeline->tempo_events[existing].bpm = bpm;
        if (existing == 0) timeline->timeline_bpm = bpm;
        return true;
    }

    if (count >= TIMELINE_MAX_TEMPO_EVENTS) return false;
    int insert_at = count;
    while (insert_at > 0 && timeline->tempo_events[insert_at - 1].tick > tick) {
        timeline->tempo_events[insert_at] = timeline->tempo_events[insert_at - 1];
        --insert_at;
    }
    timeline->tempo_events[insert_at].tick = tick;
    timeline->tempo_events[insert_at].bpm = bpm;
    timeline->tempo_event_count = count + 1;
    if (insert_at == 0) timeline->timeline_bpm = bpm;
    return true;
}

static bool timeline_tick_is_navigation_seam(const MasterTimeline *timeline, int64_t tick) {
    return timeline && tick > 0 && timeline_tempo_event_index_at_tick(timeline, tick) > 0;
}

static const char *timeline_seam_side_label(TimelineSeamSide side) {
    switch (side) {
        case TIMELINE_SEAM_BEFORE: return "before seam";
        case TIMELINE_SEAM_AFTER: return "after seam";
        case TIMELINE_SEAM_NONE:
        default: return "";
    }
}

static const char *timeline_seam_side_short_label(TimelineSeamSide side) {
    switch (side) {
        case TIMELINE_SEAM_BEFORE: return "<";
        case TIMELINE_SEAM_AFTER: return ">";
        case TIMELINE_SEAM_NONE:
        default: return "";
    }
}

static const char *timeline_tape_control_mode_label(TimelineTapeControlMode mode) {
    switch (mode) {
        case TIMELINE_TAPE_CONTROL_PITCH: return "pitch";
        case TIMELINE_TAPE_CONTROL_BPM:
        default: return "bpm";
    }
}

static int64_t timeline_tape_reference_tick(App *app) {
    if (audio_engine_timeline_is_playing(&app->audio)) {
        return audio_engine_get_timeline_playhead_tick(&app->audio);
    }
    return app->timeline.timeline_cursor_tick;
}

static TimelineSeamSide timeline_default_seam_side_for_tick(const MasterTimeline *timeline, int64_t tick) {
    return timeline_tick_is_navigation_seam(timeline, tick) ? TIMELINE_SEAM_AFTER : TIMELINE_SEAM_NONE;
}

static void timeline_normalize_cursor_seam_side_no_lock(App *app) {
    if (!app) return;
    if (timeline_tick_is_navigation_seam(&app->timeline, app->timeline.timeline_cursor_tick)) {
        if (app->timeline.timeline_cursor_seam_side != TIMELINE_SEAM_BEFORE &&
            app->timeline.timeline_cursor_seam_side != TIMELINE_SEAM_AFTER) {
            app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_AFTER;
        }
    } else {
        app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
    }
}

static void timeline_normalize_ghost_seam_side(App *app) {
    if (!app) return;
    if (timeline_tick_is_navigation_seam(&app->timeline, app->timeline_edit_ghost_start_tick)) {
        if (app->timeline_edit_ghost_seam_side != TIMELINE_SEAM_BEFORE &&
            app->timeline_edit_ghost_seam_side != TIMELINE_SEAM_AFTER) {
            app->timeline_edit_ghost_seam_side = TIMELINE_SEAM_AFTER;
        }
    } else {
        app->timeline_edit_ghost_seam_side = TIMELINE_SEAM_NONE;
    }
}

static void timeline_nudge_tick_with_seams(const MasterTimeline *timeline,
                                           int64_t *tick,
                                           TimelineSeamSide *side,
                                           int direction,
                                           int64_t step_ticks,
                                           bool respect_seams) {
    if (!timeline || !tick || !side || direction == 0) return;
    int64_t length = timeline->length_ticks > 0 ? timeline->length_ticks : 0;
    if (step_ticks < 1) step_ticks = 1;

    /*
     * Tempo seams are edit-intent barriers, not alternate tick values.
     * The first grid press parks the cursor on the near side of the seam;
     * the next press crosses to the far side at the same tick. Larger hops
     * can opt out and land directly on the far side.
     */
    if (respect_seams && timeline_tick_is_navigation_seam(timeline, *tick)) {
        if (direction > 0 && *side == TIMELINE_SEAM_BEFORE) {
            *side = TIMELINE_SEAM_AFTER;
            return;
        }
        if (direction < 0 && *side == TIMELINE_SEAM_AFTER) {
            *side = TIMELINE_SEAM_BEFORE;
            return;
        }
    }

    int64_t next = *tick + step_ticks * (direction > 0 ? 1 : -1);
    next = clamp_i64(next, 0, length);
    if (respect_seams && next != *tick) {
        int64_t crossed_seam = -1;
        int count = timeline_valid_tempo_event_count(timeline);
        for (int i = 0; i < count; ++i) {
            int64_t event_tick = timeline->tempo_events[i].tick;
            if (event_tick <= 0) continue;
            if (direction > 0) {
                if (event_tick > *tick && event_tick <= next &&
                    (crossed_seam < 0 || event_tick < crossed_seam)) {
                    crossed_seam = event_tick;
                }
            } else {
                if (event_tick < *tick && event_tick >= next &&
                    (crossed_seam < 0 || event_tick > crossed_seam)) {
                    crossed_seam = event_tick;
                }
            }
        }
        if (crossed_seam >= 0) {
            *tick = crossed_seam;
            *side = direction > 0 ? TIMELINE_SEAM_BEFORE : TIMELINE_SEAM_AFTER;
            return;
        }
    }
    *tick = next;
    if (timeline_tick_is_navigation_seam(timeline, next)) {
        *side = respect_seams ?
            (direction > 0 ? TIMELINE_SEAM_BEFORE : TIMELINE_SEAM_AFTER) :
            (direction > 0 ? TIMELINE_SEAM_AFTER : TIMELINE_SEAM_BEFORE);
    } else {
        *side = TIMELINE_SEAM_NONE;
    }
}

static int64_t timeline_context_snapped_tick(const App *app) {
    int64_t tick = app->timeline_context_menu_open ?
        app->timeline_context_menu_tick : app->timeline.timeline_cursor_tick;
    return timeline_snap_tick_down(app, tick);
}

static bool timeline_context_has_removable_tempo_event(const App *app) {
    if (!app || app->timeline_focus_zone != TIMELINE_FOCUS_RULER) return false;
    int64_t tick = timeline_context_snapped_tick(app);
    int index = timeline_tempo_event_index_at_tick(&app->timeline, tick);
    return index > 0 && app->timeline.tempo_events[index].tick > 0;
}

static int timeline_context_menu_items(const App *app,
                                       TimelineContextMenuItem *items,
                                       int max_items) {
    if (!app || !items || max_items <= 0 || !app->timeline_context_menu_open) return 0;
    int count = 0;
    switch (app->timeline_context_menu_scope) {
        case TIMELINE_CONTEXT_SCOPE_TIMELINE:
            if (app->timeline_focus_zone == TIMELINE_FOCUS_RULER) {
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_MARK_TEMPO;
                if (timeline_context_has_removable_tempo_event(app) && count < max_items) {
                    items[count++] = TIMELINE_CONTEXT_ITEM_REMOVE_TEMPO;
                }
            } else if (app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) {
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_APPLY_LANE_VELOCITY;
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER;
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
                break;
            }
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_INSERT_BAR;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_INSTANCE:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_INSERT_BAR;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_ROSTER:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_OPEN_WAVEFORM;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_RENAME_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_PLACE_FREE;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_PLACE_PULSE;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_INSERT_PULSE;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_DELETE_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_PATTERN:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_NEW_PATTERN;
            if (app_pattern_index_valid(app, app->timeline_context_menu_pattern_index)) {
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_EDIT_PATTERN;
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_RENAME_PATTERN;
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_PLACE_FREE;
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_DUPLICATE_PATTERN;
                if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_DELETE_PATTERN;
            }
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_ROSTER;
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CANCEL;
            break;
        case TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE:
            if (count < max_items) items[count++] = TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_PATTERN;
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
        case TIMELINE_CONTEXT_SCOPE_PATTERN: return "PATTERN MENU";
        case TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE: return "DELETE ROSTER CLIP?";
        case TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE: return "DELETE PATTERN?";
        case TIMELINE_CONTEXT_SCOPE_NONE:
        default: return "MENU";
    }
}

static const char *timeline_context_item_label(TimelineContextMenuItem item) {
    switch (item) {
        case TIMELINE_CONTEXT_ITEM_INSERT_BAR: return "Insert bar";
        case TIMELINE_CONTEXT_ITEM_MARK_TEMPO: return "Mark tempo";
        case TIMELINE_CONTEXT_ITEM_REMOVE_TEMPO: return "Remove tempo";
        case TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE: return "Remove instance";
        case TIMELINE_CONTEXT_ITEM_APPLY_LANE_VELOCITY: return "Apply lane velocity...";
        case TIMELINE_CONTEXT_ITEM_OPEN_WAVEFORM: return "Open waveform";
        case TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER: return "Bounce to roster";
        case TIMELINE_CONTEXT_ITEM_RENAME_ROSTER: return "Rename roster clip";
        case TIMELINE_CONTEXT_ITEM_PLACE_FREE: return "Place free";
        case TIMELINE_CONTEXT_ITEM_PLACE_PULSE: return "Place pulse";
        case TIMELINE_CONTEXT_ITEM_INSERT_PULSE: return "Insert pulse";
        case TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER: return "Export WAV";
        case TIMELINE_CONTEXT_ITEM_DELETE_ROSTER: return "Delete roster clip";
        case TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_ROSTER: return "Delete clip and instances";
        case TIMELINE_CONTEXT_ITEM_NEW_PATTERN: return "New pattern";
        case TIMELINE_CONTEXT_ITEM_EDIT_PATTERN: return "Edit pattern";
        case TIMELINE_CONTEXT_ITEM_RENAME_PATTERN: return "Rename pattern";
        case TIMELINE_CONTEXT_ITEM_DUPLICATE_PATTERN: return "Duplicate pattern";
        case TIMELINE_CONTEXT_ITEM_DELETE_PATTERN: return "Delete pattern";
        case TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_PATTERN: return "Delete pattern and instances";
        case TIMELINE_CONTEXT_ITEM_CANCEL:
        default: return "Cancel";
    }
}

static const char *project_menu_item_label(ProjectMenuItem item) {
    switch (item) {
        case PROJECT_MENU_ITEM_SAVE: return "Save As...";
        case PROJECT_MENU_ITEM_OPEN: return "Open project...";
        case PROJECT_MENU_ITEM_EXPORT_TIMELINE_WAV: return "Export timeline WAV";
        case PROJECT_MENU_ITEM_QUIT: return "Quit";
        case PROJECT_MENU_ITEM_COUNT:
        default: return "Project";
    }
}

static const char *waveform_menu_item_label(const App *app, WaveformMenuItem item);

static const char *roster_commit_menu_item_label(RosterCommitMenuItem item) {
    switch (item) {
        case ROSTER_COMMIT_ITEM_NEW_CLIP: return "New roster clip";
        case ROSTER_COMMIT_ITEM_REPLACE_CLIP: return "Replace source clip";
        case ROSTER_COMMIT_ITEM_CANCEL:
        default: return "Cancel";
    }
}

static bool app_save_project_bundle_default(App *app);
static void app_project_browser_clear_preview(App *app);
static bool write_project_preview_wav(App *app, const char *path);
static void safe_project_render_stem(const App *app, char *out, size_t out_size);
static bool app_export_timeline_wav_named(App *app, const char *filename_text);
static bool app_export_roster_clip_wav_named(App *app, int roster_index, const char *filename_text);
static bool app_rename_roster_clip_named(App *app, int roster_index, const char *display_name);
static bool app_rename_drum_pattern_named(App *app, int pattern_index, const char *display_name);
static bool app_start_timeline_bounce(App *app);
static void roster_export_filename(const RosterClip *clip, char *out, size_t out_size);
static void app_text_entry_open(App *app,
                                AppTextEntryMode mode,
                                AppTextEntryAction action,
                                const char *title,
                                const char *prompt,
                                const char *initial_text,
                                int max_length);

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
        timeline->timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
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
    timeline_normalize_cursor_seam_side_no_lock(app);
    timeline->playhead_tick = clamp_i64(timeline->playhead_tick, 0, length);
}

static void sync_timeline_play_range(App *app) {
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    sync_timeline_play_range_no_lock(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
}

static void app_ensure_roster_timeline_surface_no_lock(App *app) {
    if (!app || app->timeline.initialized) return;
    app->timeline.initialized = true;
    if (app->timeline.ticks_per_beat <= 0) {
        app->timeline.ticks_per_beat = app->transport.ppqn > 0 ? app->transport.ppqn : 960;
    }
    if (app->timeline.timeline_bpm <= 0.0) {
        app->timeline.timeline_bpm = app->transport.bpm > 0.0 ? app->transport.bpm : TIMELINE_DEFAULT_BPM;
    }
    if (app->timeline.timeline_beats_per_bar <= 0) app->timeline.timeline_beats_per_bar = 4;
    if (app->timeline.timeline_beat_unit <= 0) app->timeline.timeline_beat_unit = 4;
    if (app->timeline.view_span_ticks <= 0.0) {
        app->timeline.view_span_ticks = (double)app->timeline.ticks_per_beat * 4.0;
    }
    timeline_ensure_tempo_anchor_no_lock(&app->timeline);
    sync_timeline_play_range_no_lock(app);
}

static void app_timeline_fit_play_range_anchors(App *app) {
    if (!app->timeline.initialized || app->timeline.length_ticks <= 0) return;
    int64_t start = 0, end = 0;
    timeline_effective_play_range(&app->timeline, &start, &end);
    if (end <= start) return;

    double length = (double)app->timeline.length_ticks;
    double range_start = (double)start;
    double range_end = (double)end;
    double range_span = range_end - range_start;
    double min_span = (double)timeline_min_range_ticks(&app->timeline);
    double needed_span = range_span * 1.12;
    if (needed_span < range_span + min_span) needed_span = range_span + min_span;
    if (needed_span > length) needed_span = length;

    double current_span = app->timeline.view_span_ticks;
    if (current_span <= 0.0) current_span = needed_span;
    if (current_span < needed_span) current_span = needed_span;

    double pad = current_span * 0.08;
    double visible_start = app->timeline.view_center_tick - current_span * 0.5;
    double visible_end = app->timeline.view_center_tick + current_span * 0.5;
    double center = app->timeline.view_center_tick;
    if (range_start < visible_start + pad) {
        center = range_start - pad + current_span * 0.5;
    }
    if (range_end > visible_end - pad) {
        center = range_end + pad - current_span * 0.5;
    }

    app->timeline.view_span_ticks = current_span;
    app->timeline.view_center_tick = center;
    clamp_timeline_view(app);
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
    int ticks_per_beat = app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat : 960;
    if (clip->tempo_calibrated && clip->target_beats > 0.0) {
        return beats_to_ticks(clip->target_beats, ticks_per_beat);
    }
    if (clip->frame_count == 0 || clip->sample_rate <= 0) return 0;
    double seconds = (double)clip->frame_count / (double)clip->sample_rate;
    double bpm = timeline_effective_bpm_at_tick(&app->timeline, (double)app->timeline.timeline_cursor_tick);
    if (bpm <= 0.0) bpm = timeline_base_bpm(&app->timeline);
    if (bpm <= 0.0) bpm = TIMELINE_DEFAULT_BPM;
    int64_t ticks = (int64_t)llround(seconds * bpm * (double)ticks_per_beat / 60.0);
    return ticks > 0 ? ticks : 1;
}

static int64_t timeline_pattern_duration_ticks(const App *app, int pattern_index) {
    if (!app_pattern_index_valid(app, pattern_index)) return 0;
    int64_t length = app->drum_patterns[pattern_index].length_ticks;
    if (length <= 0) length = timeline_bar_ticks(&app->timeline);
    return length > 0 ? length : 1;
}

static int app_next_available_pattern_locator_note(const App *app) {
    bool used[128] = {0};
    if (app) {
        for (int i = 0; i < app->drum_pattern_count; ++i) {
            const DrumPattern *pattern = &app->drum_patterns[i];
            if (pattern->locator_channel == DRUM_PATTERN_LOCATOR_CHANNEL &&
                pattern->locator_note >= 0 &&
                pattern->locator_note < 128) {
                used[pattern->locator_note] = true;
            }
        }
    }
    for (int note = 24; note < 96; ++note) {
        if (!used[note]) return note;
    }
    for (int note = 0; note < 128; ++note) {
        if (!used[note]) return note;
    }
    return 24;
}

static int drum_pattern_event_index(const DrumPattern *pattern, int64_t tick, int note) {
    if (!pattern) return -1;
    for (int i = 0; i < pattern->event_count; ++i) {
        const DrumPatternEvent *event = &pattern->events[i];
        if (event->tick == tick && event->note == note) return i;
    }
    return -1;
}

static bool drum_pattern_add_event(DrumPattern *pattern,
                                   int64_t tick,
                                   int note,
                                   int velocity,
                                   int64_t duration_ticks) {
    if (!pattern || pattern->event_count >= APP_MAX_DRUM_PATTERN_EVENTS) return false;
    if (duration_ticks <= 0) duration_ticks = 1;
    int existing = drum_pattern_event_index(pattern, tick, note);
    if (existing >= 0) {
        pattern->events[existing].velocity = clamp_int(velocity, 1, 127);
        pattern->events[existing].duration_ticks = duration_ticks;
        return true;
    }
    DrumPatternEvent event = {
        .tick = tick < 0 ? 0 : tick,
        .note = clamp_int(note, 0, 127),
        .velocity = clamp_int(velocity, 1, 127),
        .duration_ticks = duration_ticks
    };
    int insert_at = pattern->event_count;
    while (insert_at > 0) {
        DrumPatternEvent *prev = &pattern->events[insert_at - 1];
        if (prev->tick < event.tick || (prev->tick == event.tick && prev->note <= event.note)) break;
        pattern->events[insert_at] = *prev;
        --insert_at;
    }
    pattern->events[insert_at] = event;
    pattern->event_count++;
    return true;
}

static void drum_pattern_remove_event_at(DrumPattern *pattern, int index) {
    if (!pattern || index < 0 || index >= pattern->event_count) return;
    for (int i = index; i + 1 < pattern->event_count; ++i) {
        pattern->events[i] = pattern->events[i + 1];
    }
    pattern->event_count--;
    if (pattern->event_count >= 0) {
        SDL_memset(&pattern->events[pattern->event_count], 0, sizeof(pattern->events[pattern->event_count]));
    }
}

static void drum_pattern_seed_basic_beat(DrumPattern *pattern) {
    if (!pattern || pattern->length_ticks <= 0) return;
    int64_t step = pattern->length_ticks / 16;
    if (step <= 0) step = 1;
    int64_t dur = step;
    for (int i = 0; i < 16; i += 2) {
        drum_pattern_add_event(pattern, (int64_t)i * step, 42, 82, dur);
    }
    drum_pattern_add_event(pattern, 0 * step, 36, 112, dur);
    drum_pattern_add_event(pattern, 8 * step, 36, 108, dur);
    drum_pattern_add_event(pattern, 4 * step, 38, 118, dur);
    drum_pattern_add_event(pattern, 12 * step, 38, 118, dur);
}

static void drum_pattern_init_defaults(App *app, DrumPattern *pattern, int index, const char *name) {
    SDL_memset(pattern, 0, sizeof(*pattern));
    generate_stable_id("pat", pattern->pattern_id, sizeof(pattern->pattern_id));
    SDL_snprintf(pattern->name, sizeof(pattern->name), "%s", name && name[0] ? name : "basic beat");
    pattern->length_ticks = timeline_bar_ticks(&app->timeline);
    pattern->locator_channel = DRUM_PATTERN_LOCATOR_CHANNEL;
    pattern->locator_note = app_next_available_pattern_locator_note(app);
    pattern->color = roster_color_for_index(index);
}

static bool timeline_range_overlaps_existing(const App *app,
                                             int lane_index,
                                             int64_t start,
                                             int64_t duration,
                                             TimelineInstanceRef ignore_instance,
                                             bool insertion_shifts_later) {
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
        if (insertion_shifts_later && instance->start_tick >= start) continue;
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
    if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN &&
        ghost_lane->type != TIMELINE_LANE_DRUMS) {
        return false;
    }
    if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP &&
        ghost_lane->type != TIMELINE_LANE_AUDIO) {
        return false;
    }
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
    bool insertion_shifts_later = app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP &&
                                  app->timeline_edit_placement_mode == TIMELINE_INSERT_PULSE;
    return !timeline_range_overlaps_existing(app,
                                             app->timeline_edit_ghost_lane,
                                             app->timeline_edit_ghost_start_tick,
                                             app->timeline_edit_duration_ticks,
                                             ignore,
                                             insertion_shifts_later);
}

static void timeline_update_ghost_valid(App *app) {
    app->timeline_edit_ghost_valid = timeline_ghost_is_valid(app);
}

static void timeline_clamp_ghost_start(App *app) {
    if (app->timeline_edit_ghost_start_tick < 0) app->timeline_edit_ghost_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline_edit_ghost_start_tick);
    timeline_normalize_ghost_seam_side(app);
}

static double timeline_roster_clip_source_bpm(const App *app, int roster_clip_index) {
    if (!app || roster_clip_index < 0 || roster_clip_index >= app->roster_clip_count) {
        return timeline_base_bpm(app ? &app->timeline : NULL);
    }
    const RosterClip *clip = &app->roster[roster_clip_index];
    if (!clip->tempo_calibrated || clip->source_bpm <= 0.0) return 0.0;
    return timeline_clamp_bpm(clip->source_bpm);
}

static void timeline_shift_instances_starting_at_no_lock(MasterTimeline *timeline,
                                                         int64_t insertion_tick,
                                                         int64_t amount_ticks) {
    if (!timeline || amount_ticks == 0) return;
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &timeline->lanes[lane_index];
        for (int i = 0; i < lane->instance_count; ++i) {
            TimelineInstance *instance = &lane->instances[i];
            if (instance->start_tick >= insertion_tick) instance->start_tick += amount_ticks;
        }
    }
}

static void timeline_shift_tempo_events_after_tick_no_lock(MasterTimeline *timeline,
                                                           int64_t insertion_tick,
                                                           int64_t amount_ticks,
                                                           bool include_event_at_tick) {
    if (!timeline || amount_ticks == 0) return;
    int count = timeline_valid_tempo_event_count(timeline);
    for (int i = 0; i < count; ++i) {
        TimelineTempoEvent *event = &timeline->tempo_events[i];
        if (event->tick <= 0) continue;
        bool should_shift = include_event_at_tick ?
            event->tick >= insertion_tick :
            event->tick > insertion_tick;
        if (should_shift) event->tick += amount_ticks;
    }
}

static bool timeline_place_pulse_no_lock(App *app,
                                         int64_t tick,
                                         TimelineSeamSide side,
                                         double bpm,
                                         const char **error) {
    if (!app) return false;
    tick = timeline_snap_tick_down(app, tick);
    int existing = timeline_tempo_event_index_at_tick(&app->timeline, tick);
    if (existing > 0 && side == TIMELINE_SEAM_BEFORE) {
        if (error) *error = "Use Insert pulse before seam";
        return false;
    }
    if (existing < 0 && timeline_valid_tempo_event_count(&app->timeline) >= TIMELINE_MAX_TEMPO_EVENTS) {
        if (error) *error = "Tempo map full";
        return false;
    }
    if (!timeline_set_tempo_event_no_lock(&app->timeline, tick, bpm)) {
        if (error) *error = "Tempo map full";
        return false;
    }
    return true;
}

static bool timeline_insert_pulse_no_lock(App *app,
                                          int64_t insertion_tick,
                                          TimelineSeamSide side,
                                          int64_t duration_ticks,
                                          double bpm,
                                          int64_t old_playhead,
                                          int64_t *new_playhead,
                                          const char **error) {
    if (!app || duration_ticks <= 0) return false;
    insertion_tick = timeline_snap_tick_down(app, insertion_tick);
    MasterTimeline *timeline = &app->timeline;
    int existing = timeline_tempo_event_index_at_tick(timeline, insertion_tick);
    bool before_existing_seam = existing > 0 && side == TIMELINE_SEAM_BEFORE;
    bool needs_new_event = existing < 0 || before_existing_seam;
    if (needs_new_event && timeline_valid_tempo_event_count(timeline) >= TIMELINE_MAX_TEMPO_EVENTS) {
        if (error) *error = "Tempo map full";
        return false;
    }

    timeline_shift_instances_starting_at_no_lock(timeline, insertion_tick, duration_ticks);
    timeline_shift_tempo_events_after_tick_no_lock(timeline,
                                                   insertion_tick,
                                                   duration_ticks,
                                                   before_existing_seam);
    if (!timeline_set_tempo_event_no_lock(timeline, insertion_tick, bpm)) {
        if (error) *error = "Tempo map full";
        return false;
    }

    int64_t shifted_playhead = old_playhead >= insertion_tick ? old_playhead + duration_ticks : old_playhead;
    timeline->playhead_tick = shifted_playhead;
    if (new_playhead) *new_playhead = shifted_playhead;
    if (timeline->play_range_custom) {
        if (timeline->play_range_start_tick >= insertion_tick) timeline->play_range_start_tick += duration_ticks;
        if (timeline->play_range_end_tick >= insertion_tick) timeline->play_range_end_tick += duration_ticks;
    }
    return true;
}

static void timeline_enter_move_instance(App *app) {
    if (!timeline_instance_ref_valid(&app->timeline, app->selected_timeline_instance)) {
        app_set_status(app, "no instance selected");
        return;
    }
    TimelineInstance *instance = timeline_instance_from_ref(&app->timeline, app->selected_timeline_instance);
    app->timeline_edit_mode = TIMELINE_EDIT_MOVE_INSTANCE;
    app->timeline_edit_placement_mode = TIMELINE_PLACE_FREE;
    app->timeline_edit_instance = app->selected_timeline_instance;
    app->timeline_edit_instance_kind = instance->kind;
    app->timeline_edit_roster_clip_index = instance->roster_clip_index;
    app->timeline_edit_pattern_index = instance->pattern_index;
    app->timeline_edit_original_lane = app->selected_timeline_instance.lane_index;
    app->timeline_edit_ghost_lane = app->selected_timeline_instance.lane_index;
    app->timeline_edit_original_start_tick = instance->start_tick;
    app->timeline_edit_ghost_start_tick = instance->start_tick;
    app->timeline_edit_original_seam_side = timeline_default_seam_side_for_tick(&app->timeline, instance->start_tick);
    if (app->timeline.timeline_cursor_tick == instance->start_tick &&
        app->timeline.timeline_cursor_seam_side != TIMELINE_SEAM_NONE) {
        app->timeline_edit_original_seam_side = app->timeline.timeline_cursor_seam_side;
    }
    app->timeline_edit_ghost_seam_side = app->timeline_edit_original_seam_side;
    app->timeline_edit_duration_ticks = instance->duration_ticks;
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    app_set_timeline_edit_status(app);
}

static void timeline_enter_place_clip(App *app, TimelinePlacementMode placement_mode) {
    if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
        app_set_status(app, "roster empty");
        return;
    }
    int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    if (app->timeline.lanes[lane_index].type != TIMELINE_LANE_AUDIO) {
        app_set_status(app, "Select an audio lane for clips");
        return;
    }
    if (app->timeline.lanes[lane_index].instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
        app_set_status(app, "lane full");
        return;
    }
    int64_t duration = timeline_clip_duration_ticks(app, app->selected_roster_clip);
    if (duration <= 0) {
        app_set_status(app, "Invalid roster clip");
        return;
    }
    if ((placement_mode == TIMELINE_PLACE_PULSE || placement_mode == TIMELINE_INSERT_PULSE) &&
        timeline_roster_clip_source_bpm(app, app->selected_roster_clip) <= 0.0) {
        app_set_status(app, "clip has no BPM");
        return;
    }
    app->timeline_edit_mode = TIMELINE_EDIT_PLACE_CLIP;
    app->timeline_edit_placement_mode = placement_mode;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_instance_kind = TIMELINE_INSTANCE_AUDIO_CLIP;
    app->timeline_edit_roster_clip_index = app->selected_roster_clip;
    app->timeline_edit_pattern_index = -1;
    app->timeline_edit_original_lane = lane_index;
    app->timeline_edit_ghost_lane = lane_index;
    app->timeline_edit_original_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    app->timeline_edit_original_seam_side = TIMELINE_SEAM_NONE;
    app->timeline_edit_ghost_seam_side = app->timeline.timeline_cursor_tick == app->timeline_edit_ghost_start_tick ?
        app->timeline.timeline_cursor_seam_side :
        timeline_default_seam_side_for_tick(&app->timeline, app->timeline_edit_ghost_start_tick);
    app->timeline_edit_duration_ticks = duration;
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    if (app->timeline_edit_ghost_valid) app_set_timeline_edit_status(app);
    else app_set_status(app, "overlap blocked");
}

static void timeline_enter_place_pattern(App *app) {
    if (!app_selected_timeline_lane_is_drum(app)) {
        app_set_status(app, "Select a drum lane for patterns");
        return;
    }
    if (!app_pattern_index_valid(app, app->selected_drum_pattern)) {
        app_set_status(app, "pattern roster empty");
        return;
    }
    int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    if (app->timeline.lanes[lane_index].instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
        app_set_status(app, "lane full");
        return;
    }
    int64_t duration = timeline_pattern_duration_ticks(app, app->selected_drum_pattern);
    if (duration <= 0) {
        app_set_status(app, "Invalid drum pattern");
        return;
    }
    app->timeline_edit_mode = TIMELINE_EDIT_PLACE_CLIP;
    app->timeline_edit_placement_mode = TIMELINE_PLACE_FREE;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_instance_kind = TIMELINE_INSTANCE_DRUM_PATTERN;
    app->timeline_edit_roster_clip_index = -1;
    app->timeline_edit_pattern_index = app->selected_drum_pattern;
    app->timeline_edit_original_lane = lane_index;
    app->timeline_edit_ghost_lane = lane_index;
    app->timeline_edit_original_start_tick = 0;
    app->timeline_edit_ghost_start_tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    app->timeline_edit_original_seam_side = TIMELINE_SEAM_NONE;
    app->timeline_edit_ghost_seam_side = app->timeline.timeline_cursor_tick == app->timeline_edit_ghost_start_tick ?
        app->timeline.timeline_cursor_seam_side :
        timeline_default_seam_side_for_tick(&app->timeline, app->timeline_edit_ghost_start_tick);
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
    double playhead_tick = app->timeline.playhead_tick > 0 ? (double)app->timeline.playhead_tick : 0.0;
    app->transport.bpm = timeline_effective_bpm_at_tick(&app->timeline, playhead_tick);
    app->transport.beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    app->transport.beat_unit = app->timeline.timeline_beat_unit > 0 ? app->timeline.timeline_beat_unit : 4;
    app->transport.current_tick = app->timeline.playhead_tick > 0 ? (uint64_t)app->timeline.playhead_tick : 0;
    app->transport.current_seconds = timeline_seconds_at_tick(&app->timeline, playhead_tick);
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
    if (params->target_bars < 0.25) params->target_bars = 0.25;
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

static void app_set_waveform_source_generated(App *app) {
    app->waveform_source_mode = WAVEFORM_SOURCE_GENERATED;
    app->waveform_source_roster_index = -1;
    app->waveform_source_name[0] = '\0';
    app->waveform_source_path[0] = '\0';
    app->waveform_source_offset_frame = 0;
    app->waveform_sidecar_confirm_open = false;
    app->waveform_menu_open = false;
    app->waveform_render_dialog_open = false;
    app->roster_commit_menu_open = false;
    app->waveform_loop_edge_arm = WAVEFORM_LOOP_EDGE_ARM_NONE;
}

static void app_set_waveform_source_wav(App *app, const char *path) {
    app->waveform_source_mode = WAVEFORM_SOURCE_WAV;
    app->waveform_source_roster_index = -1;
    app->waveform_source_offset_frame = 0;
    SDL_strlcpy(app->waveform_source_path, path ? path : "", sizeof(app->waveform_source_path));
    capture_base_name(&app->clip, app->waveform_source_name, sizeof(app->waveform_source_name));
    app->waveform_sidecar_confirm_open = false;
    app->waveform_menu_open = false;
    app->waveform_render_dialog_open = false;
    app->roster_commit_menu_open = false;
    app->waveform_loop_edge_arm = WAVEFORM_LOOP_EDGE_ARM_NONE;
}

static void app_set_waveform_source_roster(App *app, int roster_index, const RosterClip *clip) {
    app->waveform_source_mode = WAVEFORM_SOURCE_ROSTER;
    app->waveform_source_roster_index = roster_index;
    app->waveform_source_offset_frame = clip ? clip->source_loop_start_frame : 0;
    SDL_strlcpy(app->waveform_source_name,
                clip && clip->name[0] ? clip->name : "roster",
                sizeof(app->waveform_source_name));
    SDL_strlcpy(app->waveform_source_path,
                clip && clip->source_path[0] ? clip->source_path : "",
                sizeof(app->waveform_source_path));
    app->waveform_sidecar_confirm_open = false;
    app->waveform_menu_open = false;
    app->waveform_render_dialog_open = false;
    app->roster_commit_menu_open = false;
    app->waveform_loop_edge_arm = WAVEFORM_LOOP_EDGE_ARM_NONE;
}

void app_clear_waveform_frame_grip(App *app) {
    if (!app) return;
    app->waveform_frame_grip_active = false;
    app->waveform_frame_grip_exact_valid = false;
    app->waveform_frame_grip_left_frame = 0;
    app->waveform_frame_grip_right_frame = 0;
    app->waveform_frame_grip_snap_active = false;
    app->waveform_frame_grip_snap_index = -1;
    app->waveform_frame_grip_snap_beats = 0.0;
    app->waveform_frame_grip_l2_seconds = 0.0;
    app->waveform_frame_grip_anchor = WAVEFORM_FRAME_GRIP_ANCHOR_NONE;
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

static bool app_get_trusted_tempo_params(const App *app, TempoLockParams *params) {
    if (!app) return false;
    if (app->tempo_lock_mode) {
        if (params) *params = app->tempo_lock_draft;
        return true;
    }
    if (app->clip.clip_tempo_locked) {
        if (params) *params = app->clip.tempo_lock;
        return true;
    }
    return false;
}

static bool tempo_params_are_explicitly_calibrated(const App *app, const TempoLockParams *params) {
    if (!app || !params) return false;
    if (!app->clip.clip_tempo_locked) return false;
    if (params->bpm <= 0.0) return false;
    if (params->beats_per_bar <= 0 || params->beat_unit <= 0) return false;
    if (params->target_bars <= 0.0) return false;
    if (app->clip.frame_count < 2) return false;
    if (app->clip.loop_end_frame <= app->clip.loop_start_frame ||
        app->clip.loop_end_frame > app->clip.frame_count) {
        return false;
    }
    if (params->downbeat_frame < app->clip.loop_start_frame ||
        params->downbeat_frame >= app->clip.loop_end_frame) {
        return false;
    }
    return true;
}

static bool app_get_explicit_loop_calibration(const App *app, TempoLockParams *params) {
    TempoLockParams tempo;
    SDL_memset(&tempo, 0, sizeof(tempo));
    if (!app || !app_get_trusted_tempo_params(app, &tempo)) return false;
    if (!tempo_params_are_explicitly_calibrated(app, &tempo)) return false;
    if (params) *params = tempo;
    return true;
}

static const char *waveform_menu_item_label(const App *app, WaveformMenuItem item) {
    switch (item) {
        case WAVEFORM_MENU_ITEM_NORMALIZE: return "Normalize";
        case WAVEFORM_MENU_ITEM_CAPTURE_WHOLE_SOURCE: return "Send whole source to roster";
        case WAVEFORM_MENU_ITEM_RENDER_TEMPO:
            return app_get_explicit_loop_calibration(app, NULL) ? "Render tempo to BPM..." : "Render tempo percent...";
        case WAVEFORM_MENU_ITEM_RENDER_PITCH: return "Render pitch semitones...";
        case WAVEFORM_MENU_ITEM_RENDER_RATE: return "Render rate percent...";
        case WAVEFORM_MENU_ITEM_CANCEL:
        default: return "Cancel";
    }
}

typedef struct {
    float *samples;
    size_t start_frame;
    size_t end_frame;
    size_t frame_count;
    size_t byte_count;
    bool tempo_calibrated;
    TempoLockParams tempo;
} CurrentLoopCapture;

static void current_loop_capture_destroy(CurrentLoopCapture *capture) {
    if (!capture) return;
    SDL_free(capture->samples);
    SDL_memset(capture, 0, sizeof(*capture));
}

static bool app_prepare_current_loop_capture(App *app, CurrentLoopCapture *capture, bool require_roster_room) {
    if (!app || !capture) return false;
    SDL_memset(capture, 0, sizeof(*capture));
    if (audio_engine_timeline_is_playing(&app->audio)) {
        app_set_status(app, "Stop timeline before capture");
        return false;
    }
    if (!app->clip.samples || app->clip.frame_count < 2) {
        app_set_status(app, "No clip to capture");
        return false;
    }
    if (require_roster_room && app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        app_set_status(app, "Roster full");
        return false;
    }

    size_t start = app->clip.loop_start_frame;
    size_t end = app->clip.loop_end_frame;
    if (end > app->clip.frame_count) end = app->clip.frame_count;
    if (end <= start || end - start < APP_MIN_CAPTURE_FRAMES) {
        app_set_status(app, "Captured loop is too short");
        return false;
    }

    size_t frame_count = end - start;
    if (frame_count > APP_MAX_CAPTURE_FRAMES) {
        app_set_status(app, "Clip too large to capture");
        return false;
    }
    if (app->clip.channels <= 0 || app->clip.sample_rate <= 0) {
        app_set_status(app, "No clip to capture");
        return false;
    }
    size_t channels = (size_t)app->clip.channels;
    if (frame_count > SIZE_MAX / channels) {
        app_set_status(app, "Clip too large to capture");
        return false;
    }
    size_t sample_count = frame_count * channels;
    if (sample_count > SIZE_MAX / sizeof(float)) {
        app_set_status(app, "Clip too large to capture");
        return false;
    }
    size_t byte_count = sample_count * sizeof(float);
    if (byte_count > APP_MAX_CAPTURE_BYTES) {
        app_set_status(app, "Clip too large to capture");
        return false;
    }

    float *samples = (float *)SDL_malloc(byte_count);
    if (!samples) {
        app_set_status(app, "Memory allocation failed");
        return false;
    }
    SDL_memcpy(samples, app->clip.samples + start * channels, byte_count);

    TempoLockParams tempo;
    SDL_memset(&tempo, 0, sizeof(tempo));
    bool tempo_calibrated = app_get_explicit_loop_calibration(app, &tempo);

    capture->samples = samples;
    capture->start_frame = start;
    capture->end_frame = end;
    capture->frame_count = frame_count;
    capture->byte_count = byte_count;
    capture->tempo_calibrated = tempo_calibrated;
    capture->tempo = tempo;
    return true;
}

static void app_open_roster_commit_menu(App *app) {
    if (!app) return;
    app->roster_commit_menu_open = true;
    app->roster_commit_menu_selected = 0;
    app->waveform_menu_open = false;
    app->waveform_render_dialog_open = false;
    app->sample_selector_open = false;
    app_set_status(app, "Commit roster edit");
}

static void app_capture_current_loop_to_roster_new(App *app) {
    CurrentLoopCapture capture;
    if (!app_prepare_current_loop_capture(app, &capture, true)) return;

    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Could not lock audio stream");
        return;
    }
    if (app->timeline.playing) {
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Stop timeline before capture");
        return;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Roster full");
        return;
    }

    RosterClip next;
    SDL_memset(&next, 0, sizeof(next));
    generate_stable_id("sample", next.sample_id, sizeof(next.sample_id));
    generate_stable_id("roster", next.roster_clip_id, sizeof(next.roster_clip_id));
    char base[APP_ROSTER_CLIP_NAME_MAX];
    if (app->waveform_source_mode == WAVEFORM_SOURCE_ROSTER && app->waveform_source_name[0]) {
        SDL_strlcpy(base, app->waveform_source_name, sizeof(base));
    } else {
        capture_base_name(&app->clip, base, sizeof(base));
    }
    int number = next_capture_number(app, base);
    SDL_snprintf(next.name, sizeof(next.name), "%s#%03d", base, number);
    const char *source_path = app->waveform_source_path[0] ? app->waveform_source_path : app->clip.file_path;
    SDL_strlcpy(next.source_path, source_path, sizeof(next.source_path));
    next.source_loop_start_frame = app->waveform_source_offset_frame + capture.start_frame;
    next.source_loop_end_frame = app->waveform_source_offset_frame + capture.end_frame;
    next.source_sample_rate = app->clip.sample_rate;
    next.loop_start_frame = 0;
    next.loop_end_frame = capture.frame_count;
    next.sample_rate = app->clip.sample_rate;
    next.channels = app->clip.channels;
    next.frame_count = capture.frame_count;
    next.samples = capture.samples;
    capture.samples = NULL;
    next.tempo_calibrated = capture.tempo_calibrated;
    if (next.tempo_calibrated) {
        next.source_bpm = capture.tempo.bpm;
        next.beats_per_bar = capture.tempo.beats_per_bar;
        next.beat_unit = capture.tempo.beat_unit;
        next.target_bars = capture.tempo.target_bars;
        next.target_beats = capture.tempo.target_bars * (double)capture.tempo.beats_per_bar;
    } else {
        next.source_bpm = 0.0;
        next.beats_per_bar = 4;
        next.beat_unit = 4;
        next.target_bars = 0.0;
        next.target_beats = 0.0;
    }
    next.midi_note = app_next_available_midi_note(app);
    next.midi_channel = 0;
    next.midi_velocity = 127;
    if (next.tempo_calibrated && capture.tempo.downbeat_frame > capture.start_frame) {
        size_t offset = capture.tempo.downbeat_frame - capture.start_frame;
        next.downbeat_offset_frames = offset < capture.frame_count ? offset : capture.frame_count - 1;
    } else {
        next.downbeat_offset_frames = 0;
    }
    next.color = roster_color_for_append(app);

    int roster_index = app->roster_clip_count;
    app->roster[roster_index] = next;
    app->roster_clip_count++;
    app->selected_roster_clip = roster_index;
    app->selected_roster_clip_armed = false;

    if (!app->timeline.initialized) {
        app->timeline.initialized = true;
        app->timeline.timeline_bpm = next.tempo_calibrated && next.source_bpm > 0.0 ?
            next.source_bpm :
            (app->transport.bpm > 0.0 ? app->transport.bpm : TIMELINE_DEFAULT_BPM);
        app->timeline.timeline_beats_per_bar = next.tempo_calibrated && next.beats_per_bar > 0 ? next.beats_per_bar : 4;
        app->timeline.timeline_beat_unit = next.tempo_calibrated && next.beat_unit > 0 ? next.beat_unit : 4;
        app->timeline.ticks_per_beat = app->transport.ppqn > 0 ? app->transport.ppqn : 960;
        app->timeline.tempo_event_count = 0;
        timeline_set_tempo_event_no_lock(&app->timeline, 0, app->timeline.timeline_bpm);
        timeline_init_lanes(&app->timeline);
        TimelineLane *lane = &app->timeline.lanes[0];
        lane->instance_count = 1;
        lane->instances[0].roster_clip_index = roster_index;
        lane->instances[0].start_tick = 0;
        lane->instances[0].duration_ticks = timeline_clip_duration_ticks(app, roster_index);
        lane->instances[0].midi_note = next.midi_note;
        lane->instances[0].midi_channel = next.midi_channel;
        lane->instances[0].midi_velocity = next.midi_velocity;
        app->timeline.length_ticks = lane->instances[0].duration_ticks;
        app->timeline.playhead_tick = 0;
        app->timeline.timeline_cursor_tick = 0;
        app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
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

static bool app_capture_whole_source_to_roster(App *app) {
    if (!app || app->view_mode != APP_VIEW_WAVEFORM ||
        app->waveform_source_mode != WAVEFORM_SOURCE_WAV ||
        !app->waveform_source_path[0]) {
        app_set_status(app, "Whole-source capture is source audio only");
        return false;
    }
    if (!app->clip.samples || app->clip.frame_count == 0 ||
        app->clip.channels <= 0 || app->clip.sample_rate <= 0) {
        app_set_status(app, "No source to capture");
        return false;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        app_set_status(app, "Roster full");
        return false;
    }

    size_t channels = (size_t)app->clip.channels;
    if (app->clip.frame_count > SIZE_MAX / channels) {
        app_set_status(app, "source too large for long roster capture");
        return false;
    }
    size_t sample_count = app->clip.frame_count * channels;
    if (sample_count > SIZE_MAX / sizeof(float)) {
        app_set_status(app, "source too large for long roster capture");
        return false;
    }
    size_t byte_count = sample_count * sizeof(float);
    if (byte_count > APP_MAX_LONG_CAPTURE_BYTES) {
        double needed_mib = bytes_to_mib(byte_count);
        double limit_mib = bytes_to_mib(APP_MAX_LONG_CAPTURE_BYTES);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Long roster capture rejected: source needs %.1f MiB, limit %.1f MiB",
                    needed_mib,
                    limit_mib);
        SDL_snprintf(app->status_text,
                     sizeof(app->status_text),
                     "source too large for long roster capture (needs %.1f MiB, limit %.0f MiB)",
                     needed_mib,
                     limit_mib);
        return false;
    }

    float *samples = (float *)SDL_malloc(byte_count);
    if (!samples) {
        app_set_status(app, "Memory allocation failed");
        return false;
    }
    SDL_memcpy(samples, app->clip.samples, byte_count);

    RosterClip next;
    SDL_memset(&next, 0, sizeof(next));
    generate_stable_id("sample", next.sample_id, sizeof(next.sample_id));
    generate_stable_id("roster", next.roster_clip_id, sizeof(next.roster_clip_id));

    char source_base[APP_ROSTER_CLIP_NAME_MAX];
    capture_base_name(&app->clip, source_base, sizeof(source_base));
    char whole_base[APP_ROSTER_CLIP_NAME_MAX];
    SDL_snprintf(whole_base, sizeof(whole_base), "%s whole", source_base);
    if (roster_clip_name_exists(app, whole_base)) {
        int number = next_capture_number(app, whole_base);
        SDL_snprintf(next.name, sizeof(next.name), "%s#%03d", whole_base, number);
    } else {
        SDL_strlcpy(next.name, whole_base, sizeof(next.name));
    }

    const char *source_path = app->waveform_source_path[0] ? app->waveform_source_path : app->clip.file_path;
    SDL_strlcpy(next.source_path, source_path, sizeof(next.source_path));
    next.source_loop_start_frame = 0;
    next.source_loop_end_frame = app->clip.frame_count;
    next.source_sample_rate = app->clip.sample_rate;
    next.loop_start_frame = 0;
    next.loop_end_frame = app->clip.frame_count;
    next.sample_rate = app->clip.sample_rate;
    next.channels = app->clip.channels;
    next.frame_count = app->clip.frame_count;
    next.samples = samples;
    next.midi_note = app_next_available_midi_note(app);
    next.midi_channel = 0;
    next.midi_velocity = 127;
    next.color = roster_color_for_append(app);

    if (app->clip.has_full_source_tempo_metadata) {
        TempoLockParams tempo = app->clip.full_source_tempo_metadata;
        next.tempo_calibrated = tempo.bpm > 0.0 &&
            tempo.beats_per_bar > 0 &&
            tempo.beat_unit > 0 &&
            tempo.target_bars > 0.0;
        if (next.tempo_calibrated) {
            next.source_bpm = timeline_clamp_bpm(tempo.bpm);
            next.beats_per_bar = tempo.beats_per_bar;
            next.beat_unit = tempo.beat_unit;
            next.target_bars = tempo.target_bars;
            next.target_beats = tempo.target_bars * (double)tempo.beats_per_bar;
            next.downbeat_offset_frames = tempo.downbeat_frame < next.frame_count ? tempo.downbeat_frame : 0;
        }
    }
    if (!next.tempo_calibrated) {
        next.source_bpm = 0.0;
        next.beats_per_bar = 4;
        next.beat_unit = 4;
        next.target_bars = 0.0;
        next.target_beats = 0.0;
        next.downbeat_offset_frames = 0;
    }

    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        SDL_free(next.samples);
        next.samples = NULL;
        app_set_status(app, "Could not lock audio stream");
        return false;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        SDL_free(next.samples);
        next.samples = NULL;
        app_set_status(app, "Roster full");
        return false;
    }

    int roster_index = app->roster_clip_count;
    app->roster[roster_index] = next;
    app->roster_clip_count++;
    app->selected_roster_clip = roster_index;
    app->selected_roster_clip_armed = false;
    app_clamp_roster_scroll(app, app->roster_visible_rows);
    app_ensure_roster_timeline_surface_no_lock(app);
    app->timeline_focus_zone = TIMELINE_FOCUS_ROSTER;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Long roster capture copied %.1f MiB into roster clip %d",
                bytes_to_mib(byte_count),
                roster_index);
    SDL_snprintf(app->status_text,
                 sizeof(app->status_text),
                 "Captured %s to roster (%.1f MiB)",
                 app->roster[roster_index].name,
                 bytes_to_mib(byte_count));
    return true;
}

static void waveform_render_name_suffix(WaveformRenderDialogMode dialog_mode,
                                        double source_bpm,
                                        double value,
                                        char *out,
                                        size_t out_size) {
    if (!out || out_size == 0) return;
    switch (dialog_mode) {
        case WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM:
            SDL_snprintf(out, out_size, "tempo %.2fto%.2f", source_bpm, value);
            break;
        case WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT:
            SDL_snprintf(out, out_size, "tempo %+.2f", value);
            break;
        case WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES:
            SDL_snprintf(out, out_size, "pitch %+.2fst", value);
            break;
        case WAVEFORM_RENDER_DIALOG_RATE_PERCENT:
            SDL_snprintf(out, out_size, "rate %+.2f", value);
            break;
        default:
            SDL_strlcpy(out, "render", out_size);
            break;
    }
}

static bool app_render_current_loop_to_roster(App *app, WaveformRenderDialogMode dialog_mode, double value) {
    CurrentLoopCapture capture;
    if (!app_prepare_current_loop_capture(app, &capture, true)) return false;

    VpStretchRequest request;
    request.sample_rate = app->clip.sample_rate;
    request.channels = app->clip.channels;

    double render_amount = value;
    switch (dialog_mode) {
        case WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM:
            if (!capture.tempo_calibrated || capture.tempo.bpm <= 0.0 || value <= 0.0) {
                current_loop_capture_destroy(&capture);
                app_set_status(app, "No calibrated source BPM");
                return false;
            }
            render_amount = (value / capture.tempo.bpm - 1.0) * 100.0;
            request.mode = VP_STRETCH_TEMPO_PERCENT;
            break;
        case WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT:
            request.mode = VP_STRETCH_TEMPO_PERCENT;
            break;
        case WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES:
            request.mode = VP_STRETCH_PITCH_SEMITONES;
            break;
        case WAVEFORM_RENDER_DIALOG_RATE_PERCENT:
            request.mode = VP_STRETCH_RATE_PERCENT;
            break;
        default:
            current_loop_capture_destroy(&capture);
            app_set_status(app, "Invalid render mode");
            return false;
    }
    request.amount = (float)render_amount;

    float *rendered = NULL;
    int64_t rendered_frames_i64 = 0;
    int result = vp_soundtouch_render_f32(capture.samples,
                                          (int64_t)capture.frame_count,
                                          &request,
                                          &rendered,
                                          &rendered_frames_i64);
    if (result != 0 || !rendered || rendered_frames_i64 <= 0) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "SoundTouch render failed");
        return false;
    }

    uint64_t rendered_frames_u64 = (uint64_t)rendered_frames_i64;
    if (rendered_frames_u64 > (uint64_t)SIZE_MAX) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Rendered clip too large");
        return false;
    }
    size_t rendered_frames = (size_t)rendered_frames_u64;
    if (rendered_frames < APP_MIN_CAPTURE_FRAMES || rendered_frames > APP_MAX_CAPTURE_FRAMES) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Rendered clip is too short or too large");
        return false;
    }

    size_t channels = (size_t)app->clip.channels;
    if (rendered_frames > SIZE_MAX / channels) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Rendered clip too large");
        return false;
    }
    size_t rendered_samples = rendered_frames * channels;
    if (rendered_samples > SIZE_MAX / sizeof(float)) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Rendered clip too large");
        return false;
    }
    size_t rendered_bytes = rendered_samples * sizeof(float);
    if (rendered_bytes > APP_MAX_CAPTURE_BYTES) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Rendered clip too large");
        return false;
    }

    float *roster_samples = (float *)SDL_malloc(rendered_bytes);
    if (!roster_samples) {
        vp_soundtouch_free(rendered);
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Memory allocation failed");
        return false;
    }
    SDL_memcpy(roster_samples, rendered, rendered_bytes);
    vp_soundtouch_free(rendered);
    rendered = NULL;

    RosterClip next;
    SDL_memset(&next, 0, sizeof(next));
    generate_stable_id("sample", next.sample_id, sizeof(next.sample_id));
    generate_stable_id("roster", next.roster_clip_id, sizeof(next.roster_clip_id));
    char base[APP_ROSTER_CLIP_NAME_MAX];
    if (app->waveform_source_mode == WAVEFORM_SOURCE_ROSTER && app->waveform_source_name[0]) {
        SDL_strlcpy(base, app->waveform_source_name, sizeof(base));
    } else {
        capture_base_name(&app->clip, base, sizeof(base));
    }
    char suffix[64];
    waveform_render_name_suffix(dialog_mode,
                                capture.tempo_calibrated ? capture.tempo.bpm : 0.0,
                                value,
                                suffix,
                                sizeof(suffix));
    SDL_snprintf(next.name, sizeof(next.name), "%s %s", base, suffix);
    const char *source_path = app->waveform_source_path[0] ? app->waveform_source_path : app->clip.file_path;
    SDL_strlcpy(next.source_path, source_path, sizeof(next.source_path));
    next.source_loop_start_frame = app->waveform_source_offset_frame + capture.start_frame;
    next.source_loop_end_frame = app->waveform_source_offset_frame + capture.end_frame;
    next.source_sample_rate = app->clip.sample_rate;
    next.loop_start_frame = 0;
    next.loop_end_frame = rendered_frames;
    next.sample_rate = app->clip.sample_rate;
    next.channels = app->clip.channels;
    next.frame_count = rendered_frames;
    next.samples = roster_samples;
    next.midi_note = app_next_available_midi_note(app);
    next.midi_channel = 0;
    next.midi_velocity = 127;
    next.color = roster_color_for_append(app);

    if (capture.tempo_calibrated) {
        double output_bpm = capture.tempo.bpm;
        switch (dialog_mode) {
            case WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM:
                output_bpm = value;
                break;
            case WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT:
            case WAVEFORM_RENDER_DIALOG_RATE_PERCENT:
                output_bpm = capture.tempo.bpm * (1.0 + value / 100.0);
                break;
            case WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES:
            default:
                output_bpm = capture.tempo.bpm;
                break;
        }
        next.tempo_calibrated = output_bpm > 0.0 &&
            capture.tempo.beats_per_bar > 0 &&
            capture.tempo.beat_unit > 0 &&
            capture.tempo.target_bars > 0.0;
        next.source_bpm = next.tempo_calibrated ? output_bpm : 0.0;
        next.beats_per_bar = next.tempo_calibrated ? capture.tempo.beats_per_bar : 4;
        next.beat_unit = next.tempo_calibrated ? capture.tempo.beat_unit : 4;
        next.target_bars = next.tempo_calibrated ? capture.tempo.target_bars : 0.0;
        next.target_beats = next.target_bars > 0.0 ? next.target_bars * (double)next.beats_per_bar : 0.0;
        if (next.tempo_calibrated &&
            capture.tempo.downbeat_frame > capture.start_frame &&
            capture.frame_count > 0) {
            size_t input_offset = capture.tempo.downbeat_frame - capture.start_frame;
            double scale = (double)rendered_frames / (double)capture.frame_count;
            size_t output_offset = (size_t)llround((double)input_offset * scale);
            next.downbeat_offset_frames = output_offset < rendered_frames ? output_offset : rendered_frames - 1;
        } else {
            next.downbeat_offset_frames = 0;
        }
    } else {
        next.tempo_calibrated = false;
        next.source_bpm = 0.0;
        next.beats_per_bar = 4;
        next.beat_unit = 4;
        next.target_bars = 0.0;
        next.target_beats = 0.0;
        next.downbeat_offset_frames = 0;
    }

    app_stop_active_audio(app);
    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        SDL_free(next.samples);
        next.samples = NULL;
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Could not lock audio stream");
        return false;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        SDL_free(next.samples);
        next.samples = NULL;
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Roster full");
        return false;
    }

    int roster_index = app->roster_clip_count;
    app->roster[roster_index] = next;
    app->roster_clip_count++;
    app->selected_roster_clip = roster_index;
    app->selected_roster_clip_armed = false;
    if (!app->timeline.initialized) {
        app->timeline.initialized = true;
        app->timeline.timeline_bpm = next.tempo_calibrated && next.source_bpm > 0.0 ?
            next.source_bpm :
            (app->transport.bpm > 0.0 ? app->transport.bpm : TIMELINE_DEFAULT_BPM);
        app->timeline.timeline_beats_per_bar = next.tempo_calibrated && next.beats_per_bar > 0 ? next.beats_per_bar : 4;
        app->timeline.timeline_beat_unit = next.tempo_calibrated && next.beat_unit > 0 ? next.beat_unit : 4;
        app->timeline.ticks_per_beat = app->transport.ppqn > 0 ? app->transport.ppqn : 960;
        app->timeline.tempo_event_count = 0;
        timeline_set_tempo_event_no_lock(&app->timeline, 0, app->timeline.timeline_bpm);
        timeline_init_lanes(&app->timeline);
        TimelineLane *lane = &app->timeline.lanes[0];
        lane->instance_count = 1;
        lane->instances[0].roster_clip_index = roster_index;
        lane->instances[0].start_tick = 0;
        lane->instances[0].duration_ticks = timeline_clip_duration_ticks(app, roster_index);
        lane->instances[0].midi_note = next.midi_note;
        lane->instances[0].midi_channel = next.midi_channel;
        lane->instances[0].midi_velocity = next.midi_velocity;
        app->timeline.length_ticks = lane->instances[0].duration_ticks;
        app->timeline.playhead_tick = 0;
        app->timeline.timeline_cursor_tick = 0;
        app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
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
    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }

    sync_timeline_play_range_no_lock(app);
    clamp_timeline_view(app);
    current_loop_capture_destroy(&capture);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Rendered %s to roster", app->roster[roster_index].name);
    return true;
}

static void app_replace_source_roster_clip_from_waveform(App *app) {
    if (!app || app->waveform_source_mode != WAVEFORM_SOURCE_ROSTER ||
        app->waveform_source_roster_index < 0 ||
        app->waveform_source_roster_index >= app->roster_clip_count) {
        app_set_status(app, "No source roster clip");
        return;
    }

    int roster_index = app->waveform_source_roster_index;
    CurrentLoopCapture capture;
    if (!app_prepare_current_loop_capture(app, &capture, false)) return;

    app_stop_active_audio(app);
    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        current_loop_capture_destroy(&capture);
        app_set_status(app, "Could not lock audio stream");
        return;
    }

    RosterClip *clip = &app->roster[roster_index];
    SDL_free(clip->samples);
    clip->samples = capture.samples;
    capture.samples = NULL;
    clip->sample_rate = app->clip.sample_rate;
    clip->channels = app->clip.channels;
    clip->frame_count = capture.frame_count;
    clip->source_sample_rate = app->clip.sample_rate;
    clip->source_loop_start_frame = app->waveform_source_offset_frame + capture.start_frame;
    clip->source_loop_end_frame = app->waveform_source_offset_frame + capture.end_frame;
    const char *source_path = app->waveform_source_path[0] ? app->waveform_source_path : app->clip.file_path;
    SDL_strlcpy(clip->source_path, source_path, sizeof(clip->source_path));
    clip->loop_start_frame = 0;
    clip->loop_end_frame = capture.frame_count;
    clip->tempo_calibrated = capture.tempo_calibrated;
    if (clip->tempo_calibrated) {
        clip->source_bpm = capture.tempo.bpm;
        clip->beats_per_bar = capture.tempo.beats_per_bar;
        clip->beat_unit = capture.tempo.beat_unit;
        clip->target_bars = capture.tempo.target_bars;
        clip->target_beats = capture.tempo.target_bars * (double)capture.tempo.beats_per_bar;
    } else {
        clip->source_bpm = 0.0;
        clip->beats_per_bar = 4;
        clip->beat_unit = 4;
        clip->target_bars = 0.0;
        clip->target_beats = 0.0;
    }
    if (clip->tempo_calibrated && capture.tempo.downbeat_frame > capture.start_frame) {
        size_t offset = capture.tempo.downbeat_frame - capture.start_frame;
        clip->downbeat_offset_frames = offset < capture.frame_count ? offset : capture.frame_count - 1;
    } else {
        clip->downbeat_offset_frames = 0;
    }

    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }

    app->selected_roster_clip = roster_index;
    app->roster_commit_menu_open = false;
    app->roster_commit_menu_selected = 0;
    app_open_selected_roster_clip_waveform(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Replaced roster clip: %s", app->roster[roster_index].name);
}

void app_capture_current_loop_to_roster(App *app) {
    if (app && app->waveform_source_mode == WAVEFORM_SOURCE_ROSTER &&
        app->waveform_source_roster_index >= 0 &&
        app->waveform_source_roster_index < app->roster_clip_count) {
        app_open_roster_commit_menu(app);
        return;
    }
    app_capture_current_loop_to_roster_new(app);
}

void app_open_selected_roster_clip_waveform(App *app) {
    if (app->selected_roster_clip < 0 || app->selected_roster_clip >= app->roster_clip_count) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No roster clip selected");
        return;
    }

    RosterClip *roster_clip = &app->roster[app->selected_roster_clip];
    if (!roster_clip->samples || roster_clip->frame_count < 1 ||
        roster_clip->channels <= 0 || roster_clip->sample_rate <= 0) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Invalid roster clip");
        return;
    }

    size_t sample_count = roster_clip->frame_count * (size_t)roster_clip->channels;
    if (sample_count > SIZE_MAX / sizeof(float)) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Clip too large");
        return;
    }
    size_t byte_count = sample_count * sizeof(float);
    float *samples = (float *)SDL_malloc(byte_count);
    if (!samples) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Memory allocation failed");
        return;
    }
    SDL_memcpy(samples, roster_clip->samples, byte_count);

    AudioClip next;
    SDL_memset(&next, 0, sizeof(next));
    SDL_snprintf(next.file_path, sizeof(next.file_path), "roster://%s", roster_clip->name);
    next.sample_rate = roster_clip->sample_rate;
    next.channels = roster_clip->channels;
    next.frame_count = roster_clip->frame_count;
    next.samples = samples;
    next.loop_start_frame = roster_clip->loop_start_frame < next.frame_count ? roster_clip->loop_start_frame : 0;
    next.loop_end_frame = roster_clip->loop_end_frame > next.loop_start_frame && roster_clip->loop_end_frame <= next.frame_count ?
        roster_clip->loop_end_frame :
        next.frame_count;
    if (roster_clip->tempo_calibrated && roster_clip->source_bpm > 0.0) {
        next.source_bpm = timeline_clamp_bpm(roster_clip->source_bpm);
        next.has_clip_metadata_bpm = true;
        next.clip_metadata_bpm = next.source_bpm;
        next.clip_tempo_locked = true;
        next.beats_per_bar = roster_clip->beats_per_bar > 0 ? roster_clip->beats_per_bar : 4;
        next.beat_unit = roster_clip->beat_unit > 0 ? roster_clip->beat_unit : 4;
        next.downbeat_frame = roster_clip->downbeat_offset_frames < next.frame_count ?
            roster_clip->downbeat_offset_frames : 0;
        next.tempo_lock.bpm = next.source_bpm;
        next.tempo_lock.downbeat_frame = next.downbeat_frame;
        next.tempo_lock.beats_per_bar = next.beats_per_bar;
        next.tempo_lock.beat_unit = next.beat_unit;
        next.tempo_lock.target_bars = roster_clip->target_bars > 0.0 ?
            roster_clip->target_bars :
            (roster_clip->target_beats > 0.0 ? roster_clip->target_beats / (double)next.beats_per_bar : 1.0);
    } else {
        next.source_bpm = 0.0;
        next.has_clip_metadata_bpm = false;
        next.clip_metadata_bpm = 0.0;
        next.clip_tempo_locked = false;
        next.beats_per_bar = 4;
        next.beat_unit = 4;
        next.downbeat_frame = 0;
        next.tempo_lock.bpm = 0.0;
        next.tempo_lock.downbeat_frame = 0;
        next.tempo_lock.beats_per_bar = 4;
        next.tempo_lock.beat_unit = 4;
        next.tempo_lock.target_bars = 0.0;
    }
    next.playback_rate = 1.0;
    next.gain = 1.0f;

    audio_engine_stop_timeline(&app->audio, true);
    audio_engine_stop_preview(&app->audio);
    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        clip_destroy(&next);
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Could not lock audio stream");
        return;
    }

    AudioClip old = app->clip;
    app->clip = next;
    clip_destroy(&old);
    app->view_mode = APP_VIEW_WAVEFORM;
    app->tempo_lock_mode = false;
    app->transport.playing = false;
    app->transport_bpm_manual = false;
    app->transport_bpm = app->clip.source_bpm > 0.0 ? app->clip.source_bpm :
        (app->transport.bpm > 0.0 ? app->transport.bpm : TIMELINE_DEFAULT_BPM);
    app->has_retained_tempo_lock_params = false;
    app->retained_tempo_lock_stale = false;
    app_clear_waveform_frame_grip(app);
    app_set_waveform_source_roster(app, app->selected_roster_clip, roster_clip);
    audio_engine_set_playback_mode(&app->audio, AUDIO_PLAYBACK_WAVEFORM);
    audio_engine_set_playhead(&app->audio, app->clip.loop_start_frame);
    transport_jump_to_seconds(&app->transport, 0.0);
    waveform_view_init(&app->view);
    app_timeline_clear_context_menu(app);
    sync_transport_from_app(app);

    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }

    SDL_snprintf(app->status_text, sizeof(app->status_text), "Opened %s in waveform", roster_clip->name);
}

static void app_active_sidecar_tempo_params(App *app, TempoLockParams *params) {
    if (app->tempo_lock_mode) {
        *params = app->tempo_lock_draft;
        return;
    }
    if (app->transport_bpm_manual) {
        *params = default_tempo_params(app);
        params->bpm = app->transport_bpm;
        params->downbeat_frame = app->clip.loop_start_frame;
        return;
    }
    if (app_get_active_tempo_params(app, params)) return;
    *params = default_tempo_params(app);
    params->bpm = app->transport.bpm > 0.0 ? app->transport.bpm : app->transport_bpm;
    params->downbeat_frame = app->clip.downbeat_frame;
}

void app_request_write_tempo_sidecar(App *app) {
    if (app->view_mode != APP_VIEW_WAVEFORM) return;
    if (app->waveform_source_mode != WAVEFORM_SOURCE_WAV || !app->waveform_source_path[0]) {
        app_set_status(app, "Sidecar write is source audio only");
        return;
    }
    app->waveform_sidecar_confirm_open = true;
    app->sample_selector_open = false;
    app_set_status(app, "Confirm tempo sidecar write");
}

void app_cancel_write_tempo_sidecar(App *app) {
    app->waveform_sidecar_confirm_open = false;
    app_set_status(app, "Tempo sidecar write cancelled");
}

void app_confirm_write_tempo_sidecar(App *app) {
    if (!app->waveform_sidecar_confirm_open) return;
    app->waveform_sidecar_confirm_open = false;
    if (app->waveform_source_mode != WAVEFORM_SOURCE_WAV || !app->waveform_source_path[0]) {
        app_set_status(app, "Sidecar write is source audio only");
        return;
    }

    TempoLockParams params;
    app_active_sidecar_tempo_params(app, &params);
    clamp_tempo_params(app, &params);

    char sidecar[CLIP_MAX_PATH + 6];
    SDL_snprintf(sidecar, sizeof(sidecar), "%s.json", app->waveform_source_path);

    char json[512];
    if (params.downbeat_frame > 0) {
        SDL_snprintf(json, sizeof(json),
                     "{\n  \"bpm\": %.6g,\n  \"beats_per_bar\": %d,\n  \"beat_unit\": %d,\n  \"target_bars\": %.6g,\n  \"downbeat_frame\": %llu\n}\n",
                     params.bpm,
                     params.beats_per_bar,
                     params.beat_unit,
                     params.target_bars,
                     (unsigned long long)params.downbeat_frame);
    } else {
        SDL_snprintf(json, sizeof(json),
                     "{\n  \"bpm\": %.6g,\n  \"beats_per_bar\": %d,\n  \"beat_unit\": %d,\n  \"target_bars\": %.6g\n}\n",
                     params.bpm,
                     params.beats_per_bar,
                     params.beat_unit,
                     params.target_bars);
    }

    SDL_IOStream *io = SDL_IOFromFile(sidecar, "wb");
    if (!io) {
        app_set_status(app, "Could not write tempo sidecar");
        return;
    }
    size_t len = SDL_strlen(json);
    bool ok = SDL_WriteIO(io, json, len) == len;
    ok = SDL_CloseIO(io) && ok;
    if (!ok) {
        app_set_status(app, "Could not write tempo sidecar");
        return;
    }
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Wrote %s", sidecar);
}

void app_toggle_view_mode(App *app) {
    if (app->view_mode == APP_VIEW_WAVEFORM) {
        app->transport.playing = false;
        app_clear_waveform_frame_grip(app);
        audio_engine_set_playback_mode(&app->audio, AUDIO_PLAYBACK_TIMELINE);
        app->view_mode = APP_VIEW_TIMELINE;
        app->tempo_lock_mode = false;
        app_timeline_clear_context_menu(app);
        sync_transport_from_app(app);
    } else if (app->view_mode == APP_VIEW_TIMELINE) {
        app->view_mode = APP_VIEW_MASTER_MIX;
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Master Mix");
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

void app_master_mix_return_to_timeline(App *app) {
    if (!app || app->view_mode != APP_VIEW_MASTER_MIX) return;
    app->view_mode = APP_VIEW_TIMELINE;
    app_set_status(app, "Timeline");
}

void app_master_mix_cycle_focus(App *app, int direction) {
    if (!app || direction == 0) return;
    int focus = (int)app->master_mix_focus + direction;
    while (focus < 0) focus += (int)MASTER_MIX_FOCUS_COUNT;
    focus %= (int)MASTER_MIX_FOCUS_COUNT;
    app->master_mix_focus = (MasterMixFocusSection)focus;
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Master Mix: %s",
                 master_mix_focus_label(app->master_mix_focus));
}

static float app_master_reverb_param_value(const MasterReverbParams *params, MasterReverbParamId param) {
    if (!params) return 0.0f;
    switch (param) {
        case MASTER_REVERB_PARAM_ENABLED: return params->enabled ? 1.0f : 0.0f;
        case MASTER_REVERB_PARAM_SEND: return params->send;
        case MASTER_REVERB_PARAM_RETURN: return params->return_gain;
        case MASTER_REVERB_PARAM_PREDELAY_MS: return params->predelay_ms;
        case MASTER_REVERB_PARAM_DECAY_SECONDS: return params->decay_seconds;
        case MASTER_REVERB_PARAM_SIZE: return params->size;
        case MASTER_REVERB_PARAM_DIFFUSION: return params->diffusion;
        case MASTER_REVERB_PARAM_DAMPING: return params->damping;
        case MASTER_REVERB_PARAM_LOW_CUT_HZ: return params->low_cut_hz;
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ: return params->high_cut_hz;
        case MASTER_REVERB_PARAM_WIDTH: return params->width;
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS: return params->mod_depth_ms;
        case MASTER_REVERB_PARAM_MOD_RATE_HZ: return params->mod_rate_hz;
        case MASTER_REVERB_PARAM_COUNT:
        default: return 0.0f;
    }
}

static float app_master_reverb_param_step(MasterReverbParamId param, bool fine) {
    switch (param) {
        case MASTER_REVERB_PARAM_SEND:
        case MASTER_REVERB_PARAM_RETURN:
        case MASTER_REVERB_PARAM_SIZE:
        case MASTER_REVERB_PARAM_DIFFUSION:
        case MASTER_REVERB_PARAM_DAMPING:
        case MASTER_REVERB_PARAM_WIDTH:
            return fine ? 0.01f : 0.05f;
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS:
            return fine ? 0.10f : 0.50f;
        case MASTER_REVERB_PARAM_MOD_RATE_HZ:
            return fine ? 0.01f : 0.05f;
        case MASTER_REVERB_PARAM_PREDELAY_MS:
            return fine ? 1.0f : 5.0f;
        case MASTER_REVERB_PARAM_DECAY_SECONDS:
            return fine ? 0.05f : 0.25f;
        case MASTER_REVERB_PARAM_LOW_CUT_HZ:
            return fine ? 5.0f : 20.0f;
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ:
            return fine ? 100.0f : 500.0f;
        case MASTER_REVERB_PARAM_ENABLED:
        case MASTER_REVERB_PARAM_COUNT:
        default:
            return 1.0f;
    }
}

void app_master_reverb_select_param_delta(App *app, int delta) {
    if (!app || delta == 0) return;
    int param = (int)app->master_reverb_selected_param + delta;
    while (param < 0) param += (int)MASTER_REVERB_PARAM_COUNT;
    param %= (int)MASTER_REVERB_PARAM_COUNT;
    app->master_reverb_selected_param = (MasterReverbParamId)param;
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Reverb 1: %s",
                 audio_engine_master_reverb_param_label(app->master_reverb_selected_param));
}

void app_master_reverb_adjust_param(App *app, int direction, bool fine) {
    if (!app || direction == 0) return;
    MasterReverbParamId param = app->master_reverb_selected_param;
    if (param == MASTER_REVERB_PARAM_ENABLED) {
        audio_engine_set_master_reverb_enabled(&app->audio, direction > 0);
    } else {
        MasterReverbParams target;
        audio_engine_get_master_reverb_params(&app->audio, NULL, &target);
        float value = app_master_reverb_param_value(&target, param);
        value += (float)direction * app_master_reverb_param_step(param, fine);
        audio_engine_set_master_reverb_param(&app->audio, param, value);
    }

    MasterReverbParams target_after;
    audio_engine_get_master_reverb_params(&app->audio, NULL, &target_after);
    if (param == MASTER_REVERB_PARAM_ENABLED) {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Reverb 1 %s",
                     target_after.enabled ? "enabled" : "bypassed");
    } else {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Reverb 1: %s %.2f",
                     audio_engine_master_reverb_param_label(param),
                     app_master_reverb_param_value(&target_after, param));
    }
}

void app_master_reverb_activate_selected(App *app) {
    if (!app) return;
    if (app->master_reverb_selected_param == MASTER_REVERB_PARAM_ENABLED) {
        audio_engine_toggle_master_reverb(&app->audio);
        MasterReverbParams target;
        audio_engine_get_master_reverb_params(&app->audio, NULL, &target);
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Reverb 1 %s",
                     target.enabled ? "enabled" : "bypassed");
    }
}

void app_master_reverb_clear_tail(App *app) {
    if (!app) return;
    audio_engine_clear_master_reverb_tail(&app->audio);
    app_set_status(app, "Reverb 1 tail cleared");
}

void app_toggle_controls_legend(App *app) {
    app->controls_legend_open = !app->controls_legend_open;
}

void app_toggle_debug_overlay(App *app) {
    if (!app) return;
    switch (app->debug_overlay_mode) {
        case APP_DEBUG_OVERLAY_GAMEPAD_STATS:
            app->debug_overlay_mode = APP_DEBUG_OVERLAY_OFF;
            app_set_status(app, "Debug overlay off");
            break;
        case APP_DEBUG_OVERLAY_OFF:
            app->debug_overlay_mode = APP_DEBUG_OVERLAY_GAMEPAD;
            app_set_status(app, "Debug overlay: gamepad");
            break;
        case APP_DEBUG_OVERLAY_GAMEPAD:
        default:
            app->debug_overlay_mode = APP_DEBUG_OVERLAY_GAMEPAD_STATS;
            app_set_status(app, "Debug overlay: gamepad + stats");
            break;
    }
}

bool app_stop_active_audio(App *app) {
    if (!app) return false;

    bool stopped = false;
    if (audio_engine_timeline_is_playing(&app->audio)) {
        audio_engine_stop_timeline(&app->audio, false);
        sync_transport_from_app(app);
        stopped = true;
    }
    if (app->audio.preview_active) {
        audio_engine_stop_preview(&app->audio);
        stopped = true;
    }
    if (app->audio.file_preview_active) {
        app_project_browser_clear_preview(app);
        stopped = true;
    }
    if (app->audio.playback_mode == AUDIO_PLAYBACK_WAVEFORM && app->transport.playing) {
        app->transport.playing = false;
        app->transport.metronome_env = 0.0f;
        stopped = true;
    }

    if (stopped) {
        if (app->audio.stream) {
            SDL_LockAudioStream(app->audio.stream);
            SDL_ClearAudioStream(app->audio.stream);
            SDL_UnlockAudioStream(app->audio.stream);
        }
        app_set_status(app, "Audio stopped");
    }
    return stopped;
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
        audio_engine_stop_timeline(&app->audio, false);
        sync_transport_from_app(app);
        app_set_status(app, "Timeline paused");
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
    app->timeline.timeline_cursor_seam_side = timeline_default_seam_side_for_tick(&app->timeline, range_start);
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
    app->timeline.timeline_cursor_seam_side = timeline_default_seam_side_for_tick(&app->timeline, range_start);
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
    int64_t snap = timeline_edit_snap_ticks(app);
    timeline_nudge_tick_with_seams(&app->timeline,
                                   &app->timeline.timeline_cursor_tick,
                                   &app->timeline.timeline_cursor_seam_side,
                                   direction,
                                   snap,
                                   true);
}

void app_timeline_move_cursor_by_bar(App *app, int direction) {
    if (direction == 0) return;
    sync_timeline_play_range(app);
    int beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    int64_t bar_ticks = timeline_snap_ticks(&app->timeline) * (int64_t)beats_per_bar;
    if (bar_ticks < 1) bar_ticks = 1;
    timeline_nudge_tick_with_seams(&app->timeline,
                                   &app->timeline.timeline_cursor_tick,
                                   &app->timeline.timeline_cursor_seam_side,
                                   direction,
                                   bar_ticks,
                                   false);
}

void app_timeline_select_play_range_handle(App *app, TimelineRangeHandle handle) {
    app->timeline_play_range_handle = handle;
    app->timeline_focus_zone = TIMELINE_FOCUS_PLAY_RANGE;
    app_timeline_clear_context_menu(app);
    app_set_status(app, handle == TIMELINE_RANGE_HANDLE_START ? "Play range start handle" : "Play range end handle");
}

void app_timeline_nudge_play_range(App *app, int direction, bool by_bar) {
    if (direction == 0) return;
    int64_t length = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
    if (length <= 0) {
        app_set_status(app, "timeline empty");
        return;
    }

    int64_t snap = by_bar ? timeline_snap_ticks(&app->timeline) : timeline_edit_snap_ticks(app);
    if (by_bar) {
        int beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
        snap *= (int64_t)beats_per_bar;
    }
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

void app_timeline_fit_play_range_view(App *app) {
    if (!app || !app->timeline.initialized || app->timeline.length_ticks <= 0) {
        if (app) app_set_status(app, "timeline empty");
        return;
    }
    int64_t start = 0, end = 0;
    timeline_effective_play_range(&app->timeline, &start, &end);
    if (end <= start) {
        app_set_status(app, "timeline empty");
        return;
    }
    app_timeline_fit_play_range_anchors(app);
    app_set_status(app, "Fit play range");
}

void app_timeline_select_roster_delta(App *app, int delta) {
    if (app_selected_timeline_lane_is_drum(app)) {
        if (app->drum_pattern_count <= 0) {
            app->selected_drum_pattern = -1;
            app->selected_drum_pattern_armed = false;
            app_set_status(app, "pattern roster empty");
            return;
        }
        if (app->selected_drum_pattern < 0) app->selected_drum_pattern = 0;
        app->selected_drum_pattern += delta;
        if (app->selected_drum_pattern < 0) app->selected_drum_pattern = 0;
        if (app->selected_drum_pattern >= app->drum_pattern_count) {
            app->selected_drum_pattern = app->drum_pattern_count - 1;
        }
        app->selected_drum_pattern_armed = false;
        return;
    }
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
    app_clamp_roster_scroll(app, app->roster_visible_rows);
    app->selected_roster_clip_armed = false;
}

void app_timeline_select_lane_delta(App *app, int delta) {
    if (delta == 0) return;
    app->selected_timeline_lane = clamp_int(app->selected_timeline_lane + delta, 0, TIMELINE_MAX_LANES - 1);
    app->selected_roster_clip_armed = false;
    app->selected_drum_pattern_armed = false;
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Track lane %d", app->selected_timeline_lane + 1);
}

void app_timeline_nudge_edit_ghost(App *app, int direction) {
    if (direction == 0 || app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    int64_t snap = timeline_edit_snap_ticks(app);
    timeline_nudge_tick_with_seams(&app->timeline,
                                   &app->timeline_edit_ghost_start_tick,
                                   &app->timeline_edit_ghost_seam_side,
                                   direction,
                                   snap,
                                   true);
    timeline_clamp_ghost_start(app);
    timeline_update_ghost_valid(app);
    if (!app->timeline_edit_ghost_valid) app_set_status(app, "overlap blocked");
    else app_set_timeline_edit_status(app);
}

void app_timeline_nudge_edit_ghost_by_bar(App *app, int direction) {
    if (direction == 0 || app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    int beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    int64_t bar_ticks = timeline_snap_ticks(&app->timeline) * (int64_t)beats_per_bar;
    if (bar_ticks < 1) bar_ticks = 1;
    timeline_nudge_tick_with_seams(&app->timeline,
                                   &app->timeline_edit_ghost_start_tick,
                                   &app->timeline_edit_ghost_seam_side,
                                   direction,
                                   bar_ticks,
                                   false);
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
    int64_t tolerance = timeline_edit_snap_ticks(app);
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

    bool insert_pulse = app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP &&
                        app->timeline_edit_placement_mode == TIMELINE_INSERT_PULSE;
    int64_t old_playhead = audio_engine_get_timeline_playhead_tick(&app->audio);
    int64_t new_playhead = old_playhead;
    if (insert_pulse) {
        audio_engine_stop_timeline(&app->audio, false);
        audio_engine_stop_preview(&app->audio);
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
        app->timeline.timeline_cursor_seam_side = app->timeline_edit_ghost_seam_side;
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
            (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP &&
             (app->timeline_edit_roster_clip_index < 0 ||
              app->timeline_edit_roster_clip_index >= app->roster_clip_count)) ||
            (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN &&
             !app_pattern_index_valid(app, app->timeline_edit_pattern_index))) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app_set_status(app, "Invalid placement");
            return;
        }
        TimelineLane *lane = &app->timeline.lanes[app->timeline_edit_ghost_lane];
        if ((app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP && lane->type != TIMELINE_LANE_AUDIO) ||
            (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN && lane->type != TIMELINE_LANE_DRUMS)) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app_set_status(app, "Lane type mismatch");
            return;
        }
        if (lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) {
            if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
            app_set_status(app, "lane full");
            return;
        }
        RosterClip *clip = app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP ?
            &app->roster[app->timeline_edit_roster_clip_index] : NULL;
        int64_t old_length = app->timeline.length_ticks > 0 ? app->timeline.length_ticks : 0;
        int64_t length_floor = old_length;
        const char *pulse_error = NULL;
        if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP &&
            app->timeline_edit_placement_mode == TIMELINE_PLACE_PULSE) {
            double bpm = timeline_roster_clip_source_bpm(app, app->timeline_edit_roster_clip_index);
            if (bpm <= 0.0) {
                if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
                app_set_status(app, "clip has no BPM");
                return;
            }
            if (!timeline_place_pulse_no_lock(app,
                                              app->timeline_edit_ghost_start_tick,
                                              app->timeline_edit_ghost_seam_side,
                                              bpm,
                                              &pulse_error)) {
                if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
                app_set_status(app, pulse_error ? pulse_error : "Pulse placement blocked");
                return;
            }
            app->timeline_edit_ghost_seam_side = timeline_default_seam_side_for_tick(&app->timeline,
                                                                                     app->timeline_edit_ghost_start_tick);
        } else if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP &&
                   app->timeline_edit_placement_mode == TIMELINE_INSERT_PULSE) {
            double bpm = timeline_roster_clip_source_bpm(app, app->timeline_edit_roster_clip_index);
            if (bpm <= 0.0) {
                if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
                app_set_status(app, "clip has no BPM");
                return;
            }
            if (!timeline_insert_pulse_no_lock(app,
                                               app->timeline_edit_ghost_start_tick,
                                               app->timeline_edit_ghost_seam_side,
                                               app->timeline_edit_duration_ticks,
                                               bpm,
                                               old_playhead,
                                               &new_playhead,
                                               &pulse_error)) {
                if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
                app_set_status(app, pulse_error ? pulse_error : "Pulse insert blocked");
                return;
            }
            app->timeline_edit_ghost_seam_side = timeline_default_seam_side_for_tick(&app->timeline,
                                                                                     app->timeline_edit_ghost_start_tick);
            length_floor = old_length + app->timeline_edit_duration_ticks;
        }

        int index = lane->instance_count++;
        TimelineInstance *instance = &lane->instances[index];
        instance->kind = app->timeline_edit_instance_kind;
        instance->roster_clip_index = app->timeline_edit_instance_kind == TIMELINE_INSTANCE_AUDIO_CLIP ?
            app->timeline_edit_roster_clip_index : -1;
        instance->pattern_index = app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN ?
            app->timeline_edit_pattern_index : -1;
        instance->start_tick = app->timeline_edit_ghost_start_tick;
        instance->duration_ticks = app->timeline_edit_duration_ticks;
        if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
            DrumPattern *pattern = &app->drum_patterns[app->timeline_edit_pattern_index];
            instance->midi_note = clamp_int(pattern->locator_note, 0, 127);
            instance->midi_channel = clamp_int(pattern->locator_channel, 0, 15);
        } else {
            instance->midi_note = clip->midi_note;
            instance->midi_channel = clip->midi_channel;
        }
        instance->midi_velocity = app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN ?
            127 : clip->midi_velocity;
        app->timeline.timeline_cursor_tick = app->timeline_edit_ghost_start_tick;
        app->timeline.timeline_cursor_seam_side = app->timeline_edit_ghost_seam_side;
        app->selected_timeline_lane = app->timeline_edit_ghost_lane;
        app->selected_timeline_instance = (TimelineInstanceRef){ app->timeline_edit_ghost_lane, index };
        recompute_timeline_length_no_lock(app);
        if (app->timeline.length_ticks < length_floor) app->timeline.length_ticks = length_floor;
        sync_timeline_play_range_no_lock(app);
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
        if (insert_pulse) audio_engine_set_timeline_playhead(&app->audio, new_playhead);
        TimelinePlacementMode completed_mode = app->timeline_edit_placement_mode;
        app->timeline_edit_mode = TIMELINE_EDIT_NONE;
        app->timeline_edit_placement_mode = TIMELINE_PLACE_FREE;
        if (completed_mode == TIMELINE_INSERT_PULSE) app_set_status(app, "Pulse inserted");
        else if (completed_mode == TIMELINE_PLACE_PULSE) app_set_status(app, "Pulse placed");
        else if (instance->kind == TIMELINE_INSTANCE_DRUM_PATTERN) app_set_status(app, "Pattern placed");
        else app_set_status(app, "Clip placed");
    }
}

static void app_timeline_cancel_edit_mode(App *app) {
    if (app->timeline_edit_mode == TIMELINE_EDIT_NONE) return;
    if (app->timeline_edit_mode == TIMELINE_EDIT_MOVE_INSTANCE) {
        app->timeline.timeline_cursor_tick = app->timeline_edit_original_start_tick;
        app->timeline.timeline_cursor_seam_side = app->timeline_edit_original_seam_side;
        app->selected_timeline_lane = app->timeline_edit_original_lane;
        app_set_status(app, "Move cancelled");
    } else {
        app_set_status(app, "Placement cancelled");
    }
    app->timeline_edit_mode = TIMELINE_EDIT_NONE;
    app->timeline_edit_placement_mode = TIMELINE_PLACE_FREE;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_instance_kind = TIMELINE_INSTANCE_AUDIO_CLIP;
    app->timeline_edit_roster_clip_index = -1;
    app->timeline_edit_pattern_index = -1;
    app->timeline_edit_ghost_lane = 0;
    app->timeline_edit_original_seam_side = TIMELINE_SEAM_NONE;
    app->timeline_edit_ghost_seam_side = TIMELINE_SEAM_NONE;
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
            if (app_selected_timeline_lane_is_drum(app)) {
                if (app->drum_pattern_count <= 0) {
                    app_create_drum_pattern(app);
                }
                if (app->selected_drum_pattern < 0 && app->drum_pattern_count > 0) {
                    app->selected_drum_pattern = 0;
                    app->selected_drum_pattern_armed = false;
                }
                if (app_pattern_index_valid(app, app->selected_drum_pattern)) {
                    if (!app->selected_drum_pattern_armed) {
                        app->selected_drum_pattern_armed = true;
                        SDL_snprintf(app->status_text,
                                     sizeof(app->status_text),
                                     "Selected %s",
                                     app->drum_patterns[app->selected_drum_pattern].name);
                    } else {
                        timeline_enter_place_pattern(app);
                    }
                } else {
                    app_set_status(app, "pattern roster empty");
                }
                break;
            }
            if (app->selected_roster_clip < 0 && app->roster_clip_count > 0) {
                app->selected_roster_clip = 0;
                app->selected_roster_clip_armed = false;
            }
            if (app->selected_roster_clip >= 0 && app->selected_roster_clip < app->roster_clip_count) {
                if (!app->selected_roster_clip_armed) {
                    app->selected_roster_clip_armed = true;
                    SDL_snprintf(app->status_text, sizeof(app->status_text), "Selected %s", app->roster[app->selected_roster_clip].name);
                } else {
                    timeline_enter_place_clip(app, TIMELINE_PLACE_FREE);
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
    if (app->project_menu_open) {
        app_project_menu_close(app);
        return;
    }
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

void app_project_menu_open(App *app) {
    if (!app || app->view_mode != APP_VIEW_TIMELINE) return;
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE || app->timeline_play_range_adjusting) {
        app_timeline_cancel_focus(app);
        return;
    }
    app_timeline_clear_context_menu(app);
    app->project_menu_open = true;
    app->project_menu_selected = 0;
    app_set_status(app, "Project menu");
}

void app_project_menu_close(App *app) {
    if (!app) return;
    app->project_menu_open = false;
    app->project_menu_selected = 0;
    app_set_status(app, "Project menu closed");
}

void app_project_menu_move(App *app, int delta) {
    if (!app || !app->project_menu_open || delta == 0) return;
    int selected = app->project_menu_selected + delta;
    while (selected < 0) selected += (int)PROJECT_MENU_ITEM_COUNT;
    selected %= (int)PROJECT_MENU_ITEM_COUNT;
    app->project_menu_selected = selected;
}

void app_project_menu_apply(App *app) {
    if (!app || !app->project_menu_open) return;
    ProjectMenuItem item = (ProjectMenuItem)clamp_int(app->project_menu_selected, 0, PROJECT_MENU_ITEM_COUNT - 1);
    switch (item) {
        case PROJECT_MENU_ITEM_SAVE:
            app_text_entry_open(app,
                                APP_TEXT_ENTRY_DISPLAY_NAME,
                                APP_TEXT_ENTRY_ACTION_SAVE_AS_PROJECT,
                                "SAVE AS",
                                "Name",
                                app->project_name[0] ? app->project_name : "",
                                APP_SAMPLE_NAME_MAX - 1);
            break;
        case PROJECT_MENU_ITEM_OPEN:
            app_project_browser_open(app);
            break;
        case PROJECT_MENU_ITEM_EXPORT_TIMELINE_WAV:
        {
            char initial[APP_SAMPLE_NAME_MAX];
            safe_project_render_stem(app, initial, sizeof(initial));
            app_text_entry_open(app,
                                APP_TEXT_ENTRY_FILENAME_SAFE,
                                APP_TEXT_ENTRY_ACTION_EXPORT_TIMELINE_WAV,
                                "EXPORT TIMELINE WAV",
                                "File",
                                initial,
                                APP_SAMPLE_NAME_MAX - 1);
            break;
        }
        case PROJECT_MENU_ITEM_QUIT:
            app->running = false;
            break;
        case PROJECT_MENU_ITEM_COUNT:
        default:
            break;
    }
}

void app_waveform_menu_open(App *app) {
    if (!app || app->view_mode != APP_VIEW_WAVEFORM) return;
    app->waveform_menu_open = true;
    app->waveform_menu_selected = 0;
    app->waveform_render_dialog_open = false;
    app->sample_selector_open = false;
    app->project_menu_open = false;
    app_timeline_clear_context_menu(app);
    app_set_status(app, "Waveform menu");
}

void app_waveform_menu_close(App *app) {
    if (!app) return;
    app->waveform_menu_open = false;
    app->waveform_menu_selected = 0;
    app_set_status(app, "Waveform menu closed");
}

static const double waveform_render_steps[] = { 0.01, 0.10, 1.00, 10.00 };

static int waveform_render_step_count(void) {
    return (int)(sizeof(waveform_render_steps) / sizeof(waveform_render_steps[0]));
}

static double waveform_render_step_for_index(int index) {
    int count = waveform_render_step_count();
    if (count <= 0) return 1.0;
    index = clamp_int(index, 0, count - 1);
    return waveform_render_steps[index];
}

static const char *waveform_render_dialog_title(WaveformRenderDialogMode mode) {
    switch (mode) {
        case WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM: return "RENDER TEMPO TO BPM";
        case WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT: return "RENDER TEMPO PERCENT";
        case WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES: return "RENDER PITCH";
        case WAVEFORM_RENDER_DIALOG_RATE_PERCENT: return "RENDER RATE";
        default: return "RENDER";
    }
}

static const char *waveform_render_dialog_unit(WaveformRenderDialogMode mode) {
    switch (mode) {
        case WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM: return "BPM";
        case WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT: return "%";
        case WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES: return "st";
        case WAVEFORM_RENDER_DIALOG_RATE_PERCENT: return "%";
        default: return "";
    }
}

static double waveform_render_dialog_clamp_value(WaveformRenderDialogMode mode, double value) {
    switch (mode) {
        case WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM:
            return clamp_double(value, TIMELINE_MIN_BPM, TIMELINE_MAX_BPM);
        case WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT:
        case WAVEFORM_RENDER_DIALOG_RATE_PERCENT:
            return clamp_double(value, -50.0, 100.0);
        case WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES:
            return clamp_double(value, -24.0, 24.0);
        default:
            return value;
    }
}

static void app_waveform_render_dialog_open_mode(App *app, WaveformRenderDialogMode mode) {
    if (!app) return;
    TempoLockParams tempo;
    SDL_memset(&tempo, 0, sizeof(tempo));
    bool calibrated = app_get_explicit_loop_calibration(app, &tempo);
    if (mode == WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM && !calibrated) {
        mode = WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT;
    }
    app->waveform_render_dialog_open = true;
    app->waveform_render_dialog_mode = mode;
    app->waveform_render_source_bpm = calibrated ? tempo.bpm : 0.0;
    app->waveform_render_value = mode == WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM ? tempo.bpm : 0.0;
    app->waveform_render_value = waveform_render_dialog_clamp_value(mode, app->waveform_render_value);
    app->waveform_render_step_index = 2;
    app->waveform_render_error[0] = '\0';
    app->waveform_menu_open = false;
    app->roster_commit_menu_open = false;
    app->sample_selector_open = false;
    app_set_status(app, waveform_render_dialog_title(mode));
}

void app_waveform_render_dialog_cancel(App *app) {
    if (!app) return;
    app->waveform_render_dialog_open = false;
    app->waveform_render_error[0] = '\0';
    app_set_status(app, "Render canceled");
}

void app_waveform_render_dialog_adjust(App *app, int direction) {
    if (!app || !app->waveform_render_dialog_open || direction == 0) return;
    double step = waveform_render_step_for_index(app->waveform_render_step_index);
    app->waveform_render_value += step * (double)direction;
    app->waveform_render_value = waveform_render_dialog_clamp_value(app->waveform_render_dialog_mode,
                                                                     app->waveform_render_value);
    app->waveform_render_error[0] = '\0';
}

void app_waveform_render_dialog_cycle_step(App *app, int direction) {
    if (!app || !app->waveform_render_dialog_open || direction == 0) return;
    int count = waveform_render_step_count();
    if (count <= 0) return;
    int index = app->waveform_render_step_index + direction;
    while (index < 0) index += count;
    index %= count;
    app->waveform_render_step_index = index;
    app->waveform_render_error[0] = '\0';
}

void app_waveform_render_dialog_confirm(App *app) {
    if (!app || !app->waveform_render_dialog_open) return;
    WaveformRenderDialogMode mode = app->waveform_render_dialog_mode;
    double value = waveform_render_dialog_clamp_value(mode, app->waveform_render_value);
    app->waveform_render_value = value;
    if (mode == WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM &&
        app->waveform_render_source_bpm <= 0.0) {
        SDL_strlcpy(app->waveform_render_error, "No calibrated source BPM", sizeof(app->waveform_render_error));
        app_set_status(app, app->waveform_render_error);
        return;
    }
    if (app_render_current_loop_to_roster(app, mode, value)) {
        app->waveform_render_dialog_open = false;
        app->waveform_render_error[0] = '\0';
    } else {
        SDL_strlcpy(app->waveform_render_error,
                    app->status_text[0] ? app->status_text : "Render failed",
                    sizeof(app->waveform_render_error));
    }
}

void app_waveform_menu_move(App *app, int delta) {
    if (!app || !app->waveform_menu_open || delta == 0) return;
    int selected = app->waveform_menu_selected + delta;
    while (selected < 0) selected += (int)WAVEFORM_MENU_ITEM_COUNT;
    selected %= (int)WAVEFORM_MENU_ITEM_COUNT;
    app->waveform_menu_selected = selected;
}

static bool app_normalize_waveform_clip(App *app) {
    if (!app || !app->clip.samples || app->clip.frame_count < 1 || app->clip.channels <= 0) {
        app_set_status(app, "No waveform to normalize");
        return false;
    }
    size_t sample_count = app->clip.frame_count * (size_t)app->clip.channels;
    float peak = 0.0f;
    for (size_t i = 0; i < sample_count; ++i) {
        float abs_sample = fabsf(app->clip.samples[i]);
        if (abs_sample > peak) peak = abs_sample;
    }
    if (peak <= 0.0000001f) {
        app_set_status(app, "Cannot normalize silence");
        return false;
    }

    float gain = 1.0f / peak;
    app_stop_active_audio(app);
    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        app_set_status(app, "Could not lock audio stream");
        return false;
    }

    for (size_t i = 0; i < sample_count; ++i) app->clip.samples[i] *= gain;

    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }
    double gain_db = 20.0 * log10((double)gain);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Normalized waveform %+4.1f dB", gain_db);
    return true;
}

void app_waveform_menu_apply(App *app) {
    if (!app || !app->waveform_menu_open) return;
    WaveformMenuItem item = (WaveformMenuItem)clamp_int(app->waveform_menu_selected, 0, WAVEFORM_MENU_ITEM_COUNT - 1);
    switch (item) {
        case WAVEFORM_MENU_ITEM_NORMALIZE:
            if (app_normalize_waveform_clip(app)) {
                app->waveform_menu_open = false;
                app->waveform_menu_selected = 0;
            }
            break;
        case WAVEFORM_MENU_ITEM_RENDER_TEMPO:
            app_waveform_render_dialog_open_mode(app,
                app_get_explicit_loop_calibration(app, NULL) ?
                    WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM :
                    WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT);
            break;
        case WAVEFORM_MENU_ITEM_RENDER_PITCH:
            app_waveform_render_dialog_open_mode(app, WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES);
            break;
        case WAVEFORM_MENU_ITEM_RENDER_RATE:
            app_waveform_render_dialog_open_mode(app, WAVEFORM_RENDER_DIALOG_RATE_PERCENT);
            break;
        case WAVEFORM_MENU_ITEM_CAPTURE_WHOLE_SOURCE:
            if (app_capture_whole_source_to_roster(app)) {
                app->waveform_menu_open = false;
                app->waveform_menu_selected = 0;
            }
            break;
        case WAVEFORM_MENU_ITEM_CANCEL:
        default:
            app_waveform_menu_close(app);
            break;
    }
}

void app_roster_commit_menu_close(App *app) {
    if (!app) return;
    app->roster_commit_menu_open = false;
    app->roster_commit_menu_selected = 0;
    app_set_status(app, "Roster edit cancelled");
}

void app_roster_commit_menu_move(App *app, int delta) {
    if (!app || !app->roster_commit_menu_open || delta == 0) return;
    int selected = app->roster_commit_menu_selected + delta;
    while (selected < 0) selected += (int)ROSTER_COMMIT_ITEM_COUNT;
    selected %= (int)ROSTER_COMMIT_ITEM_COUNT;
    app->roster_commit_menu_selected = selected;
}

void app_roster_commit_menu_apply(App *app) {
    if (!app || !app->roster_commit_menu_open) return;
    RosterCommitMenuItem item = (RosterCommitMenuItem)clamp_int(app->roster_commit_menu_selected,
                                                                0,
                                                                ROSTER_COMMIT_ITEM_COUNT - 1);
    switch (item) {
        case ROSTER_COMMIT_ITEM_NEW_CLIP:
            app->roster_commit_menu_open = false;
            app->roster_commit_menu_selected = 0;
            app_capture_current_loop_to_roster_new(app);
            break;
        case ROSTER_COMMIT_ITEM_REPLACE_CLIP:
            app_replace_source_roster_clip_from_waveform(app);
            break;
        case ROSTER_COMMIT_ITEM_CANCEL:
        default:
            app_roster_commit_menu_close(app);
            break;
    }
}

void app_timeline_open_context_menu(App *app) {
    if (app->view_mode != APP_VIEW_TIMELINE) return;
    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE || app->timeline_play_range_adjusting) {
        app_set_status(app, "Finish current edit first");
        return;
    }
    app->project_menu_open = false;
    app->project_menu_selected = 0;
    app_timeline_clear_context_menu(app);
    if (app->timeline_focus_zone == TIMELINE_FOCUS_ROSTER) {
        if (app_selected_timeline_lane_is_drum(app)) {
            if (app->drum_pattern_count <= 0) {
                app->selected_drum_pattern = -1;
            } else if (!app_pattern_index_valid(app, app->selected_drum_pattern)) {
                app->selected_drum_pattern = 0;
            }
            app->timeline_context_menu_open = true;
            app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_PATTERN;
            app->timeline_context_menu_pattern_index = app->selected_drum_pattern;
            app->timeline_context_menu_tick = app->timeline.timeline_cursor_tick;
            app_set_status(app, "Pattern menu");
            return;
        }
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
        app_set_status(app, "Ruler menu");
        return;
    }

    if (app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) {
        app->timeline_context_menu_open = true;
        app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_TIMELINE;
        app->timeline_context_menu_tick = app->timeline.timeline_cursor_tick;
        app_set_status(app, "Lane menu");
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
    if (scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE) {
        app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_PATTERN;
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

static int timeline_lane_initial_velocity(const App *app, int lane_index) {
    if (!app || !timeline_lane_index_valid(lane_index)) return 127;
    if (timeline_instance_ref_valid(&app->timeline, app->selected_timeline_instance) &&
        app->selected_timeline_instance.lane_index == lane_index) {
        int index = app->selected_timeline_instance.instance_index;
        const TimelineLane *selected_lane = &app->timeline.lanes[lane_index];
        if (index >= 0 && index < selected_lane->instance_count) {
            return clamp_int(selected_lane->instances[index].midi_velocity, 1, 127);
        }
    }
    const TimelineLane *lane = &app->timeline.lanes[lane_index];
    if (lane->instance_count > 0) return clamp_int(lane->instances[0].midi_velocity, 1, 127);
    return 127;
}

static void app_open_lane_velocity_entry(App *app, int lane_index) {
    if (!app || !timeline_lane_index_valid(lane_index)) {
        app_set_status(app, "No lane selected");
        return;
    }
    char initial[8];
    SDL_snprintf(initial, sizeof(initial), "%d", timeline_lane_initial_velocity(app, lane_index));
    app_text_entry_open(app,
                        APP_TEXT_ENTRY_FILENAME_SAFE,
                        APP_TEXT_ENTRY_ACTION_APPLY_LANE_VELOCITY,
                        "APPLY LANE VELOCITY",
                        "Velocity",
                        initial,
                        3);
    app->text_entry_target_lane_index = lane_index;
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
        case TIMELINE_CONTEXT_ITEM_MARK_TEMPO:
            app_timeline_mark_tempo_at_cursor(app);
            break;
        case TIMELINE_CONTEXT_ITEM_REMOVE_TEMPO:
            app_timeline_remove_tempo_at_cursor(app);
            break;
        case TIMELINE_CONTEXT_ITEM_INSERT_BAR:
            app_timeline_insert_bar_at_cursor(app);
            break;
        case TIMELINE_CONTEXT_ITEM_BOUNCE_TO_ROSTER:
            app_start_timeline_bounce(app);
            break;
        case TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE:
            if (timeline_instance_ref_valid(&app->timeline, app->timeline_context_menu_instance)) {
                app->selected_timeline_instance = app->timeline_context_menu_instance;
                app->selected_timeline_lane = app->timeline_context_menu_instance.lane_index;
            }
            app_timeline_remove_selected_instance(app);
            break;
        case TIMELINE_CONTEXT_ITEM_APPLY_LANE_VELOCITY:
        {
            int lane_index = app->selected_timeline_lane;
            if (timeline_instance_ref_valid(&app->timeline, app->timeline_context_menu_instance)) {
                lane_index = app->timeline_context_menu_instance.lane_index;
                app->selected_timeline_lane = lane_index;
                app->selected_timeline_instance = app->timeline_context_menu_instance;
            }
            app_open_lane_velocity_entry(app, lane_index);
            break;
        }
        case TIMELINE_CONTEXT_ITEM_OPEN_WAVEFORM:
            if (app->timeline_context_menu_roster_index >= 0 &&
                app->timeline_context_menu_roster_index < app->roster_clip_count) {
                app->selected_roster_clip = app->timeline_context_menu_roster_index;
            }
            app_open_selected_roster_clip_waveform(app);
            break;
        case TIMELINE_CONTEXT_ITEM_RENAME_ROSTER:
            if (app->timeline_context_menu_roster_index >= 0 &&
                app->timeline_context_menu_roster_index < app->roster_clip_count) {
                app->selected_roster_clip = app->timeline_context_menu_roster_index;
                app_text_entry_open(app,
                                    APP_TEXT_ENTRY_DISPLAY_NAME,
                                    APP_TEXT_ENTRY_ACTION_RENAME_ROSTER_CLIP,
                                    "RENAME ROSTER CLIP",
                                    "Name",
                                    app->roster[app->timeline_context_menu_roster_index].name,
                                    APP_ROSTER_CLIP_NAME_MAX - 1);
                app->text_entry_target_roster_index = app->timeline_context_menu_roster_index;
            }
            break;
        case TIMELINE_CONTEXT_ITEM_PLACE_FREE:
        case TIMELINE_CONTEXT_ITEM_PLACE_PULSE:
        case TIMELINE_CONTEXT_ITEM_INSERT_PULSE:
        {
            if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_PATTERN &&
                app_pattern_index_valid(app, app->timeline_context_menu_pattern_index)) {
                app->selected_drum_pattern = app->timeline_context_menu_pattern_index;
                app_timeline_clear_context_menu(app);
                timeline_enter_place_pattern(app);
                break;
            }
            if (app->timeline_context_menu_roster_index >= 0 &&
                app->timeline_context_menu_roster_index < app->roster_clip_count) {
                app->selected_roster_clip = app->timeline_context_menu_roster_index;
            }
            TimelinePlacementMode mode = TIMELINE_PLACE_FREE;
            if (item == TIMELINE_CONTEXT_ITEM_PLACE_PULSE) mode = TIMELINE_PLACE_PULSE;
            else if (item == TIMELINE_CONTEXT_ITEM_INSERT_PULSE) mode = TIMELINE_INSERT_PULSE;
            app_timeline_clear_context_menu(app);
            timeline_enter_place_clip(app, mode);
            break;
        }
        case TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER:
            if (app->timeline_context_menu_roster_index >= 0 &&
                app->timeline_context_menu_roster_index < app->roster_clip_count) {
                app->selected_roster_clip = app->timeline_context_menu_roster_index;
                char initial[APP_ROSTER_CLIP_NAME_MAX + 8];
                roster_export_filename(&app->roster[app->timeline_context_menu_roster_index],
                                       initial,
                                       sizeof(initial));
                char *dot = SDL_strrchr(initial, '.');
                if (dot) *dot = '\0';
                app_text_entry_open(app,
                                    APP_TEXT_ENTRY_FILENAME_SAFE,
                                    APP_TEXT_ENTRY_ACTION_EXPORT_ROSTER_WAV,
                                    "EXPORT ROSTER WAV",
                                    "File",
                                    initial,
                                    APP_ROSTER_CLIP_NAME_MAX - 1);
                app->text_entry_target_roster_index = app->timeline_context_menu_roster_index;
            }
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
        case TIMELINE_CONTEXT_ITEM_NEW_PATTERN:
            app_timeline_clear_context_menu(app);
            app_create_drum_pattern(app);
            break;
        case TIMELINE_CONTEXT_ITEM_EDIT_PATTERN:
            if (app_pattern_index_valid(app, app->timeline_context_menu_pattern_index)) {
                app->selected_drum_pattern = app->timeline_context_menu_pattern_index;
                app_timeline_clear_context_menu(app);
                app_open_drum_machine_for_selected_pattern(app);
            }
            break;
        case TIMELINE_CONTEXT_ITEM_RENAME_PATTERN:
            if (app_pattern_index_valid(app, app->timeline_context_menu_pattern_index)) {
                app->selected_drum_pattern = app->timeline_context_menu_pattern_index;
                app_text_entry_open(app,
                                    APP_TEXT_ENTRY_DISPLAY_NAME,
                                    APP_TEXT_ENTRY_ACTION_RENAME_DRUM_PATTERN,
                                    "RENAME PATTERN",
                                    "Name",
                                    app->drum_patterns[app->timeline_context_menu_pattern_index].name,
                                    APP_ROSTER_CLIP_NAME_MAX - 1);
                app->text_entry_target_pattern_index = app->timeline_context_menu_pattern_index;
            }
            break;
        case TIMELINE_CONTEXT_ITEM_DUPLICATE_PATTERN:
            if (app_pattern_index_valid(app, app->timeline_context_menu_pattern_index)) {
                app->selected_drum_pattern = app->timeline_context_menu_pattern_index;
                app_timeline_clear_context_menu(app);
                app_duplicate_selected_drum_pattern(app);
            }
            break;
        case TIMELINE_CONTEXT_ITEM_DELETE_PATTERN:
            app->timeline_context_menu_scope = TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE;
            app->timeline_context_menu_selected = 0;
            app_set_status(app, "Confirm pattern delete");
            break;
        case TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_PATTERN:
            if (app_pattern_index_valid(app, app->timeline_context_menu_pattern_index)) {
                app->selected_drum_pattern = app->timeline_context_menu_pattern_index;
            }
            app_delete_selected_drum_pattern(app);
            break;
        case TIMELINE_CONTEXT_ITEM_CANCEL:
        default:
            app_timeline_close_context_menu(app);
            break;
    }
}

void app_timeline_mark_tempo_at_cursor(App *app) {
    if (!app->timeline.initialized) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "timeline empty");
        return;
    }

    int64_t tick = timeline_context_snapped_tick(app);
    if (tick < 0) tick = 0;
    if (app->timeline.length_ticks > 0 && tick > app->timeline.length_ticks) tick = app->timeline.length_ticks;
    double bpm = timeline_effective_bpm_at_tick(&app->timeline, (double)tick);

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    bool ok = timeline_set_tempo_event_no_lock(&app->timeline, tick, bpm);
    if (ok && app->timeline.timeline_cursor_tick == tick &&
        app->timeline.timeline_cursor_seam_side == TIMELINE_SEAM_NONE) {
        app->timeline.timeline_cursor_seam_side = timeline_default_seam_side_for_tick(&app->timeline, tick);
    }
    timeline_normalize_cursor_seam_side_no_lock(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    app_timeline_clear_context_menu(app);
    if (!ok) {
        app_set_status(app, "Tempo map full");
        return;
    }
    sync_transport_from_app(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Marked tempo %.2f bpm at tick %lld",
                 bpm, (long long)tick);
}

void app_timeline_remove_tempo_at_cursor(App *app) {
    if (!app->timeline.initialized) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "timeline empty");
        return;
    }

    int64_t tick = timeline_context_snapped_tick(app);
    int index = timeline_tempo_event_index_at_tick(&app->timeline, tick);
    if (index <= 0) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, tick == 0 ? "Base tempo cannot be removed" : "No tempo event at cursor");
        return;
    }

    double removed_bpm = app->timeline.tempo_events[index].bpm;
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    int count = timeline_valid_tempo_event_count(&app->timeline);
    for (int i = index; i < count - 1; ++i) {
        app->timeline.tempo_events[i] = app->timeline.tempo_events[i + 1];
    }
    app->timeline.tempo_event_count = count - 1;
    timeline_ensure_tempo_anchor_no_lock(&app->timeline);
    timeline_normalize_cursor_seam_side_no_lock(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    app_timeline_clear_context_menu(app);
    sync_transport_from_app(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Removed %.2f bpm tempo at tick %lld",
                 removed_bpm, (long long)tick);
}

void app_timeline_adjust_tempo_event_at_cursor(App *app, double delta) {
    if (delta == 0.0) return;
    if (!app->timeline.initialized) {
        app_set_status(app, "timeline empty");
        return;
    }

    int64_t tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    int index = timeline_tempo_event_index_at_tick(&app->timeline, tick);
    if (index < 0) {
        app_set_status(app, "No tempo event at cursor");
        return;
    }

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    double bpm = timeline_clamp_bpm(app->timeline.tempo_events[index].bpm + delta);
    app->timeline.tempo_events[index].bpm = bpm;
    if (index == 0) app->timeline.timeline_bpm = bpm;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    sync_transport_from_app(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Tempo at tick %lld: %.2f bpm",
                 (long long)tick, bpm);
}

bool app_timeline_cursor_on_tempo_event(const App *app) {
    if (!app || !app->timeline.initialized) return false;
    int64_t tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    return timeline_tempo_event_index_at_tick(&app->timeline, tick) >= 0;
}

void app_timeline_set_tape_control_mode(App *app, TimelineTapeControlMode mode) {
    if (!app) return;
    app->timeline_tape_control_mode = mode;
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Tape control: %s",
                 timeline_tape_control_mode_label(app->timeline_tape_control_mode));
}

void app_timeline_toggle_tape_control_mode(App *app) {
    if (!app) return;
    TimelineTapeControlMode next =
        app->timeline_tape_control_mode == TIMELINE_TAPE_CONTROL_BPM ?
        TIMELINE_TAPE_CONTROL_PITCH : TIMELINE_TAPE_CONTROL_BPM;
    app_timeline_set_tape_control_mode(app, next);
}

void app_timeline_adjust_tape_control(App *app, int direction, double bpm_step) {
    if (!app || direction == 0) return;
    if (bpm_step <= 0.0) bpm_step = 1.0;

    int64_t tick = timeline_tape_reference_tick(app);
    float speed = timeline_effective_tape_speed(&app->timeline);
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    if (app->timeline_tape_control_mode == TIMELINE_TAPE_CONTROL_PITCH) {
        double semitones = timeline_tape_pitch_semitones(speed) + (double)direction;
        speed = timeline_tape_speed_from_pitch_semitones(semitones);
    } else {
        double audible_bpm = timeline_audible_bpm_at_tick(&app->timeline, (double)tick) +
                             (double)direction * bpm_step;
        speed = timeline_tape_speed_from_audible_bpm(&app->timeline, (double)tick, audible_bpm);
    }
    app->timeline.tape_speed = speed;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    double canonical_bpm = timeline_effective_bpm_at_tick(&app->timeline, (double)tick);
    double audible_bpm = timeline_audible_bpm_at_tick(&app->timeline, (double)tick);
    double semitones = timeline_tape_pitch_semitones(speed);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Tape %s: %.3fx  canon %.2f  hear %.2f  pitch %+.2f st",
                 timeline_tape_control_mode_label(app->timeline_tape_control_mode),
                 speed,
                 canonical_bpm,
                 audible_bpm,
                 semitones);
}

void app_timeline_reset_tape_speed(App *app) {
    if (!app) return;
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    app->timeline.tape_speed = TIMELINE_TAPE_SPEED_DEFAULT;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    app_set_status(app, "Tape speed reset: 1.000x");
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
    int tempo_count = timeline_valid_tempo_event_count(&app->timeline);
    for (int i = 0; i < tempo_count; ++i) {
        TimelineTempoEvent *event = &app->timeline.tempo_events[i];
        if (event->tick > 0 && event->tick >= insertion_tick) event->tick += bar_ticks;
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
        app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
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

static bool io_write_text(SDL_IOStream *io, const char *text) {
    if (!io || !text) return false;
    size_t len = SDL_strlen(text);
    return SDL_WriteIO(io, text, len) == len;
}

static bool io_printf(SDL_IOStream *io, const char *fmt, ...) {
    if (!io || !fmt) return false;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(buffer)) return false;
    return SDL_WriteIO(io, buffer, (size_t)written) == (size_t)written;
}

static bool json_write_string(SDL_IOStream *io, const char *text) {
    if (!io) return false;
    if (!io_write_text(io, "\"")) return false;
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        unsigned char c = *p++;
        switch (c) {
            case '\"': if (!io_write_text(io, "\\\"")) return false; break;
            case '\\': if (!io_write_text(io, "\\\\")) return false; break;
            case '\b': if (!io_write_text(io, "\\b")) return false; break;
            case '\f': if (!io_write_text(io, "\\f")) return false; break;
            case '\n': if (!io_write_text(io, "\\n")) return false; break;
            case '\r': if (!io_write_text(io, "\\r")) return false; break;
            case '\t': if (!io_write_text(io, "\\t")) return false; break;
            default:
                if (c < 0x20) {
                    if (!io_printf(io, "\\u%04x", (unsigned int)c)) return false;
                } else {
                    if (SDL_WriteIO(io, &c, 1) != 1) return false;
                }
                break;
        }
    }
    return io_write_text(io, "\"");
}

static bool path_exists_any(const char *path) {
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info);
}

static bool ensure_directory(const char *path) {
    if (path_is_directory(path)) return true;
    return path && path[0] && SDL_CreateDirectory(path);
}

static void project_bundle_name_from_path(const char *path, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    const char *base = path_basename(path);
    SDL_strlcpy(out, base, out_size);
    size_t len = SDL_strlen(out);
    size_t suffix_len = SDL_strlen(VAPORPLANE_PROJECT_BUNDLE_SUFFIX);
    if (len > suffix_len && SDL_strcasecmp(out + len - suffix_len, VAPORPLANE_PROJECT_BUNDLE_SUFFIX) == 0) {
        out[len - suffix_len] = '\0';
    }
    if (!out[0]) SDL_strlcpy(out, "vaporplane_project", out_size);
}

static bool project_sample_filename_for_clip(const RosterClip *clip, int fallback_index, char *out, size_t out_size) {
    const char *sample_id = clip && clip->sample_id[0] ? clip->sample_id : NULL;
    if (sample_id) {
        return out && out_size > 0 && SDL_snprintf(out, out_size, "%s.wav", sample_id) > 0;
    }
    return out && out_size > 0 && SDL_snprintf(out, out_size, "sample_%03d.wav", fallback_index + 1) > 0;
}

static bool project_sample_relative_path_for_clip(const RosterClip *clip, int fallback_index, char *out, size_t out_size) {
    char filename[APP_STABLE_ID_MAX + 8];
    if (!project_sample_filename_for_clip(clip, fallback_index, filename, sizeof(filename))) return false;
    path_join(out, out_size, VAPORPLANE_PROJECT_SAMPLES_DIRNAME, filename);
    return out && out[0];
}

static bool project_drum_sample_filename(const DrumKit *kit, const DrumPad *pad, char *out, size_t out_size) {
    if (!kit || !pad || !out || out_size == 0) return false;
    const char *kit_id = kit->kit_id[0] ? kit->kit_id : "kit";
    return SDL_snprintf(out, out_size, "%s_note_%03d.wav", kit_id, clamp_int(pad->note, 0, 127)) > 0;
}

static bool project_drum_sample_relative_path(const DrumKit *kit, const DrumPad *pad, char *out, size_t out_size) {
    char dir[CLIP_MAX_PATH];
    char filename[APP_STABLE_ID_MAX + 32];
    path_join(dir, sizeof(dir), VAPORPLANE_PROJECT_SAMPLES_DIRNAME, "drums");
    if (!project_drum_sample_filename(kit, pad, filename, sizeof(filename))) return false;
    path_join(out, out_size, dir, filename);
    return out && out[0];
}

static bool drum_lane_uses_kit(const TimelineLane *lane, int kit_index, const DrumKit *kit) {
    if (!lane || lane->type != TIMELINE_LANE_DRUMS || !kit) return false;
    if (lane->drum_kit_index == kit_index) return true;
    return lane->drum_kit_id[0] && SDL_strcmp(lane->drum_kit_id, kit->kit_id) == 0;
}

static bool app_project_uses_drum_pad(const App *app, int kit_index, int note) {
    if (!app_kit_index_valid(app, kit_index)) return false;
    const DrumKit *kit = &app->drum_kits[kit_index];
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        const TimelineLane *lane = &app->timeline.lanes[lane_index];
        if (!drum_lane_uses_kit(lane, kit_index, kit)) continue;
        for (int instance_index = 0; instance_index < lane->instance_count; ++instance_index) {
            const TimelineInstance *instance = &lane->instances[instance_index];
            if (instance->kind != TIMELINE_INSTANCE_DRUM_PATTERN ||
                !app_pattern_index_valid(app, instance->pattern_index)) {
                continue;
            }
            const DrumPattern *pattern = &app->drum_patterns[instance->pattern_index];
            for (int event_index = 0; event_index < pattern->event_count; ++event_index) {
                if (pattern->events[event_index].note == note) return true;
            }
        }
    }
    return false;
}

static void app_ensure_project_identity(App *app, const char *bundle_path) {
    if (!app) return;
    if (!app->project_id[0]) {
        generate_stable_id("project", app->project_id, sizeof(app->project_id));
    }
    if (!app->project_name[0]) {
        project_bundle_name_from_path(bundle_path, app->project_name, sizeof(app->project_name));
    }
    for (int i = 0; i < app->roster_clip_count; ++i) {
        RosterClip *clip = &app->roster[i];
        if (!clip->sample_id[0]) generate_stable_id("sample", clip->sample_id, sizeof(clip->sample_id));
        if (!clip->roster_clip_id[0]) generate_stable_id("roster", clip->roster_clip_id, sizeof(clip->roster_clip_id));
        if (clip->loop_end_frame <= clip->loop_start_frame || clip->loop_end_frame > clip->frame_count) {
            clip->loop_start_frame = 0;
            clip->loop_end_frame = clip->frame_count;
        }
    }
    for (int i = 0; i < app->drum_pattern_count; ++i) {
        DrumPattern *pattern = &app->drum_patterns[i];
        if (!pattern->pattern_id[0]) generate_stable_id("pat", pattern->pattern_id, sizeof(pattern->pattern_id));
        if (!pattern->name[0]) SDL_snprintf(pattern->name, sizeof(pattern->name), "pattern %02d", i + 1);
        if (pattern->length_ticks <= 0) pattern->length_ticks = timeline_bar_ticks(&app->timeline);
        if (pattern->locator_channel < 0 || pattern->locator_channel > 15) pattern->locator_channel = DRUM_PATTERN_LOCATOR_CHANNEL;
        if (pattern->locator_note < 0 || pattern->locator_note > 127) pattern->locator_note = app_next_available_pattern_locator_note(app);
        if (pattern->color.a == 0) pattern->color = roster_color_for_index(i);
    }
}

static bool write_project_float_wav_buffer(const float *samples, size_t frame_count, const char *path) {
    if (!samples || frame_count == 0 || !path) return false;

    Uint64 data_size = (Uint64)frame_count * (Uint64)VAPORPLANE_PROJECT_CHANNELS * sizeof(float);
    if (data_size > 0xffffffffu) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;

    Uint16 block_align = (Uint16)(VAPORPLANE_PROJECT_CHANNELS * (VAPORPLANE_PROJECT_WAV_BITS_PER_SAMPLE / 8));
    Uint32 byte_rate = (Uint32)(VAPORPLANE_PROJECT_SAMPLE_RATE * block_align);
    bool ok = true;
    ok = ok && write_fourcc(io, "RIFF");
    ok = ok && SDL_WriteU32LE(io, 36u + (Uint32)data_size);
    ok = ok && write_fourcc(io, "WAVE");
    ok = ok && write_fourcc(io, "fmt ");
    ok = ok && SDL_WriteU32LE(io, 16);
    ok = ok && SDL_WriteU16LE(io, 3);
    ok = ok && SDL_WriteU16LE(io, (Uint16)VAPORPLANE_PROJECT_CHANNELS);
    ok = ok && SDL_WriteU32LE(io, (Uint32)VAPORPLANE_PROJECT_SAMPLE_RATE);
    ok = ok && SDL_WriteU32LE(io, byte_rate);
    ok = ok && SDL_WriteU16LE(io, block_align);
    ok = ok && SDL_WriteU16LE(io, (Uint16)VAPORPLANE_PROJECT_WAV_BITS_PER_SAMPLE);
    ok = ok && write_fourcc(io, "data");
    ok = ok && SDL_WriteU32LE(io, (Uint32)data_size);
    ok = ok && SDL_WriteIO(io, samples, (size_t)data_size) == (size_t)data_size;

    ok = SDL_CloseIO(io) && ok;
    return ok;
}

typedef struct {
    SDL_IOStream *io;
    Uint64 data_bytes;
    bool failed;
} FloatWavStreamWriter;

static bool float_wav_stream_open(FloatWavStreamWriter *writer, const char *path) {
    if (!writer || !path || !path[0]) return false;
    SDL_memset(writer, 0, sizeof(*writer));
    writer->io = SDL_IOFromFile(path, "wb+");
    if (!writer->io) return false;

    Uint16 block_align = (Uint16)(VAPORPLANE_PROJECT_CHANNELS * (VAPORPLANE_PROJECT_WAV_BITS_PER_SAMPLE / 8));
    Uint32 byte_rate = (Uint32)(VAPORPLANE_PROJECT_SAMPLE_RATE * block_align);
    bool ok = true;
    ok = ok && write_fourcc(writer->io, "RIFF");
    ok = ok && SDL_WriteU32LE(writer->io, 0);
    ok = ok && write_fourcc(writer->io, "WAVE");
    ok = ok && write_fourcc(writer->io, "fmt ");
    ok = ok && SDL_WriteU32LE(writer->io, 16);
    ok = ok && SDL_WriteU16LE(writer->io, 3);
    ok = ok && SDL_WriteU16LE(writer->io, (Uint16)VAPORPLANE_PROJECT_CHANNELS);
    ok = ok && SDL_WriteU32LE(writer->io, (Uint32)VAPORPLANE_PROJECT_SAMPLE_RATE);
    ok = ok && SDL_WriteU32LE(writer->io, byte_rate);
    ok = ok && SDL_WriteU16LE(writer->io, block_align);
    ok = ok && SDL_WriteU16LE(writer->io, (Uint16)VAPORPLANE_PROJECT_WAV_BITS_PER_SAMPLE);
    ok = ok && write_fourcc(writer->io, "data");
    ok = ok && SDL_WriteU32LE(writer->io, 0);
    writer->failed = !ok;
    return ok;
}

static bool float_wav_stream_write(FloatWavStreamWriter *writer, const float *samples, int frame_count) {
    if (!writer || !writer->io || !samples || frame_count <= 0 || writer->failed) return false;
    Uint64 bytes = (Uint64)frame_count * (Uint64)VAPORPLANE_PROJECT_CHANNELS * sizeof(float);
    if (bytes > 0xffffffffu || writer->data_bytes + bytes > 0xffffffffu) {
        writer->failed = true;
        return false;
    }
    if (SDL_WriteIO(writer->io, samples, (size_t)bytes) != (size_t)bytes) {
        writer->failed = true;
        return false;
    }
    writer->data_bytes += bytes;
    return true;
}

static bool float_wav_stream_close(FloatWavStreamWriter *writer) {
    if (!writer || !writer->io) return false;
    bool ok = !writer->failed && writer->data_bytes > 0 && writer->data_bytes <= 0xffffffffu;
    if (ok) {
        Uint32 data_size = (Uint32)writer->data_bytes;
        ok = ok && SDL_SeekIO(writer->io, 4, SDL_IO_SEEK_SET) >= 0;
        ok = ok && SDL_WriteU32LE(writer->io, 36u + data_size);
        ok = ok && SDL_SeekIO(writer->io, 40, SDL_IO_SEEK_SET) >= 0;
        ok = ok && SDL_WriteU32LE(writer->io, data_size);
    }
    ok = SDL_CloseIO(writer->io) && ok;
    writer->io = NULL;
    return ok;
}

static void float_wav_stream_abort(FloatWavStreamWriter *writer) {
    if (!writer || !writer->io) return;
    SDL_CloseIO(writer->io);
    writer->io = NULL;
    writer->failed = true;
}

static bool write_project_float_wav(const RosterClip *clip, const char *path) {
    if (!clip || !path || !clip->samples || clip->frame_count == 0 ||
        clip->channels <= 0 || clip->sample_rate <= 0) {
        return false;
    }
    Uint64 input_bytes64 = (Uint64)clip->frame_count * (Uint64)clip->channels * sizeof(float);
    if (input_bytes64 > (Uint64)INT_MAX) return false;

    SDL_AudioSpec src = {
        .format = SDL_AUDIO_F32,
        .channels = clip->channels,
        .freq = clip->sample_rate
    };
    SDL_AudioSpec dst = {
        .format = SDL_AUDIO_F32,
        .channels = VAPORPLANE_PROJECT_CHANNELS,
        .freq = VAPORPLANE_PROJECT_SAMPLE_RATE
    };
    Uint8 *converted = NULL;
    int converted_len = 0;
    if (!SDL_ConvertAudioSamples(&src,
                                  (const Uint8 *)clip->samples,
                                  (int)input_bytes64,
                                  &dst,
                                  &converted,
                                  &converted_len)) {
        return false;
    }
    if (converted_len <= 0 || (converted_len % (int)(sizeof(float) * VAPORPLANE_PROJECT_CHANNELS)) != 0) {
        SDL_free(converted);
        return false;
    }

    size_t frame_count = (size_t)converted_len / (sizeof(float) * VAPORPLANE_PROJECT_CHANNELS);
    bool ok = write_project_float_wav_buffer((const float *)converted, frame_count, path);
    SDL_free(converted);
    return ok;
}

static bool write_project_audio_clip_float_wav(const AudioClip *clip, const char *path) {
    if (!clip || !path || !clip->samples || clip->frame_count == 0 ||
        clip->channels <= 0 || clip->sample_rate <= 0) {
        return false;
    }
    Uint64 input_bytes64 = (Uint64)clip->frame_count * (Uint64)clip->channels * sizeof(float);
    if (input_bytes64 > (Uint64)INT_MAX) return false;

    SDL_AudioSpec src = {
        .format = SDL_AUDIO_F32,
        .channels = clip->channels,
        .freq = clip->sample_rate
    };
    SDL_AudioSpec dst = {
        .format = SDL_AUDIO_F32,
        .channels = VAPORPLANE_PROJECT_CHANNELS,
        .freq = VAPORPLANE_PROJECT_SAMPLE_RATE
    };
    Uint8 *converted = NULL;
    int converted_len = 0;
    if (!SDL_ConvertAudioSamples(&src,
                                  (const Uint8 *)clip->samples,
                                  (int)input_bytes64,
                                  &dst,
                                  &converted,
                                  &converted_len)) {
        return false;
    }
    if (converted_len <= 0 || (converted_len % (int)(sizeof(float) * VAPORPLANE_PROJECT_CHANNELS)) != 0) {
        SDL_free(converted);
        return false;
    }

    size_t frame_count = (size_t)converted_len / (sizeof(float) * VAPORPLANE_PROJECT_CHANNELS);
    bool ok = write_project_float_wav_buffer((const float *)converted, frame_count, path);
    SDL_free(converted);
    return ok;
}

typedef struct {
    Uint8 *data;
    size_t size;
    size_t capacity;
    bool ok;
} MidiBuffer;

static bool midi_buffer_reserve(MidiBuffer *buffer, size_t extra) {
    if (!buffer || !buffer->ok) return false;
    if (extra > SIZE_MAX - buffer->size) {
        buffer->ok = false;
        return false;
    }
    size_t needed = buffer->size + extra;
    if (needed <= buffer->capacity) return true;
    size_t next_capacity = buffer->capacity ? buffer->capacity * 2 : 256;
    while (next_capacity < needed) {
        if (next_capacity > SIZE_MAX / 2) {
            next_capacity = needed;
            break;
        }
        next_capacity *= 2;
    }
    Uint8 *next = (Uint8 *)SDL_realloc(buffer->data, next_capacity);
    if (!next) {
        buffer->ok = false;
        return false;
    }
    buffer->data = next;
    buffer->capacity = next_capacity;
    return true;
}

static bool midi_buffer_u8(MidiBuffer *buffer, Uint8 value) {
    if (!midi_buffer_reserve(buffer, 1)) return false;
    buffer->data[buffer->size++] = value;
    return true;
}

static bool midi_buffer_varlen(MidiBuffer *buffer, Uint32 value) {
    Uint8 bytes[5];
    int count = 0;
    bytes[count++] = (Uint8)(value & 0x7f);
    while ((value >>= 7) != 0 && count < 5) {
        bytes[count++] = (Uint8)((value & 0x7f) | 0x80);
    }
    for (int i = count - 1; i >= 0; --i) {
        if (!midi_buffer_u8(buffer, bytes[i])) return false;
    }
    return true;
}

static bool midi_buffer_meta_end(MidiBuffer *buffer) {
    return midi_buffer_varlen(buffer, 0) &&
           midi_buffer_u8(buffer, 0xff) &&
           midi_buffer_u8(buffer, 0x2f) &&
           midi_buffer_u8(buffer, 0x00);
}

static bool midi_write_be16(SDL_IOStream *io, Uint16 value) {
    return SDL_WriteU8(io, (Uint8)((value >> 8) & 0xff)) &&
           SDL_WriteU8(io, (Uint8)(value & 0xff));
}

static bool midi_write_be32(SDL_IOStream *io, Uint32 value) {
    return SDL_WriteU8(io, (Uint8)((value >> 24) & 0xff)) &&
           SDL_WriteU8(io, (Uint8)((value >> 16) & 0xff)) &&
           SDL_WriteU8(io, (Uint8)((value >> 8) & 0xff)) &&
           SDL_WriteU8(io, (Uint8)(value & 0xff));
}

static bool midi_write_track(SDL_IOStream *io, const MidiBuffer *track) {
    if (!io || !track || !track->ok || track->size > 0xffffffffu) return false;
    return write_fourcc(io, "MTrk") &&
           midi_write_be32(io, (Uint32)track->size) &&
           SDL_WriteIO(io, track->data, track->size) == track->size;
}

static Uint32 midi_delta_from_ticks(int64_t from_tick, int64_t to_tick) {
    if (to_tick <= from_tick) return 0;
    int64_t delta = to_tick - from_tick;
    if (delta > 0x0fffffff) return 0x0fffffff;
    return (Uint32)delta;
}

static bool midi_build_tempo_track(const MasterTimeline *timeline, MidiBuffer *track) {
    if (!timeline || !track) return false;
    track->ok = true;
    int count = timeline_valid_tempo_event_count(timeline);
    if (count <= 0) count = 1;
    int64_t last_tick = 0;
    for (int i = 0; i < count; ++i) {
        int64_t tick = i < timeline->tempo_event_count ? timeline->tempo_events[i].tick : 0;
        double bpm = i < timeline->tempo_event_count ? timeline->tempo_events[i].bpm : timeline_base_bpm(timeline);
        if (tick < 0) tick = 0;
        if (bpm <= 0.0) bpm = TIMELINE_DEFAULT_BPM;
        Uint32 mpqn = (Uint32)llround(60000000.0 / bpm);
        if (mpqn < 1) mpqn = 1;
        if (!midi_buffer_varlen(track, midi_delta_from_ticks(last_tick, tick)) ||
            !midi_buffer_u8(track, 0xff) ||
            !midi_buffer_u8(track, 0x51) ||
            !midi_buffer_u8(track, 0x03) ||
            !midi_buffer_u8(track, (Uint8)((mpqn >> 16) & 0xff)) ||
            !midi_buffer_u8(track, (Uint8)((mpqn >> 8) & 0xff)) ||
            !midi_buffer_u8(track, (Uint8)(mpqn & 0xff))) {
            return false;
        }
        last_tick = tick;
    }
    return midi_buffer_meta_end(track);
}

typedef struct {
    int64_t tick;
    bool note_on;
    Uint8 channel;
    Uint8 note;
    Uint8 velocity;
} ProjectMidiNoteEvent;

static int compare_midi_note_events(const void *a, const void *b) {
    const ProjectMidiNoteEvent *ea = (const ProjectMidiNoteEvent *)a;
    const ProjectMidiNoteEvent *eb = (const ProjectMidiNoteEvent *)b;
    if (ea->tick < eb->tick) return -1;
    if (ea->tick > eb->tick) return 1;
    if (ea->note_on != eb->note_on) return ea->note_on ? 1 : -1;
    if (ea->note < eb->note) return -1;
    if (ea->note > eb->note) return 1;
    return 0;
}

static bool midi_build_lane_track(const TimelineLane *lane, MidiBuffer *track) {
    if (!lane || !track) return false;
    track->ok = true;
    ProjectMidiNoteEvent events[APP_MAX_TIMELINE_INSTANCES_PER_LANE * 2];
    int event_count = 0;
    for (int i = 0; i < lane->instance_count && event_count + 1 < (int)(sizeof(events) / sizeof(events[0])); ++i) {
        const TimelineInstance *instance = &lane->instances[i];
        if (instance->duration_ticks <= 0) continue;
        int64_t start = instance->start_tick >= 0 ? instance->start_tick : 0;
        int64_t end = start + instance->duration_ticks;
        Uint8 channel = (Uint8)clamp_int(instance->midi_channel, 0, 15);
        Uint8 note = (Uint8)clamp_int(instance->midi_note, 0, 127);
        Uint8 velocity = (Uint8)clamp_int(instance->midi_velocity, 1, 127);
        events[event_count++] = (ProjectMidiNoteEvent){ start, true, channel, note, velocity };
        events[event_count++] = (ProjectMidiNoteEvent){ end, false, channel, note, 0 };
    }
    qsort(events, (size_t)event_count, sizeof(events[0]), compare_midi_note_events);
    int64_t last_tick = 0;
    for (int i = 0; i < event_count; ++i) {
        ProjectMidiNoteEvent *event = &events[i];
        Uint8 status = (Uint8)((event->note_on ? 0x90 : 0x80) | (event->channel & 0x0f));
        if (!midi_buffer_varlen(track, midi_delta_from_ticks(last_tick, event->tick)) ||
            !midi_buffer_u8(track, status) ||
            !midi_buffer_u8(track, event->note) ||
            !midi_buffer_u8(track, event->velocity)) {
            return false;
        }
        last_tick = event->tick;
    }
    return midi_buffer_meta_end(track);
}

static bool midi_build_pattern_track(const DrumPattern *pattern, MidiBuffer *track) {
    if (!pattern || !track) return false;
    track->ok = true;
    ProjectMidiNoteEvent events[APP_MAX_DRUM_PATTERN_EVENTS * 2];
    int event_count = 0;
    for (int i = 0; i < pattern->event_count && event_count + 1 < (int)(sizeof(events) / sizeof(events[0])); ++i) {
        const DrumPatternEvent *event = &pattern->events[i];
        if (event->duration_ticks <= 0) continue;
        int64_t start = event->tick >= 0 ? event->tick : 0;
        if (pattern->length_ticks > 0 && start >= pattern->length_ticks) continue;
        int64_t end = start + event->duration_ticks;
        if (pattern->length_ticks > 0 && end > pattern->length_ticks) end = pattern->length_ticks;
        if (end <= start) end = start + 1;
        Uint8 note = (Uint8)clamp_int(event->note, 0, 127);
        Uint8 velocity = (Uint8)clamp_int(event->velocity, 1, 127);
        events[event_count++] = (ProjectMidiNoteEvent){ start, true, DRUM_MIDI_CHANNEL, note, velocity };
        events[event_count++] = (ProjectMidiNoteEvent){ end, false, DRUM_MIDI_CHANNEL, note, 0 };
    }
    if (pattern->length_ticks > 0 && event_count + 1 < (int)(sizeof(events) / sizeof(events[0]))) {
        events[event_count++] = (ProjectMidiNoteEvent){
            pattern->length_ticks,
            false,
            DRUM_MIDI_CHANNEL,
            0,
            0
        };
    }
    qsort(events, (size_t)event_count, sizeof(events[0]), compare_midi_note_events);
    int64_t last_tick = 0;
    for (int i = 0; i < event_count; ++i) {
        ProjectMidiNoteEvent *event = &events[i];
        Uint8 status = (Uint8)((event->note_on ? 0x90 : 0x80) | (event->channel & 0x0f));
        if (!midi_buffer_varlen(track, midi_delta_from_ticks(last_tick, event->tick)) ||
            !midi_buffer_u8(track, status) ||
            !midi_buffer_u8(track, event->note) ||
            !midi_buffer_u8(track, event->velocity)) {
            return false;
        }
        last_tick = event->tick;
    }
    return midi_buffer_meta_end(track);
}

static void midi_buffer_destroy(MidiBuffer *buffer) {
    if (!buffer) return;
    SDL_free(buffer->data);
    SDL_memset(buffer, 0, sizeof(*buffer));
}

static bool write_project_pattern_mid(const App *app, const DrumPattern *pattern, const char *path) {
    if (!app || !pattern || !path) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;
    Uint16 ppqn = (Uint16)clamp_int(app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat : app->transport.ppqn,
                                    1,
                                    32767);
    bool ok = true;
    ok = ok && write_fourcc(io, "MThd");
    ok = ok && midi_write_be32(io, 6);
    ok = ok && midi_write_be16(io, 0);
    ok = ok && midi_write_be16(io, 1);
    ok = ok && midi_write_be16(io, ppqn);
    MidiBuffer track = {0};
    ok = ok && midi_build_pattern_track(pattern, &track) && midi_write_track(io, &track);
    midi_buffer_destroy(&track);
    ok = SDL_CloseIO(io) && ok;
    return ok;
}

static bool write_project_timeline_mid(const App *app, const char *path) {
    if (!app || !path) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;

    bool ok = true;
    const int track_count = 1 + TIMELINE_MAX_LANES;
    Uint16 ppqn = (Uint16)clamp_int(app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat : app->transport.ppqn,
                                    1,
                                    32767);
    ok = ok && write_fourcc(io, "MThd");
    ok = ok && midi_write_be32(io, 6);
    ok = ok && midi_write_be16(io, 1);
    ok = ok && midi_write_be16(io, (Uint16)track_count);
    ok = ok && midi_write_be16(io, ppqn);

    MidiBuffer track = {0};
    if (ok) {
        ok = midi_build_tempo_track(&app->timeline, &track) && midi_write_track(io, &track);
        midi_buffer_destroy(&track);
    }
    for (int lane_index = 0; ok && lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        SDL_memset(&track, 0, sizeof(track));
        ok = midi_build_lane_track(&app->timeline.lanes[lane_index], &track) && midi_write_track(io, &track);
        midi_buffer_destroy(&track);
    }

    ok = SDL_CloseIO(io) && ok;
    return ok;
}

static bool write_project_surfaces_json(const App *app, const char *path) {
    if (!app || !path) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;
    MasterFxChain chain;
    MasterReverbParams reverb;
    audio_engine_get_master_fx_chain(&app->audio, &chain);
    audio_engine_get_master_reverb_params(&app->audio, NULL, &reverb);

    bool ok = true;
    ok = ok && io_printf(io, "{\n");
    ok = ok && io_printf(io, "  \"format\": \"%s\",\n", VAPORPLANE_SURFACES_FORMAT_NAME);
    ok = ok && io_printf(io, "  \"version\": %d,\n", VAPORPLANE_SURFACES_FORMAT_VERSION);
    ok = ok && io_printf(io, "  \"master\": {\n");
    ok = ok && io_printf(io, "    \"gain\": %.6f,\n", app->audio.master_gain);
    ok = ok && io_printf(io, "    \"clip_protection\": { \"enabled\": false }\n");
    ok = ok && io_printf(io, "  },\n");
    ok = ok && io_printf(io, "  \"timeline_surface\": {\n");
    ok = ok && io_printf(io, "    \"tape_speed\": %.6f,\n", app->timeline.tape_speed);
    ok = ok && io_printf(io, "    \"play_range\": {\n");
    ok = ok && io_printf(io, "      \"start_tick\": %lld,\n", (long long)app->timeline.play_range_start_tick);
    ok = ok && io_printf(io, "      \"end_tick\": %lld,\n", (long long)app->timeline.play_range_end_tick);
    ok = ok && io_printf(io, "      \"loop_enabled\": %s\n", app->timeline.play_range_loop_enabled ? "true" : "false");
    ok = ok && io_printf(io, "    }\n");
    ok = ok && io_printf(io, "  },\n");
    ok = ok && io_printf(io, "  \"lanes\": [\n");
    for (int lane_index = 0; ok && lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        const TimelineLane *lane = &app->timeline.lanes[lane_index];
        ok = ok && io_printf(io, "    {\n");
        ok = ok && io_printf(io, "      \"lane\": %d,\n", lane_index + 1);
        ok = ok && io_printf(io, "      \"type\": ");
        ok = ok && json_write_string(io, timeline_lane_type_label(lane->type));
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "      \"muted\": %s,\n", lane->muted ? "true" : "false");
        ok = ok && io_printf(io, "      \"gain\": %.6f,\n", lane->gain);
        ok = ok && io_printf(io, "      \"palette\": %d,\n", lane->palette_index);
        ok = ok && io_printf(io, "      \"midi_channel\": %d,\n", clamp_int(lane->midi_channel, 0, 15) + 1);
        ok = ok && io_printf(io, "      \"drum_kit_id\": ");
        ok = ok && json_write_string(io, lane->drum_kit_id);
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "      \"drum_step_resolution\": %d,\n", lane->drum_step_resolution > 0 ? lane->drum_step_resolution : 16);
        ok = ok && io_printf(io, "      \"fx_chain\": []\n");
        ok = ok && io_printf(io, "    }%s\n", lane_index + 1 < TIMELINE_MAX_LANES ? "," : "");
    }
    ok = ok && io_printf(io, "  ],\n");
    ok = ok && io_printf(io, "  \"fx_chain\": [\n");
    for (int i = 0; ok && i < chain.unit_count && i < MASTER_FX_CHAIN_MAX_UNITS; ++i) {
        const MasterFxUnit *unit = &chain.units[i];
        if (i > 0) ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "    {\n");
        ok = ok && io_printf(io, "      \"unit_id\": ");
        const char *unit_id = unit->type == MASTER_FX_UNIT_REVERB ? "reverb_1" :
                              (unit->type == MASTER_FX_UNIT_SOFT_CLIP_LIMITER ? "limiter_1" :
                               audio_engine_master_fx_unit_label(unit->type));
        const char *unit_type = unit->type == MASTER_FX_UNIT_REVERB ? "reverb" :
                                (unit->type == MASTER_FX_UNIT_SOFT_CLIP_LIMITER ? "limiter" :
                                 audio_engine_master_fx_unit_label(unit->type));
        ok = ok && json_write_string(io, unit_id);
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "      \"type\": ");
        ok = ok && json_write_string(io, unit_type);
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "      \"enabled\": %s,\n", unit->enabled ? "true" : "false");
        ok = ok && io_printf(io, "      \"bypassed\": %s,\n", unit->bypassed ? "true" : "false");
        ok = ok && io_printf(io, "      \"parameters\": {");
        if (unit->type == MASTER_FX_UNIT_REVERB) {
            ok = ok && io_printf(io, "\n");
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.enabled\": %s,\n", reverb.enabled ? "true" : "false");
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.send\": %.6f,\n", reverb.send);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.return\": %.6f,\n", reverb.return_gain);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.predelay_ms\": %.6f,\n", reverb.predelay_ms);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.decay\": %.6f,\n", reverb.decay_seconds);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.size\": %.6f,\n", reverb.size);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.diffusion\": %.6f,\n", reverb.diffusion);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.damping\": %.6f,\n", reverb.damping);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.low_cut_hz\": %.6f,\n", reverb.low_cut_hz);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.high_cut_hz\": %.6f,\n", reverb.high_cut_hz);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.width\": %.6f,\n", reverb.width);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.mod_depth_ms\": %.6f,\n", reverb.mod_depth_ms);
            ok = ok && io_printf(io, "        \"master.fx.reverb_1.mod_rate_hz\": %.6f\n", reverb.mod_rate_hz);
            ok = ok && io_printf(io, "      }\n");
        } else if (unit->type == MASTER_FX_UNIT_SOFT_CLIP_LIMITER) {
            ok = ok && io_printf(io, "\n");
            ok = ok && io_printf(io, "        \"master.fx.limiter_1.enabled\": %s,\n", unit->enabled && !unit->bypassed ? "true" : "false");
            ok = ok && io_printf(io, "        \"master.fx.limiter_1.ceiling\": %.6f,\n", MASTER_LIMITER_CEILING);
            ok = ok && io_printf(io, "        \"master.fx.limiter_1.release_ms\": %.6f\n", MASTER_LIMITER_RELEASE_MS);
            ok = ok && io_printf(io, "      }\n");
        } else {
            ok = ok && io_printf(io, " }\n");
        }
        ok = ok && io_printf(io, "    }");
    }
    ok = ok && io_printf(io, "\n  ],\n");
    ok = ok && io_printf(io, "  \"timeline_cc_envelopes\": [],\n");
    ok = ok && io_printf(io, "  \"external_cc_bindings\": []\n");
    ok = ok && io_printf(io, "}\n");

    ok = SDL_CloseIO(io) && ok;
    return ok;
}

static bool write_project_manifest_json(const App *app, const char *bundle_path, const char *path) {
    if (!app || !path) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;
    char project_name[APP_SAMPLE_NAME_MAX];
    if (app->project_name[0]) SDL_strlcpy(project_name, app->project_name, sizeof(project_name));
    else project_bundle_name_from_path(bundle_path, project_name, sizeof(project_name));
    const char *project_id = app->project_id[0] ? app->project_id : project_name;
    int ppqn = app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat : app->transport.ppqn;
    if (ppqn <= 0) ppqn = 960;

    bool ok = true;
    ok = ok && io_printf(io, "{\n");
    ok = ok && io_printf(io, "  \"format\": \"%s\",\n", VAPORPLANE_PROJECT_FORMAT_NAME);
    ok = ok && io_printf(io, "  \"version\": %d,\n", VAPORPLANE_PROJECT_FORMAT_VERSION);
    ok = ok && io_printf(io, "  \"project_id\": ");
    ok = ok && json_write_string(io, project_id);
    ok = ok && io_printf(io, ",\n  \"name\": ");
    ok = ok && json_write_string(io, project_name);
    ok = ok && io_printf(io, ",\n  \"ppqn\": %d,\n", ppqn);
    ok = ok && io_printf(io, "  \"timeline\": \"%s\",\n", VAPORPLANE_PROJECT_TIMELINE_FILENAME);
    ok = ok && io_printf(io, "  \"surfaces\": \"%s\",\n", VAPORPLANE_PROJECT_SURFACES_FILENAME);

    ok = ok && io_printf(io, "  \"samples\": [\n");
    for (int i = 0; ok && i < app->roster_clip_count; ++i) {
        const RosterClip *clip = &app->roster[i];
        char rel_path[CLIP_MAX_PATH];
        char sample_id[APP_STABLE_ID_MAX];
        if (clip->sample_id[0]) SDL_strlcpy(sample_id, clip->sample_id, sizeof(sample_id));
        else SDL_snprintf(sample_id, sizeof(sample_id), "sample_%03d", i + 1);
        ok = ok && project_sample_relative_path_for_clip(clip, i, rel_path, sizeof(rel_path));
        ok = ok && io_printf(io, "    { \"sample_id\": ");
        ok = ok && json_write_string(io, sample_id);
        ok = ok && io_printf(io, ", \"path\": ");
        ok = ok && json_write_string(io, rel_path);
        ok = ok && io_printf(io, " }%s\n", i + 1 < app->roster_clip_count ? "," : "");
    }
    ok = ok && io_printf(io, "  ],\n");

    ok = ok && io_printf(io, "  \"roster_clips\": [\n");
    for (int i = 0; ok && i < app->roster_clip_count; ++i) {
        const RosterClip *clip = &app->roster[i];
        char roster_clip_id[APP_STABLE_ID_MAX];
        char sample_id[APP_STABLE_ID_MAX];
        if (clip->roster_clip_id[0]) SDL_strlcpy(roster_clip_id, clip->roster_clip_id, sizeof(roster_clip_id));
        else SDL_snprintf(roster_clip_id, sizeof(roster_clip_id), "roster_%03d", i + 1);
        if (clip->sample_id[0]) SDL_strlcpy(sample_id, clip->sample_id, sizeof(sample_id));
        else SDL_snprintf(sample_id, sizeof(sample_id), "sample_%03d", i + 1);
        ok = ok && io_printf(io, "    {\n");
        ok = ok && io_printf(io, "      \"roster_clip_id\": ");
        ok = ok && json_write_string(io, roster_clip_id);
        ok = ok && io_printf(io, ",\n      \"sample_id\": ");
        ok = ok && json_write_string(io, sample_id);
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "      \"name\": ");
        ok = ok && json_write_string(io, clip->name);
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "      \"tempo_calibrated\": %s,\n", clip->tempo_calibrated ? "true" : "false");
        if (clip->tempo_calibrated) {
            ok = ok && io_printf(io, "      \"source_bpm\": %.6f,\n", clip->source_bpm);
            ok = ok && io_printf(io, "      \"beats_per_bar\": %d,\n", clip->beats_per_bar);
            ok = ok && io_printf(io, "      \"beat_unit\": %d,\n", clip->beat_unit);
            ok = ok && io_printf(io, "      \"target_bars\": %.6f,\n", clip->target_bars);
            ok = ok && io_printf(io, "      \"target_beats\": %.6f,\n", clip->target_beats);
            ok = ok && io_printf(io, "      \"downbeat_offset_frames\": %llu,\n", (unsigned long long)clip->downbeat_offset_frames);
        }
        ok = ok && io_printf(io, "      \"loop_start_frame\": %llu,\n", (unsigned long long)clip->loop_start_frame);
        ok = ok && io_printf(io, "      \"loop_end_frame\": %llu,\n", (unsigned long long)clip->loop_end_frame);
        ok = ok && io_printf(io, "      \"midi_binding\": {\n");
        ok = ok && io_printf(io, "        \"channel\": %d,\n", clamp_int(clip->midi_channel, 0, 15) + 1);
        ok = ok && io_printf(io, "        \"note\": %d\n", clamp_int(clip->midi_note, 0, 127));
        ok = ok && io_printf(io, "      },\n");
        ok = ok && io_printf(io, "      \"source_lineage\": {\n");
        ok = ok && io_printf(io, "        \"path\": ");
        ok = ok && json_write_string(io, clip->source_path);
        ok = ok && io_printf(io, ",\n");
        ok = ok && io_printf(io, "        \"start_frame\": %llu,\n", (unsigned long long)clip->source_loop_start_frame);
        ok = ok && io_printf(io, "        \"end_frame\": %llu,\n", (unsigned long long)clip->source_loop_end_frame);
        ok = ok && io_printf(io, "        \"sample_rate\": %d\n", clip->source_sample_rate > 0 ? clip->source_sample_rate : clip->sample_rate);
        ok = ok && io_printf(io, "      }\n");
        ok = ok && io_printf(io, "    }%s\n", i + 1 < app->roster_clip_count ? "," : "");
    }
    ok = ok && io_printf(io, "  ],\n");

    ok = ok && io_printf(io, "  \"patterns\": [\n");
    for (int i = 0; ok && i < app->drum_pattern_count; ++i) {
        const DrumPattern *pattern = &app->drum_patterns[i];
        char pattern_id[APP_STABLE_ID_MAX];
        char midi_path[CLIP_MAX_PATH];
        if (pattern->pattern_id[0]) SDL_strlcpy(pattern_id, pattern->pattern_id, sizeof(pattern_id));
        else SDL_snprintf(pattern_id, sizeof(pattern_id), "pat_%03d", i + 1);
        SDL_snprintf(midi_path, sizeof(midi_path), "%s/%s.mid", VAPORPLANE_PROJECT_PATTERNS_DIRNAME, pattern_id);
        ok = ok && io_printf(io, "    {\n");
        ok = ok && io_printf(io, "      \"pattern_id\": ");
        ok = ok && json_write_string(io, pattern_id);
        ok = ok && io_printf(io, ",\n      \"name\": ");
        ok = ok && json_write_string(io, pattern->name);
        ok = ok && io_printf(io, ",\n      \"midi_file\": ");
        ok = ok && json_write_string(io, midi_path);
        ok = ok && io_printf(io, ",\n      \"length_ticks\": %lld,\n", (long long)pattern->length_ticks);
        ok = ok && io_printf(io, "      \"midi_binding\": {\n");
        ok = ok && io_printf(io, "        \"channel\": %d,\n", clamp_int(pattern->locator_channel, 0, 15) + 1);
        ok = ok && io_printf(io, "        \"note\": %d\n", clamp_int(pattern->locator_note, 0, 127));
        ok = ok && io_printf(io, "      }\n");
        ok = ok && io_printf(io, "    }%s\n", i + 1 < app->drum_pattern_count ? "," : "");
    }
    ok = ok && io_printf(io, "  ],\n");

    ok = ok && io_printf(io, "  \"drum_samples\": [\n");
    bool first_drum_sample = true;
    for (int kit_index = 0; ok && kit_index < app->drum_kit_count; ++kit_index) {
        const DrumKit *kit = &app->drum_kits[kit_index];
        for (int pad_index = 0; ok && pad_index < kit->pad_count; ++pad_index) {
            const DrumPad *pad = &kit->pads[pad_index];
            if (!pad->loaded || !app_project_uses_drum_pad(app, kit_index, pad->note)) continue;
            char rel_path[CLIP_MAX_PATH];
            if (!project_drum_sample_relative_path(kit, pad, rel_path, sizeof(rel_path))) {
                ok = false;
                break;
            }
            if (!first_drum_sample) ok = ok && io_printf(io, ",\n");
            first_drum_sample = false;
            ok = ok && io_printf(io, "    {\n");
            ok = ok && io_printf(io, "      \"kit_id\": ");
            ok = ok && json_write_string(io, kit->kit_id);
            ok = ok && io_printf(io, ",\n      \"kit_name\": ");
            ok = ok && json_write_string(io, kit->name);
            ok = ok && io_printf(io, ",\n      \"note\": %d,\n", clamp_int(pad->note, 0, 127));
            ok = ok && io_printf(io, "      \"name\": ");
            ok = ok && json_write_string(io, pad->name);
            ok = ok && io_printf(io, ",\n      \"path\": ");
            ok = ok && json_write_string(io, rel_path);
            ok = ok && io_printf(io, "\n    }");
        }
    }
    ok = ok && io_printf(io, "\n  ]\n");
    ok = ok && io_printf(io, "}\n");

    ok = SDL_CloseIO(io) && ok;
    return ok;
}

bool app_save_project_bundle(App *app, const char *bundle_path) {
    if (!app || !bundle_path || !bundle_path[0]) {
        if (app) app_set_status(app, "No project path");
        return false;
    }
    if (app->roster_clip_count <= 0 && app->drum_pattern_count <= 0 && !timeline_has_instances(&app->timeline)) {
        app_set_status(app, "Nothing to save yet");
        return false;
    }
    app_project_browser_clear_preview(app);
    if (path_exists_any(bundle_path)) {
        app_set_status(app, "Project bundle already exists");
        return false;
    }
    app_ensure_project_identity(app, bundle_path);
    if (!ensure_directory(bundle_path)) {
        app_set_status(app, "Could not create project bundle");
        return false;
    }

    char samples_dir[CLIP_MAX_PATH];
    path_join(samples_dir, sizeof(samples_dir), bundle_path, VAPORPLANE_PROJECT_SAMPLES_DIRNAME);
    if (!ensure_directory(samples_dir)) {
        app_set_status(app, "Could not create project samples folder");
        return false;
    }

    char patterns_dir[CLIP_MAX_PATH];
    path_join(patterns_dir, sizeof(patterns_dir), bundle_path, VAPORPLANE_PROJECT_PATTERNS_DIRNAME);
    if (app->drum_pattern_count > 0 && !ensure_directory(patterns_dir)) {
        app_set_status(app, "Could not create project patterns folder");
        return false;
    }

    for (int i = 0; i < app->roster_clip_count; ++i) {
        char filename[APP_STABLE_ID_MAX + 8];
        char sample_path[CLIP_MAX_PATH];
        if (!project_sample_filename_for_clip(&app->roster[i], i, filename, sizeof(filename))) {
            app_set_status(app, "Could not name project sample WAV");
            return false;
        }
        path_join(sample_path, sizeof(sample_path), samples_dir, filename);
        if (!write_project_float_wav(&app->roster[i], sample_path)) {
            app_set_status(app, "Could not write project sample WAV");
            return false;
        }
    }

    for (int i = 0; i < app->drum_pattern_count; ++i) {
        char filename[APP_STABLE_ID_MAX + 8];
        char pattern_path[CLIP_MAX_PATH];
        const char *pattern_id = app->drum_patterns[i].pattern_id[0] ? app->drum_patterns[i].pattern_id : "pattern";
        SDL_snprintf(filename, sizeof(filename), "%s.mid", pattern_id);
        path_join(pattern_path, sizeof(pattern_path), patterns_dir, filename);
        if (!write_project_pattern_mid(app, &app->drum_patterns[i], pattern_path)) {
            app_set_status(app, "Could not write drum pattern MIDI");
            return false;
        }
    }

    char drum_samples_dir[CLIP_MAX_PATH];
    path_join(drum_samples_dir, sizeof(drum_samples_dir), samples_dir, "drums");
    bool drum_samples_dir_ready = false;
    for (int kit_index = 0; kit_index < app->drum_kit_count; ++kit_index) {
        DrumKit *kit = &app->drum_kits[kit_index];
        for (int pad_index = 0; pad_index < kit->pad_count; ++pad_index) {
            DrumPad *pad = &kit->pads[pad_index];
            if (!pad->loaded || !app_project_uses_drum_pad(app, kit_index, pad->note)) continue;
            if (!drum_samples_dir_ready) {
                if (!ensure_directory(drum_samples_dir)) {
                    app_set_status(app, "Could not create drum samples folder");
                    return false;
                }
                drum_samples_dir_ready = true;
            }
            char filename[APP_STABLE_ID_MAX + 32];
            char sample_path[CLIP_MAX_PATH];
            if (!project_drum_sample_filename(kit, pad, filename, sizeof(filename))) {
                app_set_status(app, "Could not name drum sample");
                return false;
            }
            path_join(sample_path, sizeof(sample_path), drum_samples_dir, filename);
            if (!write_project_audio_clip_float_wav(&pad->clip, sample_path)) {
                app_set_status(app, "Could not write drum sample WAV");
                return false;
            }
        }
    }

    char timeline_path[CLIP_MAX_PATH];
    char surfaces_path[CLIP_MAX_PATH];
    char preview_path[CLIP_MAX_PATH];
    char project_path[CLIP_MAX_PATH];
    char project_tmp_path[CLIP_MAX_PATH];
    path_join(timeline_path, sizeof(timeline_path), bundle_path, VAPORPLANE_PROJECT_TIMELINE_FILENAME);
    path_join(surfaces_path, sizeof(surfaces_path), bundle_path, VAPORPLANE_PROJECT_SURFACES_FILENAME);
    path_join(preview_path, sizeof(preview_path), bundle_path, VAPORPLANE_PROJECT_PREVIEW_FILENAME);
    path_join(project_path, sizeof(project_path), bundle_path, VAPORPLANE_PROJECT_MANIFEST_FILENAME);
    SDL_snprintf(project_tmp_path, sizeof(project_tmp_path), "%s.tmp", project_path);

    if (!write_project_timeline_mid(app, timeline_path)) {
        app_set_status(app, "Could not write project timeline MIDI");
        return false;
    }
    if (!write_project_surfaces_json(app, surfaces_path)) {
        app_set_status(app, "Could not write project surfaces");
        return false;
    }
    if (!write_project_manifest_json(app, bundle_path, project_tmp_path)) {
        app_set_status(app, "Could not write project manifest");
        SDL_RemovePath(project_tmp_path);
        return false;
    }
    if (!SDL_RenamePath(project_tmp_path, project_path)) {
        app_set_status(app, "Could not finalize project manifest");
        SDL_RemovePath(project_tmp_path);
        return false;
    }

    bool preview_ok = write_project_preview_wav(app, preview_path);
    if (preview_ok) {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Saved %s", bundle_path);
    } else {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Saved %s (preview skipped)", bundle_path);
    }
    return true;
}

static bool resolve_project_export_dir(const App *app, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    if (app && app->project_export_dir[0]) {
        SDL_strlcpy(out, app->project_export_dir, out_size);
        return ensure_directory(out);
    }
    char exports_dir[CLIP_MAX_PATH];
    const char *base = SDL_GetBasePath();
    if (base && base[0]) {
        path_join(exports_dir, sizeof(exports_dir), base, "exports");
        if (ensure_directory(exports_dir)) {
            path_join(out, out_size, exports_dir, "projects");
            if (ensure_directory(out)) return true;
        }
    }
    SDL_strlcpy(exports_dir, "exports", sizeof(exports_dir));
    if (!ensure_directory(exports_dir)) return false;
    path_join(out, out_size, exports_dir, "projects");
    return ensure_directory(out);
}

static bool unique_project_bundle_path(const App *app, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    char dir[CLIP_MAX_PATH];
    if (!resolve_project_export_dir(app, dir, sizeof(dir))) return false;
    char leaf[64];
    for (int i = 1; i <= 999; ++i) {
        SDL_snprintf(leaf, sizeof(leaf), "vaporplane_project_%03d%s", i, VAPORPLANE_PROJECT_BUNDLE_SUFFIX);
        path_join(out, out_size, dir, leaf);
        if (!path_exists_any(out)) return true;
    }
    out[0] = '\0';
    return false;
}

static void app_text_entry_open(App *app,
                                AppTextEntryMode mode,
                                AppTextEntryAction action,
                                const char *title,
                                const char *prompt,
                                const char *initial_text,
                                int max_length) {
    if (!app) return;
    app->text_entry_open = true;
    app->text_entry_mode = mode;
    app->text_entry_action = action;
    SDL_strlcpy(app->text_entry_title, title ? title : "TEXT ENTRY", sizeof(app->text_entry_title));
    SDL_strlcpy(app->text_entry_prompt, prompt ? prompt : "Name", sizeof(app->text_entry_prompt));
    SDL_strlcpy(app->text_entry_text, initial_text ? initial_text : "", sizeof(app->text_entry_text));
    app->text_entry_text[sizeof(app->text_entry_text) - 1] = '\0';
    app->text_entry_error[0] = '\0';
    app->text_entry_max_length = clamp_int(max_length, 1, (int)sizeof(app->text_entry_text) - 1);
    app->text_entry_text[app->text_entry_max_length] = '\0';
    app->text_entry_caret = (int)SDL_strlen(app->text_entry_text);
    app->text_entry_key_row = 0;
    app->text_entry_key_col = 0;
    app->text_entry_uppercase = false;
    app->text_entry_target_roster_index = -1;
    app->text_entry_target_pattern_index = -1;
    app->text_entry_target_lane_index = -1;
    app->project_menu_open = false;
    app->project_menu_selected = 0;
    if (app->window) SDL_StartTextInput(app->window);
    app_set_status(app, "Text entry");
}

static bool text_entry_char_allowed(AppTextEntryMode mode, char c) {
    if ((unsigned char)c < 32 || c == 127) return false;
    bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (mode == APP_TEXT_ENTRY_SEARCH_FILTER) return true;
    if (mode == APP_TEXT_ENTRY_FILENAME_SAFE) return alnum || c == '-' || c == '_' || c == '#';
    return alnum || c == ' ' || c == '-' || c == '_' || c == '\'' ||
           c == '#' || c == '.' || c == '(' || c == ')';
}

static void app_text_entry_set_error(App *app, const char *text) {
    if (!app) return;
    SDL_strlcpy(app->text_entry_error, text ? text : "", sizeof(app->text_entry_error));
    if (text && text[0]) app_set_status(app, text);
}

void app_text_entry_insert_text(App *app, const char *text) {
    if (!app || !app->text_entry_open || !text) return;
    app->text_entry_error[0] = '\0';
    for (size_t i = 0; text[i]; ++i) {
        char c = text[i];
        if (!text_entry_char_allowed(app->text_entry_mode, c)) continue;
        int len = (int)SDL_strlen(app->text_entry_text);
        if (len >= app->text_entry_max_length) break;
        app->text_entry_caret = clamp_int(app->text_entry_caret, 0, len);
        SDL_memmove(&app->text_entry_text[app->text_entry_caret + 1],
                    &app->text_entry_text[app->text_entry_caret],
                    (size_t)(len - app->text_entry_caret + 1));
        app->text_entry_text[app->text_entry_caret] = c;
        app->text_entry_caret++;
    }
}

void app_text_entry_backspace(App *app) {
    if (!app || !app->text_entry_open) return;
    int len = (int)SDL_strlen(app->text_entry_text);
    app->text_entry_caret = clamp_int(app->text_entry_caret, 0, len);
    if (app->text_entry_caret <= 0) return;
    SDL_memmove(&app->text_entry_text[app->text_entry_caret - 1],
                &app->text_entry_text[app->text_entry_caret],
                (size_t)(len - app->text_entry_caret + 1));
    app->text_entry_caret--;
    app->text_entry_error[0] = '\0';
}

void app_text_entry_delete_forward(App *app) {
    if (!app || !app->text_entry_open) return;
    int len = (int)SDL_strlen(app->text_entry_text);
    app->text_entry_caret = clamp_int(app->text_entry_caret, 0, len);
    if (app->text_entry_caret >= len) return;
    SDL_memmove(&app->text_entry_text[app->text_entry_caret],
                &app->text_entry_text[app->text_entry_caret + 1],
                (size_t)(len - app->text_entry_caret));
    app->text_entry_error[0] = '\0';
}

void app_text_entry_move_caret(App *app, int delta) {
    if (!app || !app->text_entry_open || delta == 0) return;
    int len = (int)SDL_strlen(app->text_entry_text);
    app->text_entry_caret = clamp_int(app->text_entry_caret + delta, 0, len);
}

static const char *text_entry_keyboard_row(int row) {
    static const char *rows[] = {
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm-_'",
        "1234567890"
    };
    if (row < 0 || row >= 4) return rows[0];
    return rows[row];
}

static int text_entry_keyboard_row_len(int row) {
    return (int)SDL_strlen(text_entry_keyboard_row(row));
}

void app_text_entry_move_key(App *app, int dx, int dy) {
    if (!app || !app->text_entry_open) return;
    int row = clamp_int(app->text_entry_key_row + dy, 0, 3);
    int col = app->text_entry_key_col + dx;
    int len = text_entry_keyboard_row_len(row);
    while (col < 0) col += len;
    col %= len;
    app->text_entry_key_row = row;
    app->text_entry_key_col = col;
}

void app_text_entry_insert_selected_key(App *app) {
    if (!app || !app->text_entry_open) return;
    const char *row = text_entry_keyboard_row(app->text_entry_key_row);
    int len = (int)SDL_strlen(row);
    if (len <= 0) return;
    int col = clamp_int(app->text_entry_key_col, 0, len - 1);
    char c = row[col];
    if (app->text_entry_uppercase && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    char text[2] = { c, '\0' };
    app_text_entry_insert_text(app, text);
}

void app_text_entry_insert_separator(App *app) {
    if (!app || !app->text_entry_open) return;
    const char *separator = app->text_entry_mode == APP_TEXT_ENTRY_FILENAME_SAFE ? "_" : " ";
    app_text_entry_insert_text(app, separator);
}

void app_text_entry_toggle_shift(App *app) {
    if (!app || !app->text_entry_open) return;
    app->text_entry_uppercase = !app->text_entry_uppercase;
}

void app_text_entry_cancel(App *app) {
    if (!app || !app->text_entry_open) return;
    app->text_entry_open = false;
    app->text_entry_action = APP_TEXT_ENTRY_ACTION_NONE;
    app->text_entry_target_roster_index = -1;
    app->text_entry_target_pattern_index = -1;
    app->text_entry_target_lane_index = -1;
    app->text_entry_error[0] = '\0';
    if (app->window) SDL_StopTextInput(app->window);
    app_set_status(app, "Text entry canceled");
}

static bool sanitize_project_bundle_leaf(const char *display_name, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!display_name) return false;

    size_t start = 0;
    size_t end = SDL_strlen(display_name);
    while (start < end && isspace((unsigned char)display_name[start])) start++;
    while (end > start && isspace((unsigned char)display_name[end - 1])) end--;
    if (end <= start) return false;

    char clean[APP_SAMPLE_NAME_MAX];
    size_t write = 0;
    bool last_underscore = false;
    for (size_t i = start; i < end && write + 1 < sizeof(clean); ++i) {
        char c = display_name[i];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '#';
        bool dot = c == '.';
        bool whitespace = isspace((unsigned char)c);
        char out_c = (safe || dot) ? c : '_';
        if (whitespace) out_c = '_';
        if (out_c == '_') {
            if (last_underscore) continue;
            last_underscore = true;
        } else {
            last_underscore = false;
        }
        clean[write++] = out_c;
    }
    while (write > 0 && clean[0] == '.') {
        SDL_memmove(clean, clean + 1, write - 1);
        write--;
    }
    while (write > 0 && clean[write - 1] == '_') write--;
    if (write == 0) return false;
    clean[write] = '\0';

    size_t suffix_len = SDL_strlen(VAPORPLANE_PROJECT_BUNDLE_SUFFIX);
    size_t clean_len = SDL_strlen(clean);
    if (clean_len > suffix_len &&
        SDL_strcasecmp(clean + clean_len - suffix_len, VAPORPLANE_PROJECT_BUNDLE_SUFFIX) == 0) {
        SDL_strlcpy(out, clean, out_size);
    } else {
        SDL_snprintf(out, out_size, "%s%s", clean, VAPORPLANE_PROJECT_BUNDLE_SUFFIX);
    }
    return out[0] != '\0';
}

static void trim_display_name(const char *input, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!input) return;
    size_t start = 0;
    size_t end = SDL_strlen(input);
    while (start < end && isspace((unsigned char)input[start])) start++;
    while (end > start && isspace((unsigned char)input[end - 1])) end--;
    size_t len = end - start;
    if (len >= out_size) len = out_size - 1;
    SDL_memcpy(out, input + start, len);
    out[len] = '\0';
}

static bool sanitize_wav_filename_leaf(const char *filename_text, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!filename_text) return false;

    char trimmed[APP_SAMPLE_NAME_MAX];
    trim_display_name(filename_text, trimmed, sizeof(trimmed));
    if (!trimmed[0]) return false;

    char clean[APP_SAMPLE_NAME_MAX + 8];
    size_t write = 0;
    bool last_underscore = false;
    for (size_t read = 0; trimmed[read] && write + 1 < sizeof(clean); ++read) {
        char c = trimmed[read];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '#';
        bool dot = c == '.';
        bool whitespace = isspace((unsigned char)c);
        char out_c = (safe || dot) ? c : '_';
        if (whitespace) out_c = '_';
        if (out_c == '_') {
            if (last_underscore) continue;
            last_underscore = true;
        } else {
            last_underscore = false;
        }
        clean[write++] = out_c;
    }
    while (write > 0 && clean[0] == '.') {
        SDL_memmove(clean, clean + 1, write - 1);
        write--;
    }
    while (write > 0 && clean[write - 1] == '_') write--;
    if (write == 0) return false;
    clean[write] = '\0';

    const char *suffix = ".wav";
    size_t suffix_len = SDL_strlen(suffix);
    size_t clean_len = SDL_strlen(clean);
    if (clean_len > suffix_len &&
        SDL_strcasecmp(clean + clean_len - suffix_len, suffix) == 0) {
        SDL_strlcpy(out, clean, out_size);
    } else {
        SDL_snprintf(out, out_size, "%s%s", clean, suffix);
    }
    return out[0] != '\0';
}

static bool app_save_project_bundle_named(App *app, const char *display_name) {
    if (!app) return false;
    char trimmed[APP_SAMPLE_NAME_MAX];
    trim_display_name(display_name, trimmed, sizeof(trimmed));
    if (!trimmed[0]) {
        app_text_entry_set_error(app, "Enter a project name");
        return false;
    }

    char leaf[APP_SAMPLE_NAME_MAX + 16];
    if (!sanitize_project_bundle_leaf(trimmed, leaf, sizeof(leaf))) {
        app_text_entry_set_error(app, "Enter a project name");
        return false;
    }

    char dir[CLIP_MAX_PATH];
    if (!resolve_project_export_dir(app, dir, sizeof(dir))) {
        app_text_entry_set_error(app, "Could not open project folder");
        return false;
    }
    char path[CLIP_MAX_PATH];
    path_join(path, sizeof(path), dir, leaf);
    if (path_exists_any(path)) {
        app_text_entry_set_error(app, "Project already exists");
        return false;
    }

    char previous_id[APP_STABLE_ID_MAX];
    char previous_name[APP_SAMPLE_NAME_MAX];
    SDL_strlcpy(previous_id, app->project_id, sizeof(previous_id));
    SDL_strlcpy(previous_name, app->project_name, sizeof(previous_name));
    SDL_strlcpy(app->project_name, trimmed, sizeof(app->project_name));
    if (!app_save_project_bundle(app, path)) {
        SDL_strlcpy(app->project_id, previous_id, sizeof(app->project_id));
        SDL_strlcpy(app->project_name, previous_name, sizeof(app->project_name));
        if (!app->text_entry_error[0]) app_text_entry_set_error(app, app->status_text[0] ? app->status_text : "Could not save project");
        return false;
    }
    return true;
}

static bool app_rename_roster_clip_named(App *app, int roster_index, const char *display_name) {
    if (!app || roster_index < 0 || roster_index >= app->roster_clip_count) {
        app_text_entry_set_error(app, "No roster clip selected");
        return false;
    }

    char trimmed[APP_ROSTER_CLIP_NAME_MAX];
    trim_display_name(display_name, trimmed, sizeof(trimmed));
    if (!trimmed[0]) {
        app_text_entry_set_error(app, "Enter a roster name");
        return false;
    }

    SDL_strlcpy(app->roster[roster_index].name, trimmed, sizeof(app->roster[roster_index].name));
    if (app->waveform_source_mode == WAVEFORM_SOURCE_ROSTER &&
        app->waveform_source_roster_index == roster_index) {
        SDL_strlcpy(app->waveform_source_name, trimmed, sizeof(app->waveform_source_name));
    }
    app->selected_roster_clip = roster_index;
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Renamed roster clip: %s", trimmed);
    return true;
}

static bool parse_velocity_text(const char *text, int *velocity) {
    if (!text || !velocity) return false;
    char trimmed[16];
    trim_display_name(text, trimmed, sizeof(trimmed));
    if (!trimmed[0]) return false;
    int value = 0;
    for (size_t i = 0; trimmed[i]; ++i) {
        if (!isdigit((unsigned char)trimmed[i])) return false;
        value = value * 10 + (trimmed[i] - '0');
        if (value > 127) return false;
    }
    if (value < 1 || value > 127) return false;
    *velocity = value;
    return true;
}

static bool app_apply_lane_velocity_named(App *app, int lane_index, const char *velocity_text) {
    if (!app || !timeline_lane_index_valid(lane_index)) {
        app_text_entry_set_error(app, "No lane selected");
        return false;
    }
    int velocity = 0;
    if (!parse_velocity_text(velocity_text, &velocity)) {
        app_text_entry_set_error(app, "Enter velocity 1-127");
        return false;
    }

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    int count = 0;
    for (int i = 0; i < lane->instance_count; ++i) {
        lane->instances[i].midi_velocity = velocity;
        count++;
    }
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    app->selected_timeline_lane = lane_index;
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 "Lane %d velocity %d (%d clips)", lane_index + 1, velocity, count);
    return true;
}

void app_text_entry_confirm(App *app) {
    if (!app || !app->text_entry_open) return;
    bool ok = false;
    switch (app->text_entry_action) {
        case APP_TEXT_ENTRY_ACTION_SAVE_AS_PROJECT:
            ok = app_save_project_bundle_named(app, app->text_entry_text);
            break;
        case APP_TEXT_ENTRY_ACTION_EXPORT_TIMELINE_WAV:
            ok = app_export_timeline_wav_named(app, app->text_entry_text);
            break;
        case APP_TEXT_ENTRY_ACTION_EXPORT_ROSTER_WAV:
            ok = app_export_roster_clip_wav_named(app,
                                                  app->text_entry_target_roster_index,
                                                  app->text_entry_text);
            break;
        case APP_TEXT_ENTRY_ACTION_RENAME_ROSTER_CLIP:
            ok = app_rename_roster_clip_named(app,
                                              app->text_entry_target_roster_index,
                                              app->text_entry_text);
            break;
        case APP_TEXT_ENTRY_ACTION_RENAME_DRUM_PATTERN:
            ok = app_rename_drum_pattern_named(app,
                                               app->text_entry_target_pattern_index,
                                               app->text_entry_text);
            break;
        case APP_TEXT_ENTRY_ACTION_APPLY_LANE_VELOCITY:
            ok = app_apply_lane_velocity_named(app,
                                               app->text_entry_target_lane_index,
                                               app->text_entry_text);
            break;
        case APP_TEXT_ENTRY_ACTION_NONE:
        default:
            ok = true;
            break;
    }
    if (!ok) return;
    app->text_entry_open = false;
    app->text_entry_action = APP_TEXT_ENTRY_ACTION_NONE;
    app->text_entry_target_roster_index = -1;
    app->text_entry_target_pattern_index = -1;
    app->text_entry_target_lane_index = -1;
    app->text_entry_error[0] = '\0';
    if (app->window) SDL_StopTextInput(app->window);
}

static bool app_save_project_bundle_default(App *app) {
    char path[CLIP_MAX_PATH];
    if (!unique_project_bundle_path(app, path, sizeof(path))) {
        app_set_status(app, "Could not create project save path");
        return false;
    }
    return app_save_project_bundle(app, path);
}

static bool resolve_render_export_dir(const App *app, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    if (app && app->render_export_dir[0]) {
        SDL_strlcpy(out, app->render_export_dir, out_size);
        return ensure_directory(out);
    }
    char exports_dir[CLIP_MAX_PATH];
    const char *base = SDL_GetBasePath();
    if (base && base[0]) {
        path_join(exports_dir, sizeof(exports_dir), base, "exports");
        if (ensure_directory(exports_dir)) {
            path_join(out, out_size, exports_dir, "renders");
            if (ensure_directory(out)) return true;
        }
    }
    SDL_strlcpy(exports_dir, "exports", sizeof(exports_dir));
    if (!ensure_directory(exports_dir)) return false;
    path_join(out, out_size, exports_dir, "renders");
    return ensure_directory(out);
}

static void safe_project_render_stem(const App *app, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    const char *name = app && app->project_name[0] ? app->project_name :
                       (app && app->project_id[0] ? app->project_id : "vaporplane_timeline");
    char clean[APP_SAMPLE_NAME_MAX];
    size_t write = 0;
    for (size_t read = 0; name[read] && write + 1 < sizeof(clean); ++read) {
        char c = name[read];
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '#';
        clean[write++] = keep ? c : '_';
    }
    if (write == 0) clean[write++] = 'r';
    clean[write] = '\0';
    SDL_strlcpy(out, clean, out_size);
}

static void safe_project_render_filename(const App *app, char *out, size_t out_size) {
    char stem[APP_SAMPLE_NAME_MAX];
    safe_project_render_stem(app, stem, sizeof(stem));
    SDL_snprintf(out, out_size, "%s.wav", stem[0] ? stem : "vaporplane_timeline");
}

static bool default_timeline_render_path(const App *app, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    char dir[CLIP_MAX_PATH];
    char filename[APP_SAMPLE_NAME_MAX + 8];
    if (!resolve_render_export_dir(app, dir, sizeof(dir))) return false;
    safe_project_render_filename(app, filename, sizeof(filename));
    path_join(out, out_size, dir, filename);
    return out[0] != '\0';
}

static bool app_export_timeline_wav_named(App *app, const char *filename_text) {
    if (!app) return false;
    char leaf[APP_SAMPLE_NAME_MAX + 8];
    if (!sanitize_wav_filename_leaf(filename_text, leaf, sizeof(leaf))) {
        app_text_entry_set_error(app, "Enter a file name");
        return false;
    }

    char dir[CLIP_MAX_PATH];
    if (!resolve_render_export_dir(app, dir, sizeof(dir))) {
        app_text_entry_set_error(app, "Could not open render folder");
        return false;
    }

    char path[CLIP_MAX_PATH];
    path_join(path, sizeof(path), dir, leaf);
    if (path_exists_any(path)) {
        app_text_entry_set_error(app, "File already exists");
        return false;
    }

    if (!app_export_timeline_wav(app, path)) {
        app_text_entry_set_error(app, app->status_text[0] ? app->status_text : "Could not export timeline WAV");
        return false;
    }
    app->project_menu_open = false;
    app->project_menu_selected = 0;
    return true;
}

static int64_t timeline_last_instance_end_tick(const MasterTimeline *timeline) {
    int64_t end_tick = 0;
    if (!timeline) return 0;
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        const TimelineLane *lane = &timeline->lanes[lane_index];
        for (int i = 0; i < lane->instance_count; ++i) {
            const TimelineInstance *instance = &lane->instances[i];
            if (instance->duration_ticks <= 0) continue;
            int64_t end = instance->start_tick + instance->duration_ticks;
            if (end > end_tick) end_tick = end;
        }
    }
    return end_tick;
}

static bool app_timeline_export_range(const App *app, int64_t *start, int64_t *end) {
    if (!app || !start || !end) return false;
    int64_t last_end = timeline_last_instance_end_tick(&app->timeline);
    if (last_end <= 0) return false;

    int64_t s = 0;
    int64_t e = last_end;
    if (app->timeline.play_range_custom &&
        app->timeline.play_range_start_tick >= 0 &&
        app->timeline.play_range_end_tick > app->timeline.play_range_start_tick) {
        s = app->timeline.play_range_start_tick;
        e = app->timeline.play_range_end_tick;
    }
    if (s < 0) s = 0;
    if (e <= s) return false;
    *start = s;
    *end = e;
    return true;
}

static bool app_timeline_has_renderable_clip_in_range(const App *app, int64_t start_tick, int64_t end_tick) {
    if (!app || end_tick <= start_tick) return false;
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        const TimelineLane *lane = &app->timeline.lanes[lane_index];
        if (lane->muted) continue;
        for (int i = 0; i < lane->instance_count; ++i) {
            const TimelineInstance *instance = &lane->instances[i];
            if (instance->duration_ticks <= 0) continue;
            int64_t instance_end = instance->start_tick + instance->duration_ticks;
            if (instance_end <= start_tick || instance->start_tick >= end_tick) continue;
            if (instance->kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
                if (!app_pattern_index_valid(app, instance->pattern_index) ||
                    !app_kit_index_valid(app, lane->drum_kit_index)) {
                    continue;
                }
                const DrumPattern *pattern = &app->drum_patterns[instance->pattern_index];
                const DrumKit *kit = &app->drum_kits[lane->drum_kit_index];
                for (int event_index = 0; event_index < pattern->event_count; ++event_index) {
                    int note = pattern->events[event_index].note;
                    for (int pad_index = 0; pad_index < kit->pad_count; ++pad_index) {
                        const DrumPad *pad = &kit->pads[pad_index];
                        if (pad->note == note && pad->loaded && pad->clip.samples && pad->clip.frame_count > 1) return true;
                    }
                }
                continue;
            }
            if (instance->roster_clip_index < 0 || instance->roster_clip_index >= app->roster_clip_count) continue;
            const RosterClip *clip = &app->roster[instance->roster_clip_index];
            if (clip->samples && clip->frame_count > 1 && clip->sample_rate > 0 && clip->channels > 0) {
                return true;
            }
        }
    }
    return false;
}

static bool app_timeline_first_renderable_range(const App *app, int64_t *start_tick, int64_t *end_tick) {
    if (!app || !start_tick || !end_tick) return false;
    bool found = false;
    int64_t first = 0;
    int64_t last = 0;
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        const TimelineLane *lane = &app->timeline.lanes[lane_index];
        if (lane->muted) continue;
        for (int i = 0; i < lane->instance_count; ++i) {
            const TimelineInstance *instance = &lane->instances[i];
            if (instance->duration_ticks <= 0) continue;
            bool renderable = false;
            if (instance->kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
                if (app_pattern_index_valid(app, instance->pattern_index) &&
                    app_kit_index_valid(app, lane->drum_kit_index)) {
                    const DrumPattern *pattern = &app->drum_patterns[instance->pattern_index];
                    const DrumKit *kit = &app->drum_kits[lane->drum_kit_index];
                    for (int event_index = 0; event_index < pattern->event_count && !renderable; ++event_index) {
                        int note = pattern->events[event_index].note;
                        for (int pad_index = 0; pad_index < kit->pad_count; ++pad_index) {
                            const DrumPad *pad = &kit->pads[pad_index];
                            if (pad->note == note && pad->loaded && pad->clip.samples && pad->clip.frame_count > 1) {
                                renderable = true;
                                break;
                            }
                        }
                    }
                }
            } else {
                if (instance->roster_clip_index < 0 || instance->roster_clip_index >= app->roster_clip_count) continue;
                const RosterClip *clip = &app->roster[instance->roster_clip_index];
                renderable = clip->samples && clip->frame_count > 1 && clip->sample_rate > 0 && clip->channels > 0;
            }
            if (!renderable) continue;
            int64_t end = instance->start_tick + instance->duration_ticks;
            if (!found || instance->start_tick < first) first = instance->start_tick;
            if (!found || end > last) last = end;
            found = true;
        }
    }
    if (!found || last <= first) return false;
    *start_tick = first;
    *end_tick = last;
    return true;
}

static bool app_timeline_preview_range(const App *app, int64_t *start_tick, int64_t *end_tick) {
    if (!app || !start_tick || !end_tick) return false;
    if (app->timeline.play_range_custom &&
        app->timeline.play_range_start_tick >= 0 &&
        app->timeline.play_range_end_tick > app->timeline.play_range_start_tick) {
        int64_t start = app->timeline.play_range_start_tick;
        int64_t end = app->timeline.play_range_end_tick;
        if (!app_timeline_has_renderable_clip_in_range(app, start, end)) return false;
        *start_tick = start;
        *end_tick = end;
        return true;
    }
    return app_timeline_first_renderable_range(app, start_tick, end_tick);
}

static bool write_project_preview_wav(App *app, const char *path) {
    if (!app || !path || !path[0]) return false;
    int64_t start_tick = 0;
    int64_t end_tick = 0;
    if (!app_timeline_preview_range(app, &start_tick, &end_tick)) return false;

    FloatWavStreamWriter writer;
    if (!float_wav_stream_open(&writer, path)) return false;

    AudioEngine render_audio;
    audio_engine_init_offline_timeline_render(&render_audio,
                                              &app->audio,
                                              app->roster,
                                              &app->roster_clip_count,
                                              &app->timeline,
                                              VAPORPLANE_PROJECT_SAMPLE_RATE);

    const size_t preview_frames = (size_t)VAPORPLANE_PROJECT_SAMPLE_RATE * 7u;
    AudioTimelineRenderState state;
    audio_timeline_render_state_init(&state, start_tick, end_tick);
    size_t written_frames = 0;
    bool had_active = false;
    float block[AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES * 2];

    while (written_frames < preview_frames) {
        int block_frames = (int)(preview_frames - written_frames);
        if (block_frames > AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES) block_frames = AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES;
        int active_count = 0;
        if (!state.finished) {
            active_count = audio_engine_render_timeline_block(&render_audio,
                                                              &state,
                                                              block,
                                                              block_frames,
                                                              VAPORPLANE_PROJECT_SAMPLE_RATE);
        } else {
            audio_engine_render_master_fx_silence_block(&render_audio,
                                                        block,
                                                        block_frames,
                                                        VAPORPLANE_PROJECT_SAMPLE_RATE);
        }
        if (active_count > 0) had_active = true;
        if (!float_wav_stream_write(&writer, block, block_frames)) {
            float_wav_stream_abort(&writer);
            SDL_RemovePath(path);
            return false;
        }
        written_frames += (size_t)block_frames;
    }

    if (!had_active || !float_wav_stream_close(&writer)) {
        float_wav_stream_abort(&writer);
        SDL_RemovePath(path);
        return false;
    }
    return true;
}

bool app_export_timeline_wav(App *app, const char *path) {
    if (!app) return false;
    int64_t start_tick = 0;
    int64_t end_tick = 0;
    if (!app_timeline_export_range(app, &start_tick, &end_tick)) {
        app_set_status(app, "No timeline range to render");
        return false;
    }
    if (!app_timeline_has_renderable_clip_in_range(app, start_tick, end_tick)) {
        app_set_status(app, "No renderable timeline clips");
        return false;
    }

    char output_path[CLIP_MAX_PATH];
    if (path && path[0]) {
        SDL_strlcpy(output_path, path, sizeof(output_path));
    } else if (!default_timeline_render_path(app, output_path, sizeof(output_path))) {
        app_set_status(app, "Could not create render path");
        return false;
    }

    double speed = (double)timeline_effective_tape_speed(&app->timeline);
    double seconds = timeline_seconds_between_ticks(&app->timeline, (double)start_tick, (double)end_tick);
    if (speed <= 0.0) speed = 1.0;
    seconds /= speed;
    if (seconds <= 0.0) {
        app_set_status(app, "No timeline range to render");
        return false;
    }

    size_t output_frames = (size_t)ceil(seconds * (double)VAPORPLANE_PROJECT_SAMPLE_RATE);
    if (output_frames == 0) {
        app_set_status(app, "No timeline range to render");
        return false;
    }
    AudioEngine render_audio;
    audio_engine_init_offline_timeline_render(&render_audio,
                                              &app->audio,
                                              app->roster,
                                              &app->roster_clip_count,
                                              &app->timeline,
                                              VAPORPLANE_PROJECT_SAMPLE_RATE);
    bool render_tail = audio_engine_timeline_has_tail_fx(&render_audio);
    double tail_cap_seconds = audio_engine_timeline_tail_cap_seconds(&render_audio);
    size_t max_tail_frames = render_tail && tail_cap_seconds > 0.0 ?
        (size_t)ceil(tail_cap_seconds * (double)VAPORPLANE_PROJECT_SAMPLE_RATE) : 0u;
    if (output_frames > ((size_t)-1) - max_tail_frames ||
        output_frames + max_tail_frames > ((size_t)-1) / (sizeof(float) * 2u)) {
        app_set_status(app, "Timeline render is too large");
        return false;
    }
    Uint64 data_size = (Uint64)(output_frames + max_tail_frames) * 2u * sizeof(float);
    if (data_size > 0xffffffffu) {
        app_set_status(app, "Timeline render is too large");
        return false;
    }

    FloatWavStreamWriter writer;
    if (!float_wav_stream_open(&writer, output_path)) {
        app_set_status(app, "Could not write timeline render");
        return false;
    }

    AudioTimelineRenderState state;
    audio_timeline_render_state_init(&state, start_tick, end_tick);
    size_t written_frames = 0;
    float peak = 0.0f;
    bool had_active = false;
    float block[AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES * 2];
    while (written_frames < output_frames) {
        int block_frames = (int)(output_frames - written_frames);
        if (block_frames > AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES) block_frames = AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES;
        int active_count = audio_engine_render_timeline_block(&render_audio,
                                                              &state,
                                                              block,
                                                              block_frames,
                                                              VAPORPLANE_PROJECT_SAMPLE_RATE);
        if (active_count > 0) had_active = true;
        for (int frame = 0; frame < block_frames; ++frame) {
            float left = block[frame * 2];
            float right = block[frame * 2 + 1];
            float abs_left = fabsf(left);
            float abs_right = fabsf(right);
            if (abs_left > peak) peak = abs_left;
            if (abs_right > peak) peak = abs_right;
        }
        if (!float_wav_stream_write(&writer, block, block_frames)) {
            float_wav_stream_abort(&writer);
            SDL_RemovePath(output_path);
            app_set_status(app, "Could not write timeline render");
            return false;
        }
        written_frames += (size_t)block_frames;
        if (state.finished) break;
    }

    if (!had_active) {
        float_wav_stream_abort(&writer);
        SDL_RemovePath(output_path);
        app_set_status(app, "No renderable timeline clips");
        return false;
    }

    if (render_tail && max_tail_frames > 0) {
        size_t tail_frames = 0;
        while (tail_frames < max_tail_frames) {
            int block_frames = (int)(max_tail_frames - tail_frames);
            if (block_frames > AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES) block_frames = AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES;
            audio_engine_render_master_fx_silence_block(&render_audio,
                                                        block,
                                                        block_frames,
                                                        VAPORPLANE_PROJECT_SAMPLE_RATE);
            float block_peak = 0.0f;
            for (int frame = 0; frame < block_frames; ++frame) {
                float abs_left = fabsf(block[frame * 2]);
                float abs_right = fabsf(block[frame * 2 + 1]);
                if (abs_left > block_peak) block_peak = abs_left;
                if (abs_right > block_peak) block_peak = abs_right;
                if (abs_left > peak) peak = abs_left;
                if (abs_right > peak) peak = abs_right;
            }
            if (!float_wav_stream_write(&writer, block, block_frames)) {
                float_wav_stream_abort(&writer);
                SDL_RemovePath(output_path);
                app_set_status(app, "Could not write timeline render");
                return false;
            }
            tail_frames += (size_t)block_frames;
            written_frames += (size_t)block_frames;
            if (block_frames == AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES && block_peak < 0.0000316f) break;
        }
    }

    if (!float_wav_stream_close(&writer)) {
        SDL_RemovePath(output_path);
        app_set_status(app, "Could not write timeline render");
        return false;
    }

    const char *file = path_basename(output_path);
    if (peak > 1.0f) {
        SDL_snprintf(app->status_text, sizeof(app->status_text),
                     "Rendered %s (CLIPPED peak %.2f)",
                     file,
                     peak);
    } else {
        SDL_snprintf(app->status_text, sizeof(app->status_text),
                     "Rendered %s (peak %.2f)",
                     file,
                     peak);
    }
    return true;
}

static void app_reset_timeline_bounce_state(App *app) {
    if (!app) return;
    app->timeline_bounce_active = false;
    app->timeline_bounce_samples = NULL;
    app->timeline_bounce_target_frames = 0;
    app->timeline_bounce_recorded_frames = 0;
    app->timeline_bounce_sample_rate = 0;
    app->timeline_bounce_start_tick = 0;
    app->timeline_bounce_end_tick = 0;
    app->timeline_bounce_prev_metronome_enabled = false;
    app->timeline_bounce_prev_play_range_loop_enabled = false;
}

static void app_restore_timeline_bounce_settings(App *app) {
    if (!app) return;
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    app->transport.metronome_enabled = app->timeline_bounce_prev_metronome_enabled;
    if (!app->transport.metronome_enabled) app->transport.metronome_env = 0.0f;
    app->timeline.play_range_loop_enabled = app->timeline_bounce_prev_play_range_loop_enabled;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    sync_transport_from_app(app);
}

static void app_clear_queued_audio(App *app) {
    if (!app || !app->audio.stream) return;
    SDL_LockAudioStream(app->audio.stream);
    SDL_ClearAudioStream(app->audio.stream);
    SDL_UnlockAudioStream(app->audio.stream);
}

static bool app_append_timeline_bounce_to_roster(App *app,
                                                 float *samples,
                                                 size_t frame_count,
                                                 int sample_rate,
                                                 int64_t start_tick,
                                                 int64_t end_tick) {
    if (!app || !samples || frame_count < APP_MIN_CAPTURE_FRAMES || sample_rate <= 0 || end_tick <= start_tick) {
        return false;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        app_set_status(app, "Roster full");
        return false;
    }

    RosterClip next;
    SDL_memset(&next, 0, sizeof(next));
    generate_stable_id("sample", next.sample_id, sizeof(next.sample_id));
    generate_stable_id("roster", next.roster_clip_id, sizeof(next.roster_clip_id));
    const char *base = "timeline bounce";
    int number = next_capture_number(app, base);
    SDL_snprintf(next.name, sizeof(next.name), "%s#%03d", base, number);
    SDL_strlcpy(next.source_path, "timeline bounce", sizeof(next.source_path));
    next.source_loop_start_frame = 0;
    next.source_loop_end_frame = frame_count;
    next.source_sample_rate = sample_rate;
    next.loop_start_frame = 0;
    next.loop_end_frame = frame_count;
    next.sample_rate = sample_rate;
    next.channels = 2;
    next.frame_count = frame_count;
    next.samples = samples;
    next.midi_note = app_next_available_midi_note(app);
    next.midi_channel = 0;
    next.midi_velocity = 127;
    next.color = roster_color_for_append(app);

    int ticks_per_beat = app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat : 960;
    int beats_per_bar = app->timeline.timeline_beats_per_bar > 0 ? app->timeline.timeline_beats_per_bar : 4;
    int beat_unit = app->timeline.timeline_beat_unit > 0 ? app->timeline.timeline_beat_unit : 4;
    double target_beats = (double)(end_tick - start_tick) / (double)ticks_per_beat;
    double source_bpm = timeline_audible_bpm_at_tick(&app->timeline, (double)start_tick);
    next.tempo_calibrated = source_bpm > 0.0 && target_beats > 0.0;
    if (next.tempo_calibrated) {
        next.source_bpm = timeline_clamp_bpm(source_bpm);
        next.beats_per_bar = beats_per_bar;
        next.beat_unit = beat_unit;
        next.target_beats = target_beats;
        next.target_bars = target_beats / (double)beats_per_bar;
        next.downbeat_offset_frames = 0;
    } else {
        next.source_bpm = 0.0;
        next.beats_per_bar = 4;
        next.beat_unit = 4;
        next.target_beats = 0.0;
        next.target_bars = 0.0;
        next.downbeat_offset_frames = 0;
    }

    if (app->audio.stream && !SDL_LockAudioStream(app->audio.stream)) {
        app_set_status(app, "Could not lock audio stream");
        return false;
    }
    int roster_index = app->roster_clip_count;
    app->roster[roster_index] = next;
    app->roster_clip_count++;
    app->selected_roster_clip = roster_index;
    app->selected_roster_clip_armed = false;
    app_clamp_roster_scroll(app, app->roster_visible_rows);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    SDL_snprintf(app->status_text, sizeof(app->status_text), "Bounced %s to roster", app->roster[roster_index].name);
    return true;
}

static void app_finish_timeline_bounce(App *app, size_t recorded_frames) {
    if (!app || !app->timeline_bounce_active) return;

    audio_engine_cancel_timeline_bounce_recording(&app->audio);
    audio_engine_stop_timeline(&app->audio, false);
    app_restore_timeline_bounce_settings(app);

    float *samples = app->timeline_bounce_samples;
    size_t frame_count = recorded_frames;
    if (frame_count > app->timeline_bounce_target_frames) frame_count = app->timeline_bounce_target_frames;
    int sample_rate = app->timeline_bounce_sample_rate;
    int64_t start_tick = app->timeline_bounce_start_tick;
    int64_t end_tick = app->timeline_bounce_end_tick;

    app_reset_timeline_bounce_state(app);
    bool appended = app_append_timeline_bounce_to_roster(app, samples, frame_count, sample_rate, start_tick, end_tick);
    if (!appended) {
        SDL_free(samples);
        if (!app->status_text[0]) app_set_status(app, "Could not bounce to roster");
    }
}

static void app_abort_timeline_bounce(App *app, const char *status) {
    if (!app || (!app->timeline_bounce_active && !app->timeline_bounce_samples)) return;
    audio_engine_cancel_timeline_bounce_recording(&app->audio);
    audio_engine_stop_timeline(&app->audio, false);
    app_clear_queued_audio(app);
    app_restore_timeline_bounce_settings(app);
    SDL_free(app->timeline_bounce_samples);
    app_reset_timeline_bounce_state(app);
    app_set_status(app, status && status[0] ? status : "Bounce cancelled");
}

void app_cancel_timeline_bounce(App *app) {
    app_abort_timeline_bounce(app, "Bounce cancelled");
}

static void app_update_timeline_bounce(App *app) {
    if (!app || !app->timeline_bounce_active) return;
    AudioTimelineBounceRecordingState state;
    audio_engine_get_timeline_bounce_recording_state(&app->audio, &state);
    app->timeline_bounce_recorded_frames = state.recorded_frames;
    if (state.failed) {
        app_abort_timeline_bounce(app, "Bounce recording failed");
        return;
    }
    if (state.complete) {
        app_finish_timeline_bounce(app, state.recorded_frames);
        return;
    }
    if (!audio_engine_timeline_is_playing(&app->audio)) {
        if (state.recorded_frames >= APP_MIN_CAPTURE_FRAMES) {
            app_finish_timeline_bounce(app, state.recorded_frames);
        } else {
            app_abort_timeline_bounce(app, "Bounce stopped");
        }
    }
}

static bool app_start_timeline_bounce(App *app) {
    if (!app) return false;
    if (app->timeline_bounce_active) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Bounce already recording");
        return false;
    }
    if (!app->audio.stream) {
        app_timeline_clear_context_menu(app);
        app_set_audio_unavailable_status(app);
        return false;
    }

    sync_timeline_play_range(app);
    int64_t start_tick = 0;
    int64_t end_tick = 0;
    if (!app->timeline.initialized || !timeline_has_instances(&app->timeline) ||
        !app_timeline_export_range(app, &start_tick, &end_tick)) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No timeline range to bounce");
        return false;
    }
    if (!app_timeline_has_renderable_clip_in_range(app, start_tick, end_tick)) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No renderable timeline clips");
        return false;
    }
    if (app->roster_clip_count >= APP_MAX_ROSTER_CLIPS) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Roster full");
        return false;
    }

    int sample_rate = app->audio.spec.freq > 0 ? app->audio.spec.freq : VAPORPLANE_PROJECT_SAMPLE_RATE;
    double speed = (double)timeline_effective_tape_speed(&app->timeline);
    double seconds = timeline_seconds_between_ticks(&app->timeline, (double)start_tick, (double)end_tick);
    if (speed <= 0.0) speed = 1.0;
    seconds /= speed;
    if (seconds <= 0.0) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No timeline range to bounce");
        return false;
    }
    size_t target_frames = (size_t)ceil(seconds * (double)sample_rate);
    if (target_frames < APP_MIN_CAPTURE_FRAMES || target_frames > APP_MAX_CAPTURE_FRAMES ||
        target_frames > SIZE_MAX / 2u ||
        target_frames * 2u > SIZE_MAX / sizeof(float)) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Bounce is too large");
        return false;
    }
    size_t sample_count = target_frames * 2u;
    size_t byte_count = sample_count * sizeof(float);
    if (byte_count > APP_MAX_CAPTURE_BYTES) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Bounce is too large");
        return false;
    }

    float *samples = (float *)SDL_malloc(byte_count);
    if (!samples) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "Memory allocation failed");
        return false;
    }
    SDL_memset(samples, 0, byte_count);

    app_stop_active_audio(app);
    app_timeline_clear_context_menu(app);
    app->project_menu_open = false;
    app->timeline_bounce_active = true;
    app->timeline_bounce_samples = samples;
    app->timeline_bounce_target_frames = target_frames;
    app->timeline_bounce_recorded_frames = 0;
    app->timeline_bounce_sample_rate = sample_rate;
    app->timeline_bounce_start_tick = start_tick;
    app->timeline_bounce_end_tick = end_tick;
    app->timeline_bounce_prev_metronome_enabled = app->transport.metronome_enabled;
    app->timeline_bounce_prev_play_range_loop_enabled = app->timeline.play_range_loop_enabled;

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    app->transport.metronome_enabled = false;
    app->transport.metronome_env = 0.0f;
    app->timeline.play_range_loop_enabled = false;
    if (app->audio.stream) {
        SDL_ClearAudioStream(app->audio.stream);
        SDL_UnlockAudioStream(app->audio.stream);
    }
    audio_engine_clear_master_reverb_tail(&app->audio);
    audio_engine_set_timeline_playhead(&app->audio, start_tick);
    if (!audio_engine_start_timeline_bounce_recording(&app->audio, samples, target_frames, target_frames, end_tick)) {
        app_abort_timeline_bounce(app, "Could not start bounce");
        return false;
    }
    audio_engine_start_timeline(&app->audio);
    if (!audio_engine_timeline_is_playing(&app->audio)) {
        app_abort_timeline_bounce(app, "Could not start timeline");
        return false;
    }

    SDL_snprintf(app->status_text, sizeof(app->status_text), "Bounce recording %.2fs", seconds);
    return true;
}

static int compare_project_browser_entries(const void *a, const void *b) {
    const ProjectBrowserEntry *ea = (const ProjectBrowserEntry *)a;
    const ProjectBrowserEntry *eb = (const ProjectBrowserEntry *)b;
    if (ea->modify_time > eb->modify_time) return -1;
    if (ea->modify_time < eb->modify_time) return 1;
    return SDL_strcasecmp(ea->folder_name, eb->folder_name);
}

static bool project_validation_openable(ProjectValidationStatus status) {
    return status == PROJECT_VALIDATION_VALID || status == PROJECT_VALIDATION_WARNING;
}

static void project_browser_init_blank_entry(ProjectBrowserEntry *entry) {
    if (!entry) return;
    SDL_memset(entry, 0, sizeof(*entry));
    entry->is_blank_project = true;
    SDL_strlcpy(entry->folder_name, "<new blank project>", sizeof(entry->folder_name));
    entry->quick_validation.status = PROJECT_VALIDATION_VALID;
    SDL_strlcpy(entry->quick_validation.reason, "New blank project", sizeof(entry->quick_validation.reason));
    SDL_strlcpy(entry->quick_validation.detail,
                "Clears the current timeline, roster, drum patterns, and master FX.",
                sizeof(entry->quick_validation.detail));
    SDL_strlcpy(entry->quick_validation.project_name, "<new blank project>", sizeof(entry->quick_validation.project_name));
    entry->full_validation = entry->quick_validation;
    entry->full_validation_ready = true;
}

static void app_project_browser_clear_preview(App *app) {
    if (!app) return;
    audio_engine_stop_file_preview(&app->audio);
    if (app->project_browser_preview_clip_loaded) {
        clip_destroy(&app->project_browser_preview_clip);
        app->project_browser_preview_clip_loaded = false;
    }
}

static void app_project_browser_validate_selected(App *app) {
    if (!app || app->project_browser_count <= 0) return;
    app->project_browser_selected = clamp_int(app->project_browser_selected, 0, app->project_browser_count - 1);
    ProjectBrowserEntry *entry = &app->project_browser_entries[app->project_browser_selected];
    if (entry->is_blank_project) {
        entry->full_validation = entry->quick_validation;
        entry->full_validation_ready = true;
        return;
    }
    project_validate_bundle(entry->path, PROJECT_VALIDATION_FULL, &entry->full_validation);
    entry->full_validation_ready = true;
}

void app_project_browser_refresh(App *app) {
    if (!app) return;
    app->project_browser_count = 0;
    app->project_browser_selected = 0;
    app->project_browser_blank_confirm_open = false;
    SDL_memset(app->project_browser_entries, 0, sizeof(app->project_browser_entries));
    project_browser_init_blank_entry(&app->project_browser_entries[app->project_browser_count++]);

    if (!resolve_project_export_dir(app, app->project_browser_dir, sizeof(app->project_browser_dir))) {
        app_project_browser_validate_selected(app);
        app_set_status(app, "Project browser: blank project only");
        return;
    }

    int count = 0;
    char **matches = SDL_GlobDirectory(app->project_browser_dir, "*.vapor", 0, &count);
    if (matches) {
        for (int i = 0; i < count && app->project_browser_count < APP_MAX_PROJECTS; ++i) {
            char path[CLIP_MAX_PATH];
            path_join(path, sizeof(path), app->project_browser_dir, matches[i]);
            SDL_PathInfo info;
            if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) continue;
            ProjectBrowserEntry *entry = &app->project_browser_entries[app->project_browser_count++];
            SDL_strlcpy(entry->path, path, sizeof(entry->path));
            SDL_strlcpy(entry->folder_name, matches[i], sizeof(entry->folder_name));
            entry->modify_time = info.modify_time;
            project_validate_bundle(entry->path, PROJECT_VALIDATION_QUICK, &entry->quick_validation);
        }
        SDL_free(matches);
    }

    int bundle_count = app->project_browser_count - 1;
    if (bundle_count > 1) {
        qsort(&app->project_browser_entries[1],
              (size_t)bundle_count,
              sizeof(app->project_browser_entries[0]),
              compare_project_browser_entries);
    }
    app_project_browser_validate_selected(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text),
                 bundle_count > 0 ? "Project browser: %d bundle%s" : "Project browser: no saved bundles",
                 bundle_count,
                 bundle_count == 1 ? "" : "s");
}

void app_project_browser_open(App *app) {
    if (!app || app->view_mode != APP_VIEW_TIMELINE) return;
    app_timeline_clear_context_menu(app);
    app->sample_selector_open = false;
    app->project_menu_open = false;
    app->project_menu_selected = 0;
    app->project_browser_open = true;
    app_project_browser_refresh(app);
}

void app_project_browser_close(App *app) {
    if (!app) return;
    app_project_browser_clear_preview(app);
    app->project_browser_open = false;
    app->project_browser_blank_confirm_open = false;
    app_set_status(app, "Project browser closed");
}

void app_project_browser_move(App *app, int delta) {
    if (!app || !app->project_browser_open || app->project_browser_blank_confirm_open || delta == 0 || app->project_browser_count <= 0) return;
    int selected = app->project_browser_selected + delta;
    while (selected < 0) selected += app->project_browser_count;
    selected %= app->project_browser_count;
    if (selected == app->project_browser_selected) return;
    app->project_browser_selected = selected;
    app_project_browser_validate_selected(app);
}

static void app_reset_current_project_to_blank(App *app) {
    if (!app) return;

    app_cancel_timeline_bounce(app);
    app_stop_active_audio(app);
    app_project_browser_clear_preview(app);

    if (app->waveform_source_mode == WAVEFORM_SOURCE_ROSTER) {
        app_set_waveform_source_generated(app);
    }
    app_clear_waveform_frame_grip(app);

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);

    for (int i = 0; i < app->roster_clip_count; ++i) {
        roster_clip_destroy(&app->roster[i]);
    }
    SDL_memset(app->roster, 0, sizeof(app->roster));
    app->roster_clip_count = 0;
    SDL_memset(app->drum_patterns, 0, sizeof(app->drum_patterns));
    app->drum_pattern_count = 0;

    transport_init(&app->transport, TIMELINE_DEFAULT_BPM, 960, 4, 4);
    app->transport.playing = false;
    app->transport.metronome_env = 0.0f;
    app->transport_bpm = TIMELINE_DEFAULT_BPM;
    app->transport_bpm_manual = false;

    SDL_memset(&app->timeline, 0, sizeof(app->timeline));
    app->timeline.ticks_per_beat = app->transport.ppqn;
    app->timeline.timeline_bpm = TIMELINE_DEFAULT_BPM;
    app->timeline.timeline_beats_per_bar = 4;
    app->timeline.timeline_beat_unit = 4;
    timeline_set_tempo_event_no_lock(&app->timeline, 0, app->timeline.timeline_bpm);
    timeline_init_lanes(&app->timeline);
    app->timeline.timeline_cursor_tick = 0;
    app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
    app->timeline.play_range_start_tick = 0;
    app->timeline.play_range_end_tick = 0;
    app->timeline.play_range_loop_enabled = false;
    app->timeline.play_range_custom = false;
    app->timeline.view_center_tick = (double)app->timeline.ticks_per_beat * 2.0;
    app->timeline.view_span_ticks = (double)app->timeline.ticks_per_beat * 4.0;
    app->timeline.tape_speed = TIMELINE_TAPE_SPEED_DEFAULT;

    app->audio.playback_mode = AUDIO_PLAYBACK_TIMELINE;
    app->audio.timeline_playhead_tick = 0.0;
    app->audio.preview_active = false;
    app->audio.preview_roster_clip_index = -1;
    app->audio.preview_frame = 0.0;
    app->audio.file_preview_active = false;
    app->audio.file_preview_clip = NULL;
    app->audio.file_preview_frame = 0.0;
    app->audio.metronome_beat_valid = false;
    app->audio.master_gain = 1.0f;
    SDL_memset(&app->audio.meter, 0, sizeof(app->audio.meter));
    audio_engine_init_master_fx(&app->audio);
    SDL_memset(app->audio.lane_meters, 0, sizeof(app->audio.lane_meters));
    SDL_memset(app->audio.lane_analyzer_samples, 0, sizeof(app->audio.lane_analyzer_samples));
    app->audio.lane_analyzer_write_index = 0;
    app->audio.lane_analyzer_sample_count = 0;
    app->audio.active_analyzer_lane = -1;
    app->audio.lane_analyzer_active = false;
    if (app->audio.stream) SDL_ClearAudioStream(app->audio.stream);

    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    app->project_id[0] = '\0';
    app->project_name[0] = '\0';
    app->selected_roster_clip = -1;
    app->roster_scroll_offset = 0;
    app->roster_visible_rows = 1;
    app->selected_roster_clip_armed = false;
    app->selected_drum_pattern = -1;
    app->selected_drum_pattern_armed = false;
    app->drum_machine_step = 0;
    app->drum_machine_pad = 0;
    app->selected_timeline_lane = 0;
    app->inspected_timeline_lane = 0;
    app->lane_analyzer_visual_lane = -1;
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    app->timeline_focus_zone = TIMELINE_FOCUS_RULER;
    app->timeline_tape_control_mode = TIMELINE_TAPE_CONTROL_BPM;
    app->timeline_play_range_handle = TIMELINE_RANGE_HANDLE_START;
    app->timeline_play_range_adjusting = false;
    app->timeline_edit_mode = TIMELINE_EDIT_NONE;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_instance_kind = TIMELINE_INSTANCE_AUDIO_CLIP;
    app->timeline_edit_roster_clip_index = -1;
    app->timeline_edit_pattern_index = -1;
    app->timeline_edit_ghost_valid = false;
    app->project_menu_open = false;
    app->project_menu_selected = 0;
    app->project_browser_open = false;
    app->project_browser_blank_confirm_open = false;
    app->sample_selector_open = false;
    app->waveform_sidecar_confirm_open = false;
    app->waveform_menu_open = false;
    app->waveform_render_dialog_open = false;
    app->roster_commit_menu_open = false;
    app_timeline_clear_context_menu(app);
    app->view_mode = APP_VIEW_TIMELINE;
    sync_transport_from_app(app);
}

void app_project_browser_open_selected(App *app) {
    if (!app || !app->project_browser_open) return;
    if (app->project_browser_count <= 0) {
        app_set_status(app, "No project selected");
        return;
    }
    app_project_browser_validate_selected(app);
    ProjectBrowserEntry *entry = &app->project_browser_entries[app->project_browser_selected];
    if (entry->is_blank_project) {
        app_project_browser_clear_preview(app);
        app->project_browser_blank_confirm_open = true;
        app_set_status(app, "Confirm new blank project");
        return;
    }
    ProjectValidationResult *validation = &entry->full_validation;
    if (!project_validation_openable(validation->status)) {
        SDL_snprintf(app->status_text, sizeof(app->status_text),
                     "Cannot open: %s",
                     validation->reason[0] ? validation->reason : project_validation_status_label(validation->status));
        return;
    }

    audio_engine_stop_timeline(&app->audio, false);
    audio_engine_stop_preview(&app->audio);
    app_project_browser_clear_preview(app);
    if (app_load_project_bundle(app, entry->path)) {
        app->project_browser_open = false;
        app->project_menu_open = false;
        app->project_menu_selected = 0;
    }
}

void app_project_browser_confirm_blank_project(App *app) {
    if (!app || !app->project_browser_open || !app->project_browser_blank_confirm_open) return;
    app_reset_current_project_to_blank(app);
    app_set_status(app, "New blank project");
}

void app_project_browser_cancel_blank_project(App *app) {
    if (!app || !app->project_browser_blank_confirm_open) return;
    app->project_browser_blank_confirm_open = false;
    app_set_status(app, "New blank cancelled");
}

void app_project_browser_preview_selected(App *app) {
    if (!app || !app->project_browser_open) return;
    if (app->project_browser_count <= 0) {
        app_set_status(app, "No project selected");
        return;
    }
    app_project_browser_validate_selected(app);
    ProjectBrowserEntry *entry = &app->project_browser_entries[app->project_browser_selected];
    if (entry->is_blank_project) {
        app_project_browser_clear_preview(app);
        app_set_status(app, "No preview for blank project");
        return;
    }
    ProjectValidationResult *validation = &entry->full_validation;
    if (!validation->preview_available) {
        SDL_snprintf(app->status_text, sizeof(app->status_text),
                     "Preview unavailable: %s",
                     validation->reason[0] ? validation->reason : "missing preview.wav");
        return;
    }

    char preview_path[CLIP_MAX_PATH];
    path_join(preview_path, sizeof(preview_path), entry->path, VAPORPLANE_PROJECT_PREVIEW_FILENAME);
    AudioClip next;
    if (!clip_init_from_wav(&next, preview_path)) {
        app_set_status(app, "Could not load preview.wav");
        return;
    }

    app_project_browser_clear_preview(app);
    app->project_browser_preview_clip = next;
    app->project_browser_preview_clip_loaded = true;
    if (audio_engine_preview_file_clip(&app->audio, &app->project_browser_preview_clip)) {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Previewing %s", entry->folder_name);
    } else {
        app_project_browser_clear_preview(app);
        app_set_status(app, "Could not play preview.wav");
    }
}

typedef struct {
    char sample_id[APP_STABLE_ID_MAX];
    char path[CLIP_MAX_PATH];
} ProjectSampleMapEntry;

typedef struct {
    char roster_clip_id[APP_STABLE_ID_MAX];
    RosterClip clip;
    int midi_channel;
    int midi_note;
    bool has_midi_binding;
} ProjectRosterLoadEntry;

typedef struct {
    char project_id[APP_STABLE_ID_MAX];
    char project_name[APP_SAMPLE_NAME_MAX];
    RosterClip roster[APP_MAX_ROSTER_CLIPS];
    int roster_clip_count;
    int midi_binding_to_roster[16][128];
    DrumPattern drum_patterns[APP_MAX_DRUM_PATTERNS];
    int drum_pattern_count;
    int midi_binding_to_pattern[16][128];
    DrumKit drum_kits[APP_MAX_DRUM_KITS];
    int drum_kit_count;
    MasterTimeline timeline;
    float master_gain;
    bool has_master_gain;
    MasterReverbParams reverb_params;
    bool reverb_param_present[MASTER_REVERB_PARAM_COUNT];
} ProjectLoadState;

static void project_load_state_init(ProjectLoadState *state) {
    SDL_memset(state, 0, sizeof(*state));
    for (int ch = 0; ch < 16; ++ch) {
        for (int note = 0; note < 128; ++note) {
            state->midi_binding_to_roster[ch][note] = -1;
            state->midi_binding_to_pattern[ch][note] = -1;
        }
    }
    state->master_gain = 1.0f;
    state->timeline.ticks_per_beat = 960;
    state->timeline.timeline_bpm = TIMELINE_DEFAULT_BPM;
    state->timeline.timeline_beats_per_bar = 4;
    state->timeline.timeline_beat_unit = 4;
    state->timeline.tape_speed = TIMELINE_TAPE_SPEED_DEFAULT;
    timeline_init_lanes(&state->timeline);
}

static void project_load_state_destroy(ProjectLoadState *state) {
    if (!state) return;
    for (int i = 0; i < state->roster_clip_count; ++i) {
        roster_clip_destroy(&state->roster[i]);
    }
    for (int i = 0; i < state->drum_kit_count; ++i) {
        drum_kit_destroy(&state->drum_kits[i]);
    }
    SDL_memset(state, 0, sizeof(*state));
}

static const char *range_strstr(const char *start, const char *end, const char *needle) {
    if (!start || !end || !needle || start > end) return NULL;
    size_t needle_len = SDL_strlen(needle);
    if (needle_len == 0) return start;
    for (const char *p = start; p + needle_len <= end; ++p) {
        if (SDL_memcmp(p, needle, needle_len) == 0) return p;
    }
    return NULL;
}

static const char *json_key_value(const char *start, const char *end, const char *key) {
    char needle[128];
    SDL_snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = range_strstr(start, end, needle);
    if (!p) return NULL;
    p += SDL_strlen(needle);
    while (p < end && isspace((unsigned char)*p)) ++p;
    if (p >= end || *p != ':') return NULL;
    ++p;
    while (p < end && isspace((unsigned char)*p)) ++p;
    return p < end ? p : NULL;
}

static bool json_get_int_range(const char *start, const char *end, const char *key, int *out) {
    const char *p = json_key_value(start, end, key);
    if (!p) return false;
    char *parse_end = NULL;
    long value = strtol(p, &parse_end, 10);
    if (parse_end == p || parse_end > end) return false;
    if (out) *out = (int)value;
    return true;
}

static bool json_get_int64_range(const char *start, const char *end, const char *key, int64_t *out) {
    const char *p = json_key_value(start, end, key);
    if (!p) return false;
    char *parse_end = NULL;
    long long value = strtoll(p, &parse_end, 10);
    if (parse_end == p || parse_end > end) return false;
    if (out) *out = (int64_t)value;
    return true;
}

static bool json_get_double_range(const char *start, const char *end, const char *key, double *out) {
    const char *p = json_key_value(start, end, key);
    if (!p) return false;
    char *parse_end = NULL;
    double value = strtod(p, &parse_end);
    if (parse_end == p || parse_end > end) return false;
    if (out) *out = value;
    return true;
}

static bool json_get_bool_range(const char *start, const char *end, const char *key, bool *out) {
    const char *p = json_key_value(start, end, key);
    if (!p) return false;
    if (p + 4 <= end && SDL_memcmp(p, "true", 4) == 0) {
        if (out) *out = true;
        return true;
    }
    if (p + 5 <= end && SDL_memcmp(p, "false", 5) == 0) {
        if (out) *out = false;
        return true;
    }
    return false;
}

static bool json_get_string_range(const char *start, const char *end, const char *key, char *out, size_t out_size) {
    const char *p = json_key_value(start, end, key);
    if (!p || p >= end || *p != '"' || !out || out_size == 0) return false;
    ++p;
    size_t write = 0;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char esc = *p++;
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: c = esc; break;
            }
        }
        if (write + 1 < out_size) out[write++] = c;
    }
    if (p >= end || *p != '"') return false;
    out[write] = '\0';
    return true;
}

static bool json_find_compound_range(const char *start, const char *end, const char *key, char open, char close, const char **out_start, const char **out_end) {
    const char *p = json_key_value(start, end, key);
    if (!p || p >= end || *p != open) return false;
    const char *compound_start = p;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; p < end; ++p) {
        char c = *p;
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == open) {
            depth++;
        } else if (c == close) {
            depth--;
            if (depth == 0) {
                if (out_start) *out_start = compound_start;
                if (out_end) *out_end = p + 1;
                return true;
            }
        }
    }
    return false;
}

static bool json_find_array_range(const char *start, const char *end, const char *key, const char **out_start, const char **out_end) {
    return json_find_compound_range(start, end, key, '[', ']', out_start, out_end);
}

static bool json_find_object_range(const char *start, const char *end, const char *key, const char **out_start, const char **out_end) {
    return json_find_compound_range(start, end, key, '{', '}', out_start, out_end);
}

static bool json_next_object(const char **cursor, const char *end, const char **out_start, const char **out_end) {
    const char *p = *cursor;
    while (p < end && *p != '{') ++p;
    if (p >= end) {
        *cursor = end;
        return false;
    }
    const char *object_start = p;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; p < end; ++p) {
        char c = *p;
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                *out_start = object_start;
                *out_end = p + 1;
                *cursor = p + 1;
                return true;
            }
        }
    }
    *cursor = end;
    return false;
}

static bool load_text_file(const char *path, char **out_text, size_t *out_size) {
    size_t size = 0;
    void *bytes = SDL_LoadFile(path, &size);
    if (!bytes) return false;
    char *text = (char *)SDL_malloc(size + 1);
    if (!text) {
        SDL_free(bytes);
        return false;
    }
    SDL_memcpy(text, bytes, size);
    text[size] = '\0';
    SDL_free(bytes);
    if (out_text) *out_text = text;
    else SDL_free(text);
    if (out_size) *out_size = size;
    return true;
}

static int project_sample_index_by_id(const ProjectSampleMapEntry *samples, int count, const char *sample_id) {
    for (int i = 0; i < count; ++i) {
        if (SDL_strcmp(samples[i].sample_id, sample_id) == 0) return i;
    }
    return -1;
}

static int project_roster_index_by_id(const ProjectRosterLoadEntry *entries, int count, const char *roster_clip_id) {
    for (int i = 0; i < count; ++i) {
        if (SDL_strcmp(entries[i].roster_clip_id, roster_clip_id) == 0) return i;
    }
    return -1;
}

static void project_roster_entries_destroy(ProjectRosterLoadEntry *entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; ++i) {
        roster_clip_destroy(&entries[i].clip);
    }
}

static bool load_roster_wav_for_project(const char *bundle_path, const char *relative_path, RosterClip *clip) {
    if (!project_validation_is_safe_relative_path(relative_path)) return false;
    char path[CLIP_MAX_PATH];
    path_join(path, sizeof(path), bundle_path, relative_path);
    AudioClip loaded;
    if (!clip_init_from_wav(&loaded, path)) return false;
    clip->sample_rate = loaded.sample_rate;
    clip->channels = loaded.channels;
    clip->frame_count = loaded.frame_count;
    clip->samples = loaded.samples;
    loaded.samples = NULL;
    clip->source_loop_start_frame = 0;
    clip->source_loop_end_frame = clip->frame_count;
    clip->source_sample_rate = clip->sample_rate;
    SDL_strlcpy(clip->source_path, path, sizeof(clip->source_path));
    clip_destroy(&loaded);
    return true;
}

static int project_find_or_add_loaded_drum_kit(ProjectLoadState *state,
                                               const char *kit_id,
                                               const char *kit_name) {
    if (!state || !kit_id || !kit_id[0]) return -1;
    for (int i = 0; i < state->drum_kit_count; ++i) {
        if (SDL_strcmp(state->drum_kits[i].kit_id, kit_id) == 0) return i;
    }
    if (state->drum_kit_count >= APP_MAX_DRUM_KITS) return -1;
    int index = state->drum_kit_count++;
    DrumKit *kit = &state->drum_kits[index];
    SDL_memset(kit, 0, sizeof(*kit));
    SDL_strlcpy(kit->kit_id, kit_id, sizeof(kit->kit_id));
    SDL_strlcpy(kit->name, kit_name && kit_name[0] ? kit_name : kit_id, sizeof(kit->name));
    kit->project_local = true;
    return index;
}

static bool load_project_drum_sample(const char *bundle_path,
                                     const char *relative_path,
                                     DrumPad *pad) {
    if (!project_validation_is_safe_relative_path(relative_path) || !pad) return false;
    char path[CLIP_MAX_PATH];
    path_join(path, sizeof(path), bundle_path, relative_path);
    AudioClip loaded;
    if (!clip_init_from_wav(&loaded, path)) return false;
    pad->clip = loaded;
    pad->loaded = true;
    pad->project_local = true;
    SDL_strlcpy(pad->source_path, path, sizeof(pad->source_path));
    return true;
}

static bool load_drum_pattern_mid_for_project(const char *bundle_path,
                                              const char *relative_path,
                                              DrumPattern *pattern);

static bool parse_project_manifest(const char *bundle_path, const char *json, ProjectLoadState *state) {
    const char *json_end = json + SDL_strlen(json);
    int version = 0;
    if (!json_get_int_range(json, json_end, "version", &version) || version < 1 || version > VAPORPLANE_PROJECT_FORMAT_VERSION) {
        return false;
    }
    int ppqn = 960;
    json_get_int_range(json, json_end, "ppqn", &ppqn);
    state->timeline.ticks_per_beat = clamp_int(ppqn, 1, 32767);
    json_get_string_range(json, json_end, "project_id", state->project_id, sizeof(state->project_id));
    json_get_string_range(json, json_end, "name", state->project_name, sizeof(state->project_name));
    if (!state->project_id[0]) project_bundle_name_from_path(bundle_path, state->project_id, sizeof(state->project_id));
    if (!state->project_name[0]) project_bundle_name_from_path(bundle_path, state->project_name, sizeof(state->project_name));

    ProjectSampleMapEntry samples[APP_MAX_ROSTER_CLIPS];
    int sample_count = 0;
    const char *array_start = NULL;
    const char *array_end = NULL;
    if (!json_find_array_range(json, json_end, "samples", &array_start, &array_end)) return false;
    const char *cursor = array_start + 1;
    const char *object_start = NULL;
    const char *object_end = NULL;
    while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
        if (sample_count >= APP_MAX_ROSTER_CLIPS) return false;
        if (!json_get_string_range(object_start, object_end, "sample_id", samples[sample_count].sample_id, sizeof(samples[sample_count].sample_id)) ||
            !json_get_string_range(object_start, object_end, "path", samples[sample_count].path, sizeof(samples[sample_count].path))) {
            return false;
        }
        sample_count++;
    }

    ProjectRosterLoadEntry entries[APP_MAX_ROSTER_CLIPS];
    SDL_memset(entries, 0, sizeof(entries));
    int roster_count = 0;
    if (!json_find_array_range(json, json_end, "roster_clips", &array_start, &array_end)) return false;
    cursor = array_start + 1;
    while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
        if (roster_count >= APP_MAX_ROSTER_CLIPS) {
            project_roster_entries_destroy(entries, roster_count);
            return false;
        }
        ProjectRosterLoadEntry *entry = &entries[roster_count];
        char sample_id[APP_STABLE_ID_MAX];
        if (!json_get_string_range(object_start, object_end, "roster_clip_id", entry->roster_clip_id, sizeof(entry->roster_clip_id)) ||
            !json_get_string_range(object_start, object_end, "sample_id", sample_id, sizeof(sample_id))) {
            project_roster_entries_destroy(entries, roster_count);
            return false;
        }
        int sample_index = project_sample_index_by_id(samples, sample_count, sample_id);
        if (sample_index < 0) {
            project_roster_entries_destroy(entries, roster_count);
            return false;
        }
        RosterClip *clip = &entry->clip;
        if (!load_roster_wav_for_project(bundle_path, samples[sample_index].path, clip)) {
            project_roster_entries_destroy(entries, roster_count);
            return false;
        }
        SDL_strlcpy(clip->sample_id, sample_id, sizeof(clip->sample_id));
        SDL_strlcpy(clip->roster_clip_id, entry->roster_clip_id, sizeof(clip->roster_clip_id));
        if (!json_get_string_range(object_start, object_end, "name", clip->name, sizeof(clip->name))) {
            SDL_strlcpy(clip->name, entry->roster_clip_id, sizeof(clip->name));
        }
        double value = 0.0;
        int int_value = 0;
        int64_t frame_value = 0;
        bool tempo_calibrated = true;
        json_get_bool_range(object_start, object_end, "tempo_calibrated", &tempo_calibrated);
        clip->tempo_calibrated = tempo_calibrated;
        if (clip->tempo_calibrated) {
            if (json_get_double_range(object_start, object_end, "source_bpm", &value)) clip->source_bpm = value;
            else clip->source_bpm = TIMELINE_DEFAULT_BPM;
            if (clip->source_bpm <= 0.0) {
                clip->tempo_calibrated = false;
                clip->source_bpm = 0.0;
            }
        } else {
            clip->source_bpm = 0.0;
        }
        if (clip->tempo_calibrated) {
            if (json_get_int_range(object_start, object_end, "beats_per_bar", &int_value)) clip->beats_per_bar = int_value;
            else clip->beats_per_bar = 4;
            if (json_get_int_range(object_start, object_end, "beat_unit", &int_value)) clip->beat_unit = int_value;
            else clip->beat_unit = 4;
            if (json_get_double_range(object_start, object_end, "target_bars", &value)) clip->target_bars = value;
            else clip->target_bars = 4.0;
            if (json_get_double_range(object_start, object_end, "target_beats", &value)) clip->target_beats = value;
            else clip->target_beats = clip->target_bars * (double)clip->beats_per_bar;
            if (json_get_int64_range(object_start, object_end, "downbeat_offset_frames", &frame_value) && frame_value >= 0) {
                clip->downbeat_offset_frames = (size_t)frame_value;
            }
        } else {
            clip->beats_per_bar = 4;
            clip->beat_unit = 4;
            clip->target_bars = 0.0;
            clip->target_beats = 0.0;
            clip->downbeat_offset_frames = 0;
        }
        clip->loop_start_frame = 0;
        clip->loop_end_frame = clip->frame_count;
        if (json_get_int64_range(object_start, object_end, "loop_start_frame", &frame_value) && frame_value >= 0 &&
            (size_t)frame_value < clip->frame_count) {
            clip->loop_start_frame = (size_t)frame_value;
        }
        if (json_get_int64_range(object_start, object_end, "loop_end_frame", &frame_value) && frame_value > 0 &&
            (size_t)frame_value <= clip->frame_count && (size_t)frame_value > clip->loop_start_frame) {
            clip->loop_end_frame = (size_t)frame_value;
        }
        const char *lineage_start = NULL;
        const char *lineage_end = NULL;
        if (json_find_object_range(object_start, object_end, "source_lineage", &lineage_start, &lineage_end)) {
            json_get_string_range(lineage_start, lineage_end, "path", clip->source_path, sizeof(clip->source_path));
            if (json_get_int64_range(lineage_start, lineage_end, "start_frame", &frame_value) && frame_value >= 0) {
                clip->source_loop_start_frame = (size_t)frame_value;
            }
            if (json_get_int64_range(lineage_start, lineage_end, "end_frame", &frame_value) && frame_value >= 0) {
                clip->source_loop_end_frame = (size_t)frame_value;
            }
            if (json_get_int_range(lineage_start, lineage_end, "sample_rate", &int_value) && int_value > 0) {
                clip->source_sample_rate = int_value;
            }
        }
        entry->midi_channel = 0;
        entry->midi_note = clamp_int(60 + roster_count, 0, 127);
        const char *binding_start = NULL;
        const char *binding_end = NULL;
        if (json_find_object_range(object_start, object_end, "midi_binding", &binding_start, &binding_end)) {
            int channel = 1;
            int note = 60 + roster_count;
            json_get_int_range(binding_start, binding_end, "channel", &channel);
            json_get_int_range(binding_start, binding_end, "note", &note);
            entry->midi_channel = clamp_int(channel - 1, 0, 15);
            entry->midi_note = clamp_int(note, 0, 127);
            entry->has_midi_binding = true;
        }
        clip->midi_channel = entry->midi_channel;
        clip->midi_note = entry->midi_note;
        clip->midi_velocity = 127;
        clip->color = roster_color_for_index(roster_count);
        roster_count++;
    }

    if (json_find_array_range(json, json_end, "drum_samples", &array_start, &array_end)) {
        cursor = array_start + 1;
        while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
            char kit_id[APP_STABLE_ID_MAX];
            char kit_name[APP_ROSTER_CLIP_NAME_MAX];
            char pad_name[APP_ROSTER_CLIP_NAME_MAX];
            char rel_path[CLIP_MAX_PATH];
            int note = -1;
            kit_id[0] = '\0';
            kit_name[0] = '\0';
            pad_name[0] = '\0';
            rel_path[0] = '\0';
            if (!json_get_string_range(object_start, object_end, "kit_id", kit_id, sizeof(kit_id)) ||
                !json_get_string_range(object_start, object_end, "path", rel_path, sizeof(rel_path)) ||
                !json_get_int_range(object_start, object_end, "note", &note)) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            json_get_string_range(object_start, object_end, "kit_name", kit_name, sizeof(kit_name));
            json_get_string_range(object_start, object_end, "name", pad_name, sizeof(pad_name));
            int kit_index = project_find_or_add_loaded_drum_kit(state, kit_id, kit_name);
            if (kit_index < 0) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            DrumKit *kit = &state->drum_kits[kit_index];
            if (kit->pad_count >= APP_MAX_DRUM_PADS) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            DrumPad *pad = &kit->pads[kit->pad_count];
            SDL_memset(pad, 0, sizeof(*pad));
            pad->note = clamp_int(note, 0, 127);
            SDL_strlcpy(pad->name, pad_name[0] ? pad_name : "pad", sizeof(pad->name));
            if (!load_project_drum_sample(bundle_path, rel_path, pad)) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            kit->pad_count++;
        }
    }

    if (json_find_array_range(json, json_end, "patterns", &array_start, &array_end)) {
        cursor = array_start + 1;
        while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
            if (state->drum_pattern_count >= APP_MAX_DRUM_PATTERNS) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            DrumPattern *pattern = &state->drum_patterns[state->drum_pattern_count];
            SDL_memset(pattern, 0, sizeof(*pattern));
            char midi_file[CLIP_MAX_PATH];
            if (!json_get_string_range(object_start, object_end, "pattern_id", pattern->pattern_id, sizeof(pattern->pattern_id)) ||
                !json_get_string_range(object_start, object_end, "midi_file", midi_file, sizeof(midi_file))) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            if (!json_get_string_range(object_start, object_end, "name", pattern->name, sizeof(pattern->name))) {
                SDL_strlcpy(pattern->name, pattern->pattern_id, sizeof(pattern->name));
            }
            int64_t length_ticks = 0;
            if (json_get_int64_range(object_start, object_end, "length_ticks", &length_ticks) && length_ticks > 0) {
                pattern->length_ticks = length_ticks;
            } else {
                pattern->length_ticks = (int64_t)state->timeline.ticks_per_beat * 4;
            }
            pattern->locator_channel = DRUM_PATTERN_LOCATOR_CHANNEL;
            pattern->locator_note = 24 + state->drum_pattern_count;
            const char *binding_start = NULL;
            const char *binding_end = NULL;
            if (json_find_object_range(object_start, object_end, "midi_binding", &binding_start, &binding_end)) {
                int channel = DRUM_PATTERN_LOCATOR_CHANNEL + 1;
                int note = 24 + state->drum_pattern_count;
                json_get_int_range(binding_start, binding_end, "channel", &channel);
                json_get_int_range(binding_start, binding_end, "note", &note);
                pattern->locator_channel = clamp_int(channel - 1, 0, 15);
                pattern->locator_note = clamp_int(note, 0, 127);
            }
            pattern->color = roster_color_for_index(state->drum_pattern_count);
            if (!load_drum_pattern_mid_for_project(bundle_path, midi_file, pattern)) {
                project_roster_entries_destroy(entries, roster_count);
                return false;
            }
            int ch = clamp_int(pattern->locator_channel, 0, 15);
            int note = clamp_int(pattern->locator_note, 0, 127);
            if (state->midi_binding_to_pattern[ch][note] < 0) {
                state->midi_binding_to_pattern[ch][note] = state->drum_pattern_count;
            }
            state->drum_pattern_count++;
        }
    }

    if (version == 1 && json_find_array_range(json, json_end, "clip_instances", &array_start, &array_end)) {
        cursor = array_start + 1;
        while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
            char roster_clip_id[APP_STABLE_ID_MAX];
            if (!json_get_string_range(object_start, object_end, "roster_clip_id", roster_clip_id, sizeof(roster_clip_id))) continue;
            int roster_index = project_roster_index_by_id(entries, roster_count, roster_clip_id);
            if (roster_index < 0 || entries[roster_index].has_midi_binding) continue;
            const char *locator_start = NULL;
            const char *locator_end = NULL;
            if (json_find_object_range(object_start, object_end, "midi_locator", &locator_start, &locator_end)) {
                int channel = 1;
                int note = 60 + roster_index;
                json_get_int_range(locator_start, locator_end, "channel", &channel);
                json_get_int_range(locator_start, locator_end, "note", &note);
                entries[roster_index].midi_channel = clamp_int(channel - 1, 0, 15);
                entries[roster_index].midi_note = clamp_int(note, 0, 127);
                entries[roster_index].has_midi_binding = true;
                entries[roster_index].clip.midi_channel = entries[roster_index].midi_channel;
                entries[roster_index].clip.midi_note = entries[roster_index].midi_note;
            }
        }
    }

    for (int i = 0; i < roster_count; ++i) {
        ProjectRosterLoadEntry *entry = &entries[i];
        int ch = clamp_int(entry->midi_channel, 0, 15);
        int note = clamp_int(entry->midi_note, 0, 127);
        if (state->midi_binding_to_roster[ch][note] < 0) {
            state->midi_binding_to_roster[ch][note] = i;
        }
        if (version < 3) {
            generate_stable_id("sample", entry->clip.sample_id, sizeof(entry->clip.sample_id));
            generate_stable_id("roster", entry->clip.roster_clip_id, sizeof(entry->clip.roster_clip_id));
        }
        state->roster[i] = entry->clip;
        SDL_memset(&entry->clip, 0, sizeof(entry->clip));
    }
    state->roster_clip_count = roster_count;
    return roster_count > 0 || state->drum_pattern_count > 0;
}

static bool midi_read_be16_mem(const Uint8 *data, size_t size, size_t *pos, Uint16 *out) {
    if (*pos + 2 > size) return false;
    *out = (Uint16)(((Uint16)data[*pos] << 8) | data[*pos + 1]);
    *pos += 2;
    return true;
}

static bool midi_read_be32_mem(const Uint8 *data, size_t size, size_t *pos, Uint32 *out) {
    if (*pos + 4 > size) return false;
    *out = ((Uint32)data[*pos] << 24) | ((Uint32)data[*pos + 1] << 16) | ((Uint32)data[*pos + 2] << 8) | (Uint32)data[*pos + 3];
    *pos += 4;
    return true;
}

static bool midi_read_varlen_mem(const Uint8 *data, size_t size, size_t *pos, Uint32 *out) {
    Uint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        if (*pos >= size) return false;
        Uint8 byte = data[(*pos)++];
        value = (value << 7) | (Uint32)(byte & 0x7f);
        if ((byte & 0x80) == 0) {
            *out = value;
            return true;
        }
    }
    return false;
}

typedef struct {
    bool active;
    int64_t start_tick;
    Uint8 velocity;
} MidiOpenNote;

static bool parse_drum_pattern_midi_track(DrumPattern *pattern, const Uint8 *data, size_t size) {
    size_t pos = 0;
    int64_t tick = 0;
    Uint8 running_status = 0;
    MidiOpenNote open_notes[16][128];
    SDL_memset(open_notes, 0, sizeof(open_notes));
    while (pos < size) {
        Uint32 delta = 0;
        if (!midi_read_varlen_mem(data, size, &pos, &delta)) return false;
        tick += (int64_t)delta;
        if (pos >= size) return false;
        Uint8 status = data[pos];
        if (status < 0x80) {
            if (running_status == 0) return false;
            status = running_status;
        } else {
            pos++;
            if (status < 0xf0) running_status = status;
        }
        if (status == 0xff) {
            if (pos >= size) return false;
            Uint8 meta = data[pos++];
            Uint32 length = 0;
            if (!midi_read_varlen_mem(data, size, &pos, &length) || pos + length > size) return false;
            if (meta == 0x2f) return true;
            pos += length;
            continue;
        }
        if (status == 0xf0 || status == 0xf7) {
            Uint32 length = 0;
            if (!midi_read_varlen_mem(data, size, &pos, &length) || pos + length > size) return false;
            pos += length;
            continue;
        }
        Uint8 type = status & 0xf0;
        Uint8 channel = status & 0x0f;
        int data_bytes = (type == 0xc0 || type == 0xd0) ? 1 : 2;
        if (pos + (size_t)data_bytes > size) return false;
        Uint8 d1 = data[pos++];
        Uint8 d2 = data_bytes == 2 ? data[pos++] : 0;
        if (type == 0x90 && d2 > 0) {
            open_notes[channel][d1].active = true;
            open_notes[channel][d1].start_tick = tick;
            open_notes[channel][d1].velocity = d2;
        } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
            MidiOpenNote *open = &open_notes[channel][d1];
            if (!open->active) continue;
            int64_t duration = tick - open->start_tick;
            open->active = false;
            if (duration <= 0) continue;
            if (pattern->event_count >= APP_MAX_DRUM_PATTERN_EVENTS) return false;
            drum_pattern_add_event(pattern, open->start_tick, d1, open->velocity, duration);
        }
    }
    return true;
}

static bool load_drum_pattern_mid_for_project(const char *bundle_path,
                                              const char *relative_path,
                                              DrumPattern *pattern) {
    if (!bundle_path || !relative_path || !pattern ||
        !project_validation_is_safe_relative_path(relative_path)) {
        return false;
    }
    char path[CLIP_MAX_PATH];
    path_join(path, sizeof(path), bundle_path, relative_path);
    size_t size = 0;
    Uint8 *data = (Uint8 *)SDL_LoadFile(path, &size);
    if (!data) return false;
    bool ok = false;
    size_t pos = 0;
    Uint32 header_length = 0;
    Uint16 format = 0, track_count = 0, division = 0;
    if (size < 14 || SDL_memcmp(data, "MThd", 4) != 0) goto done;
    pos = 4;
    if (!midi_read_be32_mem(data, size, &pos, &header_length) || header_length < 6 || pos + header_length > size) goto done;
    if (!midi_read_be16_mem(data, size, &pos, &format) ||
        !midi_read_be16_mem(data, size, &pos, &track_count) ||
        !midi_read_be16_mem(data, size, &pos, &division)) {
        goto done;
    }
    if (format > 1 || (division & 0x8000) != 0) goto done;
    pos = 8 + header_length;
    pattern->event_count = 0;
    for (int track = 0; track < track_count; ++track) {
        Uint32 track_length = 0;
        if (pos + 8 > size || SDL_memcmp(data + pos, "MTrk", 4) != 0) goto done;
        pos += 4;
        if (!midi_read_be32_mem(data, size, &pos, &track_length) || pos + track_length > size) goto done;
        if (!parse_drum_pattern_midi_track(pattern, data + pos, track_length)) goto done;
        pos += track_length;
    }
    if (pattern->length_ticks <= 0) pattern->length_ticks = 3840;
    ok = true;
done:
    SDL_free(data);
    return ok;
}

static bool project_add_tempo_event(ProjectLoadState *state, int64_t tick, double bpm) {
    if (state->timeline.tempo_event_count >= TIMELINE_MAX_TEMPO_EVENTS) return false;
    return timeline_set_tempo_event_no_lock(&state->timeline, tick, bpm);
}

static bool parse_project_midi_track(ProjectLoadState *state, const Uint8 *data, size_t size, int track_index) {
    size_t pos = 0;
    int64_t tick = 0;
    Uint8 running_status = 0;
    MidiOpenNote open_notes[16][128];
    SDL_memset(open_notes, 0, sizeof(open_notes));
    while (pos < size) {
        Uint32 delta = 0;
        if (!midi_read_varlen_mem(data, size, &pos, &delta)) return false;
        tick += (int64_t)delta;
        if (pos >= size) return false;
        Uint8 status = data[pos];
        bool consumed_status = false;
        if (status < 0x80) {
            if (running_status == 0) return false;
            status = running_status;
        } else {
            pos++;
            consumed_status = true;
            if (status < 0xf0) running_status = status;
        }
        if (status == 0xff) {
            if (pos >= size) return false;
            Uint8 meta = data[pos++];
            Uint32 length = 0;
            if (!midi_read_varlen_mem(data, size, &pos, &length) || pos + length > size) return false;
            if (meta == 0x51 && length == 3) {
                Uint32 mpqn = ((Uint32)data[pos] << 16) | ((Uint32)data[pos + 1] << 8) | (Uint32)data[pos + 2];
                if (mpqn > 0) {
                    double bpm = 60000000.0 / (double)mpqn;
                    if (!project_add_tempo_event(state, tick, bpm)) return false;
                }
            } else if (meta == 0x2f) {
                return true;
            }
            pos += length;
            continue;
        }
        if (status == 0xf0 || status == 0xf7) {
            Uint32 length = 0;
            if (!midi_read_varlen_mem(data, size, &pos, &length) || pos + length > size) return false;
            pos += length;
            continue;
        }
        Uint8 type = status & 0xf0;
        Uint8 channel = status & 0x0f;
        int data_bytes = (type == 0xc0 || type == 0xd0) ? 1 : 2;
        if (pos + (size_t)data_bytes > size) return false;
        Uint8 d1 = data[pos++];
        Uint8 d2 = data_bytes == 2 ? data[pos++] : 0;
        (void)consumed_status;
        if (track_index <= 0 || track_index > TIMELINE_MAX_LANES) continue;
        int lane_index = track_index - 1;
        if (type == 0x90 && d2 > 0) {
            open_notes[channel][d1].active = true;
            open_notes[channel][d1].start_tick = tick;
            open_notes[channel][d1].velocity = d2;
        } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
            MidiOpenNote *open = &open_notes[channel][d1];
            if (!open->active) continue;
            int64_t duration = tick - open->start_tick;
            open->active = false;
            if (duration <= 0) continue;
            int pattern_index = state->midi_binding_to_pattern[channel][d1];
            if (pattern_index >= 0 && pattern_index < state->drum_pattern_count) {
                TimelineLane *lane = &state->timeline.lanes[lane_index];
                if (lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) return false;
                TimelineInstance *instance = &lane->instances[lane->instance_count++];
                instance->kind = TIMELINE_INSTANCE_DRUM_PATTERN;
                instance->roster_clip_index = -1;
                instance->pattern_index = pattern_index;
                instance->start_tick = open->start_tick;
                instance->duration_ticks = duration;
                instance->midi_channel = channel;
                instance->midi_note = d1;
                instance->midi_velocity = clamp_int(open->velocity, 1, 127);
                int64_t end_tick = open->start_tick + duration;
                if (end_tick > state->timeline.length_ticks) state->timeline.length_ticks = end_tick;
                continue;
            }
            int roster_index = state->midi_binding_to_roster[channel][d1];
            if (roster_index < 0 || roster_index >= state->roster_clip_count) continue;
            TimelineLane *lane = &state->timeline.lanes[lane_index];
            if (lane->instance_count >= APP_MAX_TIMELINE_INSTANCES_PER_LANE) return false;
            TimelineInstance *instance = &lane->instances[lane->instance_count++];
            instance->kind = TIMELINE_INSTANCE_AUDIO_CLIP;
            instance->roster_clip_index = roster_index;
            instance->pattern_index = -1;
            instance->start_tick = open->start_tick;
            instance->duration_ticks = duration;
            instance->midi_channel = channel;
            instance->midi_note = d1;
            instance->midi_velocity = clamp_int(open->velocity, 1, 127);
            int64_t end_tick = open->start_tick + duration;
            if (end_tick > state->timeline.length_ticks) state->timeline.length_ticks = end_tick;
        }
    }
    return true;
}

static bool parse_project_timeline_mid(const char *path, ProjectLoadState *state) {
    size_t size = 0;
    Uint8 *data = (Uint8 *)SDL_LoadFile(path, &size);
    if (!data) return false;
    bool ok = false;
    size_t pos = 0;
    Uint32 header_length = 0;
    Uint16 format = 0, track_count = 0, division = 0;
    if (size < 14 || SDL_memcmp(data, "MThd", 4) != 0) goto done;
    pos = 4;
    if (!midi_read_be32_mem(data, size, &pos, &header_length) || header_length < 6 || pos + header_length > size) goto done;
    if (!midi_read_be16_mem(data, size, &pos, &format) ||
        !midi_read_be16_mem(data, size, &pos, &track_count) ||
        !midi_read_be16_mem(data, size, &pos, &division)) {
        goto done;
    }
    if (format > 1 || (division & 0x8000) != 0) goto done;
    state->timeline.ticks_per_beat = clamp_int((int)division, 1, 32767);
    pos = 8 + header_length;
    state->timeline.tempo_event_count = 0;
    for (int track = 0; track < track_count; ++track) {
        Uint32 track_length = 0;
        if (pos + 8 > size || SDL_memcmp(data + pos, "MTrk", 4) != 0) goto done;
        pos += 4;
        if (!midi_read_be32_mem(data, size, &pos, &track_length) || pos + track_length > size) goto done;
        if (!parse_project_midi_track(state, data + pos, track_length, track)) goto done;
        pos += track_length;
    }
    if (state->timeline.tempo_event_count <= 0) {
        if (!project_add_tempo_event(state, 0, TIMELINE_DEFAULT_BPM)) goto done;
    }
    state->timeline.timeline_bpm = timeline_base_bpm(&state->timeline);
    state->timeline.initialized = timeline_has_instances(&state->timeline);
    state->timeline.playhead_tick = 0;
    state->timeline.timeline_cursor_tick = 0;
    state->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
    state->timeline.play_range_start_tick = 0;
    state->timeline.play_range_end_tick = state->timeline.length_ticks;
    state->timeline.play_range_loop_enabled = false;
    state->timeline.play_range_custom = false;
    state->timeline.view_center_tick = state->timeline.length_ticks > 0 ? (double)state->timeline.length_ticks * 0.5 : (double)state->timeline.ticks_per_beat * 2.0;
    state->timeline.view_span_ticks = state->timeline.length_ticks > 0 ? (double)state->timeline.length_ticks : (double)state->timeline.ticks_per_beat * 4.0;
    ok = true;
done:
    SDL_free(data);
    return ok;
}

static void parse_project_surfaces(const char *json, ProjectLoadState *state) {
    const char *end = json + SDL_strlen(json);
    double value = 0.0;
    bool bool_value = false;
    const char *object_start = NULL;
    const char *object_end = NULL;
    if (json_find_object_range(json, end, "master", &object_start, &object_end) &&
        json_get_double_range(object_start, object_end, "gain", &value)) {
        state->master_gain = (float)value;
        state->has_master_gain = true;
    }
    if (json_find_object_range(json, end, "timeline_surface", &object_start, &object_end)) {
        if (json_get_double_range(object_start, object_end, "tape_speed", &value)) {
            state->timeline.tape_speed = (float)clamp_double(value, TIMELINE_TAPE_SPEED_MIN, TIMELINE_TAPE_SPEED_MAX);
        }
        const char *range_start = NULL;
        const char *range_end = NULL;
        if (json_find_object_range(object_start, object_end, "play_range", &range_start, &range_end)) {
            int64_t tick_value = 0;
            if (json_get_int64_range(range_start, range_end, "start_tick", &tick_value)) {
                state->timeline.play_range_start_tick = clamp_i64(tick_value, 0, state->timeline.length_ticks);
            }
            if (json_get_int64_range(range_start, range_end, "end_tick", &tick_value)) {
                state->timeline.play_range_end_tick = clamp_i64(tick_value, state->timeline.play_range_start_tick, state->timeline.length_ticks);
            }
            if (json_get_bool_range(range_start, range_end, "loop_enabled", &bool_value)) {
                state->timeline.play_range_loop_enabled = bool_value;
            }
            state->timeline.play_range_custom =
                state->timeline.play_range_start_tick > 0 ||
                state->timeline.play_range_end_tick < state->timeline.length_ticks ||
                state->timeline.play_range_loop_enabled;
        }
    }
    const char *array_start = NULL;
    const char *array_end = NULL;
    if (json_find_array_range(json, end, "lanes", &array_start, &array_end)) {
        const char *cursor = array_start + 1;
        while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
            int lane_number = 0;
            if (!json_get_int_range(object_start, object_end, "lane", &lane_number)) continue;
            int lane_index = lane_number - 1;
            if (lane_index < 0 || lane_index >= TIMELINE_MAX_LANES) continue;
            TimelineLane *lane = &state->timeline.lanes[lane_index];
            int int_value = 0;
            char type_text[32];
            if (json_get_string_range(object_start, object_end, "type", type_text, sizeof(type_text))) {
                lane->type = SDL_strcasecmp(type_text, "drums") == 0 ? TIMELINE_LANE_DRUMS : TIMELINE_LANE_AUDIO;
                lane->midi_channel = lane->type == TIMELINE_LANE_DRUMS ? DRUM_MIDI_CHANNEL : 0;
            }
            if (json_get_bool_range(object_start, object_end, "muted", &bool_value)) lane->muted = bool_value;
            if (json_get_double_range(object_start, object_end, "gain", &value)) lane->gain = (float)clamp_double(value, 0.0, 2.0);
            if (json_get_int_range(object_start, object_end, "palette", &int_value)) lane->palette_index = clamp_int(int_value, 0, lane_palette_count() - 1);
            if (json_get_int_range(object_start, object_end, "midi_channel", &int_value)) {
                lane->midi_channel = clamp_int(int_value - 1, 0, 15);
            }
            json_get_string_range(object_start, object_end, "drum_kit_id", lane->drum_kit_id, sizeof(lane->drum_kit_id));
            if (json_get_int_range(object_start, object_end, "drum_step_resolution", &int_value)) {
                lane->drum_step_resolution = clamp_int(int_value, 1, 64);
            }
            lane->drum_kit_index = -1;
            if (lane->type == TIMELINE_LANE_DRUMS && lane->drum_kit_id[0]) {
                for (int kit_index = 0; kit_index < state->drum_kit_count; ++kit_index) {
                    if (SDL_strcmp(state->drum_kits[kit_index].kit_id, lane->drum_kit_id) == 0) {
                        lane->drum_kit_index = kit_index;
                        break;
                    }
                }
            }
            if (lane->type == TIMELINE_LANE_DRUMS && lane->drum_kit_index < 0 && state->drum_kit_count > 0) {
                lane->drum_kit_index = 0;
                SDL_strlcpy(lane->drum_kit_id, state->drum_kits[0].kit_id, sizeof(lane->drum_kit_id));
            }
        }
    }
    if (json_get_bool_range(json, end, "master.fx.reverb_1.enabled", &bool_value)) {
        state->reverb_params.enabled = bool_value;
        state->reverb_param_present[MASTER_REVERB_PARAM_ENABLED] = true;
    }
    struct {
        const char *key;
        MasterReverbParamId param;
        float *target;
    } params[] = {
        { "master.fx.reverb_1.send", MASTER_REVERB_PARAM_SEND, &state->reverb_params.send },
        { "master.fx.reverb_1.return", MASTER_REVERB_PARAM_RETURN, &state->reverb_params.return_gain },
        { "master.fx.reverb_1.predelay_ms", MASTER_REVERB_PARAM_PREDELAY_MS, &state->reverb_params.predelay_ms },
        { "master.fx.reverb_1.decay", MASTER_REVERB_PARAM_DECAY_SECONDS, &state->reverb_params.decay_seconds },
        { "master.fx.reverb_1.size", MASTER_REVERB_PARAM_SIZE, &state->reverb_params.size },
        { "master.fx.reverb_1.diffusion", MASTER_REVERB_PARAM_DIFFUSION, &state->reverb_params.diffusion },
        { "master.fx.reverb_1.damping", MASTER_REVERB_PARAM_DAMPING, &state->reverb_params.damping },
        { "master.fx.reverb_1.low_cut_hz", MASTER_REVERB_PARAM_LOW_CUT_HZ, &state->reverb_params.low_cut_hz },
        { "master.fx.reverb_1.high_cut_hz", MASTER_REVERB_PARAM_HIGH_CUT_HZ, &state->reverb_params.high_cut_hz },
        { "master.fx.reverb_1.width", MASTER_REVERB_PARAM_WIDTH, &state->reverb_params.width },
        { "master.fx.reverb_1.mod_depth_ms", MASTER_REVERB_PARAM_MOD_DEPTH_MS, &state->reverb_params.mod_depth_ms },
        { "master.fx.reverb_1.mod_rate_hz", MASTER_REVERB_PARAM_MOD_RATE_HZ, &state->reverb_params.mod_rate_hz },
    };
    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); ++i) {
        if (json_get_double_range(json, end, params[i].key, &value)) {
            *params[i].target = (float)value;
            state->reverb_param_present[params[i].param] = true;
        }
    }
}

static bool stage_project_bundle(const char *bundle_path, ProjectLoadState *state) {
    char project_path[CLIP_MAX_PATH];
    char timeline_path[CLIP_MAX_PATH];
    char surfaces_path[CLIP_MAX_PATH];
    path_join(project_path, sizeof(project_path), bundle_path, VAPORPLANE_PROJECT_MANIFEST_FILENAME);
    path_join(timeline_path, sizeof(timeline_path), bundle_path, VAPORPLANE_PROJECT_TIMELINE_FILENAME);
    path_join(surfaces_path, sizeof(surfaces_path), bundle_path, VAPORPLANE_PROJECT_SURFACES_FILENAME);
    char *project_json = NULL;
    char *surfaces_json = NULL;
    if (!load_text_file(project_path, &project_json, NULL)) return false;
    bool ok = parse_project_manifest(bundle_path, project_json, state);
    SDL_free(project_json);
    if (!ok) return false;
    ok = parse_project_timeline_mid(timeline_path, state);
    if (!ok) return false;
    if (!load_text_file(surfaces_path, &surfaces_json, NULL)) return false;
    audio_engine_get_master_reverb_params(NULL, NULL, &state->reverb_params);
    parse_project_surfaces(surfaces_json, state);
    SDL_free(surfaces_json);
    return true;
}

static void app_apply_loaded_surfaces(App *app, const ProjectLoadState *state) {
    if (state->has_master_gain) {
        if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
        app->audio.master_gain = state->master_gain;
        if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    }
    for (int param = 0; param < MASTER_REVERB_PARAM_COUNT; ++param) {
        if (!state->reverb_param_present[param]) continue;
        float value = 0.0f;
        switch ((MasterReverbParamId)param) {
            case MASTER_REVERB_PARAM_ENABLED: value = state->reverb_params.enabled ? 1.0f : 0.0f; break;
            case MASTER_REVERB_PARAM_SEND: value = state->reverb_params.send; break;
            case MASTER_REVERB_PARAM_RETURN: value = state->reverb_params.return_gain; break;
            case MASTER_REVERB_PARAM_PREDELAY_MS: value = state->reverb_params.predelay_ms; break;
            case MASTER_REVERB_PARAM_DECAY_SECONDS: value = state->reverb_params.decay_seconds; break;
            case MASTER_REVERB_PARAM_SIZE: value = state->reverb_params.size; break;
            case MASTER_REVERB_PARAM_DIFFUSION: value = state->reverb_params.diffusion; break;
            case MASTER_REVERB_PARAM_DAMPING: value = state->reverb_params.damping; break;
            case MASTER_REVERB_PARAM_LOW_CUT_HZ: value = state->reverb_params.low_cut_hz; break;
            case MASTER_REVERB_PARAM_HIGH_CUT_HZ: value = state->reverb_params.high_cut_hz; break;
            case MASTER_REVERB_PARAM_WIDTH: value = state->reverb_params.width; break;
            case MASTER_REVERB_PARAM_MOD_DEPTH_MS: value = state->reverb_params.mod_depth_ms; break;
            case MASTER_REVERB_PARAM_MOD_RATE_HZ: value = state->reverb_params.mod_rate_hz; break;
            case MASTER_REVERB_PARAM_COUNT: break;
        }
        audio_engine_set_master_reverb_param(&app->audio, (MasterReverbParamId)param, value);
    }
    audio_engine_clear_master_reverb_tail(&app->audio);
}

bool app_load_project_bundle(App *app, const char *bundle_path) {
    if (!app || !bundle_path || !bundle_path[0]) {
        if (app) app_set_status(app, "No project path");
        return false;
    }
    app_project_browser_clear_preview(app);
    ProjectValidationResult validation;
    if (!project_validate_bundle(bundle_path, PROJECT_VALIDATION_FULL, &validation) ||
        validation.status == PROJECT_VALIDATION_INVALID ||
        validation.status == PROJECT_VALIDATION_UNKNOWN) {
        app_set_status(app, validation.reason[0] ? validation.reason : "Could not validate project bundle");
        return false;
    }
    ProjectLoadState staged;
    project_load_state_init(&staged);
    if (!stage_project_bundle(bundle_path, &staged)) {
        project_load_state_destroy(&staged);
        app_set_status(app, "Could not load project bundle");
        return false;
    }

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    app->audio.playback_mode = AUDIO_PLAYBACK_TIMELINE;
    app->audio.timeline_playhead_tick = 0.0;
    app->audio.preview_active = false;
    app->audio.preview_roster_clip_index = -1;
    app->audio.preview_frame = 0.0;
    app->audio.metronome_beat_valid = false;
    for (int i = 0; i < app->roster_clip_count; ++i) {
        roster_clip_destroy(&app->roster[i]);
    }
    SDL_memset(app->roster, 0, sizeof(app->roster));
    for (int i = 0; i < staged.roster_clip_count; ++i) {
        app->roster[i] = staged.roster[i];
        SDL_memset(&staged.roster[i], 0, sizeof(staged.roster[i]));
    }
    app->roster_clip_count = staged.roster_clip_count;
    SDL_memset(app->drum_patterns, 0, sizeof(app->drum_patterns));
    for (int i = 0; i < staged.drum_pattern_count; ++i) {
        app->drum_patterns[i] = staged.drum_patterns[i];
        SDL_memset(&staged.drum_patterns[i], 0, sizeof(staged.drum_patterns[i]));
    }
    app->drum_pattern_count = staged.drum_pattern_count;
    if (staged.drum_kit_count > 0) {
        app_clear_drum_kits(app);
        for (int i = 0; i < staged.drum_kit_count; ++i) {
            app->drum_kits[i] = staged.drum_kits[i];
            SDL_memset(&staged.drum_kits[i], 0, sizeof(staged.drum_kits[i]));
        }
        app->drum_kit_count = staged.drum_kit_count;
    }
    app->timeline = staged.timeline;
    if (!app->timeline.initialized && app->drum_pattern_count > 0) {
        app->timeline.initialized = true;
        int64_t length = app->drum_patterns[0].length_ticks > 0 ? app->drum_patterns[0].length_ticks :
            (int64_t)(app->timeline.ticks_per_beat > 0 ? app->timeline.ticks_per_beat * 4 : 3840);
        app->timeline.length_ticks = length;
        app->timeline.play_range_start_tick = 0;
        app->timeline.play_range_end_tick = length;
        app->timeline.view_center_tick = (double)length * 0.5;
        app->timeline.view_span_ticks = (double)length;
    }
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        if (lane->type != TIMELINE_LANE_DRUMS) continue;
        lane->midi_channel = DRUM_MIDI_CHANNEL;
        lane->drum_kit_index = -1;
        if (lane->drum_kit_id[0]) {
            for (int kit_index = 0; kit_index < app->drum_kit_count; ++kit_index) {
                if (SDL_strcmp(app->drum_kits[kit_index].kit_id, lane->drum_kit_id) == 0) {
                    lane->drum_kit_index = kit_index;
                    break;
                }
            }
        }
        if (lane->drum_kit_index < 0 && app->drum_kit_count > 0) {
            lane->drum_kit_index = 0;
            SDL_strlcpy(lane->drum_kit_id, app->drum_kits[0].kit_id, sizeof(lane->drum_kit_id));
        }
    }
    SDL_strlcpy(app->project_id, staged.project_id, sizeof(app->project_id));
    SDL_strlcpy(app->project_name, staged.project_name, sizeof(app->project_name));
    app->timeline.playing = false;
    app->transport.playing = false;
    app->selected_roster_clip = staged.roster_clip_count > 0 ? 0 : -1;
    app->roster_scroll_offset = 0;
    app->roster_visible_rows = 1;
    app->selected_roster_clip_armed = false;
    app->selected_drum_pattern = staged.drum_pattern_count > 0 ? 0 : -1;
    app->selected_drum_pattern_armed = false;
    app->selected_timeline_lane = 0;
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        if (app->timeline.lanes[lane_index].instance_count > 0) {
            app->selected_timeline_lane = lane_index;
            app->selected_timeline_instance = (TimelineInstanceRef){ lane_index, 0 };
            break;
        }
    }
    app->timeline_focus_zone = TIMELINE_FOCUS_RULER;
    app->timeline_play_range_handle = TIMELINE_RANGE_HANDLE_START;
    app->timeline_play_range_adjusting = false;
    app->timeline_edit_mode = TIMELINE_EDIT_NONE;
    app_timeline_clear_context_menu(app);
    app->project_menu_open = false;
    app->view_mode = APP_VIEW_TIMELINE;
    sync_transport_from_app(app);
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    audio_engine_set_drum_materials(&app->audio,
                                    app->drum_patterns,
                                    &app->drum_pattern_count,
                                    app->drum_kits,
                                    &app->drum_kit_count);

    app_apply_loaded_surfaces(app, &staged);
    project_load_state_destroy(&staged);
    if (validation.status == PROJECT_VALIDATION_WARNING) {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Loaded with warning: %s", validation.reason);
    } else {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Loaded %s", bundle_path);
    }
    return true;
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

static bool app_export_roster_clip_wav_to_path(App *app, int roster_index, const char *path) {
    if (!app || roster_index < 0 || roster_index >= app->roster_clip_count || !path || !path[0]) {
        app_set_status(app, "No roster clip selected");
        return false;
    }
    bool exported = write_roster_clip_wav(&app->roster[roster_index], path);
    if (!exported) {
        app_set_status(app, "Could not export roster WAV");
        return false;
    }
    app->selected_roster_clip = roster_index;
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Exported %s", path);
    return true;
}

static bool app_export_roster_clip_wav_named(App *app, int roster_index, const char *filename_text) {
    if (!app || roster_index < 0 || roster_index >= app->roster_clip_count) {
        app_text_entry_set_error(app, "No roster clip selected");
        return false;
    }

    char leaf[APP_SAMPLE_NAME_MAX + 8];
    if (!sanitize_wav_filename_leaf(filename_text, leaf, sizeof(leaf))) {
        app_text_entry_set_error(app, "Enter a file name");
        return false;
    }

    if (!SDL_CreateDirectory(app->roster_export_dir)) {
        if (app->roster_export_dir_is_base_path) app_use_cwd_roster_export_dir(app);
        if (!SDL_CreateDirectory(app->roster_export_dir)) {
            app_text_entry_set_error(app, "Could not create roster export folder");
            return false;
        }
    }

    char path[CLIP_MAX_PATH];
    path_join(path, sizeof(path), app->roster_export_dir, leaf);
    if (path_exists_any(path)) {
        app_text_entry_set_error(app, "File already exists");
        return false;
    }

    if (app_export_roster_clip_wav_to_path(app, roster_index, path)) return true;

    if (app->roster_export_dir_is_base_path) {
        app_use_cwd_roster_export_dir(app);
        path_join(path, sizeof(path), app->roster_export_dir, leaf);
        if (path_exists_any(path)) {
            app_text_entry_set_error(app, "File already exists");
            return false;
        }
        if (app_export_roster_clip_wav_to_path(app, roster_index, path)) return true;
    }

    app_text_entry_set_error(app, app->status_text[0] ? app->status_text : "Could not export roster WAV");
    return false;
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
    app_repair_roster_adjacent_colors(app);

    if (app->roster_clip_count <= 0) {
        app->selected_roster_clip = -1;
        app->selected_roster_clip_armed = false;
    } else {
        app->selected_roster_clip = clamp_int(delete_index, 0, app->roster_clip_count - 1);
        app->selected_roster_clip_armed = false;
    }
    app_clamp_roster_scroll(app, app->roster_visible_rows);
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    if (!timeline_has_instances(&app->timeline)) {
        app->timeline.timeline_cursor_tick = 0;
        app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
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
    int velocity = instance ? instance->midi_velocity : 127;
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
        float tape_speed = timeline_effective_tape_speed(&app->timeline);
        if (fabsf(tape_speed - 1.0f) > 0.001f) {
            SDL_snprintf(app->status_text, sizeof(app->status_text), "Preview %s @ %.3fx", clip->name, tape_speed);
        } else {
            SDL_snprintf(app->status_text, sizeof(app->status_text), "Preview %s", clip->name);
        }
    } else {
        app_set_status(app, "Preview unavailable");
    }
}

static void make_safe_stable_id(const char *prefix, const char *name, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    size_t write = 0;
    if (prefix && prefix[0]) {
        write += (size_t)SDL_snprintf(out, out_size, "%s_", prefix);
        if (write >= out_size) {
            out[out_size - 1] = '\0';
            return;
        }
    }
    const char *text = name && name[0] ? name : "kit";
    bool last_sep = false;
    for (size_t read = 0; text[read] && write + 1 < out_size; ++read) {
        char c = (char)tolower((unsigned char)text[read]);
        bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) {
            out[write++] = c;
            last_sep = false;
        } else if (!last_sep && write > 0 && write + 1 < out_size) {
            out[write++] = '_';
            last_sep = true;
        }
    }
    while (write > 0 && out[write - 1] == '_') write--;
    out[write] = '\0';
    if (!out[0]) generate_stable_id(prefix ? prefix : "id", out, out_size);
}

static bool drum_kit_id_exists(const App *app, const char *kit_id) {
    if (!app || !kit_id || !kit_id[0]) return false;
    for (int i = 0; i < app->drum_kit_count; ++i) {
        if (SDL_strcmp(app->drum_kits[i].kit_id, kit_id) == 0) return true;
    }
    return false;
}

static void path_dirname_copy(const char *path, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    SDL_strlcpy(out, path ? path : "", out_size);
    char *slash = SDL_strrchr(out, '/');
    char *backslash = SDL_strrchr(out, '\\');
    char *cut = slash > backslash ? slash : backslash;
    if (cut) *cut = '\0';
    if (!out[0]) SDL_strlcpy(out, ".", out_size);
}

static bool app_load_drum_kit_json(App *app, const char *json_path, bool project_local) {
    if (!app || !json_path || !json_path[0] || app->drum_kit_count >= APP_MAX_DRUM_KITS) return false;
    char *json = NULL;
    if (!load_text_file(json_path, &json, NULL)) return false;
    const char *end = json + SDL_strlen(json);

    DrumKit kit;
    SDL_memset(&kit, 0, sizeof(kit));
    path_dirname_copy(json_path, kit.root_path, sizeof(kit.root_path));
    kit.project_local = project_local;
    if (!json_get_string_range(json, end, "name", kit.name, sizeof(kit.name))) {
        SDL_strlcpy(kit.name, path_basename(kit.root_path), sizeof(kit.name));
    }
    if (!json_get_string_range(json, end, "id", kit.kit_id, sizeof(kit.kit_id))) {
        make_safe_stable_id("kit", kit.name, kit.kit_id, sizeof(kit.kit_id));
    }
    if (drum_kit_id_exists(app, kit.kit_id)) {
        int suffix = 2;
        char base[APP_STABLE_ID_MAX];
        SDL_strlcpy(base, kit.kit_id, sizeof(base));
        do {
            SDL_snprintf(kit.kit_id, sizeof(kit.kit_id), "%s_%d", base, suffix++);
        } while (drum_kit_id_exists(app, kit.kit_id) && suffix < 1000);
    }

    const char *array_start = NULL;
    const char *array_end = NULL;
    if (!json_find_array_range(json, end, "pads", &array_start, &array_end)) {
        SDL_free(json);
        return false;
    }
    const char *cursor = array_start + 1;
    const char *object_start = NULL;
    const char *object_end = NULL;
    while (kit.pad_count < APP_MAX_DRUM_PADS &&
           json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
        DrumPad *pad = &kit.pads[kit.pad_count];
        int note = -1;
        char file[CLIP_MAX_PATH];
        file[0] = '\0';
        if (!json_get_int_range(object_start, object_end, "note", &note) ||
            !json_get_string_range(object_start, object_end, "file", file, sizeof(file))) {
            continue;
        }
        pad->note = clamp_int(note, 0, 127);
        if (!json_get_string_range(object_start, object_end, "name", pad->name, sizeof(pad->name))) {
            SDL_snprintf(pad->name, sizeof(pad->name), "note %d", pad->note);
        }
        path_join(pad->source_path, sizeof(pad->source_path), kit.root_path, file);
        pad->project_local = project_local;
        if (clip_init_from_wav(&pad->clip, pad->source_path)) {
            pad->loaded = true;
        }
        kit.pad_count++;
    }
    SDL_free(json);
    if (kit.pad_count <= 0) {
        drum_kit_destroy(&kit);
        return false;
    }
    app->drum_kits[app->drum_kit_count++] = kit;
    return true;
}

static int app_find_drum_kit_by_id(const App *app, const char *kit_id) {
    if (!app || !kit_id || !kit_id[0]) return -1;
    for (int i = 0; i < app->drum_kit_count; ++i) {
        if (SDL_strcmp(app->drum_kits[i].kit_id, kit_id) == 0) return i;
    }
    return -1;
}

static void app_swap_drum_kits(App *app, int a, int b) {
    if (!app || a < 0 || b < 0 || a >= app->drum_kit_count || b >= app->drum_kit_count || a == b) return;
    DrumKit tmp = app->drum_kits[a];
    app->drum_kits[a] = app->drum_kits[b];
    app->drum_kits[b] = tmp;

    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        if (lane->type != TIMELINE_LANE_DRUMS) continue;
        if (lane->drum_kit_index == a) lane->drum_kit_index = b;
        else if (lane->drum_kit_index == b) lane->drum_kit_index = a;
    }
}

static void app_promote_default_drum_kit(App *app) {
    int vhs_index = app_find_drum_kit_by_id(app, "vhs_cc0");
    if (vhs_index > 0) app_swap_drum_kits(app, 0, vhs_index);
}

static void app_refresh_drum_kits(App *app) {
    if (!app) return;
    app_clear_drum_kits(app);
    char pack_dir[CLIP_MAX_PATH];
    SDL_strlcpy(pack_dir, app->drum_pack_dir[0] ? app->drum_pack_dir : "assets/drum_packs", sizeof(pack_dir));
    if (!path_is_directory(pack_dir)) return;

    int count = 0;
    char **names = SDL_GlobDirectory(pack_dir, "*.json", SDL_GLOB_CASEINSENSITIVE, &count);
    if (names) {
        qsort(names, (size_t)count, sizeof(char *), compare_strings);
        for (int i = 0; i < count && app->drum_kit_count < APP_MAX_DRUM_KITS; ++i) {
            char path[CLIP_MAX_PATH];
            path_join(path, sizeof(path), pack_dir, names[i]);
            app_load_drum_kit_json(app, path, false);
        }
        SDL_free(names);
    }

    int child_count = 0;
    char **children = SDL_GlobDirectory(pack_dir, "*", 0, &child_count);
    if (children) {
        qsort(children, (size_t)child_count, sizeof(char *), compare_strings);
        for (int i = 0; i < child_count && app->drum_kit_count < APP_MAX_DRUM_KITS; ++i) {
            char child_path[CLIP_MAX_PATH];
            path_join(child_path, sizeof(child_path), pack_dir, children[i]);
            if (!path_is_directory(child_path)) continue;
            char kit_json[CLIP_MAX_PATH];
            path_join(kit_json, sizeof(kit_json), child_path, "kit.json");
            if (path_exists_any(kit_json)) app_load_drum_kit_json(app, kit_json, false);
        }
        SDL_free(children);
    }
    app_promote_default_drum_kit(app);
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

void app_toggle_inspected_lane_type(App *app) {
    int lane_index = clamp_int(app->inspected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    if (lane->instance_count > 0) {
        app_set_status(app, "Clear lane before changing type");
        return;
    }
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    if (lane->type == TIMELINE_LANE_DRUMS) {
        lane->type = TIMELINE_LANE_AUDIO;
        lane->midi_channel = 0;
        lane->drum_kit_index = -1;
        lane->drum_kit_id[0] = '\0';
    } else {
        lane->type = TIMELINE_LANE_DRUMS;
        lane->midi_channel = DRUM_MIDI_CHANNEL;
        lane->drum_kit_index = app->drum_kit_count > 0 ? 0 : -1;
        if (app_kit_index_valid(app, lane->drum_kit_index)) {
            SDL_strlcpy(lane->drum_kit_id, app->drum_kits[lane->drum_kit_index].kit_id, sizeof(lane->drum_kit_id));
        } else {
            lane->drum_kit_id[0] = '\0';
        }
        if (lane->drum_step_resolution <= 0) lane->drum_step_resolution = 16;
    }
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    app->selected_timeline_lane = lane_index;
    app->selected_roster_clip_armed = false;
    app->selected_drum_pattern_armed = false;
    SDL_snprintf(app->status_text,
                 sizeof(app->status_text),
                 "Lane %d type %s",
                 lane_index + 1,
                 timeline_lane_type_label(lane->type));
}

void app_cycle_inspected_lane_kit(App *app, int direction) {
    if (direction == 0) return;
    int lane_index = clamp_int(app->inspected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    if (lane->type != TIMELINE_LANE_DRUMS) {
        app_set_status(app, "Lane is not drums");
        return;
    }
    if (app->drum_kit_count <= 0) {
        lane->drum_kit_index = -1;
        lane->drum_kit_id[0] = '\0';
        app_set_status(app, "No drum kits found");
        return;
    }
    int index = lane->drum_kit_index;
    if (index < 0 || index >= app->drum_kit_count) index = 0;
    index += direction;
    while (index < 0) index += app->drum_kit_count;
    index %= app->drum_kit_count;
    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    lane->drum_kit_index = index;
    SDL_strlcpy(lane->drum_kit_id, app->drum_kits[index].kit_id, sizeof(lane->drum_kit_id));
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Kit %s", app->drum_kits[index].name);
}

void app_create_drum_pattern(App *app) {
    if (!app) return;
    if (app->drum_pattern_count >= APP_MAX_DRUM_PATTERNS) {
        app_set_status(app, "pattern roster full");
        return;
    }
    int index = app->drum_pattern_count++;
    char name[APP_ROSTER_CLIP_NAME_MAX];
    SDL_snprintf(name, sizeof(name), "beat %02d", index + 1);
    drum_pattern_init_defaults(app, &app->drum_patterns[index], index, name);
    drum_pattern_seed_basic_beat(&app->drum_patterns[index]);
    if (!app->timeline.initialized) {
        app->timeline.initialized = true;
        app->timeline.length_ticks = app->drum_patterns[index].length_ticks;
        app->timeline.play_range_start_tick = 0;
        app->timeline.play_range_end_tick = app->timeline.length_ticks;
        app->timeline.view_center_tick = (double)app->timeline.length_ticks * 0.5;
        app->timeline.view_span_ticks = (double)app->timeline.length_ticks;
    }
    app->selected_drum_pattern = index;
    app->selected_drum_pattern_armed = false;
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Created %s", app->drum_patterns[index].name);
}

void app_duplicate_selected_drum_pattern(App *app) {
    if (!app_pattern_index_valid(app, app->selected_drum_pattern)) {
        app_set_status(app, "No pattern selected");
        return;
    }
    if (app->drum_pattern_count >= APP_MAX_DRUM_PATTERNS) {
        app_set_status(app, "pattern roster full");
        return;
    }
    int src_index = app->selected_drum_pattern;
    int dst_index = app->drum_pattern_count++;
    app->drum_patterns[dst_index] = app->drum_patterns[src_index];
    generate_stable_id("pat", app->drum_patterns[dst_index].pattern_id, sizeof(app->drum_patterns[dst_index].pattern_id));
    app->drum_patterns[dst_index].locator_channel = DRUM_PATTERN_LOCATOR_CHANNEL;
    app->drum_patterns[dst_index].locator_note = app_next_available_pattern_locator_note(app);
    app->drum_patterns[dst_index].color = roster_color_for_index(dst_index);
    SDL_snprintf(app->drum_patterns[dst_index].name,
                 sizeof(app->drum_patterns[dst_index].name),
                 "%s copy",
                 app->drum_patterns[src_index].name);
    app->selected_drum_pattern = dst_index;
    app->selected_drum_pattern_armed = false;
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Duplicated %s", app->drum_patterns[src_index].name);
}

void app_delete_selected_drum_pattern(App *app) {
    int delete_index = app ? app->selected_drum_pattern : -1;
    if (!app_pattern_index_valid(app, delete_index)) {
        app_timeline_clear_context_menu(app);
        app_set_status(app, "No pattern selected");
        return;
    }
    char deleted_name[APP_ROSTER_CLIP_NAME_MAX];
    SDL_strlcpy(deleted_name, app->drum_patterns[delete_index].name, sizeof(deleted_name));
    audio_engine_stop_timeline(&app->audio, false);
    audio_engine_stop_preview(&app->audio);

    if (app->audio.stream) SDL_LockAudioStream(app->audio.stream);
    for (int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        TimelineLane *lane = &app->timeline.lanes[lane_index];
        int write_index = 0;
        for (int read_index = 0; read_index < lane->instance_count; ++read_index) {
            TimelineInstance instance = lane->instances[read_index];
            if (instance.kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
                if (instance.pattern_index == delete_index) continue;
                if (instance.pattern_index > delete_index) instance.pattern_index--;
            }
            lane->instances[write_index++] = instance;
        }
        lane->instance_count = write_index;
    }
    for (int i = delete_index; i + 1 < app->drum_pattern_count; ++i) {
        app->drum_patterns[i] = app->drum_patterns[i + 1];
    }
    app->drum_pattern_count--;
    if (app->drum_pattern_count >= 0) {
        SDL_memset(&app->drum_patterns[app->drum_pattern_count],
                   0,
                   sizeof(app->drum_patterns[app->drum_pattern_count]));
    }
    if (app->drum_pattern_count <= 0) {
        app->selected_drum_pattern = -1;
        app->selected_drum_pattern_armed = false;
    } else {
        app->selected_drum_pattern = clamp_int(delete_index, 0, app->drum_pattern_count - 1);
        app->selected_drum_pattern_armed = false;
    }
    app->selected_timeline_instance = timeline_instance_ref_invalid();
    recompute_timeline_length_no_lock(app);
    int64_t playhead = app->timeline.playhead_tick;
    if (app->audio.stream) SDL_UnlockAudioStream(app->audio.stream);

    audio_engine_set_timeline_playhead(&app->audio, playhead);
    sync_transport_from_app(app);
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Deleted %s", deleted_name);
}

static bool app_rename_drum_pattern_named(App *app, int pattern_index, const char *display_name) {
    if (!app_pattern_index_valid(app, pattern_index)) {
        app_text_entry_set_error(app, "No pattern selected");
        return false;
    }
    char trimmed[APP_ROSTER_CLIP_NAME_MAX];
    trim_display_name(display_name, trimmed, sizeof(trimmed));
    if (!trimmed[0]) {
        app_text_entry_set_error(app, "Enter a pattern name");
        return false;
    }
    SDL_strlcpy(app->drum_patterns[pattern_index].name, trimmed, sizeof(app->drum_patterns[pattern_index].name));
    app->selected_drum_pattern = pattern_index;
    app->selected_drum_pattern_armed = false;
    app_timeline_clear_context_menu(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Renamed pattern %s", trimmed);
    return true;
}

static DrumKit *app_selected_lane_drum_kit(App *app) {
    if (!app) return NULL;
    int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
    TimelineLane *lane = &app->timeline.lanes[lane_index];
    if (lane->type != TIMELINE_LANE_DRUMS || !app_kit_index_valid(app, lane->drum_kit_index)) return NULL;
    return &app->drum_kits[lane->drum_kit_index];
}

void app_open_drum_machine_for_selected_pattern(App *app) {
    if (!app) return;
    if (!app_selected_timeline_lane_is_drum(app)) {
        app_set_status(app, "Select a drum lane first");
        return;
    }
    if (app->drum_pattern_count <= 0) app_create_drum_pattern(app);
    if (!app_pattern_index_valid(app, app->selected_drum_pattern)) {
        app_set_status(app, "No pattern selected");
        return;
    }
    DrumKit *kit = app_selected_lane_drum_kit(app);
    if (!kit || kit->pad_count <= 0) {
        app_set_status(app, "No drum kit loaded");
        return;
    }
    app->drum_machine_step = clamp_int(app->drum_machine_step, 0, 15);
    app->drum_machine_pad = clamp_int(app->drum_machine_pad, 0, (kit->pad_count > 12 ? 12 : kit->pad_count) - 1);
    app->view_mode = APP_VIEW_DRUM_MACHINE;
    sync_transport_from_app(app);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Editing %s", app->drum_patterns[app->selected_drum_pattern].name);
}

void app_close_drum_machine(App *app) {
    if (!app || app->view_mode != APP_VIEW_DRUM_MACHINE) return;
    app->view_mode = APP_VIEW_TIMELINE;
    sync_transport_from_app(app);
    app_set_status(app, "Timeline");
}

void app_drum_machine_move_cursor(App *app, int dx, int dy) {
    if (!app || app->view_mode != APP_VIEW_DRUM_MACHINE) return;
    DrumKit *kit = app_selected_lane_drum_kit(app);
    int pad_count = kit ? (kit->pad_count > 12 ? 12 : kit->pad_count) : 1;
    app->drum_machine_step = clamp_int(app->drum_machine_step + dx, 0, 15);
    app->drum_machine_pad = clamp_int(app->drum_machine_pad + dy, 0, pad_count - 1);
}

void app_drum_machine_toggle_step(App *app) {
    if (!app || app->view_mode != APP_VIEW_DRUM_MACHINE ||
        !app_pattern_index_valid(app, app->selected_drum_pattern)) {
        return;
    }
    DrumKit *kit = app_selected_lane_drum_kit(app);
    if (!kit || app->drum_machine_pad < 0 || app->drum_machine_pad >= kit->pad_count) return;
    DrumPattern *pattern = &app->drum_patterns[app->selected_drum_pattern];
    int64_t step_ticks = pattern->length_ticks / 16;
    if (step_ticks <= 0) step_ticks = 1;
    int64_t tick = (int64_t)app->drum_machine_step * step_ticks;
    int note = kit->pads[app->drum_machine_pad].note;
    int existing = drum_pattern_event_index(pattern, tick, note);
    if (existing >= 0) {
        drum_pattern_remove_event_at(pattern, existing);
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Removed %s step %d", kit->pads[app->drum_machine_pad].name, app->drum_machine_step + 1);
    } else if (drum_pattern_add_event(pattern, tick, note, 108, step_ticks)) {
        SDL_snprintf(app->status_text, sizeof(app->status_text), "Added %s step %d", kit->pads[app->drum_machine_pad].name, app->drum_machine_step + 1);
    } else {
        app_set_status(app, "Pattern full");
    }
}

void app_drum_machine_adjust_velocity(App *app, int delta) {
    if (!app || app->view_mode != APP_VIEW_DRUM_MACHINE ||
        !app_pattern_index_valid(app, app->selected_drum_pattern) ||
        delta == 0) {
        return;
    }
    DrumKit *kit = app_selected_lane_drum_kit(app);
    if (!kit || app->drum_machine_pad < 0 || app->drum_machine_pad >= kit->pad_count) return;
    DrumPattern *pattern = &app->drum_patterns[app->selected_drum_pattern];
    int64_t step_ticks = pattern->length_ticks / 16;
    if (step_ticks <= 0) step_ticks = 1;
    int64_t tick = (int64_t)app->drum_machine_step * step_ticks;
    int note = kit->pads[app->drum_machine_pad].note;
    int index = drum_pattern_event_index(pattern, tick, note);
    if (index < 0) {
        app_set_status(app, "No hit at cursor");
        return;
    }
    pattern->events[index].velocity = clamp_int(pattern->events[index].velocity + delta, 1, 127);
    SDL_snprintf(app->status_text, sizeof(app->status_text), "Velocity %d", pattern->events[index].velocity);
}

void app_enter_tempo_lock_mode(App *app) {
    if (app->tempo_lock_mode) {
        app_cancel_tempo_lock_mode(app);
        return;
    }
    app_clear_waveform_frame_grip(app);
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
    if (app->roster_commit_menu_open) return;
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
    app_clear_waveform_frame_grip(app);
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
    static const double options[] = {0.25, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0};
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
    char **names = SDL_GlobDirectory(app->sample_dir, "*", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) return;
    qsort(names, (size_t)count, sizeof(char *), compare_strings);

    for (int i = 0; i < count && app->sample_count < APP_MAX_SAMPLES; ++i) {
        if (!app_sample_extension_supported(names[i])) continue;
        char path[CLIP_MAX_PATH];
        path_join(path, sizeof(path), app->sample_dir, names[i]);
        if (path_is_directory(path)) continue;
        SampleEntry *entry = &app->samples[app->sample_count++];
        SDL_strlcpy(entry->path, path, sizeof(entry->path));
        SDL_strlcpy(entry->name, names[i], sizeof(entry->name));
    }
    SDL_free(names);
}

bool load_clip_from_path(App *app, const char *path) {
    AudioClip next;
    if (!clip_init_from_audio_file(&next, path)) {
        app_set_status(app, "Could not load audio file");
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
    app_clear_waveform_frame_grip(app);
    set_transport_bpm_from_metadata(app);
    app_set_waveform_source_wav(app, path);
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
        SDL_snprintf(app->status_text, sizeof(app->status_text), "No supported audio files in %s", app->sample_dir);
        return false;
    }
    return load_clip_from_path(app, app->samples[app->selected_sample].path);
}

void app_select_sample_delta(App *app, int delta) {
    if (app->sample_count <= 0) return;
    app->selected_sample = (app->selected_sample + delta) % app->sample_count;
    if (app->selected_sample < 0) app->selected_sample += app->sample_count;
}

static float debug_text_width(const char *text) {
    return text ? (float)SDL_strlen(text) * 8.0f : 0.0f;
}

static void render_debug_text_centered(SDL_Renderer *renderer, float center_x, float y, const char *text) {
    if (!text || !text[0]) return;
    SDL_RenderDebugText(renderer, center_x - debug_text_width(text) * 0.5f, y, text);
}

static void app_render_controls_legend(App *app) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_FRect panel = { 36.0f, 72.0f, 1040.0f, 610.0f };
    if (panel.w > (float)w - 72.0f) panel.w = (float)w - 72.0f;
    if (panel.h > (float)h - 104.0f) panel.h = (float)h - 104.0f;
    if (panel.w < 420.0f) panel.w = (float)w - 24.0f;
    if (panel.h < 360.0f) panel.h = (float)h - 48.0f;
    panel.x = ((float)w - panel.w) * 0.5f;
    if (panel.x < 12.0f) panel.x = 12.0f;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 9, 10, 16, 232);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 120, 220, 235, 255);
    SDL_RenderRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);

    SDL_Rect clip = {
        (int)panel.x,
        (int)panel.y,
        (int)panel.w,
        (int)panel.h
    };
    SDL_SetRenderClipRect(app->renderer, &clip);

    float padding = 16.0f;
    float key_w = panel.w * 0.26f;
    if (key_w < 132.0f) key_w = 132.0f;
    if (key_w > 220.0f) key_w = 220.0f;
    float keyboard_x = panel.x + padding;
    float gamepad_x = panel.x + panel.w - padding - key_w;
    float action_center = panel.x + panel.w * 0.5f;
    float y = panel.y + 14.0f;
    float bottom = panel.y + panel.h - 18.0f;

    typedef struct {
        const char *keyboard;
        const char *action;
        const char *gamepad;
        bool section;
    } LegendRow;
    static const LegendRow rows[] = {
        { "", "GLOBAL", "", true },
        { "F1", "Show / hide legend", "", false },
        { "F2", "Change view", "R2+Start", false },
        { "Tab", "Shift focus", "Bumpers", false },
        { "Enter / C", "Activate / menu", "South / Start", false },
        { "Esc", "Cancel / Project menu", "East / L2+Start", false },
        { "", "WAVEFORM", "", true },
        { "Space", "Play / pause", "South / Start", false },
        { "A / D", "Move loop start", "L1 arms start", false },
        { "J / L", "Move loop end", "R1 arms end", false },
        { "", "Trim armed edge", "D-pad L/R", false },
        { "Up / Down", "Zoom waveform", "D-pad U/D", false },
        { "T", "Tempo Lock", "R2+North", false },
        { "", "Set visible loop", "R2+South", false },
        { "", "Capture to roster", "L2+R2+South", false },
        { "", "TIMELINE", "", true },
        { "Space", "Play / pause", "R2+South", false },
        { "Home", "Rewind", "R2+East", false },
        { "", "Set playhead to cursor", "R2+West", false },
        { "", "Loop play range", "R2+North", false },
        { "Left / Right", "Pan / cursor", "D-pad L/R", false },
        { "Up / Down", "Zoom / lane", "D-pad U/D", false },
        { "[ / ]", "Velocity / marked BPM", "", false },
        { "", "TRACKS + ROSTER", "", true },
        { "Enter", "Select / move / place", "South", false },
        { "C", "Context menu", "Start", false },
        { "", "Preview roster clip", "Right stick", false },
        { "", "DRUMS", "", true },
        { "D", "Open drum machine", "", false },
        { "Arrows", "Move drum cursor", "D-pad", false },
        { "Space / Enter", "Toggle step", "South", false },
        { "[ / ]", "Step velocity", "Bumpers", false },
        { "", "TEMPO + MIX", "", true },
        { "[ / ]", "Adjust BPM", "", false },
        { "M", "Metronome", "Back", false },
        { "Tab", "Master Mix focus", "Bumpers", false },
        { "Arrows", "Reverb select / adjust", "D-pad", false }
    };

    SDL_RenderDebugText(app->renderer, keyboard_x, y, "KEYBOARD");
    render_debug_text_centered(app->renderer, action_center, y, "ACTION");
    SDL_RenderDebugText(app->renderer, gamepad_x, y, "GAMEPAD");
    y += 16.0f;
    SDL_SetRenderDrawColor(app->renderer, 68, 88, 104, 255);
    SDL_RenderLine(app->renderer, panel.x + padding, y + 3.0f, panel.x + panel.w - padding, y + 3.0f);
    y += 14.0f;

    for (size_t i = 0; i < SDL_arraysize(rows) && y <= bottom; ++i) {
        const LegendRow *row = &rows[i];
        if (row->section) {
            y += 5.0f;
            if (y > bottom) break;
            SDL_SetRenderDrawColor(app->renderer, 120, 220, 235, 255);
            render_debug_text_centered(app->renderer, action_center, y, row->action);
            y += 15.0f;
            continue;
        }
        SDL_SetRenderDrawColor(app->renderer, 190, 204, 214, 255);
        if (row->keyboard && row->keyboard[0]) SDL_RenderDebugText(app->renderer, keyboard_x, y, row->keyboard);
        if (row->gamepad && row->gamepad[0]) SDL_RenderDebugText(app->renderer, gamepad_x, y, row->gamepad);
        SDL_SetRenderDrawColor(app->renderer, 238, 244, 246, 255);
        render_debug_text_centered(app->renderer, action_center, y, row->action);
        y += 14.0f;
    }

    SDL_SetRenderClipRect(app->renderer, NULL);
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

static void render_project_menu(App *app, float anchor_x, float anchor_y, int w, int h) {
    if (!app || !app->project_menu_open) return;
    const int item_count = (int)PROJECT_MENU_ITEM_COUNT;
    SDL_FRect menu = {
        anchor_x + 18.0f,
        anchor_y + 34.0f,
        300.0f,
        58.0f + (float)item_count * 22.0f
    };
    float menu_pad = 18.0f;
    if (menu.x + menu.w > (float)w - menu_pad) menu.x = (float)w - menu_pad - menu.w;
    if (menu.y + menu.h > (float)h - menu_pad) menu.y = (float)h - menu_pad - menu.h;
    if (menu.x < menu_pad) menu.x = menu_pad;
    if (menu.y < menu_pad) menu.y = menu_pad;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect shadow = { menu.x + 12.0f, menu.y + 14.0f, menu.w + 20.0f, menu.h + 20.0f };
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 146);
    SDL_RenderFillRect(app->renderer, &shadow);
    SDL_FRect soft_shadow = { menu.x + 4.0f, menu.y + 6.0f, menu.w + 12.0f, menu.h + 12.0f };
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 96);
    SDL_RenderFillRect(app->renderer, &soft_shadow);
    SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 238);
    SDL_RenderFillRect(app->renderer, &menu);
    SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 255);
    SDL_RenderRect(app->renderer, &menu);
    SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);
    SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 10.0f, "PROJECT");
    SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 28.0f, "bundle");

    float item_y = menu.y + 50.0f;
    for (int i = 0; i < item_count; ++i) {
            ProjectMenuItem item = (ProjectMenuItem)i;
            SDL_FRect row = { menu.x + 8.0f, item_y - 4.0f, menu.w - 16.0f, 20.0f };
            bool selected = i == app->project_menu_selected;
            bool disabled = false;
        if (selected) {
            SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 52);
            SDL_RenderFillRect(app->renderer, &row);
            SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 255);
            SDL_RenderRect(app->renderer, &row);
        }
        if (disabled) {
            SDL_SetRenderDrawColor(app->renderer, selected ? 176 : 124, selected ? 192 : 136, selected ? 204 : 150, 255);
        } else {
            SDL_SetRenderDrawColor(app->renderer, selected ? 226 : 210, selected ? 252 : 218, selected ? 246 : 226, 255);
        }
        SDL_RenderDebugText(app->renderer, menu.x + 18.0f, item_y, project_menu_item_label(item));
        item_y += 22.0f;
    }
}

static void render_project_menu_legend_hint(App *app) {
    if (!app || !app->project_menu_open || app->controls_legend_open) return;
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    const char *hint = "F1 for legend";
    float x = (float)w - debug_text_width(hint) - 18.0f;
    float y = (float)h - 28.0f;
    if (x < 12.0f) x = 12.0f;
    if (y < 12.0f) y = 12.0f;
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 180);
    SDL_RenderDebugText(app->renderer, x + 1.0f, y + 1.0f, hint);
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(app->renderer, x, y, hint);
}

static void render_waveform_menu(App *app, int w, int h) {
    if (!app || !app->waveform_menu_open) return;
    const int item_count = (int)WAVEFORM_MENU_ITEM_COUNT;
    SDL_FRect menu = {
        64.0f,
        96.0f,
        260.0f,
        58.0f + (float)item_count * 22.0f
    };
    float menu_pad = 18.0f;
    if (menu.x + menu.w > (float)w - menu_pad) menu.x = (float)w - menu_pad - menu.w;
    if (menu.y + menu.h > (float)h - menu_pad) menu.y = (float)h - menu_pad - menu.h;
    if (menu.x < menu_pad) menu.x = menu_pad;
    if (menu.y < menu_pad) menu.y = menu_pad;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect shadow = { menu.x + 10.0f, menu.y + 12.0f, menu.w + 18.0f, menu.h + 18.0f };
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 140);
    SDL_RenderFillRect(app->renderer, &shadow);
    SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 238);
    SDL_RenderFillRect(app->renderer, &menu);
    SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 255);
    SDL_RenderRect(app->renderer, &menu);
    SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);
    SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 10.0f, "WAVEFORM");
    SDL_SetRenderDrawColor(app->renderer, 178, 190, 204, 255);
    SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 28.0f, "sample");

    float item_y = menu.y + 50.0f;
    for (int i = 0; i < item_count; ++i) {
        WaveformMenuItem item = (WaveformMenuItem)i;
        SDL_FRect row = { menu.x + 8.0f, item_y - 4.0f, menu.w - 16.0f, 20.0f };
        bool selected = i == app->waveform_menu_selected;
        if (selected) {
            SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 52);
            SDL_RenderFillRect(app->renderer, &row);
            SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 255);
            SDL_RenderRect(app->renderer, &row);
        }
        SDL_SetRenderDrawColor(app->renderer, selected ? 226 : 210, selected ? 252 : 218, selected ? 246 : 226, 255);
        SDL_RenderDebugText(app->renderer, menu.x + 18.0f, item_y, waveform_menu_item_label(app, item));
        item_y += 22.0f;
    }
}

static void render_waveform_render_dialog(App *app, int w, int h) {
    if (!app || !app->waveform_render_dialog_open) return;
    SDL_FRect panel = {
        (float)w * 0.5f - 210.0f,
        (float)h * 0.5f - 92.0f,
        420.0f,
        184.0f
    };
    const float pad = 18.0f;
    if (panel.x < pad) panel.x = pad;
    if (panel.y < pad) panel.y = pad;
    if (panel.x + panel.w > (float)w - pad) panel.x = (float)w - pad - panel.w;
    if (panel.y + panel.h > (float)h - pad) panel.y = (float)h - pad - panel.h;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect shadow = { panel.x + 12.0f, panel.y + 14.0f, panel.w + 18.0f, panel.h + 18.0f };
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(app->renderer, &shadow);
    SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 244);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 255);
    SDL_RenderRect(app->renderer, &panel);

    WaveformRenderDialogMode mode = app->waveform_render_dialog_mode;
    double step = waveform_render_step_for_index(app->waveform_render_step_index);
    const char *unit = waveform_render_dialog_unit(mode);

    SDL_SetRenderDrawColor(app->renderer, 235, 242, 245, 255);
    SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 16.0f, waveform_render_dialog_title(mode));
    SDL_SetRenderDrawColor(app->renderer, 178, 190, 204, 255);
    if (mode == WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM) {
        SDL_RenderDebugTextFormat(app->renderer,
                                  panel.x + 18.0f,
                                  panel.y + 40.0f,
                                  "source BPM %.2f",
                                  app->waveform_render_source_bpm);
    } else {
        SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 40.0f, "offline render");
    }

    SDL_FRect value_box = { panel.x + 18.0f, panel.y + 66.0f, panel.w - 36.0f, 40.0f };
    SDL_SetRenderDrawColor(app->renderer, 25, 31, 42, 250);
    SDL_RenderFillRect(app->renderer, &value_box);
    SDL_SetRenderDrawColor(app->renderer, 90, 130, 146, 255);
    SDL_RenderRect(app->renderer, &value_box);
    SDL_SetRenderDrawColor(app->renderer, 226, 252, 246, 255);
    if (mode == WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM) {
        SDL_RenderDebugTextFormat(app->renderer,
                                  value_box.x + 14.0f,
                                  value_box.y + 13.0f,
                                  "value %.2f %s",
                                  app->waveform_render_value,
                                  unit);
    } else {
        SDL_RenderDebugTextFormat(app->renderer,
                                  value_box.x + 14.0f,
                                  value_box.y + 13.0f,
                                  "value %+.2f %s",
                                  app->waveform_render_value,
                                  unit);
    }
    SDL_SetRenderDrawColor(app->renderer, 178, 190, 204, 255);
    SDL_RenderDebugTextFormat(app->renderer,
                              panel.x + 18.0f,
                              panel.y + 118.0f,
                              "step %.2f   Up/Down adjust   L1/R1 step",
                              step);
    SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 136.0f, "South/Enter renders   East/Escape cancels");
    if (app->waveform_render_error[0]) {
        SDL_SetRenderDrawColor(app->renderer, 255, 150, 132, 255);
        SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 158.0f, app->waveform_render_error);
    }
}

static void render_roster_commit_menu(App *app, int w, int h) {
    if (!app || !app->roster_commit_menu_open) return;
    const int item_count = (int)ROSTER_COMMIT_ITEM_COUNT;
    SDL_FRect menu = {
        64.0f,
        96.0f,
        330.0f,
        78.0f + (float)item_count * 22.0f
    };
    float menu_pad = 18.0f;
    if (menu.x + menu.w > (float)w - menu_pad) menu.x = (float)w - menu_pad - menu.w;
    if (menu.y + menu.h > (float)h - menu_pad) menu.y = (float)h - menu_pad - menu.h;
    if (menu.x < menu_pad) menu.x = menu_pad;
    if (menu.y < menu_pad) menu.y = menu_pad;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect shadow = { menu.x + 10.0f, menu.y + 12.0f, menu.w + 18.0f, menu.h + 18.0f };
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 148);
    SDL_RenderFillRect(app->renderer, &shadow);
    SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 242);
    SDL_RenderFillRect(app->renderer, &menu);
    SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
    SDL_RenderRect(app->renderer, &menu);
    SDL_SetRenderDrawColor(app->renderer, 235, 242, 245, 255);
    SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 10.0f, "SEND ROSTER EDIT");
    SDL_SetRenderDrawColor(app->renderer, 178, 190, 204, 255);
    SDL_RenderDebugTextFormat(app->renderer,
                              menu.x + 12.0f,
                              menu.y + 28.0f,
                              "source: %s",
                              app->waveform_source_name[0] ? app->waveform_source_name : "roster");
    SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 46.0f, "South/Enter confirms");

    float item_y = menu.y + 70.0f;
    for (int i = 0; i < item_count; ++i) {
        RosterCommitMenuItem item = (RosterCommitMenuItem)i;
        SDL_FRect row = { menu.x + 8.0f, item_y - 4.0f, menu.w - 16.0f, 20.0f };
        bool selected = i == app->roster_commit_menu_selected;
        if (selected) {
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 48);
            SDL_RenderFillRect(app->renderer, &row);
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
            SDL_RenderRect(app->renderer, &row);
        }
        SDL_SetRenderDrawColor(app->renderer, selected ? 255 : 220, selected ? 242 : 224, selected ? 190 : 230, 255);
        SDL_RenderDebugText(app->renderer, menu.x + 18.0f, item_y, roster_commit_menu_item_label(item));
        item_y += 22.0f;
    }
}

static void render_filled_circle(SDL_Renderer *renderer, float cx, float cy, int radius) {
    if (!renderer || radius <= 0) return;
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = (int)floor(sqrt((double)(radius * radius - dy * dy)));
        SDL_RenderLine(renderer, cx - (float)dx, cy + (float)dy, cx + (float)dx, cy + (float)dy);
    }
}

static void render_timeline_bounce_indicator(App *app, float x, float y) {
    if (!app || !app->timeline_bounce_active) return;
    Uint64 ticks = SDL_GetTicks();
    bool lit = ((ticks / 240u) % 2u) == 0u;
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 255, 48, 64, lit ? 255 : 96);
    render_filled_circle(app->renderer, x + 8.0f, y + 7.0f, 7);
    SDL_SetRenderDrawColor(app->renderer, 255, 238, 238, 255);
    double progress = 0.0;
    if (app->timeline_bounce_target_frames > 0) {
        progress = (double)app->timeline_bounce_recorded_frames / (double)app->timeline_bounce_target_frames;
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;
    }
    SDL_RenderDebugTextFormat(app->renderer,
                              x + 24.0f,
                              y,
                              "BOUNCE REC %3.0f%%",
                              progress * 100.0);
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
        const char *edit_verb = app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP ?
            timeline_placement_verb_label(app->timeline_edit_placement_mode) :
            timeline_edit_verb_label(app->timeline_edit_mode);
        SDL_RenderDebugTextFormat(app->renderer, 24, 196, "focus: %s / %s %s",
                                  timeline_focus_label(app->timeline_focus_zone),
                                  edit_verb,
                                  timeline_edit_clip_name(app));
    } else {
        SDL_RenderDebugTextFormat(app->renderer, 24, 196, "focus: %s%s%s%s",
                                  timeline_focus_label(app->timeline_focus_zone),
                                  app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT ? " / tape " : "",
                                  app->timeline_focus_zone == TIMELINE_FOCUS_TRANSPORT ?
                                      timeline_tape_control_mode_label(app->timeline_tape_control_mode) : "",
                                  app->timeline_play_range_adjusting ? " / adjusting" : "");
    }
    if (!app->timeline.initialized) {
        SDL_RenderDebugText(app->renderer, 24, 158, "No captured loops yet.");
        SDL_RenderDebugText(app->renderer, 24, 176, "Use L2+R2+South to capture the selected loop into the roster.");
        render_focus_outline(app, transport_rect, TIMELINE_FOCUS_TRANSPORT);
        render_project_menu(app, timeline_x, timeline_y, w, h);
        return;
    }

    int64_t playhead_for_bpm = audio_engine_get_timeline_playhead_tick(&app->audio);
    bool timeline_playing = audio_engine_timeline_is_playing(&app->audio);
    double tape_reference_tick = timeline_playing ?
        (double)playhead_for_bpm : (double)app->timeline.timeline_cursor_tick;
    double cursor_bpm = timeline_effective_bpm_at_tick(&app->timeline, (double)app->timeline.timeline_cursor_tick);
    double playhead_bpm = timeline_effective_bpm_at_tick(&app->timeline, (double)playhead_for_bpm);
    float tape_speed = timeline_effective_tape_speed(&app->timeline);
    SDL_RenderDebugTextFormat(app->renderer, 24, 158, "canon: cursor %.2f play %.2f  tape: %.3fx  hear: %.2f  pitch: %+.2f st",
                              cursor_bpm,
                              playhead_bpm,
                              tape_speed,
                              timeline_audible_bpm_at_tick(&app->timeline, tape_reference_tick),
                              timeline_tape_pitch_semitones(tape_speed));
    SDL_RenderDebugTextFormat(app->renderer, 24, 176, "length: %lld ticks  playhead: %lld  tempo events: %d  roster: %d  %s",
                              (long long)app->timeline.length_ticks,
                              (long long)playhead_for_bpm,
                              timeline_valid_tempo_event_count(&app->timeline),
                              app->roster_clip_count,
                              timeline_playing ? "playing" : "stopped");
    render_timeline_bounce_indicator(app, 24.0f, 114.0f);
    int64_t range_start = 0, range_end = 0;
    timeline_effective_play_range(&app->timeline, &range_start, &range_end);
    const char *cursor_side_label = timeline_seam_side_label(app->timeline.timeline_cursor_seam_side);
    SDL_RenderDebugTextFormat(app->renderer, 24, 214, "cursor: %lld%s%s  range: %lld..%lld  loop: %s  snap: %s",
                              (long long)app->timeline.timeline_cursor_tick,
                              cursor_side_label[0] ? " " : "",
                              cursor_side_label,
                              (long long)range_start,
                              (long long)range_end,
                              app->timeline.play_range_loop_enabled ? "on" : "off",
                              timeline_edit_snap_label(app));
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
        SDL_RenderDebugTextFormat(app->renderer,
                                  lane_index_rect.x + 7.0f,
                                  y + 5.0f,
                                  "%d%c",
                                  lane_index + 1,
                                  lane->type == TIMELINE_LANE_DRUMS ? 'D' : 'A');
    }
    SDL_SetRenderDrawColor(app->renderer, 85, 90, 112, 255);
    SDL_RenderRect(app->renderer, &track_rect);

    int64_t beat_ticks = timeline_snap_ticks(&app->timeline);
    int64_t bar_ticks = (int64_t)app->timeline.ticks_per_beat * app->timeline.timeline_beats_per_bar;
    if (bar_ticks < 1) bar_ticks = beat_ticks;
    int64_t grid_step = timeline_edit_snap_ticks(app);
    while (grid_step > 0 && view_span / (double)grid_step > 160.0) grid_step *= 2;
    int64_t first_grid = (int64_t)floor(view_start / (double)grid_step) * grid_step;
    if (first_grid < 0) first_grid = 0;
    for (int64_t tick = first_grid; grid_step > 0 && (double)tick <= view_end; tick += grid_step) {
        float x = timeline_x_for_tick((double)tick, view_start, view_span, timeline_x, timeline_w);
        bool bar = bar_ticks > 0 && tick % bar_ticks == 0;
        bool beat = beat_ticks > 0 && tick % beat_ticks == 0;
        if (bar) SDL_SetRenderDrawColor(app->renderer, 130, 135, 170, 180);
        else if (beat) SDL_SetRenderDrawColor(app->renderer, 78, 82, 108, 132);
        else SDL_SetRenderDrawColor(app->renderer, 58, 62, 84, 82);
        SDL_RenderLine(app->renderer, x, timeline_y - 58.0f, x, timeline_y + track_h + 8.0f);
        if (bar && x < timeline_x + timeline_w - 28.0f) {
            int64_t bar_index = bar_ticks > 0 ? tick / bar_ticks + 1 : 1;
            SDL_SetRenderDrawColor(app->renderer, 190, 198, 210, 255);
            SDL_RenderDebugTextFormat(app->renderer, x + 4.0f, timeline_y - 52.0f, "bar %lld", (long long)bar_index);
        } else if (beat && view_span / (double)grid_step < 24.0 && x < timeline_x + timeline_w - 18.0f) {
            int64_t beat_index = beat_ticks > 0 ? tick / beat_ticks + 1 : 1;
            SDL_SetRenderDrawColor(app->renderer, 150, 158, 176, 210);
            SDL_RenderDebugTextFormat(app->renderer, x + 4.0f, timeline_y - 38.0f, "%lld", (long long)beat_index);
        }
    }

    int64_t tempo_cursor_tick = timeline_snap_tick_down(app, app->timeline.timeline_cursor_tick);
    int selected_tempo_event = timeline_tempo_event_index_at_tick(&app->timeline, tempo_cursor_tick);
    int tempo_count = timeline_valid_tempo_event_count(&app->timeline);
    for (int i = 0; i < tempo_count; ++i) {
        TimelineTempoEvent event = app->timeline.tempo_events[i];
        float x = timeline_x_for_tick((double)event.tick, view_start, view_span, timeline_x, timeline_w);
        if (x < timeline_x || x > timeline_x + timeline_w) continue;
        bool highlighted = app->timeline_focus_zone == TIMELINE_FOCUS_RULER && i == selected_tempo_event;
        bool ghost_highlighted = app->timeline_edit_mode != TIMELINE_EDIT_NONE &&
                                 app->timeline_edit_ghost_start_tick == event.tick;
        TimelineSeamSide active_side = TIMELINE_SEAM_NONE;
        if (highlighted) active_side = app->timeline.timeline_cursor_seam_side;
        else if (ghost_highlighted) active_side = app->timeline_edit_ghost_seam_side;
        SDL_SetRenderDrawColor(app->renderer,
                               (highlighted || ghost_highlighted) ? 255 : 92,
                               (highlighted || ghost_highlighted) ? 245 : 218,
                               (highlighted || ghost_highlighted) ? 184 : 238,
                               (highlighted || ghost_highlighted) ? 255 : 220);
        SDL_RenderLine(app->renderer, x, timeline_y - 62.0f, x, timeline_y + track_h + 12.0f);
        SDL_FRect flag = { x - 4.0f, timeline_y - 31.0f, 8.0f, 11.0f };
        SDL_RenderFillRect(app->renderer, &flag);
        if (timeline_tick_is_navigation_seam(&app->timeline, event.tick) && active_side != TIMELINE_SEAM_NONE) {
            SDL_FRect side_tab = active_side == TIMELINE_SEAM_BEFORE ?
                (SDL_FRect){ x - 21.0f, timeline_y - 46.0f, 18.0f, 13.0f } :
                (SDL_FRect){ x + 3.0f, timeline_y - 46.0f, 22.0f, 13.0f };
            SDL_SetRenderDrawColor(app->renderer, 255, 250, 215, 84);
            SDL_RenderFillRect(app->renderer, &side_tab);
            SDL_SetRenderDrawColor(app->renderer, 255, 250, 215, 255);
            SDL_RenderRect(app->renderer, &side_tab);
            SDL_RenderDebugText(app->renderer,
                                side_tab.x + 3.0f,
                                side_tab.y + 2.0f,
                                timeline_seam_side_short_label(active_side));
        }
        if (x < timeline_x + timeline_w - 42.0f) {
            SDL_RenderDebugTextFormat(app->renderer, x + 5.0f, timeline_y - 28.0f, "%.1f", event.bpm);
        }
    }

    if (range_end > range_start) {
        float range_x0 = timeline_x_for_tick((double)range_start, view_start, view_span, timeline_x, timeline_w);
        float range_x1 = timeline_x_for_tick((double)range_end, view_start, view_span, timeline_x, timeline_w);
        if (range_x0 < timeline_x) range_x0 = timeline_x;
        if (range_x1 > timeline_x + timeline_w) range_x1 = timeline_x + timeline_w;
        if (range_x1 > range_x0) {
            if (app->timeline.play_range_loop_enabled) {
                SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(app->renderer, 255, 226, 90, 230);
                for (float y = timeline_y - 10.0f; y <= timeline_y - 2.0f; y += 4.0f) {
                    SDL_RenderLine(app->renderer, range_x0, y, range_x1, y);
                }
                SDL_SetRenderDrawColor(app->renderer, 255, 226, 90, 135);
                SDL_RenderLine(app->renderer, range_x0, timeline_y - 13.0f, range_x1, timeline_y - 13.0f);
                SDL_RenderLine(app->renderer, range_x0, timeline_y + 1.0f, range_x1, timeline_y + 1.0f);
            }
        }
        float start_x = timeline_x_for_tick((double)range_start, view_start, view_span, timeline_x, timeline_w);
        float end_x = timeline_x_for_tick((double)range_end, view_start, view_span, timeline_x, timeline_w);
        if (start_x >= timeline_x && start_x <= timeline_x + timeline_w) {
            SDL_FRect tab = { start_x - 5.0f, timeline_y - 20.0f, 10.0f, 18.0f };
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
            SDL_RenderFillRect(app->renderer, &tab);
            bool armed = app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE &&
                         app->timeline_play_range_handle == TIMELINE_RANGE_HANDLE_START;
            if (armed) {
                SDL_FRect pad = { tab.x - 1.0f, tab.y - 1.0f, tab.w + 2.0f, tab.h + 2.0f };
                SDL_SetRenderDrawColor(app->renderer, 255, 250, 215, app->timeline_play_range_adjusting ? 112 : 58);
                SDL_RenderFillRect(app->renderer, &pad);
                SDL_SetRenderDrawColor(app->renderer, 255, 250, 215, 255);
                SDL_RenderRect(app->renderer, &pad);
                SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
                SDL_RenderFillRect(app->renderer, &tab);
            }
        }
        if (end_x >= timeline_x && end_x <= timeline_x + timeline_w) {
            SDL_FRect tab = { end_x - 5.0f, timeline_y - 20.0f, 10.0f, 18.0f };
            SDL_SetRenderDrawColor(app->renderer, 255, 180, 100, 255);
            SDL_RenderFillRect(app->renderer, &tab);
            bool armed = app->timeline_focus_zone == TIMELINE_FOCUS_PLAY_RANGE &&
                         app->timeline_play_range_handle == TIMELINE_RANGE_HANDLE_END;
            if (armed) {
                SDL_FRect pad = { tab.x - 1.0f, tab.y - 1.0f, tab.w + 2.0f, tab.h + 2.0f };
                SDL_SetRenderDrawColor(app->renderer, 255, 235, 205, app->timeline_play_range_adjusting ? 112 : 58);
                SDL_RenderFillRect(app->renderer, &pad);
                SDL_SetRenderDrawColor(app->renderer, 255, 235, 205, 255);
                SDL_RenderRect(app->renderer, &pad);
                SDL_SetRenderDrawColor(app->renderer, 255, 180, 100, 255);
                SDL_RenderFillRect(app->renderer, &tab);
            }
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
            SDL_Color clip_color = (SDL_Color){ 210, 220, 230, 255 };
            const char *instance_name = "instance";
            if (instance->kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
                if (!app_pattern_index_valid(app, instance->pattern_index)) continue;
                DrumPattern *pattern = &app->drum_patterns[instance->pattern_index];
                clip_color = pattern->color;
                instance_name = pattern->name;
            } else {
                if (instance->roster_clip_index < 0 || instance->roster_clip_index >= app->roster_clip_count) continue;
                RosterClip *clip = &app->roster[instance->roster_clip_index];
                clip_color = clip->color;
                instance_name = clip->name;
            }
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
            SDL_snprintf(label, sizeof(label), "%s v%d", instance_name, instance->midi_velocity);
            render_timeline_block(app, block, clip_color, lane_color, label, style, lane->muted);
        }
    }

    if (app->timeline_edit_mode != TIMELINE_EDIT_NONE &&
        app->timeline_edit_duration_ticks > 0) {
        SDL_Color ghost_color = (SDL_Color){ 210, 220, 230, 255 };
        const char *ghost_name = "clip";
        bool ghost_material_valid = false;
        if (app->timeline_edit_instance_kind == TIMELINE_INSTANCE_DRUM_PATTERN) {
            if (app_pattern_index_valid(app, app->timeline_edit_pattern_index)) {
                DrumPattern *pattern = &app->drum_patterns[app->timeline_edit_pattern_index];
                ghost_color = pattern->color;
                ghost_name = pattern->name;
                ghost_material_valid = true;
            }
        } else if (app->timeline_edit_roster_clip_index >= 0 &&
                   app->timeline_edit_roster_clip_index < app->roster_clip_count) {
            RosterClip *clip = &app->roster[app->timeline_edit_roster_clip_index];
            ghost_color = clip->color;
            ghost_name = clip->name;
            ghost_material_valid = true;
        }
        if (ghost_material_valid) {
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
                const char *ghost_verb = app->timeline_edit_mode == TIMELINE_EDIT_PLACE_CLIP ?
                    timeline_placement_verb_label(app->timeline_edit_placement_mode) :
                    "moving";
                SDL_snprintf(ghost_label, sizeof(ghost_label), "%s %s",
                             ghost_verb,
                             ghost_name);
            } else {
                SDL_strlcpy(ghost_label, "overlap", sizeof(ghost_label));
            }
            TimelineLane *ghost_timeline_lane = &app->timeline.lanes[ghost_lane];
            const LanePalette *ghost_palette = lane_palette_for_index(ghost_timeline_lane->palette_index);
            SDL_Color ghost_lane_color = ghost_palette ? ghost_palette->pastel : (SDL_Color){ 180, 188, 205, 255 };
            render_timeline_block(app,
                                  ghost,
                                  ghost_color,
                                  ghost_lane_color,
                                  ghost_label,
                                  app->timeline_edit_ghost_valid ? TIMELINE_BLOCK_GHOST_VALID : TIMELINE_BLOCK_GHOST_INVALID,
                                  ghost_timeline_lane->muted);
        }
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
        if (app_selected_timeline_lane_is_drum(app)) {
            int lane_index = clamp_int(app->selected_timeline_lane, 0, TIMELINE_MAX_LANES - 1);
            const TimelineLane *selected_lane = &app->timeline.lanes[lane_index];
            const char *kit_name = "no kit";
            if (app_kit_index_valid(app, selected_lane->drum_kit_index)) {
                kit_name = app->drum_kits[selected_lane->drum_kit_index].name;
            }
            SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 14.0f, "PATTERNS");
            SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 28.0f, "lane %d kit: %s", lane_index + 1, kit_name);
            SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 44.0f, "EVT");
            SDL_RenderDebugText(app->renderer, roster_panel.x + 60.0f, roster_panel.y + 44.0f, "PATTERN");
            SDL_RenderDebugText(app->renderer, roster_panel.x + roster_panel.w - 54.0f, roster_panel.y + 44.0f, "TICKS");
            int visible = ((int)roster_panel.h - 70) / 18;
            if (visible > app->drum_pattern_count) visible = app->drum_pattern_count;
            for (int i = 0; i < visible; ++i) {
                DrumPattern *pattern = &app->drum_patterns[i];
                float y = roster_panel.y + 64.0f + (float)i * 18.0f;
                if (i == app->selected_drum_pattern) {
                    SDL_FRect row = { roster_panel.x + 10.0f, y - 3.0f, roster_panel.w - 20.0f, 16.0f };
                    if (app->selected_drum_pattern_armed) SDL_SetRenderDrawColor(app->renderer, 86, 78, 38, 230);
                    else SDL_SetRenderDrawColor(app->renderer, 64, 72, 96, 210);
                    SDL_RenderFillRect(app->renderer, &row);
                    if (app->selected_drum_pattern_armed) {
                        SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
                        SDL_RenderRect(app->renderer, &row);
                    }
                }
                SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + 14.0f, y, "%3d", pattern->event_count);
                SDL_FRect swatch = { roster_panel.x + 42.0f, y - 1.0f, 12.0f, 12.0f };
                SDL_SetRenderDrawColor(app->renderer, pattern->color.r, pattern->color.g, pattern->color.b, 255);
                SDL_RenderFillRect(app->renderer, &swatch);
                SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
                SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + 60.0f, y, "%s", pattern->name);
                SDL_RenderDebugTextFormat(app->renderer,
                                          roster_panel.x + roster_panel.w - 54.0f,
                                          y,
                                          "%lld",
                                          (long long)pattern->length_ticks);
            }
            if (app->drum_pattern_count <= 0) {
                SDL_SetRenderDrawColor(app->renderer, 178, 190, 204, 255);
                SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 72.0f, "No patterns yet.");
            }
        } else {
            int visible = ((int)roster_panel.h - 52) / 18;
            if (visible < 1) visible = 1;
            app_clamp_roster_scroll(app, visible);

            SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 14.0f, "ROSTER");
            if (app->roster_clip_count > 0) {
                SDL_RenderDebugTextFormat(app->renderer,
                                          roster_panel.x + roster_panel.w - 78.0f,
                                          roster_panel.y + 14.0f,
                                          "%d/%d",
                                          app->selected_roster_clip + 1,
                                          app->roster_clip_count);
            }
            SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, roster_panel.y + 28.0f, "BPM");
            SDL_RenderDebugText(app->renderer, roster_panel.x + 92.0f, roster_panel.y + 28.0f, "CLIP");
            SDL_RenderDebugText(app->renderer, roster_panel.x + roster_panel.w - 54.0f, roster_panel.y + 28.0f, "BEATS");
            int first = app->roster_scroll_offset;
            int last = first + visible;
            if (last > app->roster_clip_count) last = app->roster_clip_count;
            for (int i = first; i < last; ++i) {
                RosterClip *clip = &app->roster[i];
                float y = roster_panel.y + 48.0f + (float)(i - first) * 18.0f;
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
                if (clip->tempo_calibrated && clip->source_bpm > 0.0) {
                    SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + 14.0f, y, "%6.1f", clip->source_bpm);
                } else {
                    SDL_RenderDebugText(app->renderer, roster_panel.x + 14.0f, y, "   raw");
                }
                SDL_FRect swatch = { roster_panel.x + 72.0f, y - 1.0f, 12.0f, 12.0f };
                SDL_SetRenderDrawColor(app->renderer, clip->color.r, clip->color.g, clip->color.b, 255);
                SDL_RenderFillRect(app->renderer, &swatch);
                SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
                SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + 92.0f, y, "%s", clip->name);
                if (clip->tempo_calibrated && clip->target_beats > 0.0) {
                    SDL_RenderDebugTextFormat(app->renderer, roster_panel.x + roster_panel.w - 54.0f, y, "%.1fb", clip->target_beats);
                } else {
                    SDL_RenderDebugText(app->renderer, roster_panel.x + roster_panel.w - 54.0f, y, "--");
                }
            }
        }
        render_focus_outline(app, roster_panel, TIMELINE_FOCUS_ROSTER);
    }

    if (app->timeline_context_menu_open) {
        TimelineContextMenuItem items[TIMELINE_CONTEXT_MAX_ITEMS];
        int item_count = timeline_context_menu_items(app, items, TIMELINE_CONTEXT_MAX_ITEMS);
        const char *title = timeline_context_menu_title(app->timeline_context_menu_scope);
        const char *name = "timeline";
        char lane_name[32];
        if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_TIMELINE &&
            app->timeline_focus_zone == TIMELINE_FOCUS_RULER) {
            title = "RULER MENU";
            name = "tempo/grid";
        } else if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_TIMELINE &&
                   app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) {
            title = "LANE MENU";
            SDL_snprintf(lane_name, sizeof(lane_name), "lane %d", app->selected_timeline_lane + 1);
            name = lane_name;
        }
        if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_ROSTER ||
            app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE) {
            int roster_index = app->timeline_context_menu_roster_index;
            if (roster_index >= 0 && roster_index < app->roster_clip_count) name = app->roster[roster_index].name;
        } else if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_PATTERN ||
                   app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE) {
            int pattern_index = app->timeline_context_menu_pattern_index;
            if (app_pattern_index_valid(app, pattern_index)) name = app->drum_patterns[pattern_index].name;
            else name = "pattern roster";
        } else if (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_INSTANCE &&
                   timeline_instance_ref_valid(&app->timeline, app->timeline_context_menu_instance)) {
            const TimelineInstance *selected = timeline_const_instance_from_ref(&app->timeline,
                                                                                app->timeline_context_menu_instance);
            if (selected && selected->kind == TIMELINE_INSTANCE_DRUM_PATTERN &&
                app_pattern_index_valid(app, selected->pattern_index)) {
                name = app->drum_patterns[selected->pattern_index].name;
            } else if (selected && selected->roster_clip_index >= 0 && selected->roster_clip_index < app->roster_clip_count) {
                name = app->roster[selected->roster_clip_index].name;
            } else {
                name = "instance";
            }
        }
        bool confirm_delete_menu =
            app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE ||
            app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE;
        float menu_w = confirm_delete_menu ? 384.0f : 320.0f;
        float warning_h = confirm_delete_menu ? 34.0f : 0.0f;
        SDL_FRect menu = {
            (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_ROSTER ||
             app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_PATTERN ||
             app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE ||
             app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE) ? roster_x + 6.0f :
                (app->timeline_context_menu_scope == TIMELINE_CONTEXT_SCOPE_TIMELINE &&
                 app->timeline_focus_zone == TIMELINE_FOCUS_LANE_INDEX) ? lane_index_rect.x + 6.0f : timeline_x + 18.0f,
            timeline_y + track_h + 42.0f,
            menu_w,
            58.0f + warning_h + (float)item_count * 22.0f
        };
        float menu_pad = 18.0f;
        if (menu.x + menu.w > (float)w - menu_pad) menu.x = (float)w - menu_pad - menu.w;
        if (menu.y + menu.h > (float)h - menu_pad) menu.y = (float)h - menu_pad - menu.h;
        if (menu.x < menu_pad) menu.x = menu_pad;
        if (menu.y < menu_pad) menu.y = menu_pad;
        if (app->debug_overlay_mode != APP_DEBUG_OVERLAY_OFF) {
            SDL_FRect pad_rect = { 8.0f, (float)h - 104.0f, 224.0f, 96.0f };
            bool overlaps_pad = menu.x < pad_rect.x + pad_rect.w + menu_pad &&
                                menu.x + menu.w > pad_rect.x - menu_pad &&
                                menu.y < pad_rect.y + pad_rect.h + menu_pad &&
                                menu.y + menu.h > pad_rect.y - menu_pad;
            if (overlaps_pad) {
                float right_of_pad = pad_rect.x + pad_rect.w + menu_pad;
                float above_pad = pad_rect.y - menu_pad - menu.h;
                if (right_of_pad + menu.w <= (float)w - menu_pad) {
                    menu.x = right_of_pad;
                } else if (above_pad >= menu_pad) {
                    menu.y = above_pad;
                }
            }
        }
        SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
        SDL_FRect shadow = { menu.x + 12.0f, menu.y + 14.0f, menu.w + 20.0f, menu.h + 20.0f };
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 146);
        SDL_RenderFillRect(app->renderer, &shadow);
        SDL_FRect soft_shadow = { menu.x + 4.0f, menu.y + 6.0f, menu.w + 12.0f, menu.h + 12.0f };
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 96);
        SDL_RenderFillRect(app->renderer, &soft_shadow);
        SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 238);
        SDL_RenderFillRect(app->renderer, &menu);
        SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
        SDL_RenderRect(app->renderer, &menu);
        SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);
        SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 10.0f, title);
        SDL_RenderDebugText(app->renderer, menu.x + 12.0f, menu.y + 28.0f, name);
        float item_y = menu.y + 50.0f;
        if (confirm_delete_menu) {
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

    render_project_menu(app, timeline_x, timeline_y, w, h);
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

static void app_render_drum_machine(App *app) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_SetRenderDrawColor(app->renderer, 8, 7, 13, 255);
    SDL_RenderClear(app->renderer);

    if (!app_pattern_index_valid(app, app->selected_drum_pattern)) return;
    DrumKit *kit = app_selected_lane_drum_kit(app);
    DrumPattern *pattern = &app->drum_patterns[app->selected_drum_pattern];
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    SDL_RenderDebugText(app->renderer, 32.0f, 32.0f, "DRUM MACHINE");
    SDL_RenderDebugTextFormat(app->renderer,
                              32.0f,
                              52.0f,
                              "%s  events:%d  length:%lld",
                              pattern->name,
                              pattern->event_count,
                              (long long)pattern->length_ticks);
    SDL_RenderDebugTextFormat(app->renderer,
                              32.0f,
                              72.0f,
                              "lane %d kit: %s",
                              app->selected_timeline_lane + 1,
                              kit ? kit->name : "none");

    if (!kit || kit->pad_count <= 0) {
        SDL_RenderDebugText(app->renderer, 32.0f, 112.0f, "No kit pads loaded.");
        return;
    }

    int rows = kit->pad_count;
    if (rows > 12) rows = 12;
    float label_w = 116.0f;
    float grid_x = 32.0f + label_w;
    float grid_y = 120.0f;
    float grid_w = (float)w - grid_x - 42.0f;
    if (grid_w < 320.0f) grid_w = 320.0f;
    float cell_gap = 4.0f;
    float cell_w = (grid_w - cell_gap * 15.0f) / 16.0f;
    if (cell_w < 14.0f) cell_w = 14.0f;
    if (cell_w > 44.0f) cell_w = 44.0f;
    float cell_h = 28.0f;
    float row_gap = 7.0f;
    int64_t step_ticks = pattern->length_ticks / 16;
    if (step_ticks <= 0) step_ticks = 1;

    for (int step = 0; step < 16; ++step) {
        float x = grid_x + (float)step * (cell_w + cell_gap);
        SDL_SetRenderDrawColor(app->renderer, step % 4 == 0 ? 240 : 158, step % 4 == 0 ? 220 : 168, step % 4 == 0 ? 145 : 188, 230);
        SDL_RenderDebugTextFormat(app->renderer, x + 2.0f, grid_y - 18.0f, "%02d", step + 1);
    }

    for (int row = 0; row < rows; ++row) {
        DrumPad *pad = &kit->pads[row];
        float y = grid_y + (float)row * (cell_h + row_gap);
        SDL_SetRenderDrawColor(app->renderer, 210, 220, 230, 245);
        SDL_RenderDebugTextFormat(app->renderer, 32.0f, y + 8.0f, "%03d %s", pad->note, pad->name);
        for (int step = 0; step < 16; ++step) {
            int64_t tick = (int64_t)step * step_ticks;
            int event_index = drum_pattern_event_index(pattern, tick, pad->note);
            bool active = event_index >= 0;
            bool selected = row == app->drum_machine_pad && step == app->drum_machine_step;
            float x = grid_x + (float)step * (cell_w + cell_gap);
            SDL_FRect cell = { x, y, cell_w, cell_h };
            if (active) {
                int velocity = pattern->events[event_index].velocity;
                Uint8 bright = (Uint8)clamp_int(90 + velocity, 100, 217);
                SDL_SetRenderDrawColor(app->renderer, 255, bright, 82, 235);
            } else {
                SDL_SetRenderDrawColor(app->renderer, step % 4 == 0 ? 46 : 32, 36, step % 4 == 0 ? 54 : 44, 235);
            }
            SDL_RenderFillRect(app->renderer, &cell);
            SDL_SetRenderDrawColor(app->renderer, selected ? 255 : 82, selected ? 248 : 92, selected ? 214 : 112, selected ? 255 : 190);
            SDL_RenderRect(app->renderer, &cell);
        }
    }

    SDL_SetRenderDrawColor(app->renderer, 176, 188, 204, 230);
    float help_y = grid_y + (float)rows * (cell_h + row_gap) + 24.0f;
    if (help_y < (float)h - 48.0f) {
        SDL_RenderDebugText(app->renderer, 32.0f, help_y, "Arrows move   Enter/Space toggle   [ ] velocity   Esc timeline");
    }
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

    SDL_SetRenderDrawColor(app->renderer, 222, 230, 234, 220);
    SDL_RenderDebugTextFormat(app->renderer,
                              settings.x + 20.0f,
                              mute_button.y + 112.0f,
                              "TYPE %s",
                              timeline_lane_type_label(lane->type));
    if (lane->type == TIMELINE_LANE_DRUMS) {
        const char *kit_name = "none";
        if (app_kit_index_valid(app, lane->drum_kit_index)) kit_name = app->drum_kits[lane->drum_kit_index].name;
        SDL_RenderDebugTextFormat(app->renderer,
                                  settings.x + 20.0f,
                                  mute_button.y + 132.0f,
                                  "KIT %s",
                                  kit_name);
        SDL_RenderDebugTextFormat(app->renderer,
                                  settings.x + 20.0f,
                                  mute_button.y + 152.0f,
                                  "MIDI CH %d",
                                  lane->midi_channel + 1);
    }

    SDL_SetRenderDrawColor(app->renderer, 190, 198, 210, 205);
    SDL_RenderDebugText(app->renderer, settings.x + 20.0f, settings.y + settings.h - 64.0f, "Left/Right palette");
    SDL_RenderDebugText(app->renderer, settings.x + 20.0f, settings.y + settings.h - 44.0f, "T type   K kit");
    SDL_RenderDebugText(app->renderer, settings.x + 20.0f, settings.y + settings.h - 24.0f, "N new pattern   D editor");

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

static double app_amp_to_db(float amp) {
    if (amp <= 0.000032f) return -90.0;
    return 20.0 * log10((double)amp);
}

static void render_master_mix_focus_outline(App *app, SDL_FRect rect, MasterMixFocusSection section) {
    if (app->master_mix_focus != section) return;
    SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
    SDL_RenderRect(app->renderer, &rect);
    SDL_FRect inner = { rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f };
    if (inner.w > 0.0f && inner.h > 0.0f) SDL_RenderRect(app->renderer, &inner);
}

static void render_master_mix_meter_bar(App *app,
                                        SDL_FRect rect,
                                        float value,
                                        SDL_Color fill,
                                        bool clip_active) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.25f) value = 1.25f;
    float scaled = value / 1.25f;
    SDL_SetRenderDrawColor(app->renderer, 25, 27, 36, 255);
    SDL_RenderFillRect(app->renderer, &rect);

    SDL_FRect safe = rect;
    safe.w *= 1.0f / 1.25f;
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 18);
    SDL_RenderFillRect(app->renderer, &safe);

    SDL_FRect body = rect;
    body.w *= scaled;
    set_draw_color(app->renderer, clip_active ? (SDL_Color){ 255, 70, 78, 255 } : fill);
    SDL_RenderFillRect(app->renderer, &body);

    float unity_x = rect.x + rect.w * (1.0f / 1.25f);
    SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 180);
    SDL_RenderLine(app->renderer, unity_x, rect.y - 2.0f, unity_x, rect.y + rect.h + 2.0f);
}

static void render_master_mix_section(App *app,
                                      SDL_FRect rect,
                                      MasterMixFocusSection section,
                                      SDL_Color accent,
                                      const char *title) {
    SDL_Color bg = color_mix(accent, (SDL_Color){ 10, 11, 17, 255 }, 0.84f);
    bg.a = 226;
    set_draw_color(app->renderer, bg);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_Color border = color_mix(accent, (SDL_Color){ 190, 198, 210, 255 }, 0.28f);
    border.a = 220;
    set_draw_color(app->renderer, border);
    SDL_RenderRect(app->renderer, &rect);
    set_draw_color(app->renderer, accent);
    SDL_RenderDebugText(app->renderer, rect.x + 16.0f, rect.y + 14.0f, title);
    render_master_mix_focus_outline(app, rect, section);
}

static void format_master_reverb_value(const MasterReverbParams *params,
                                       MasterReverbParamId param,
                                       char *out,
                                       size_t out_size) {
    if (!out || out_size == 0) return;
    float value = app_master_reverb_param_value(params, param);
    switch (param) {
        case MASTER_REVERB_PARAM_ENABLED:
            SDL_strlcpy(out, params && params->enabled ? "ON" : "BYPASS", out_size);
            break;
        case MASTER_REVERB_PARAM_PREDELAY_MS:
            SDL_snprintf(out, out_size, "%.0f ms", value);
            break;
        case MASTER_REVERB_PARAM_DECAY_SECONDS:
            SDL_snprintf(out, out_size, "%.2f s", value);
            break;
        case MASTER_REVERB_PARAM_LOW_CUT_HZ:
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ:
            SDL_snprintf(out, out_size, "%.0f Hz", value);
            break;
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS:
            SDL_snprintf(out, out_size, "%.1f ms", value);
            break;
        case MASTER_REVERB_PARAM_MOD_RATE_HZ:
            SDL_snprintf(out, out_size, "%.2f Hz", value);
            break;
        case MASTER_REVERB_PARAM_SEND:
        case MASTER_REVERB_PARAM_RETURN:
        case MASTER_REVERB_PARAM_SIZE:
        case MASTER_REVERB_PARAM_DIFFUSION:
        case MASTER_REVERB_PARAM_DAMPING:
        case MASTER_REVERB_PARAM_WIDTH:
            SDL_snprintf(out, out_size, "%.2f", value);
            break;
        case MASTER_REVERB_PARAM_COUNT:
        default:
            SDL_strlcpy(out, "--", out_size);
            break;
    }
}

static void app_render_master_mix(App *app) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_SetRenderDrawColor(app->renderer, 8, 7, 13, 255);
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    SDL_Color master_accent = { 130, 238, 234, 255 };
    SDL_Color reverb_accent = { 196, 166, 255, 255 };
    SDL_Color fx_accent = { 255, 216, 132, 255 };
    SDL_Color midi_accent = { 156, 236, 160, 255 };

    float margin = 42.0f;
    float top = 96.0f;
    float gap = 18.0f;
    float content_w = (float)w - margin * 2.0f;
    if (content_w < 520.0f) content_w = 520.0f;
    float content_h = (float)h - top - 64.0f;
    if (content_h < 440.0f) content_h = 440.0f;

    set_draw_color(app->renderer, master_accent);
    render_debug_text_scaled(app->renderer, margin, 32.0f, 2.6f, "MASTER MIX");
    SDL_SetRenderDrawColor(app->renderer, 190, 198, 210, 205);
    SDL_RenderDebugText(app->renderer, margin, 68.0f, "Focused master bus home. Reverb 1 is a built-in master FX unit.");

    float master_h = content_h * 0.43f;
    if (master_h < 182.0f) master_h = 182.0f;
    if (master_h > 250.0f) master_h = 250.0f;
    SDL_FRect master = { margin, top, content_w, master_h };
    float lower_y = master.y + master.h + gap;
    float lower_h = content_h - master.h - gap;
    float reverb_w = content_w * 0.63f;
    if (reverb_w < 380.0f) reverb_w = 380.0f;
    if (reverb_w > content_w - 230.0f) reverb_w = content_w - 230.0f;
    float right_w = content_w - reverb_w - gap;
    SDL_FRect reverb = { margin, lower_y, reverb_w, lower_h };
    SDL_FRect fx = { margin + reverb_w + gap, lower_y, right_w, lower_h * 0.50f - gap * 0.5f };
    SDL_FRect midi = { fx.x, fx.y + fx.h + gap, right_w, lower_h - fx.h - gap };
    if (fx.h < 112.0f) fx.h = 112.0f;
    if (midi.h < 86.0f) midi.h = 86.0f;

    render_master_mix_section(app, master, MASTER_MIX_FOCUS_MASTER, master_accent, "MASTER");
    MasterMeterState meter;
    SDL_memset(&meter, 0, sizeof(meter));
    audio_engine_get_master_meter(&app->audio, &meter);
    bool clip_active = meter.clip_flash_seconds > 0.0f;
    float peak = fmaxf(fabsf(meter.peak_l), fabsf(meter.peak_r));
    double peak_db = app_amp_to_db(peak);
    SDL_SetRenderDrawColor(app->renderer, 232, 240, 244, 255);
    SDL_RenderDebugTextFormat(app->renderer, master.x + 22.0f, master.y + 46.0f,
                              "LEVEL %.2f  read-only", app->audio.master_gain);
    if (peak_db <= -89.9) {
        SDL_RenderDebugText(app->renderer, master.x + 22.0f, master.y + 66.0f, "PEAK <-90 dB");
    } else {
        SDL_RenderDebugTextFormat(app->renderer, master.x + 22.0f, master.y + 66.0f,
                                  "PEAK %+4.1f dB", peak_db);
    }
    SDL_SetRenderDrawColor(app->renderer,
                           clip_active ? 255 : 178,
                           clip_active ? 90 : 184,
                           clip_active ? 100 : 194,
                           235);
    SDL_RenderDebugTextFormat(app->renderer, master.x + 22.0f, master.y + 86.0f,
                              clip_active ? "CLIP HOLD  count %u" : "CLIP CLEAR  count %u",
                              meter.clip_count);
    SDL_RenderDebugTextFormat(app->renderer, master.x + 22.0f, master.y + 106.0f,
                              "LIMIT %.2f  ceiling %.2f",
                              meter.limiter_gain > 0.0f ? meter.limiter_gain : 1.0f,
                              MASTER_LIMITER_CEILING);

    float meter_x = master.x + 184.0f;
    float meter_w = master.w - 224.0f;
    if (meter_w < 220.0f) meter_w = 220.0f;
    SDL_FRect left_bar = { meter_x, master.y + 50.0f, meter_w, 18.0f };
    SDL_FRect right_bar = { meter_x, master.y + 82.0f, meter_w, 18.0f };
    SDL_SetRenderDrawColor(app->renderer, 210, 218, 226, 230);
    SDL_RenderDebugText(app->renderer, meter_x - 24.0f, left_bar.y + 3.0f, "L");
    SDL_RenderDebugText(app->renderer, meter_x - 24.0f, right_bar.y + 3.0f, "R");
    render_master_mix_meter_bar(app, left_bar, fabsf(meter.peak_l), master_accent, clip_active);
    render_master_mix_meter_bar(app, right_bar, fabsf(meter.peak_r), master_accent, clip_active);

    SDL_FRect gain_slot = { master.x + 22.0f, master.y + master.h - 66.0f, master.w - 44.0f, 36.0f };
    SDL_SetRenderDrawColor(app->renderer, 14, 16, 22, 185);
    SDL_RenderFillRect(app->renderer, &gain_slot);
    SDL_SetRenderDrawColor(app->renderer, 82, 92, 108, 230);
    SDL_RenderRect(app->renderer, &gain_slot);
    SDL_SetRenderDrawColor(app->renderer, 208, 216, 226, 235);
    SDL_RenderDebugText(app->renderer, gain_slot.x + 12.0f, gain_slot.y + 12.0f,
                        "Master level is fixed. Reverb 1 adds wet return after this dry signal.");

    render_master_mix_section(app, reverb, MASTER_MIX_FOCUS_REVERB, reverb_accent, "REVERB");
    MasterReverbParams reverb_target;
    audio_engine_get_master_reverb_params(&app->audio, NULL, &reverb_target);
    SDL_SetRenderDrawColor(app->renderer, 226, 232, 238, 235);
    SDL_RenderDebugText(app->renderer, reverb.x + 18.0f, reverb.y + 44.0f, "Reverb 1");
    SDL_SetRenderDrawColor(app->renderer, 176, 184, 198, 225);
    SDL_RenderDebugText(app->renderer, reverb.x + 96.0f, reverb.y + 44.0f,
                        reverb_target.enabled ? "active" : "bypassed");
    float row_y = reverb.y + 68.0f;
    for (int i = 0; i < (int)MASTER_REVERB_PARAM_COUNT; ++i) {
        MasterReverbParamId param = (MasterReverbParamId)i;
        bool selected = app->master_mix_focus == MASTER_MIX_FOCUS_REVERB &&
                        app->master_reverb_selected_param == param;
        SDL_FRect row = { reverb.x + 12.0f, row_y - 4.0f, reverb.w - 24.0f, 17.0f };
        if (selected) {
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 52);
            SDL_RenderFillRect(app->renderer, &row);
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 230);
            SDL_RenderRect(app->renderer, &row);
        }
        char value[32];
        format_master_reverb_value(&reverb_target, param, value, sizeof(value));
        SDL_SetRenderDrawColor(app->renderer, selected ? 255 : 218, selected ? 238 : 224, selected ? 178 : 232, 255);
        SDL_RenderDebugTextFormat(app->renderer, reverb.x + 18.0f, row_y, "%s", audio_engine_master_reverb_param_label(param));
        SDL_RenderDebugTextFormat(app->renderer, reverb.x + reverb.w - 96.0f, row_y, "%s", value);
        row_y += 18.0f;
    }
    SDL_SetRenderDrawColor(app->renderer, 176, 184, 198, 225);
    SDL_RenderDebugText(app->renderer, reverb.x + 18.0f, reverb.y + reverb.h - 26.0f,
                        "Left/Right adjust   Enter/South enable   R/LS clears tail");

    render_master_mix_section(app, fx, MASTER_MIX_FOCUS_FX_CHAIN, fx_accent, "FX CHAIN");
    MasterFxChain chain;
    audio_engine_get_master_fx_chain(&app->audio, &chain);
    int chain_count = chain.unit_count;
    if (chain_count < 0) chain_count = 0;
    if (chain_count > MASTER_FX_CHAIN_MAX_UNITS) chain_count = MASTER_FX_CHAIN_MAX_UNITS;
    SDL_SetRenderDrawColor(app->renderer, 226, 232, 238, 235);
    SDL_RenderDebugTextFormat(app->renderer, fx.x + 18.0f, fx.y + 48.0f,
                              "Built-in chain ready   units %d/%d",
                              chain_count,
                              MASTER_FX_CHAIN_MAX_UNITS);
    SDL_SetRenderDrawColor(app->renderer, 176, 184, 198, 225);
    float slot_y = fx.y + 74.0f;
    int visible_slots = chain_count > 0 ? chain_count : 1;
    for (int i = 0; i < visible_slots && i < MASTER_FX_CHAIN_MAX_UNITS; ++i) {
        const MasterFxUnit *unit = i < chain_count ? &chain.units[i] : NULL;
        const char *label = unit ? audio_engine_master_fx_unit_label(unit->type) : "Empty";
        const char *state = unit ? (unit->enabled && !unit->bypassed ? "active" : "bypassed") : "empty";
        SDL_RenderDebugTextFormat(app->renderer, fx.x + 18.0f, slot_y, "Slot %d: %s  %s", i + 1, label, state);
        slot_y += 18.0f;
    }
    SDL_RenderDebugText(app->renderer, fx.x + 18.0f, slot_y + 4.0f, "Final limiter is transparent until peaks cross its ceiling.");

    render_master_mix_section(app, midi, MASTER_MIX_FOCUS_MIDI_CONTROL, midi_accent, "MIDI / CONTROL");
    SDL_SetRenderDrawColor(app->renderer, 226, 232, 238, 235);
    SDL_RenderDebugText(app->renderer, midi.x + 18.0f, midi.y + 48.0f, "Future mapping surface");
    SDL_SetRenderDrawColor(app->renderer, 176, 184, 198, 225);
    SDL_RenderDebugText(app->renderer, midi.x + 18.0f, midi.y + 70.0f,
                        "Stable parameters will land here after real master controls exist.");

    SDL_SetRenderDrawColor(app->renderer, 190, 198, 210, 205);
    SDL_RenderDebugTextFormat(app->renderer, margin, (float)h - 30.0f,
                              "focus: %s   F2/R2+Start next view   Tab/Up/Down focus",
                              master_mix_focus_label(app->master_mix_focus));

    render_project_menu_legend_hint(app);
    if (app->controls_legend_open) app_render_controls_legend(app);
}

static double app_gamepad_axis_value(SDL_Gamepad *gamepad, SDL_GamepadAxis axis) {
    if (!gamepad) return 0.0;
    double v = (double)SDL_GetGamepadAxis(gamepad, axis);
    if (fabs(v) < 8000.0) return 0.0;
    return v / (v < 0.0 ? 32768.0 : 32767.0);
}

static void render_debug_button(App *app,
                                SDL_FRect rect,
                                const char *label,
                                bool active,
                                SDL_Color dim,
                                SDL_Color hot,
                                SDL_Color text) {
    set_draw_color(app->renderer, active ? hot : dim);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_Color border = active ? color_mix(hot, (SDL_Color){ 220, 255, 250, 255 }, 0.25f) : color_mix(dim, text, 0.35f);
    set_draw_color(app->renderer, border);
    SDL_RenderRect(app->renderer, &rect);
    set_draw_color(app->renderer, active ? (SDL_Color){ 10, 24, 26, 255 } : text);
    SDL_RenderDebugText(app->renderer, rect.x + 4.0f, rect.y + 3.0f, label);
}

static void render_debug_axis(App *app,
                              SDL_FRect rect,
                              double x,
                              double y,
                              SDL_Color dim,
                              SDL_Color hot) {
    set_draw_color(app->renderer, dim);
    SDL_RenderRect(app->renderer, &rect);
    float cx = rect.x + rect.w * 0.5f;
    float cy = rect.y + rect.h * 0.5f;
    SDL_RenderLine(app->renderer, rect.x + rect.w * 0.5f, rect.y + 3.0f, rect.x + rect.w * 0.5f, rect.y + rect.h - 3.0f);
    SDL_RenderLine(app->renderer, rect.x + 3.0f, rect.y + rect.h * 0.5f, rect.x + rect.w - 3.0f, rect.y + rect.h * 0.5f);
    float px = cx + (float)x * (rect.w * 0.36f);
    float py = cy + (float)y * (rect.h * 0.36f);
    SDL_FRect puck = { px - 3.0f, py - 3.0f, 6.0f, 6.0f };
    set_draw_color(app->renderer, (fabs(x) > 0.01 || fabs(y) > 0.01) ? hot : dim);
    SDL_RenderFillRect(app->renderer, &puck);
}

static void render_debug_gamepad(App *app, SDL_FRect panel, SDL_Color text, SDL_Color dim, SDL_Color hot) {
    set_draw_color(app->renderer, text);
    SDL_RenderDebugText(app->renderer, panel.x + 8.0f, panel.y + 6.0f, app->gamepad ? "PAD" : "PAD: disconnected");
    if (!app->gamepad) return;

    SDL_Gamepad *pad = app->gamepad;
    float y = panel.y + 22.0f;
    render_debug_button(app, (SDL_FRect){ panel.x + 8.0f, y, 24.0f, 15.0f }, "L1", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ panel.x + 34.0f, y, 28.0f, 15.0f }, "L2", SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000, dim, hot, text);
    render_debug_button(app, (SDL_FRect){ panel.x + panel.w - 62.0f, y, 28.0f, 15.0f }, "R2", SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000, dim, hot, text);
    render_debug_button(app, (SDL_FRect){ panel.x + panel.w - 32.0f, y, 24.0f, 15.0f }, "R1", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), dim, hot, text);

    y += 22.0f;
    render_debug_axis(app,
                      (SDL_FRect){ panel.x + 12.0f, y, 28.0f, 28.0f },
                      app_gamepad_axis_value(pad, SDL_GAMEPAD_AXIS_LEFTX),
                      app_gamepad_axis_value(pad, SDL_GAMEPAD_AXIS_LEFTY),
                      dim,
                      hot);
    render_debug_axis(app,
                      (SDL_FRect){ panel.x + panel.w - 40.0f, y, 28.0f, 28.0f },
                      app_gamepad_axis_value(pad, SDL_GAMEPAD_AXIS_RIGHTX),
                      app_gamepad_axis_value(pad, SDL_GAMEPAD_AXIS_RIGHTY),
                      dim,
                      hot);

    float dpad_x = panel.x + 54.0f;
    render_debug_button(app, (SDL_FRect){ dpad_x + 15.0f, y, 15.0f, 15.0f }, "^", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ dpad_x, y + 16.0f, 15.0f, 15.0f }, "<", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ dpad_x + 15.0f, y + 16.0f, 15.0f, 15.0f }, "v", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ dpad_x + 30.0f, y + 16.0f, 15.0f, 15.0f }, ">", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT), dim, hot, text);

    float face_x = panel.x + panel.w - 92.0f;
    render_debug_button(app, (SDL_FRect){ face_x + 18.0f, y, 18.0f, 15.0f }, "Y", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ face_x, y + 16.0f, 18.0f, 15.0f }, "X", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ face_x + 18.0f, y + 16.0f, 18.0f, 15.0f }, "A", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ face_x + 36.0f, y + 16.0f, 18.0f, 15.0f }, "B", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST), dim, hot, text);

    y += 36.0f;
    render_debug_button(app, (SDL_FRect){ panel.x + 64.0f, y, 32.0f, 15.0f }, "BACK", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ panel.x + 100.0f, y, 36.0f, 15.0f }, "START", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ panel.x + 10.0f, y, 24.0f, 15.0f }, "LS", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK), dim, hot, text);
    render_debug_button(app, (SDL_FRect){ panel.x + panel.w - 34.0f, y, 24.0f, 15.0f }, "RS", SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK), dim, hot, text);
}

static void app_render_debug_overlay(App *app) {
    if (!app || app->debug_overlay_mode == APP_DEBUG_OVERLAY_OFF) return;

    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_Color stats_bg = { 4, 12, 14, 188 };
    SDL_Color pad_bg = { 3, 10, 12, 214 };
    SDL_Color panel_border = { 34, 76, 82, 174 };
    SDL_Color text = { 76, 122, 128, 255 };
    SDL_Color dim = { 24, 58, 64, 226 };
    SDL_Color hot = { 72, 238, 226, 255 };
    const float pad = 8.0f;
    bool show_stats = app->debug_overlay_mode == APP_DEBUG_OVERLAY_GAMEPAD_STATS;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    if (show_stats) {
        const float stats_w = 260.0f;
        const float stats_h = 100.0f;
        SDL_FRect stats_panel = { (float)w - stats_w - pad, pad, stats_w, stats_h };
        if (stats_panel.x < pad) stats_panel.x = pad;
        set_draw_color(app->renderer, stats_bg);
        SDL_RenderFillRect(app->renderer, &stats_panel);
        set_draw_color(app->renderer, panel_border);
        SDL_RenderRect(app->renderer, &stats_panel);

        AudioDebugStats audio_stats;
        MasterMeterState meter;
        audio_engine_get_debug_stats(&app->audio, &audio_stats);
        audio_engine_get_master_meter(&app->audio, &meter);
        float peak = fmaxf(meter.peak_l, meter.peak_r);
        double peak_db = app_amp_to_db(peak);
        double audio_load_percent = audio_stats.audio_load * 100.0;
        if (audio_load_percent < 0.0) audio_load_percent = 0.0;
        set_draw_color(app->renderer, text);
        float y = stats_panel.y + 8.0f;
        SDL_RenderDebugText(app->renderer, stats_panel.x + 8.0f, y, "PERF"); y += 11.0f;
        SDL_RenderDebugTextFormat(app->renderer, stats_panel.x + 8.0f, y, "FPS %5.1f  frame %4.1f/%4.1fms", app->debug_fps, app->debug_frame_ms_avg, app->debug_frame_ms_max); y += 11.0f;
        SDL_RenderDebugTextFormat(app->renderer, stats_panel.x + 8.0f, y, "audio %4.2f/%4.2fms", audio_stats.callback_ms_avg, audio_stats.callback_ms_max); y += 11.0f;
        SDL_RenderDebugTextFormat(app->renderer, stats_panel.x + 8.0f, y, "budget %4.2fms  load %4.1f%%", audio_stats.audio_budget_ms, audio_load_percent); y += 11.0f;
        SDL_RenderDebugTextFormat(app->renderer, stats_panel.x + 8.0f, y, "rate %d  buf %d", audio_stats.sample_rate, audio_stats.buffer_frames); y += 11.0f;
        SDL_RenderDebugTextFormat(app->renderer, stats_panel.x + 8.0f, y, "over %u  clips %d", audio_stats.over_budget_count, audio_stats.active_clips); y += 11.0f;
        if (peak_db <= -89.9) SDL_RenderDebugText(app->renderer, stats_panel.x + 8.0f, y, "peak <-90 dB");
        else SDL_RenderDebugTextFormat(app->renderer, stats_panel.x + 8.0f, y, "peak %+4.1f dB", peak_db);
    }

    SDL_FRect gamepad_panel = { pad, (float)h - 96.0f - pad, 224.0f, 96.0f };
    if (gamepad_panel.y < pad) gamepad_panel.y = pad;
    set_draw_color(app->renderer, pad_bg);
    SDL_RenderFillRect(app->renderer, &gamepad_panel);
    set_draw_color(app->renderer, panel_border);
    SDL_RenderRect(app->renderer, &gamepad_panel);
    render_debug_gamepad(app, gamepad_panel, text, dim, hot);
}

static SDL_Color project_validation_color(ProjectValidationStatus status, bool selected) {
    switch (status) {
        case PROJECT_VALIDATION_VALID:
            return selected ? (SDL_Color){ 226, 252, 246, 255 } : (SDL_Color){ 218, 230, 234, 255 };
        case PROJECT_VALIDATION_WARNING:
            return selected ? (SDL_Color){ 255, 230, 120, 255 } : (SDL_Color){ 235, 202, 86, 255 };
        case PROJECT_VALIDATION_INVALID:
            return selected ? (SDL_Color){ 255, 120, 128, 255 } : (SDL_Color){ 224, 86, 98, 255 };
        case PROJECT_VALIDATION_UNKNOWN:
        default:
            return selected ? (SDL_Color){ 170, 180, 190, 255 } : (SDL_Color){ 118, 128, 140, 255 };
    }
}

static void render_project_browser(App *app) {
    if (!app || !app->project_browser_open) return;

    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_FRect panel = { 24.0f, 48.0f, (float)w - 48.0f, (float)h - 96.0f };
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 6, 7, 12, 232);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 100, 230, 240, 255);
    SDL_RenderRect(app->renderer, &panel);

    SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);
    SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 14.0f, "PROJECT BROWSER");
    SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 30.0f, app->project_browser_dir);
    SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 46.0f,
                        "Up/Down select   Enter/A open   R/Y refresh   X preview   Esc/B close");
    if (app->status_text[0]) {
        SDL_SetRenderDrawColor(app->renderer, 190, 202, 212, 255);
        SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 62.0f, app->status_text);
    }

    float detail_h = 78.0f;
    float list_top = panel.y + 92.0f;
    float list_bottom = panel.y + panel.h - detail_h - 12.0f;
    if (app->project_browser_count <= 0) {
        SDL_SetRenderDrawColor(app->renderer, 190, 202, 212, 255);
        SDL_RenderDebugText(app->renderer, panel.x + 16.0f, list_top, "No .vapor project bundles found.");
        return;
    }

    int visible_rows = (int)((list_bottom - list_top) / 18.0f);
    if (visible_rows < 1) visible_rows = 1;
    int first = app->project_browser_selected - visible_rows / 2;
    if (first < 0) first = 0;
    if (first + visible_rows > app->project_browser_count) first = app->project_browser_count - visible_rows;
    if (first < 0) first = 0;

    for (int row = 0; row < visible_rows && first + row < app->project_browser_count; ++row) {
        int index = first + row;
        ProjectBrowserEntry *entry = &app->project_browser_entries[index];
        bool selected = index == app->project_browser_selected;
        float y = list_top + (float)row * 18.0f;
        if (selected) {
            SDL_FRect highlight = { panel.x + 10.0f, y - 3.0f, panel.w - 20.0f, 16.0f };
            SDL_SetRenderDrawColor(app->renderer, 65, 85, 100, 215);
            SDL_RenderFillRect(app->renderer, &highlight);
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 130, 255);
            SDL_RenderRect(app->renderer, &highlight);
        }
        if (entry->is_blank_project) {
            SDL_SetRenderDrawColor(app->renderer,
                                   selected ? 226 : 180,
                                   selected ? 252 : 218,
                                   selected ? 246 : 226,
                                   255);
            SDL_RenderDebugTextFormat(app->renderer,
                                      panel.x + 16.0f,
                                      y,
                                      "%c %-8s  %-32s  %s",
                                      selected ? '>' : ' ',
                                      "NEW",
                                      entry->folder_name,
                                      "clear current workspace");
            continue;
        }
        ProjectValidationResult *row_validation = (selected && entry->full_validation_ready) ?
                                                  &entry->full_validation :
                                                  &entry->quick_validation;
        SDL_Color color = project_validation_color(row_validation->status, selected);
        set_draw_color(app->renderer, color);
        const char *name = row_validation->project_name[0] ?
                           row_validation->project_name :
                           entry->folder_name;
        SDL_RenderDebugTextFormat(app->renderer,
                                  panel.x + 16.0f,
                                  y,
                                  "%c %-8s  %-32s  %s",
                                  selected ? '>' : ' ',
                                  project_validation_status_label(row_validation->status),
                                  name,
                                  entry->folder_name);
    }

    ProjectBrowserEntry *selected = &app->project_browser_entries[app->project_browser_selected];
    ProjectValidationResult *focused = selected->full_validation_ready ?
                                       &selected->full_validation :
                                       &selected->quick_validation;
    SDL_FRect detail = { panel.x + 10.0f, panel.y + panel.h - detail_h, panel.w - 20.0f, detail_h - 10.0f };
    SDL_SetRenderDrawColor(app->renderer, 14, 18, 26, 230);
    SDL_RenderFillRect(app->renderer, &detail);
    set_draw_color(app->renderer, selected->is_blank_project ?
                                  (SDL_Color){ 255, 220, 130, 255 } :
                                  project_validation_color(focused->status, true));
    SDL_RenderRect(app->renderer, &detail);
    if (selected->is_blank_project) {
        SDL_RenderDebugText(app->renderer, detail.x + 10.0f, detail.y + 10.0f,
                            "NEW  Start from a blank in-memory project");
    } else {
        SDL_RenderDebugTextFormat(app->renderer, detail.x + 10.0f, detail.y + 10.0f,
                                  "%s  %s",
                                  project_validation_status_label(focused->status),
                                  focused->reason[0] ? focused->reason : "unknown");
    }
    SDL_SetRenderDrawColor(app->renderer, 220, 230, 235, 255);
    if (selected->is_blank_project) {
        SDL_RenderDebugText(app->renderer, detail.x + 10.0f, detail.y + 28.0f,
                            "Clears timeline, roster, drum patterns, tape speed, reverb, and plugins.");
        SDL_RenderDebugText(app->renderer, detail.x + 10.0f, detail.y + 46.0f,
                            "South/Enter asks for confirmation.");
    } else {
        SDL_RenderDebugText(app->renderer, detail.x + 10.0f, detail.y + 28.0f,
                            focused->detail[0] ? focused->detail : selected->path);
        SDL_RenderDebugText(app->renderer, detail.x + 10.0f, detail.y + 46.0f, selected->path);
    }

    if (app->project_browser_blank_confirm_open) {
        SDL_FRect confirm = {
            panel.x + panel.w * 0.5f - 235.0f,
            panel.y + panel.h * 0.5f - 72.0f,
            470.0f,
            144.0f
        };
        if (confirm.x < panel.x + 18.0f) confirm.x = panel.x + 18.0f;
        if (confirm.x + confirm.w > panel.x + panel.w - 18.0f) confirm.x = panel.x + panel.w - 18.0f - confirm.w;
        if (confirm.y < panel.y + 18.0f) confirm.y = panel.y + 18.0f;
        if (confirm.y + confirm.h > panel.y + panel.h - 18.0f) confirm.y = panel.y + panel.h - 18.0f - confirm.h;
        SDL_FRect shadow = { confirm.x + 10.0f, confirm.y + 12.0f, confirm.w + 16.0f, confirm.h + 16.0f };
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 178);
        SDL_RenderFillRect(app->renderer, &shadow);
        SDL_SetRenderDrawColor(app->renderer, 18, 10, 14, 248);
        SDL_RenderFillRect(app->renderer, &confirm);
        SDL_SetRenderDrawColor(app->renderer, 255, 120, 130, 255);
        SDL_RenderRect(app->renderer, &confirm);
        SDL_SetRenderDrawColor(app->renderer, 255, 226, 226, 255);
        SDL_RenderDebugText(app->renderer, confirm.x + 18.0f, confirm.y + 18.0f, "CONFIRM NEW BLANK PROJECT");
        SDL_SetRenderDrawColor(app->renderer, 235, 210, 214, 255);
        SDL_RenderDebugText(app->renderer, confirm.x + 18.0f, confirm.y + 46.0f,
                            "This clears the current timeline, roster, drum patterns,");
        SDL_RenderDebugText(app->renderer, confirm.x + 18.0f, confirm.y + 64.0f,
                            "surface edits, tape speed, reverb, and master plugins.");
        SDL_SetRenderDrawColor(app->renderer, 255, 238, 204, 255);
        SDL_RenderDebugText(app->renderer, confirm.x + 18.0f, confirm.y + 104.0f,
                            "South/Enter confirms   East/Esc/Start cancels");
    }
}

static void text_with_caret_display(const App *app, char *out, size_t out_size, int max_chars) {
    if (!out || out_size == 0 || !app) return;
    char with_caret[APP_SAMPLE_NAME_MAX + 4];
    int len = (int)SDL_strlen(app->text_entry_text);
    int caret = clamp_int(app->text_entry_caret, 0, len);
    SDL_memcpy(with_caret, app->text_entry_text, (size_t)caret);
    with_caret[caret] = '|';
    SDL_strlcpy(with_caret + caret + 1,
                app->text_entry_text + caret,
                sizeof(with_caret) - (size_t)caret - 1u);
    int total = (int)SDL_strlen(with_caret);
    if (total <= max_chars) {
        SDL_strlcpy(out, with_caret, out_size);
        return;
    }
    int start = total - max_chars + 1;
    SDL_snprintf(out, out_size, "<%s", with_caret + start);
}

static void render_text_entry(App *app) {
    if (!app || !app->text_entry_open) return;

    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 4, 5, 9, 225);
    SDL_FRect veil = { 0.0f, 0.0f, (float)w, (float)h };
    SDL_RenderFillRect(app->renderer, &veil);

    SDL_FRect panel = { ((float)w - 640.0f) * 0.5f, ((float)h - 380.0f) * 0.5f, 640.0f, 380.0f };
    if (panel.x < 18.0f) panel.x = 18.0f;
    if (panel.y < 18.0f) panel.y = 18.0f;
    if (panel.x + panel.w > (float)w - 18.0f) panel.w = (float)w - 36.0f;
    if (panel.y + panel.h > (float)h - 18.0f) panel.h = (float)h - 36.0f;

    SDL_SetRenderDrawColor(app->renderer, 12, 14, 22, 244);
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 130, 238, 234, 255);
    SDL_RenderRect(app->renderer, &panel);

    SDL_SetRenderDrawColor(app->renderer, 230, 238, 242, 255);
    SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 16.0f, app->text_entry_title);
    SDL_SetRenderDrawColor(app->renderer, 178, 190, 204, 255);
    SDL_RenderDebugText(app->renderer, panel.x + 18.0f, panel.y + 34.0f,
                        "A insert   B backspace   X separator   Y shift   L/R caret   Start confirm   Back cancel");

    char field[96];
    text_with_caret_display(app, field, sizeof(field), 60);
    SDL_SetRenderDrawColor(app->renderer, 226, 236, 242, 255);
    SDL_RenderDebugTextFormat(app->renderer, panel.x + 28.0f, panel.y + 72.0f,
                              "%-7s %s",
                              app->text_entry_prompt,
                              field);

    SDL_SetRenderDrawColor(app->renderer, 176, 188, 202, 255);
    if (app->text_entry_action == APP_TEXT_ENTRY_ACTION_SAVE_AS_PROJECT) {
        char folder_leaf[APP_SAMPLE_NAME_MAX + 16];
        if (!sanitize_project_bundle_leaf(app->text_entry_text, folder_leaf, sizeof(folder_leaf))) {
            SDL_strlcpy(folder_leaf, "Untitled.vapor", sizeof(folder_leaf));
        }
        SDL_RenderDebugTextFormat(app->renderer, panel.x + 28.0f, panel.y + 94.0f,
                                  "Folder: %s",
                                  folder_leaf);
    } else if (app->text_entry_action == APP_TEXT_ENTRY_ACTION_EXPORT_TIMELINE_WAV ||
               app->text_entry_action == APP_TEXT_ENTRY_ACTION_EXPORT_ROSTER_WAV) {
        char wav_leaf[APP_SAMPLE_NAME_MAX + 8];
        if (!sanitize_wav_filename_leaf(app->text_entry_text, wav_leaf, sizeof(wav_leaf))) {
            SDL_strlcpy(wav_leaf, "Untitled.wav", sizeof(wav_leaf));
        }
        SDL_RenderDebugTextFormat(app->renderer, panel.x + 28.0f, panel.y + 94.0f,
                                  "File:   %s",
                                  wav_leaf);
    } else if (app->text_entry_action == APP_TEXT_ENTRY_ACTION_RENAME_ROSTER_CLIP) {
        SDL_RenderDebugText(app->renderer,
                            panel.x + 28.0f,
                            panel.y + 94.0f,
                            "Timeline instances use the roster name");
    } else if (app->text_entry_action == APP_TEXT_ENTRY_ACTION_APPLY_LANE_VELOCITY) {
        SDL_RenderDebugTextFormat(app->renderer,
                                  panel.x + 28.0f,
                                  panel.y + 94.0f,
                                  "Lane:   %d",
                                  app->text_entry_target_lane_index + 1);
    }

    if (app->text_entry_error[0]) {
        SDL_SetRenderDrawColor(app->renderer, 255, 116, 112, 255);
        SDL_RenderDebugText(app->renderer, panel.x + 28.0f, panel.y + 118.0f, app->text_entry_error);
    }

    const float key_w = 46.0f;
    const float key_h = 32.0f;
    const float key_gap = 8.0f;
    float grid_y = panel.y + 158.0f;
    for (int row = 0; row < 4; ++row) {
        const char *keys = text_entry_keyboard_row(row);
        int len = (int)SDL_strlen(keys);
        float row_w = (float)len * key_w + (float)(len - 1) * key_gap;
        float x = panel.x + (panel.w - row_w) * 0.5f;
        float y = grid_y + (float)row * (key_h + key_gap);
        for (int col = 0; col < len; ++col) {
            bool selected = row == app->text_entry_key_row && col == app->text_entry_key_col;
            SDL_FRect key_rect = { x + (float)col * (key_w + key_gap), y, key_w, key_h };
            SDL_SetRenderDrawColor(app->renderer, selected ? 42 : 22, selected ? 70 : 26, selected ? 78 : 36, 245);
            SDL_RenderFillRect(app->renderer, &key_rect);
            SDL_SetRenderDrawColor(app->renderer, selected ? 255 : 82, selected ? 220 : 110, selected ? 130 : 126, 255);
            SDL_RenderRect(app->renderer, &key_rect);
            char label = keys[col];
            if (app->text_entry_uppercase && label >= 'a' && label <= 'z') label = (char)(label - 'a' + 'A');
            SDL_SetRenderDrawColor(app->renderer, selected ? 255 : 218, selected ? 245 : 228, selected ? 210 : 235, 255);
            SDL_RenderDebugTextFormat(app->renderer, key_rect.x + 19.0f, key_rect.y + 11.0f, "%c", label);
        }
    }

    SDL_SetRenderDrawColor(app->renderer, 176, 188, 202, 255);
    SDL_RenderDebugTextFormat(app->renderer, panel.x + 18.0f, panel.y + panel.h - 28.0f,
                              "mode: %s   shift: %s",
                              app->text_entry_mode == APP_TEXT_ENTRY_DISPLAY_NAME ? "DISPLAY_NAME" :
                              (app->text_entry_mode == APP_TEXT_ENTRY_FILENAME_SAFE ? "FILENAME_SAFE" : "SEARCH_FILTER"),
                              app->text_entry_uppercase ? "ON" : "off");
}

static const char *waveform_frame_grip_anchor_label(WaveformFrameGripAnchor anchor) {
    switch (anchor) {
        case WAVEFORM_FRAME_GRIP_ANCHOR_DOWNBEAT: return "DB";
        case WAVEFORM_FRAME_GRIP_ANCHOR_LOOP_START: return "LS";
        case WAVEFORM_FRAME_GRIP_ANCHOR_LOOP_END: return "LE";
        case WAVEFORM_FRAME_GRIP_ANCHOR_NONE:
        default: return "--";
    }
}

static SDL_Color waveform_frame_grip_anchor_color(WaveformFrameGripAnchor anchor) {
    switch (anchor) {
        case WAVEFORM_FRAME_GRIP_ANCHOR_DOWNBEAT: return (SDL_Color){ 255, 80, 220, 255 };
        case WAVEFORM_FRAME_GRIP_ANCHOR_LOOP_START: return (SDL_Color){ 255, 100, 120, 255 };
        case WAVEFORM_FRAME_GRIP_ANCHOR_LOOP_END: return (SDL_Color){ 255, 200, 110, 255 };
        case WAVEFORM_FRAME_GRIP_ANCHOR_NONE:
        default: return (SDL_Color){ 160, 168, 180, 255 };
    }
}

static void render_waveform_frame_grip_anchor_tag(App *app) {
    if (!app || app->view_mode != APP_VIEW_WAVEFORM || !app->waveform_frame_grip_active ||
        app->waveform_frame_grip_anchor == WAVEFORM_FRAME_GRIP_ANCHOR_NONE ||
        app->clip.frame_count < 1) {
        return;
    }

    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    if (w <= 0 || h <= 0) return;

    double view_start = app->view.view_center - app->view.view_span * 0.5;
    double view_end = app->view.view_center + app->view.view_span * 0.5;
    double span = view_end - view_start;
    if (span <= 0.0) span = 1.0;
    double frame_norm = (double)app->waveform_frame_grip_left_frame / (double)app->clip.frame_count;
    double unit_x = (frame_norm - view_start) / span;
    if (unit_x < 0.0) unit_x = 0.0;
    if (unit_x > 1.0) unit_x = 1.0;
    float edge_x = (float)lrint(unit_x * (double)w);
    if (edge_x < 0.0f) edge_x = 0.0f;
    if (edge_x > (float)(w - 1)) edge_x = (float)(w - 1);

    SDL_Color color = waveform_frame_grip_anchor_color(app->waveform_frame_grip_anchor);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, 170);
    SDL_RenderLine(app->renderer, edge_x, 160.0f, edge_x, (float)h - 1.0f);

    float tag_x = edge_x + 4.0f;
    if (tag_x + 30.0f > (float)w) tag_x = edge_x - 34.0f;
    if (tag_x < 2.0f) tag_x = 2.0f;
    SDL_FRect tag = { tag_x, 162.0f, 30.0f, 18.0f };
    SDL_SetRenderDrawColor(app->renderer, 8, 10, 16, 220);
    SDL_RenderFillRect(app->renderer, &tag);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, 255);
    SDL_RenderRect(app->renderer, &tag);
    SDL_RenderDebugText(app->renderer, tag.x + 7.0f, tag.y + 5.0f,
                        waveform_frame_grip_anchor_label(app->waveform_frame_grip_anchor));
}

static void app_render_overlay(App *app) {
    if (app->text_entry_open) {
        render_text_entry(app);
        return;
    }
    if (app->project_browser_open) {
        render_project_browser(app);
        return;
    }

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
        SDL_RenderDebugTextFormat(app->renderer, 12, 80, "meter: %d/%d  bars: %.2g  beats: %.1f",
                                  params.beats_per_bar, params.beat_unit, params.target_bars, target_beats);
        SDL_RenderDebugTextFormat(app->renderer, 12, 94, "loop: %.3fs  target: %.3fs  diff: %+.3fs",
                                  loop_duration, target_duration, diff);
    } else if (app->tempo_lock_mode) {
        SDL_RenderDebugText(app->renderer, 12, 66, "TEMPO LOCK MODE");
    }
    const char *view_label = app->view_mode == APP_VIEW_DRUM_MACHINE ? "drum machine" :
                             (app->view_mode == APP_VIEW_LANE_INSPECTOR ? "lane inspector" :
                             (app->view_mode == APP_VIEW_MASTER_MIX ? "master mix" :
                              (app->view_mode == APP_VIEW_TIMELINE ? "timeline" : "waveform")));
    SDL_RenderDebugTextFormat(app->renderer, 12, 108, "view: %s  roster: %d",
                              view_label,
                              app->roster_clip_count);
    if (app->view_mode == APP_VIEW_WAVEFORM && app->waveform_source_mode == WAVEFORM_SOURCE_ROSTER) {
        SDL_RenderDebugTextFormat(app->renderer, 12, 122, "waveform source: roster %s", app->waveform_source_name);
    }
    if (app->view_mode == APP_VIEW_WAVEFORM && app->waveform_frame_grip_active) {
        SDL_RenderDebugTextFormat(app->renderer, 12, 136, "FRAME GRIP %s",
                                  waveform_frame_grip_anchor_label(app->waveform_frame_grip_anchor));
        if (app->waveform_frame_grip_snap_active && app->waveform_frame_grip_snap_beats > 0.0) {
            int beats_per_bar = app->clip.tempo_lock.beats_per_bar > 0 ? app->clip.tempo_lock.beats_per_bar : 4;
            SDL_RenderDebugTextFormat(app->renderer, 12, 150, "SNAP FRAME: %.0f beats / %.2f bars",
                                      app->waveform_frame_grip_snap_beats,
                                      app->waveform_frame_grip_snap_beats / (double)beats_per_bar);
        }
        render_waveform_frame_grip_anchor_tag(app);
    }

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
            SDL_RenderDebugText(app->renderer, panel.x + 16.0f, panel.y + 58.0f, "No supported audio files found.");
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

    if (app->waveform_sidecar_confirm_open) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(app->renderer, &w, &h);
        SDL_FRect panel = { (float)w * 0.5f - 210.0f, (float)h * 0.5f - 58.0f, 420.0f, 116.0f };
        SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
        SDL_FRect shadow = { panel.x + 12.0f, panel.y + 14.0f, panel.w + 18.0f, panel.h + 18.0f };
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(app->renderer, &shadow);
        SDL_SetRenderDrawColor(app->renderer, 12, 13, 20, 242);
        SDL_RenderFillRect(app->renderer, &panel);
        SDL_SetRenderDrawColor(app->renderer, 255, 220, 120, 255);
        SDL_RenderRect(app->renderer, &panel);
        SDL_SetRenderDrawColor(app->renderer, 235, 242, 245, 255);
        SDL_RenderDebugText(app->renderer, panel.x + 14.0f, panel.y + 14.0f, "WRITE TEMPO JSON?");
        SDL_RenderDebugText(app->renderer, panel.x + 14.0f, panel.y + 34.0f, app->waveform_source_path);
        SDL_RenderDebugText(app->renderer, panel.x + 14.0f, panel.y + 58.0f, "South/Enter confirms");
        SDL_RenderDebugText(app->renderer, panel.x + 14.0f, panel.y + 76.0f, "East/Escape cancels");
        return;
    }

    if (app->roster_commit_menu_open) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(app->renderer, &w, &h);
        render_roster_commit_menu(app, w, h);
        return;
    }

    if (app->waveform_render_dialog_open) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(app->renderer, &w, &h);
        render_waveform_render_dialog(app, w, h);
        return;
    }

    if (app->waveform_menu_open) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(app->renderer, &w, &h);
        render_waveform_menu(app, w, h);
        return;
    }

    render_project_menu_legend_hint(app);
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
    app->debug_overlay_mode = APP_DEBUG_OVERLAY_GAMEPAD_STATS;
    app->debug_frame_ms_avg = 0.0;
    app->debug_frame_ms_max = 0.0;
    app->debug_fps = 0.0;
    app->project_browser_open = false;
    app->project_browser_count = 0;
    app->project_browser_selected = 0;
    app->project_browser_blank_confirm_open = false;
    app->project_browser_dir[0] = '\0';
    app->project_browser_preview_clip_loaded = false;
    app->waveform_menu_open = false;
    app->waveform_menu_selected = 0;
    app->waveform_render_dialog_open = false;
    app->waveform_render_dialog_mode = WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT;
    app->waveform_render_source_bpm = 0.0;
    app->waveform_render_value = 0.0;
    app->waveform_render_step_index = 2;
    app->waveform_render_error[0] = '\0';
    app->roster_commit_menu_open = false;
    app->roster_commit_menu_selected = 0;
    app->waveform_loop_edge_arm = WAVEFORM_LOOP_EDGE_ARM_NONE;
    app->text_entry_open = false;
    app->text_entry_mode = APP_TEXT_ENTRY_DISPLAY_NAME;
    app->text_entry_action = APP_TEXT_ENTRY_ACTION_NONE;
    app->text_entry_title[0] = '\0';
    app->text_entry_prompt[0] = '\0';
    app->text_entry_text[0] = '\0';
    app->text_entry_error[0] = '\0';
    app->text_entry_caret = 0;
    app->text_entry_max_length = APP_SAMPLE_NAME_MAX - 1;
    app->text_entry_key_row = 0;
    app->text_entry_key_col = 0;
    app->text_entry_uppercase = false;
    app->text_entry_target_roster_index = -1;
    app->text_entry_target_pattern_index = -1;
    app->text_entry_target_lane_index = -1;
    app_resolve_data_dirs(app);
    app_resolve_roster_export_dir(app);
    app_refresh_sample_list(app);
    app->drum_kit_count = 0;
    app->drum_pattern_count = 0;
    app->selected_drum_pattern = -1;
    app->selected_drum_pattern_armed = false;
    app->drum_machine_step = 0;
    app->drum_machine_pad = 0;
    app_refresh_drum_kits(app);
    clip_init_generated(&app->clip, 48000, 2.0f);
    transport_init(&app->transport, 120.0, 960, 4, 4);
    app->transport_bpm = 120.0;
    app->transport_bpm_manual = false;
    app->view_mode = APP_VIEW_WAVEFORM;
    app->master_mix_focus = MASTER_MIX_FOCUS_MASTER;
    app->master_reverb_selected_param = MASTER_REVERB_PARAM_ENABLED;
    app_set_waveform_source_generated(app);
    app_clear_waveform_frame_grip(app);
    app->controls_legend_open = false;
    app->timeline.ticks_per_beat = app->transport.ppqn;
    app->timeline.timeline_bpm = 120.0;
    app->timeline.timeline_beats_per_bar = 4;
    app->timeline.timeline_beat_unit = 4;
    timeline_set_tempo_event_no_lock(&app->timeline, 0, app->timeline.timeline_bpm);
    timeline_init_lanes(&app->timeline);
    app->timeline.timeline_cursor_tick = 0;
    app->timeline.timeline_cursor_seam_side = TIMELINE_SEAM_NONE;
    app->timeline.play_range_start_tick = 0;
    app->timeline.play_range_end_tick = 0;
    app->timeline.play_range_loop_enabled = false;
    app->timeline.play_range_custom = false;
    app->timeline.view_center_tick = (double)app->timeline.ticks_per_beat * 2.0;
    app->timeline.view_span_ticks = (double)app->timeline.ticks_per_beat * 4.0;
    app->timeline.tape_speed = TIMELINE_TAPE_SPEED_DEFAULT;
    app->timeline_focus_zone = TIMELINE_FOCUS_RULER;
    app->timeline_tape_control_mode = TIMELINE_TAPE_CONTROL_BPM;
    app->timeline_play_range_handle = TIMELINE_RANGE_HANDLE_START;
    app->timeline_play_range_adjusting = false;
    app->timeline_bounce_active = false;
    app->timeline_bounce_samples = NULL;
    app->timeline_bounce_target_frames = 0;
    app->timeline_bounce_recorded_frames = 0;
    app->timeline_bounce_sample_rate = 0;
    app->timeline_bounce_start_tick = 0;
    app->timeline_bounce_end_tick = 0;
    app->timeline_bounce_prev_metronome_enabled = false;
    app->timeline_bounce_prev_play_range_loop_enabled = false;
    app_timeline_clear_context_menu(app);
    app->timeline_edit_placement_mode = TIMELINE_PLACE_FREE;
    app->timeline_edit_instance = timeline_instance_ref_invalid();
    app->timeline_edit_instance_kind = TIMELINE_INSTANCE_AUDIO_CLIP;
    app->timeline_edit_roster_clip_index = -1;
    app->timeline_edit_pattern_index = -1;
    app->timeline_edit_original_lane = 0;
    app->timeline_edit_original_seam_side = TIMELINE_SEAM_NONE;
    app->timeline_edit_ghost_lane = 0;
    app->timeline_edit_ghost_seam_side = TIMELINE_SEAM_NONE;
    app->selected_roster_clip = -1;
    app->roster_scroll_offset = 0;
    app->roster_visible_rows = 1;
    app->selected_roster_clip_armed = false;
    app->selected_drum_pattern = -1;
    app->selected_drum_pattern_armed = false;
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
        app->audio.master_gain = 1.0f;
        app->audio.playback_mode = AUDIO_PLAYBACK_WAVEFORM;
        app->audio.active_analyzer_lane = -1;
        audio_engine_init_master_fx(&app->audio);
    }
    audio_engine_set_timeline(&app->audio, app->roster, &app->roster_clip_count, &app->timeline);
    audio_engine_set_drum_materials(&app->audio,
                                    app->drum_patterns,
                                    &app->drum_pattern_count,
                                    app->drum_kits,
                                    &app->drum_kit_count);
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

static void app_debug_update_frame_stats(App *app, double dt) {
    if (!app || dt <= 0.0) return;
    double frame_ms = dt * 1000.0;
    if (app->debug_frame_ms_avg <= 0.0) app->debug_frame_ms_avg = frame_ms;
    else app->debug_frame_ms_avg = app->debug_frame_ms_avg * 0.92 + frame_ms * 0.08;
    double decayed_max = app->debug_frame_ms_max * 0.985;
    app->debug_frame_ms_max = frame_ms > decayed_max ? frame_ms : decayed_max;
    app->debug_fps = app->debug_frame_ms_avg > 0.0 ? 1000.0 / app->debug_frame_ms_avg : 0.0;
}

void app_run(App *app){
    Uint64 prev=SDL_GetTicksNS();
    while(app->running){
        SDL_Event e; while(SDL_PollEvent(&e)) if(!input_handle_event(app,&e)) app->running=false;
        Uint64 now=SDL_GetTicksNS(); double dt=(double)(now-prev)/1e9; prev=now; app_debug_update_frame_stats(app, dt); input_update_gamepad(app,dt);
        app_update_timeline_bounce(app);
        waveform_view_update(&app->view, dt);
        if (app->view_mode == APP_VIEW_LANE_INSPECTOR) {
            app_render_lane_inspector(app);
        } else if (app->view_mode == APP_VIEW_DRUM_MACHINE) {
            app_render_drum_machine(app);
        } else if (app->view_mode == APP_VIEW_MASTER_MIX) {
            app_render_master_mix(app);
        } else if (app->view_mode == APP_VIEW_TIMELINE) {
            app_render_timeline(app);
        } else {
            TempoLockParams guide_params;
            TempoLockParams *guide = app_get_active_tempo_params(app, &guide_params) ? &guide_params : NULL;
            waveform_render(app->renderer,&app->clip,&app->view,audio_engine_get_playhead_frame(&app->audio),guide);
        }
        if (app->text_entry_open ||
            (app->view_mode != APP_VIEW_LANE_INSPECTOR && app->view_mode != APP_VIEW_MASTER_MIX)) {
            app_render_overlay(app);
        }
        app_render_debug_overlay(app);
        SDL_RenderPresent(app->renderer);
    }
}
void app_shutdown(App *app){
    if(app->text_entry_open && app->window) SDL_StopTextInput(app->window);
    app_cancel_timeline_bounce(app);
    app_project_browser_clear_preview(app);
    audio_engine_shutdown(&app->audio);
    clip_destroy(&app->clip);
    for (int i = 0; i < app->roster_clip_count; ++i) roster_clip_destroy(&app->roster[i]);
    app_clear_drum_kits(app);
    app_close_gamepad(app);
    if(app->renderer) SDL_DestroyRenderer(app->renderer);
    if(app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
}
