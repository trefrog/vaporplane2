#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "clip.h"
#include "audio_engine.h"
#include "transport.h"
#include "waveform.h"

#define APP_MAX_SAMPLES 64
#define APP_SAMPLE_NAME_MAX 128
#define APP_MAX_ROSTER_CLIPS 64
#define APP_MAX_TIMELINE_INSTANCES 128
#define APP_ROSTER_CLIP_NAME_MAX 128
#define APP_MIN_CAPTURE_FRAMES 64
#define APP_MAX_CAPTURE_FRAMES (48000 * 60 * 5)
#define APP_MAX_CAPTURE_BYTES (64u * 1024u * 1024u)

typedef struct {
    char path[CLIP_MAX_PATH];
    char name[APP_SAMPLE_NAME_MAX];
} SampleEntry;

typedef enum {
    BPM_SOURCE_DEFAULT_120,
    BPM_SOURCE_TRANSPORT_MANUAL,
    BPM_SOURCE_CLIP_METADATA,
    BPM_SOURCE_TEMPO_LOCKED
} BpmSource;

typedef enum {
    APP_VIEW_WAVEFORM,
    APP_VIEW_TIMELINE
} AppViewMode;

typedef struct {
    char name[APP_ROSTER_CLIP_NAME_MAX];
    char source_path[CLIP_MAX_PATH];
    size_t source_loop_start_frame;
    size_t source_loop_end_frame;
    int sample_rate;
    int channels;
    size_t frame_count;
    float *samples;
    double source_bpm;
    int beats_per_bar;
    int beat_unit;
    double target_bars;
    double target_beats;
    size_t downbeat_offset_frames;
    SDL_Color color;
} RosterClip;

typedef struct {
    int roster_clip_index;
    int64_t start_tick;
    int64_t duration_ticks;
} TimelineInstance;

typedef struct {
    bool initialized;
    double timeline_bpm;
    int timeline_beats_per_bar;
    int timeline_beat_unit;
    int ticks_per_beat;
    TimelineInstance instances[APP_MAX_TIMELINE_INSTANCES];
    int instance_count;
} MasterTimeline;

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
} App;

bool app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);
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
BpmSource app_bpm_source(const App *app);
const char *app_bpm_source_label(const App *app);
bool app_get_active_tempo_params(const App *app, TempoLockParams *params);
