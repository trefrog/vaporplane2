#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include "clip.h"
#include "timeline.h"
#include "transport.h"

#define LANE_ANALYZER_BUCKETS 64
#define LANE_ANALYZER_WINDOW_SIZE 1024
#define MASTER_FX_CHAIN_MAX_UNITS 4
#define MASTER_REVERB_FDN_LINES 8
#define MASTER_REVERB_PREDELAY_MAX_FRAMES 9600
#define MASTER_REVERB_DELAY_MAX_FRAMES 8192
#define AUDIO_TIMELINE_OFFLINE_BLOCK_FRAMES 512
#define MASTER_LIMITER_CEILING 0.98f
#define MASTER_LIMITER_RELEASE_MS 80.0f

typedef enum {
    AUDIO_PLAYBACK_WAVEFORM,
    AUDIO_PLAYBACK_TIMELINE
} AudioPlaybackMode;

typedef enum {
    MASTER_REVERB_PARAM_ENABLED,
    MASTER_REVERB_PARAM_SEND,
    MASTER_REVERB_PARAM_RETURN,
    MASTER_REVERB_PARAM_PREDELAY_MS,
    MASTER_REVERB_PARAM_DECAY_SECONDS,
    MASTER_REVERB_PARAM_SIZE,
    MASTER_REVERB_PARAM_DIFFUSION,
    MASTER_REVERB_PARAM_DAMPING,
    MASTER_REVERB_PARAM_LOW_CUT_HZ,
    MASTER_REVERB_PARAM_HIGH_CUT_HZ,
    MASTER_REVERB_PARAM_WIDTH,
    MASTER_REVERB_PARAM_MOD_DEPTH_MS,
    MASTER_REVERB_PARAM_MOD_RATE_HZ,
    MASTER_REVERB_PARAM_COUNT
} MasterReverbParamId;

typedef enum {
    MASTER_FX_UNIT_EMPTY,
    MASTER_FX_UNIT_REVERB,
    MASTER_FX_UNIT_LOW_HIGH_CUT,
    MASTER_FX_UNIT_DELAY,
    MASTER_FX_UNIT_SOFT_CLIP_LIMITER
} MasterFxUnitType;

typedef struct {
    MasterFxUnitType type;
    bool enabled;
    bool bypassed;
} MasterFxUnit;

typedef struct {
    MasterFxUnit units[MASTER_FX_CHAIN_MAX_UNITS];
    int unit_count;
} MasterFxChain;

typedef struct {
    bool enabled;
    float send;
    float return_gain;
    float predelay_ms;
    float decay_seconds;
    float size;
    float diffusion;
    float damping;
    float low_cut_hz;
    float high_cut_hz;
    float width;
    float mod_depth_ms;
    float mod_rate_hz;
} MasterReverbParams;

typedef struct {
    float pre_delay[MASTER_REVERB_PREDELAY_MAX_FRAMES];
    float delay_lines[MASTER_REVERB_FDN_LINES][MASTER_REVERB_DELAY_MAX_FRAMES];
    int pre_delay_write;
    int delay_write[MASTER_REVERB_FDN_LINES];
    float pre_delay_samples;
    float target_pre_delay_samples;
    float delay_samples[MASTER_REVERB_FDN_LINES];
    float target_delay_samples[MASTER_REVERB_FDN_LINES];
    float feedback_gain[MASTER_REVERB_FDN_LINES];
    float target_feedback_gain[MASTER_REVERB_FDN_LINES];
    float damping_state[MASTER_REVERB_FDN_LINES];
    float low_cut_lp;
    float high_cut_lp_l;
    float high_cut_lp_r;
    float low_cut_coeff;
    float target_low_cut_coeff;
    float high_cut_coeff;
    float target_high_cut_coeff;
    float damping_coeff;
    float target_damping_coeff;
    float enabled_amount;
    float mod_phase;
    bool tail_cleared;
} MasterReverbState;

typedef struct {
    float peak_l;
    float peak_r;
    float rms_l;
    float rms_r;
    float limiter_gain;
    unsigned int clip_count;
    float clip_flash_seconds;
    unsigned int histogram[4];
} MasterMeterState;

typedef struct {
    float gain;
} MasterLimiterState;

typedef struct {
    float peak_l;
    float peak_r;
    float rms_l;
    float rms_r;
    unsigned int clip_count;
    float clip_hold_seconds;
} LaneMonitorState;

typedef struct {
    double callback_ms_avg;
    double callback_ms_max;
    int buffer_frames;
    int sample_rate;
    double audio_budget_ms;
    double audio_load;
    unsigned int over_budget_count;
    int active_clips;
} AudioDebugStats;

