#include "clip.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void clip_defaults(AudioClip *c) {
    c->source_bpm = 120.0;
    c->beats_per_bar = 4;
    c->beat_unit = 4;
    c->downbeat_frame = 0;
    c->playback_rate = 1.0;
    c->gain = 0.9f;
}

static bool clip_generate(AudioClip *c) {
    c->sample_rate = 48000; c->channels = 1; c->frame_count = c->sample_rate * 2;
    c->samples = SDL_calloc((size_t)c->frame_count, sizeof(float));
    if (!c->samples) return false;
    for (uint64_t i = 0; i < c->frame_count; ++i) {
        double t = (double)i / c->sample_rate;
        c->samples[i] = 0.45f * (float)(sin(2.0 * M_PI * 220.0 * t) + 0.45 * sin(2.0 * M_PI * 330.0 * t));
    }
    SDL_strlcpy(c->file_path, "generated://fallback", sizeof(c->file_path));
    clip_defaults(c); clip_reset_loop(c); return true;
}

bool clip_load_or_generate(AudioClip *c, const char *path) {
    SDL_zero(*c);
    SDL_AudioSpec spec; Uint8 *buf = NULL; Uint32 len = 0;
    if (path && SDL_LoadWAV(path, &spec, &buf, &len)) {
        SDL_AudioSpec dst_spec = {.format=SDL_AUDIO_F32,.channels=spec.channels,.freq=spec.freq};
        int dst_len = 0;
        if (!SDL_ConvertAudioSamples(&spec, buf, (int)len, &dst_spec, (Uint8**)&c->samples, &dst_len)) {
            SDL_free(buf);
            return clip_generate(c);
        }
        c->sample_rate = dst_spec.freq; c->channels = dst_spec.channels;
        c->frame_count = (uint64_t)dst_len / (uint64_t)(sizeof(float) * c->channels);
        SDL_free(buf);
        SDL_strlcpy(c->file_path, path, sizeof(c->file_path));
        clip_defaults(c); clip_reset_loop(c); return true;
    }
    return clip_generate(c);
}

void clip_destroy(AudioClip *c) { SDL_free(c->samples); SDL_zero(*c); }
void clip_reset_loop(AudioClip *c) { c->loop_start_frame = 0; c->loop_end_frame = c->frame_count ? c->frame_count - 1 : 0; }
void clip_move_loop_start(AudioClip *c, int64_t d) { int64_t n=(int64_t)c->loop_start_frame+d; if(n<0)n=0; if(n>=(int64_t)c->loop_end_frame)n=(int64_t)c->loop_end_frame-1; c->loop_start_frame=(uint64_t)n; }
void clip_move_loop_end(AudioClip *c, int64_t d) { int64_t n=(int64_t)c->loop_end_frame+d; if(n<=(int64_t)c->loop_start_frame)n=(int64_t)c->loop_start_frame+1; if(n>=(int64_t)c->frame_count)n=(int64_t)c->frame_count-1; c->loop_end_frame=(uint64_t)n; }
