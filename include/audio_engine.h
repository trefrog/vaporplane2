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

typedef enum {
    AUDIO_PLAYBACK_WAVEFORM,
    AUDIO_PLAYBACK_TIMELINE
} AudioPlaybackMode;

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
    float peak_l;
    float peak_r;
    float rms_l;
    float rms_r;
    unsigned int clip_count;
    float clip_flash_seconds;
    unsigned int histogram[4];
} MasterMeterState;

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
    MasterTimeline *timeline;
    AudioPlaybackMode playback_mode;
    double playhead_frame;
    double timeline_playhead_tick;
    int64_t last_metronome_beat;
    bool metronome_beat_valid;
    bool preview_active;
    int preview_roster_clip_index;
    double preview_frame;
    float master_gain;
    MasterFxChain master_fx_chain;
    MasterMeterState meter;
    LaneMonitorState lane_meters[TIMELINE_MAX_LANES];
    float lane_analyzer_samples[LANE_ANALYZER_WINDOW_SIZE];
    unsigned int lane_analyzer_write_index;
    unsigned int lane_analyzer_sample_count;
    int active_analyzer_lane;
    bool lane_analyzer_active;
    AudioDebugStats debug_stats;
} AudioEngine;

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport);
void audio_engine_shutdown(AudioEngine *a);
void audio_engine_set_playhead(AudioEngine *a, size_t frame);
size_t audio_engine_get_playhead_frame(const AudioEngine *a);
void audio_engine_set_timeline(AudioEngine *a, RosterClip *roster, int *roster_clip_count, MasterTimeline *timeline);
void audio_engine_set_playback_mode(AudioEngine *a, AudioPlaybackMode mode);
void audio_engine_start_timeline(AudioEngine *a);
void audio_engine_stop_timeline(AudioEngine *a, bool rewind);
bool audio_engine_preview_roster_clip(AudioEngine *a, int roster_index);
void audio_engine_stop_preview(AudioEngine *a);
void audio_engine_set_timeline_playhead(AudioEngine *a, int64_t tick);
bool audio_engine_timeline_is_playing(const AudioEngine *a);
int64_t audio_engine_get_timeline_playhead_tick(const AudioEngine *a);
void audio_engine_get_master_meter(const AudioEngine *a, MasterMeterState *meter);
void audio_engine_get_master_fx_chain(const AudioEngine *a, MasterFxChain *chain);
const char *audio_engine_master_fx_unit_label(MasterFxUnitType type);
void audio_engine_get_debug_stats(const AudioEngine *a, AudioDebugStats *stats);
void audio_engine_set_active_lane_analyzer(AudioEngine *a, int lane_index);
void audio_engine_get_lane_monitor(const AudioEngine *a, int lane_index, LaneMonitorState *meter);
void audio_engine_get_lane_analyzer_snapshot(const AudioEngine *a,
                                             int lane_index,
                                             float *samples,
                                             int sample_count,
                                             int *sample_rate,
                                             bool *active);
