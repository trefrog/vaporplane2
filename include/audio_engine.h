#pragma once
#include "clip.h"
#include "transport.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    SDL_AudioStream *stream;
    SDL_AudioSpec out_spec;
    AudioClip *clip;
    Transport *transport;
    bool playing;
    double playhead_frame;

    int metro_samples_left;
    double metro_phase;
    int metro_freq;
} AudioEngine;

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport);
void audio_engine_shutdown(AudioEngine *a);
void audio_engine_set_playing(AudioEngine *a, bool playing);
void audio_engine_jump_to_loop_start(AudioEngine *a);
uint64_t audio_engine_playhead_frame(const AudioEngine *a);
