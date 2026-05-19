#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char file_path[256];
    int sample_rate;
    int channels;
    uint64_t frame_count;
    float *samples; // interleaved

    uint64_t loop_start_frame;
    uint64_t loop_end_frame;

    double source_bpm;
    int beats_per_bar;
    int beat_unit;
    uint64_t downbeat_frame;

    double playback_rate;
    float gain;
} AudioClip;

bool clip_load_or_generate(AudioClip *clip, const char *preferred_path);
void clip_reset_loop(AudioClip *clip);
void clip_free(AudioClip *clip);
uint64_t clip_clamp_frame(const AudioClip *clip, int64_t frame);
