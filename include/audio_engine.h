#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "clip.h"
#include "transport.h"

typedef struct {
    SDL_AudioDeviceID device;
    SDL_AudioStream *stream;
    SDL_AudioSpec obtained;
    SDL_Mutex *lock;

    AudioClip *clip;
    Transport *transport;

    bool metronome_enabled;
    double playhead_frame;

    double met_env;
    double met_phase;
} AudioEngine;

bool audio_engine_init(AudioEngine *engine, AudioClip *clip, Transport *transport);
void audio_engine_shutdown(AudioEngine *engine);
void audio_engine_set_metronome(AudioEngine *engine, bool enabled);
void audio_engine_toggle_metronome(AudioEngine *engine);
void audio_engine_set_playhead(AudioEngine *engine, uint64_t frame);
uint64_t audio_engine_get_playhead_frame(AudioEngine *engine);
void audio_engine_nudge_loop(AudioEngine *engine, bool move_start, int64_t delta);
