#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "clip.h"
#include "audio_engine.h"
#include "timeline.h"
#include "transport.h"
#include "waveform.h"
#include "project_validation.h"

#define APP_MAX_SAMPLES 64
#define APP_MAX_PROJECTS 64
#define APP_SAMPLE_NAME_MAX 128

typedef struct {
    char path[CLIP_MAX_PATH];
    char name[APP_SAMPLE_NAME_MAX];
} SampleEntry;

typedef struct {
    char path[CLIP_MAX_PATH];
    char folder_name[APP_SAMPLE_NAME_MAX];
    ProjectValidationResult quick_validation;
    ProjectValidationResult full_validation;
    bool full_validation_ready;
    SDL_Time modify_time;
} ProjectBrowserEntry;

typedef enum {
    BPM_SOURCE_DEFAULT_120,
    BPM_SOURCE_TRANSPORT_MANUAL,
    BPM_SOURCE_CLIP_METADATA,
    BPM_SOURCE_TEMPO_LOCKED,
    BPM_SOURCE_MASTER_TIMELINE
} BpmSource;

typedef enum {
    APP_VIEW_WAVEFORM,
    APP_VIEW_TIMELINE,
    APP_VIEW_MASTER_MIX,
    APP_VIEW_LANE_INSPECTOR,
    APP_VIEW_DRUM_MACHINE
} AppViewMode;

typedef enum {
    MASTER_MIX_FOCUS_MASTER,
    MASTER_MIX_FOCUS_REVERB,
    MASTER_MIX_FOCUS_FX_CHAIN,
    MASTER_MIX_FOCUS_MIDI_CONTROL,
    MASTER_MIX_FOCUS_COUNT
} MasterMixFocusSection;

typedef enum {
    APP_DEBUG_OVERLAY_OFF,
    APP_DEBUG_OVERLAY_GAMEPAD,
    APP_DEBUG_OVERLAY_GAMEPAD_STATS
} AppDebugOverlayMode;

typedef enum {
    WAVEFORM_SOURCE_GENERATED,
    WAVEFORM_SOURCE_WAV,
    WAVEFORM_SOURCE_ROSTER
} WaveformSourceMode;

typedef enum {
    TIMELINE_EDIT_NONE,
    TIMELINE_EDIT_MOVE_INSTANCE,
    TIMELINE_EDIT_PLACE_CLIP
} TimelineEditMode;

typedef enum {
    TIMELINE_PLACE_FREE,
    TIMELINE_PLACE_PULSE,
    TIMELINE_INSERT_PULSE
} TimelinePlacementMode;

typedef enum {
    TIMELINE_TAPE_CONTROL_BPM,
    TIMELINE_TAPE_CONTROL_PITCH
} TimelineTapeControlMode;

typedef enum {
    TIMELINE_CONTEXT_SCOPE_NONE,
    TIMELINE_CONTEXT_SCOPE_TIMELINE,
    TIMELINE_CONTEXT_SCOPE_INSTANCE,
    TIMELINE_CONTEXT_SCOPE_ROSTER,
    TIMELINE_CONTEXT_SCOPE_PATTERN,
    TIMELINE_CONTEXT_SCOPE_CONFIRM_ROSTER_DELETE,
    TIMELINE_CONTEXT_SCOPE_CONFIRM_PATTERN_DELETE
} TimelineContextMenuScope;