typedef struct {
    SDL_AudioStream *stream;
    SDL_AudioSpec spec;
    AudioClip *clip;
    Transport *transport;
    RosterClip *roster;
    int *roster_clip_count;
    DrumPattern *drum_patterns;
    int *drum_pattern_count;
    DrumKit *drum_kits;
    int *drum_kit_count;
    MasterTimeline *timeline;
    AudioPlaybackMode playback_mode;
    double playhead_frame;
    double timeline_playhead_tick;
    int64_t last_metronome_beat;
    bool metronome_beat_valid;
    bool preview_active;
    int preview_roster_clip_index;
    double preview_frame;
    bool file_preview_active;
    const AudioClip *file_preview_clip;
    double file_preview_frame;
    float master_gain;
    MasterFxChain master_fx_chain;
    MasterReverbParams master_reverb_target;
    MasterReverbParams master_reverb_current;
    MasterReverbState master_reverb_state;
    MasterLimiterState master_limiter_state;
    MasterMeterState meter;
    LaneMonitorState lane_meters[TIMELINE_MAX_LANES];
    float lane_analyzer_samples[LANE_ANALYZER_WINDOW_SIZE];
    unsigned int lane_analyzer_write_index;
    unsigned int lane_analyzer_sample_count;
    int active_analyzer_lane;
    bool lane_analyzer_active;
    AudioDebugStats debug_stats;
    bool timeline_bounce_recording_active;
    bool timeline_bounce_recording_complete;
    bool timeline_bounce_recording_failed;
    float *timeline_bounce_recording_samples;
    size_t timeline_bounce_recording_capacity_frames;
    size_t timeline_bounce_recording_target_frames;
    size_t timeline_bounce_recording_frames;
    int64_t timeline_bounce_recording_range_end_tick;
} AudioEngine;

typedef struct {
    double playhead_tick;
    int64_t range_start_tick;
    int64_t range_end_tick;
    bool finished;
} AudioTimelineRenderState;

typedef struct {
    bool active;
    bool complete;
    bool failed;
    size_t recorded_frames;
    size_t target_frames;
    size_t capacity_frames;
    int64_t range_end_tick;
} AudioTimelineBounceRecordingState;

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport);
void audio_engine_shutdown(AudioEngine *a);
void audio_engine_set_playhead(AudioEngine *a, size_t frame);
size_t audio_engine_get_playhead_frame(const AudioEngine *a);
void audio_engine_set_timeline(AudioEngine *a, RosterClip *roster, int *roster_clip_count, MasterTimeline *timeline);
void audio_engine_set_drum_materials(AudioEngine *a,
                                     DrumPattern *patterns,
                                     int *pattern_count,
                                     DrumKit *kits,
                                     int *kit_count);
void audio_engine_set_playback_mode(AudioEngine *a, AudioPlaybackMode mode);
void audio_engine_start_timeline(AudioEngine *a);
void audio_engine_stop_timeline(AudioEngine *a, bool rewind);
bool audio_engine_preview_roster_clip(AudioEngine *a, int roster_index);
void audio_engine_stop_preview(AudioEngine *a);
bool audio_engine_preview_file_clip(AudioEngine *a, const AudioClip *clip);
void audio_engine_stop_file_preview(AudioEngine *a);
void audio_engine_set_timeline_playhead(AudioEngine *a, int64_t tick);
bool audio_engine_timeline_is_playing(const AudioEngine *a);
int64_t audio_engine_get_timeline_playhead_tick(const AudioEngine *a);
bool audio_engine_start_timeline_bounce_recording(AudioEngine *a,
                                                  float *samples,
                                                  size_t capacity_frames,
                                                  size_t target_frames,
                                                  int64_t range_end_tick);
void audio_engine_cancel_timeline_bounce_recording(AudioEngine *a);
void audio_engine_get_timeline_bounce_recording_state(const AudioEngine *a, AudioTimelineBounceRecordingState *state);
void audio_timeline_render_state_init(AudioTimelineRenderState *state,
                                      int64_t range_start_tick,
                                      int64_t range_end_tick);
void audio_engine_init_offline_timeline_render(AudioEngine *offline,
                                               const AudioEngine *source,
                                               RosterClip *roster,
                                               int *roster_clip_count,
                                               MasterTimeline *timeline,
                                               int sample_rate);
bool audio_engine_timeline_has_tail_fx(const AudioEngine *a);
double audio_engine_timeline_tail_cap_seconds(const AudioEngine *a);
int audio_engine_render_timeline_block(AudioEngine *a,
                                       AudioTimelineRenderState *state,
                                       float *out,
                                       int frame_count,
                                       int sample_rate);
void audio_engine_render_master_fx_silence_block(AudioEngine *a,
                                                 float *out,
                                                 int frame_count,
                                                 int sample_rate);
void audio_engine_get_master_meter(const AudioEngine *a, MasterMeterState *meter);
void audio_engine_get_master_fx_chain(const AudioEngine *a, MasterFxChain *chain);
const char *audio_engine_master_fx_unit_label(MasterFxUnitType type);
void audio_engine_get_master_reverb_params(const AudioEngine *a, MasterReverbParams *current, MasterReverbParams *target);
void audio_engine_set_master_reverb_param(AudioEngine *a, MasterReverbParamId param, float value);
void audio_engine_set_master_reverb_enabled(AudioEngine *a, bool enabled);
void audio_engine_toggle_master_reverb(AudioEngine *a);
void audio_engine_clear_master_reverb_tail(AudioEngine *a);
const char *audio_engine_master_reverb_param_label(MasterReverbParamId param);
void audio_engine_init_master_fx(AudioEngine *a);
void audio_engine_get_debug_stats(const AudioEngine *a, AudioDebugStats *stats);
void audio_engine_set_active_lane_analyzer(AudioEngine *a, int lane_index);
void audio_engine_get_lane_monitor(const AudioEngine *a, int lane_index, LaneMonitorState *meter);
void audio_engine_get_lane_analyzer_snapshot(const AudioEngine *a,
                                             int lane_index,
                                             float *samples,
                                             int sample_count,
                                             int *sample_rate,
                                             bool *active);
