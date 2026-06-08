#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int sample_rate;
    int channels;
    size_t frame_count;
    float *samples;
} AudioFileData;

bool audio_file_decode(AudioFileData *audio, const char *path);
void audio_file_data_destroy(AudioFileData *audio);