typedef enum {
    TIMELINE_CONTEXT_ITEM_INSERT_BAR,
    TIMELINE_CONTEXT_ITEM_MARK_TEMPO,
    TIMELINE_CONTEXT_ITEM_REMOVE_TEMPO,
    TIMELINE_CONTEXT_ITEM_REMOVE_INSTANCE,
    TIMELINE_CONTEXT_ITEM_APPLY_LANE_VELOCITY,
    TIMELINE_CONTEXT_ITEM_OPEN_WAVEFORM,
    TIMELINE_CONTEXT_ITEM_RENAME_ROSTER,
    TIMELINE_CONTEXT_ITEM_PLACE_FREE,
    TIMELINE_CONTEXT_ITEM_PLACE_PULSE,
    TIMELINE_CONTEXT_ITEM_INSERT_PULSE,
    TIMELINE_CONTEXT_ITEM_EXPORT_ROSTER,
    TIMELINE_CONTEXT_ITEM_DELETE_ROSTER,
    TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_ROSTER,
    TIMELINE_CONTEXT_ITEM_NEW_PATTERN,
    TIMELINE_CONTEXT_ITEM_EDIT_PATTERN,
    TIMELINE_CONTEXT_ITEM_RENAME_PATTERN,
    TIMELINE_CONTEXT_ITEM_DUPLICATE_PATTERN,
    TIMELINE_CONTEXT_ITEM_DELETE_PATTERN,
    TIMELINE_CONTEXT_ITEM_CONFIRM_DELETE_PATTERN,
    TIMELINE_CONTEXT_ITEM_CANCEL
} TimelineContextMenuItem;

typedef enum {
    PROJECT_MENU_ITEM_SAVE,
    PROJECT_MENU_ITEM_OPEN,
    PROJECT_MENU_ITEM_EXPORT_TIMELINE_WAV,
    PROJECT_MENU_ITEM_QUIT,
    PROJECT_MENU_ITEM_COUNT
} ProjectMenuItem;

typedef enum {
    WAVEFORM_MENU_ITEM_RENDER_TEMPO,
    WAVEFORM_MENU_ITEM_RENDER_PITCH,
    WAVEFORM_MENU_ITEM_RENDER_RATE,
    WAVEFORM_MENU_ITEM_NORMALIZE,
    WAVEFORM_MENU_ITEM_CANCEL,
    WAVEFORM_MENU_ITEM_COUNT
} WaveformMenuItem;

typedef enum {
    WAVEFORM_RENDER_DIALOG_TEMPO_TO_BPM,
    WAVEFORM_RENDER_DIALOG_TEMPO_PERCENT,
    WAVEFORM_RENDER_DIALOG_PITCH_SEMITONES,
    WAVEFORM_RENDER_DIALOG_RATE_PERCENT
} WaveformRenderDialogMode;

typedef enum {
    ROSTER_COMMIT_ITEM_NEW_CLIP,
    ROSTER_COMMIT_ITEM_REPLACE_CLIP,
    ROSTER_COMMIT_ITEM_CANCEL,
    ROSTER_COMMIT_ITEM_COUNT
} RosterCommitMenuItem;

typedef enum {
    WAVEFORM_FRAME_GRIP_ANCHOR_NONE,
    WAVEFORM_FRAME_GRIP_ANCHOR_DOWNBEAT,
    WAVEFORM_FRAME_GRIP_ANCHOR_LOOP_START,
    WAVEFORM_FRAME_GRIP_ANCHOR_LOOP_END
} WaveformFrameGripAnchor;

typedef enum {
    APP_TEXT_ENTRY_DISPLAY_NAME,
    APP_TEXT_ENTRY_FILENAME_SAFE,
    APP_TEXT_ENTRY_SEARCH_FILTER
} AppTextEntryMode;

