#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char file_path[256];
    int sample_rate;
    int channels;
    uint64_t frame_count;
    float *samples;

    uint64_t loop_start_frame;
    uint64_t loop_end_frame;

    double source_bpm;
    int beats_per_bar;
    int beat_unit;
    uint64_t downbeat_frame;
    double playback_rate;
    float gain;
} AudioClip;

bool clip_load_or_generate(AudioClip *clip, const char *path);
void clip_destroy(AudioClip *clip);
void clip_reset_loop(AudioClip *clip);
void clip_move_loop_start(AudioClip *clip, int64_t delta_frames);
void clip_move_loop_end(AudioClip *clip, int64_t delta_frames);
