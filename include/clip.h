#pragma once
#include <stdbool.h>
#include <stddef.h>

#define CLIP_MAX_PATH 256

typedef struct {
    double bpm;
    size_t downbeat_frame;
    int beats_per_bar;
    int beat_unit;
    double target_bars;
} TempoLockParams;

typedef struct {
    char file_path[CLIP_MAX_PATH];
    int sample_rate;
    int channels;
    size_t frame_count;
    float *samples; // interleaved

    size_t loop_start_frame;
    size_t loop_end_frame;

    double source_bpm;
    bool has_clip_metadata_bpm;
    double clip_metadata_bpm;
    bool clip_tempo_locked;
    TempoLockParams tempo_lock;
    int beats_per_bar;
    int beat_unit;
    size_t downbeat_frame;
    double playback_rate;
    float gain;
} AudioClip;

bool clip_init_from_wav(AudioClip *clip, const char *path);
bool clip_init_from_audio_file(AudioClip *clip, const char *path);
void clip_init_generated(AudioClip *clip, int sample_rate, float seconds);
void clip_destroy(AudioClip *clip);
void clip_reset_loop(AudioClip *clip);
void clip_clamp_loop(AudioClip *clip);