typedef enum {
    APP_TEXT_ENTRY_ACTION_NONE,
    APP_TEXT_ENTRY_ACTION_SAVE_AS_PROJECT,
    APP_TEXT_ENTRY_ACTION_EXPORT_TIMELINE_WAV,
    APP_TEXT_ENTRY_ACTION_EXPORT_ROSTER_WAV,
    APP_TEXT_ENTRY_ACTION_RENAME_ROSTER_CLIP,
    APP_TEXT_ENTRY_ACTION_RENAME_DRUM_PATTERN,
    APP_TEXT_ENTRY_ACTION_APPLY_LANE_VELOCITY
} AppTextEntryAction;

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Gamepad *gamepad;
    SDL_JoystickID gamepad_id;
    bool running;
    AppDebugOverlayMode debug_overlay_mode;
    double debug_frame_ms_avg;
    double debug_frame_ms_max;
    double debug_fps;

    AudioClip clip;
    Transport transport;
    AudioEngine audio;
    WaveformView view;
    AppViewMode view_mode;
    MasterMixFocusSection master_mix_focus;
    MasterReverbParamId master_reverb_selected_param;
    WaveformSourceMode waveform_source_mode;
    int waveform_source_roster_index;
    char waveform_source_name[APP_SAMPLE_NAME_MAX];
    char waveform_source_path[CLIP_MAX_PATH];
    size_t waveform_source_offset_frame;
    bool waveform_sidecar_confirm_open;
    bool waveform_menu_open;
    int waveform_menu_selected;
    bool waveform_render_dialog_open;
    WaveformRenderDialogMode waveform_render_dialog_mode;
    double waveform_render_source_bpm;
    double waveform_render_value;
    int waveform_render_step_index;
    char waveform_render_error[96];
    bool roster_commit_menu_open;
    int roster_commit_menu_selected;
    bool waveform_frame_grip_active;
    bool waveform_frame_grip_exact_valid;
    size_t waveform_frame_grip_left_frame;
    size_t waveform_frame_grip_right_frame;
    bool waveform_frame_grip_snap_active;
    int waveform_frame_grip_snap_index;
    double waveform_frame_grip_snap_beats;
    double waveform_frame_grip_l2_seconds;
    WaveformFrameGripAnchor waveform_frame_grip_anchor;
    bool controls_legend_open;

    SampleEntry samples[APP_MAX_SAMPLES];
    int sample_count;
    int selected_sample;
    bool sample_selector_open;
    ProjectBrowserEntry project_browser_entries[APP_MAX_PROJECTS];
    int project_browser_count;
    int project_browser_selected;
    bool project_browser_open;
    char project_browser_dir[CLIP_MAX_PATH];
    AudioClip project_browser_preview_clip;
    bool project_browser_preview_clip_loaded;
    bool use_user_data_dirs;
    char user_data_dir[CLIP_MAX_PATH];
    char sample_dir[CLIP_MAX_PATH];
    char drum_pack_dir[CLIP_MAX_PATH];
    char roster_export_dir[CLIP_MAX_PATH];
    char project_export_dir[CLIP_MAX_PATH];
    char render_export_dir[CLIP_MAX_PATH];
    bool roster_export_dir_is_base_path;
    char status_text[160];
    char project_id[APP_STABLE_ID_MAX];
    char project_name[APP_SAMPLE_NAME_MAX];
    bool text_entry_open;
    AppTextEntryMode text_entry_mode;
    AppTextEntryAction text_entry_action;
    char text_entry_title[64];
    char text_entry_prompt[64];
    char text_entry_text[APP_SAMPLE_NAME_MAX];
    char text_entry_error[96];
    int text_entry_caret;
    int text_entry_max_length;
    int text_entry_key_row;
    int text_entry_key_col;
    bool text_entry_uppercase;
    int text_entry_target_roster_index;
    int text_entry_target_pattern_index;
    int text_entry_target_lane_index;

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
    DrumKit drum_kits[APP_MAX_DRUM_KITS];
    int drum_kit_count;
    DrumPattern drum_patterns[APP_MAX_DRUM_PATTERNS];
    int drum_pattern_count;
    MasterTimeline timeline;
    TimelineFocusZone timeline_focus_zone;
    TimelineTapeControlMode timeline_tape_control_mode;
    TimelineRangeHandle timeline_play_range_handle;
    bool timeline_play_range_adjusting;
    bool timeline_context_menu_open;
    bool project_menu_open;
    int project_menu_selected;
    TimelineContextMenuScope timeline_context_menu_scope;
    int timeline_context_menu_selected;
    TimelineInstanceRef timeline_context_menu_instance;
    int timeline_context_menu_roster_index;
    int timeline_context_menu_pattern_index;
    int64_t timeline_context_menu_tick;
    TimelineEditMode timeline_edit_mode;
    TimelinePlacementMode timeline_edit_placement_mode;
    TimelineInstanceRef timeline_edit_instance;
    TimelineInstanceKind timeline_edit_instance_kind;
    int timeline_edit_roster_clip_index;
    int timeline_edit_pattern_index;
    int timeline_edit_original_lane;
    int64_t timeline_edit_original_start_tick;
    TimelineSeamSide timeline_edit_original_seam_side;
    int timeline_edit_ghost_lane;
    int64_t timeline_edit_ghost_start_tick;
    TimelineSeamSide timeline_edit_ghost_seam_side;
    int64_t timeline_edit_duration_ticks;
    bool timeline_edit_ghost_valid;
    int selected_roster_clip;
    bool selected_roster_clip_armed;
    int selected_drum_pattern;
    bool selected_drum_pattern_armed;
    int drum_machine_step;
    int drum_machine_pad;
    int selected_timeline_lane;
    int inspected_timeline_lane;
    int lane_analyzer_visual_lane;
    float lane_analyzer_bars[LANE_ANALYZER_BUCKETS];
    float lane_analyzer_peaks[LANE_ANALYZER_BUCKETS];
    TimelineInstanceRef selected_timeline_instance;
} App;

