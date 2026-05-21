#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "clip.h"
#include "audio_engine.h"
#include "timeline.h"
#include "transport.h"
#include "waveform.h"

#define APP_MAX_SAMPLES 64
#define APP_SAMPLE_NAME_MAX 128

typedef struct {
    char path[CLIP_MAX_PATH];
    char name[APP_SAMPLE_NAME_MAX];
} SampleEntry;

typedef enum {
    BPM_SOURCE_DEFAULT_120,
    BPM_SOURCE_TRANSPORT_MANUAL,
    BPM_SOURCE_CLIP_METADATA,
    BPM_SOURCE_TEMPO_LOCKED,
    BPM_SOURCE_MASTER_TIMELINE
} BpmSource;

typedef enum {
    APP_VIEW_WAVEFORM,
    APP_VIEW_TIMELINE
} AppViewMode;

typedef enum {
    TIMELINE_EDIT_NONE,
    TIMELINE_EDIT_MOVE_INSTANCE,
    TIMELINE_EDIT_PLACE_CLIP
} TimelineEditMode;

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Gamepad *gamepad;
    SDL_JoystickID gamepad_id;
    bool running;

    AudioClip clip;
    Transport transport;
    AudioEngine audio;
    WaveformView view;
    AppViewMode view_mode;
    bool controls_legend_open;

    SampleEntry samples[APP_MAX_SAMPLES];
    int sample_count;
    int selected_sample;
    bool sample_selector_open;
    char status_text[160];

    bool tempo_lock_mode;
    double transport_bpm;
    bool transport_bpm_manual;
    TempoLockParams tempo_lock_draft;
    bool has_retained_tempo_lock_params;
    TempoLockParams retained_tempo_lock;
    size_t retained_loop_start_frame;
    size_t retained_loop_end_frame;
    bool retained_tempo_lock_stale;

    RosterClip roster[APP_MAX_ROSTER_CLIPS];
    int roster_clip_count;
    MasterTimeline timeline;
    TimelineFocusZone timeline_focus_zone;
    TimelineRangeHandle timeline_play_range_handle;
    bool timeline_play_range_adjusting;
    bool timeline_context_menu_open;
    TimelineEditMode timeline_edit_mode;
    int timeline_edit_instance_index;
    int timeline_edit_roster_clip_index;
    int64_t timeline_edit_original_start_tick;
    int64_t timeline_edit_ghost_start_tick;
    int64_t timeline_edit_duration_ticks;
    bool timeline_edit_ghost_valid;
    int selected_roster_clip;
    bool selected_roster_clip_armed;
    int selected_timeline_instance;
} App;

bool app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);
void app_close_gamepad(App *app);
void app_focus_loop_start(App *app);
void app_focus_loop_end(App *app);
void app_refresh_sample_list(App *app);
bool load_clip_from_path(App *app, const char *path);
bool app_load_selected_sample(App *app);
void app_select_sample_delta(App *app, int delta);
void app_enter_tempo_lock_mode(App *app);
void app_cancel_tempo_lock_mode(App *app);
void app_apply_tempo_lock(App *app);
void app_clear_tempo_lock(App *app);
void app_adjust_transport_bpm(App *app, double delta);
void app_adjust_tempo_lock_bpm(App *app, double delta);
void app_adjust_tempo_lock_downbeat(App *app, long frames);
void app_cycle_tempo_lock_target_bars(App *app, int direction);
void app_cycle_tempo_lock_meter(App *app, int direction);
void app_note_loop_anchors_moved(App *app);
void app_capture_current_loop_to_roster(App *app);
void app_toggle_view_mode(App *app);
void app_toggle_controls_legend(App *app);
void app_toggle_timeline_playback(App *app);
void app_rewind_timeline(App *app);
void app_timeline_jump_to_play_range_start(App *app);
void app_timeline_jump_playhead_to_cursor(App *app);
void app_timeline_toggle_play_range_loop(App *app);
void app_timeline_cycle_focus(App *app, int direction);
void app_timeline_activate_focus(App *app);
void app_timeline_cancel_focus(App *app);
void app_timeline_open_context_menu(App *app);
void app_timeline_close_context_menu(App *app);
void app_timeline_remove_selected_instance(App *app);
void app_timeline_move_cursor(App *app, int direction);
void app_timeline_nudge_play_range(App *app, int direction);
void app_timeline_select_play_range_handle(App *app, TimelineRangeHandle handle);
void app_timeline_reset_play_range(App *app);
void app_timeline_select_roster_delta(App *app, int delta);
void app_timeline_nudge_edit_ghost(App *app, int direction);
void app_pan_timeline_view(App *app, double fraction);
void app_zoom_timeline_view(App *app, double scale);
BpmSource app_bpm_source(const App *app);
const char *app_bpm_source_label(const App *app);
bool app_get_active_tempo_params(const App *app, TempoLockParams *params);
