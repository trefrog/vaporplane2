#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include "clip.h"
#include "transport.h"

typedef struct {
    SDL_AudioStream *stream;
    SDL_AudioSpec spec;
    AudioClip *clip;
    Transport *transport;
    double playhead_frame;
    float master_gain;
} AudioEngine;

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport);
void audio_engine_shutdown(AudioEngine *a);
void audio_engine_set_playhead(AudioEngine *a, size_t frame);
size_t audio_engine_get_playhead_frame(const AudioEngine *a);