bool app_init(App *app);
void app_run(App *app);
void app_shutdown(App *app);
void app_close_gamepad(App *app);
void app_focus_loop_start(App *app);
void app_focus_loop_end(App *app);
void app_toggle_debug_overlay(App *app);
void app_refresh_sample_list(App *app);
bool load_clip_from_path(App *app, const char *path);
bool app_load_selected_sample(App *app);
void app_select_sample_delta(App *app, int delta);
void app_enter_tempo_lock_mode(App *app);
void app_cancel_tempo_lock_mode(App *app);
void app_apply_tempo_lock(App *app);
void app_apply_tempo_lock_and_capture(App *app);
void app_clear_tempo_lock(App *app);
void app_adjust_transport_bpm(App *app, double delta);
void app_adjust_tempo_lock_bpm(App *app, double delta);
void app_adjust_tempo_lock_downbeat(App *app, long frames);
void app_snap_tempo_lock_downbeat_to_loop_start(App *app);
void app_cycle_tempo_lock_target_bars(App *app, int direction);
void app_cycle_tempo_lock_meter(App *app, int direction);
void app_note_loop_anchors_moved(App *app);
void app_capture_current_loop_to_roster(App *app);
void app_open_selected_roster_clip_waveform(App *app);
void app_request_write_tempo_sidecar(App *app);
void app_confirm_write_tempo_sidecar(App *app);
void app_cancel_write_tempo_sidecar(App *app);
void app_clear_waveform_frame_grip(App *app);
void app_toggle_view_mode(App *app);
void app_master_mix_return_to_timeline(App *app);
void app_master_mix_cycle_focus(App *app, int direction);
void app_master_reverb_select_param_delta(App *app, int delta);
void app_master_reverb_adjust_param(App *app, int direction, bool fine);
void app_master_reverb_activate_selected(App *app);
void app_master_reverb_clear_tail(App *app);
void app_toggle_controls_legend(App *app);
bool app_stop_active_audio(App *app);
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
void app_timeline_context_menu_move(App *app, int delta);
void app_timeline_context_menu_apply(App *app);
void app_project_menu_open(App *app);
void app_project_menu_close(App *app);
void app_project_menu_move(App *app, int delta);
void app_project_menu_apply(App *app);
void app_text_entry_cancel(App *app);
void app_text_entry_confirm(App *app);
void app_text_entry_insert_text(App *app, const char *text);
void app_text_entry_backspace(App *app);
void app_text_entry_delete_forward(App *app);
void app_text_entry_move_caret(App *app, int delta);
void app_text_entry_move_key(App *app, int dx, int dy);
void app_text_entry_insert_selected_key(App *app);
void app_text_entry_insert_separator(App *app);
void app_text_entry_toggle_shift(App *app);
void app_waveform_menu_open(App *app);
void app_waveform_menu_close(App *app);
void app_waveform_menu_move(App *app, int delta);
void app_waveform_menu_apply(App *app);
void app_waveform_render_dialog_cancel(App *app);
void app_waveform_render_dialog_confirm(App *app);
void app_waveform_render_dialog_adjust(App *app, int direction);
void app_waveform_render_dialog_cycle_step(App *app, int direction);
void app_roster_commit_menu_close(App *app);
void app_roster_commit_menu_move(App *app, int delta);
void app_roster_commit_menu_apply(App *app);
void app_project_browser_open(App *app);
void app_project_browser_close(App *app);
void app_project_browser_refresh(App *app);
void app_project_browser_move(App *app, int delta);
void app_project_browser_open_selected(App *app);
void app_project_browser_preview_selected(App *app);
bool app_save_project_bundle(App *app, const char *bundle_path);
bool app_load_project_bundle(App *app, const char *bundle_path);
bool app_export_timeline_wav(App *app, const char *path);
void app_timeline_insert_bar_at_cursor(App *app);
void app_timeline_mark_tempo_at_cursor(App *app);
void app_timeline_remove_tempo_at_cursor(App *app);
void app_timeline_adjust_tempo_event_at_cursor(App *app, double delta);
bool app_timeline_cursor_on_tempo_event(const App *app);
void app_timeline_set_tape_control_mode(App *app, TimelineTapeControlMode mode);
void app_timeline_toggle_tape_control_mode(App *app);
void app_timeline_adjust_tape_control(App *app, int direction, double bpm_step);
void app_timeline_reset_tape_speed(App *app);
void app_timeline_remove_selected_instance(App *app);
void app_delete_selected_roster_clip(App *app);
void app_export_selected_roster_clip(App *app);
void app_preview_selected_roster_clip(App *app);
void app_timeline_move_cursor(App *app, int direction);
void app_timeline_nudge_play_range(App *app, int direction, bool by_bar);
void app_timeline_select_play_range_handle(App *app, TimelineRangeHandle handle);
void app_timeline_reset_play_range(App *app);
void app_timeline_fit_play_range_view(App *app);
void app_timeline_select_roster_delta(App *app, int delta);
void app_timeline_select_lane_delta(App *app, int delta);
void app_timeline_move_cursor_by_bar(App *app, int direction);
void app_timeline_nudge_edit_ghost(App *app, int direction);
void app_timeline_nudge_edit_ghost_by_bar(App *app, int direction);
void app_timeline_nudge_edit_lane(App *app, int direction);
void app_timeline_adjust_selected_instance_velocity(App *app, int delta);
void app_pan_timeline_view(App *app, double fraction);
void app_zoom_timeline_view(App *app, double scale);
void app_open_lane_inspector(App *app, int lane_index);
void app_close_lane_inspector(App *app);
void app_toggle_inspected_lane_mute(App *app);
void app_cycle_inspected_lane_palette(App *app, int direction);
void app_toggle_inspected_lane_type(App *app);
void app_cycle_inspected_lane_kit(App *app, int direction);
void app_create_drum_pattern(App *app);
void app_duplicate_selected_drum_pattern(App *app);
void app_delete_selected_drum_pattern(App *app);
void app_open_drum_machine_for_selected_pattern(App *app);
void app_close_drum_machine(App *app);
void app_drum_machine_move_cursor(App *app, int dx, int dy);
void app_drum_machine_toggle_step(App *app);
void app_drum_machine_adjust_velocity(App *app, int delta);
BpmSource app_bpm_source(const App *app);
const char *app_bpm_source_label(const App *app);
bool app_get_active_tempo_params(const App *app, TempoLockParams *params);
