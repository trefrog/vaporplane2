#pragma once
#include <stdbool.h>
#include <stddef.h>

#define CLIP_MAX_PATH 256

typedef struct {
    char file_path[CLIP_MAX_PATH];
    int sample_rate;
    int channels;
    size_t frame_count;
    float *samples; // interleaved

    size_t loop_start_frame;
    size_t loop_end_frame;

    double source_bpm;
    int beats_per_bar;
    int beat_unit;
    size_t downbeat_frame;
    double playback_rate;
    float gain;
} AudioClip;

bool clip_init_from_wav(AudioClip *clip, const char *path);
void clip_init_generated(AudioClip *clip, int sample_rate, float seconds);
void clip_destroy(AudioClip *clip);
void clip_reset_loop(AudioClip *clip);
void clip_clamp_loop(AudioClip *clip);
