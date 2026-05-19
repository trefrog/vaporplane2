#include "clip.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t clip_clamp_frame(const AudioClip *clip, int64_t frame) {
    if (frame < 0) return 0;
    if ((uint64_t)frame >= clip->frame_count) return clip->frame_count > 0 ? clip->frame_count - 1 : 0;
    return (uint64_t)frame;
}

void clip_reset_loop(AudioClip *clip) {
    clip->loop_start_frame = 0;
    clip->loop_end_frame = clip->frame_count > 1 ? clip->frame_count - 1 : 0;
}

static bool generate_demo(AudioClip *clip) {
    clip->sample_rate = 48000; clip->channels = 2; clip->frame_count = 48000 * 2;
    clip->samples = (float*)malloc(sizeof(float) * clip->frame_count * 2);
    if (!clip->samples) return false;
    for (uint64_t i = 0; i < clip->frame_count; ++i) {
        double t = (double)i / (double)clip->sample_rate;
        float env = (float)(0.5 + 0.5 * sin(2.0 * M_PI * 0.25 * t));
        float s = 0.35f * env * (sinf(2.0f * (float)M_PI * 220.0f * (float)t) + 0.4f * sinf(2.0f * (float)M_PI * 330.0f * (float)t));
        clip->samples[i*2+0] = s;
        clip->samples[i*2+1] = s;
    }
    SDL_strlcpy(clip->file_path, "generated://demo", sizeof(clip->file_path));
    return true;
}

bool clip_load_or_generate(AudioClip *clip, const char *preferred_path) {
    memset(clip, 0, sizeof(*clip));
    clip->source_bpm = 120.0; clip->beats_per_bar = 4; clip->beat_unit = 4; clip->playback_rate = 1.0; clip->gain = 0.9f;

    SDL_AudioSpec spec; Uint8 *buf = NULL; Uint32 len = 0;
    if (preferred_path && SDL_LoadWAV(preferred_path, &spec, &buf, &len)) {
        SDL_AudioSpec want = {.format = SDL_AUDIO_F32, .channels = 2, .freq = spec.freq};
        SDL_AudioStream *stream = SDL_CreateAudioStream(&spec, &want);
        if (stream && SDL_PutAudioStreamData(stream, buf, (int)len) && SDL_FlushAudioStream(stream)) {
            int avail = SDL_GetAudioStreamAvailable(stream);
            float *pcm = (float*)malloc((size_t)avail);
            if (pcm && SDL_GetAudioStreamData(stream, pcm, avail) == avail) {
                clip->sample_rate = want.freq; clip->channels = 2;
                clip->frame_count = (uint64_t)(avail / (int)(sizeof(float) * clip->channels));
                clip->samples = pcm;
                SDL_strlcpy(clip->file_path, preferred_path, sizeof(clip->file_path));
            } else { free(pcm); }
        }
        if (stream) SDL_DestroyAudioStream(stream);
        SDL_free(buf);
    }

    if (!clip->samples && !generate_demo(clip)) return false;
    clip_reset_loop(clip);
    return true;
}

void clip_free(AudioClip *clip) { free(clip->samples); clip->samples = NULL; }
