#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include "clip.h"
#include "timeline.h"
#include "transport.h"

typedef enum {
    AUDIO_PLAYBACK_WAVEFORM,
    AUDIO_PLAYBACK_TIMELINE
} AudioPlaybackMode;

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
    MasterMeterState meter;
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
