#include "clip.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static void clip_defaults(AudioClip *clip) {
    clip->source_bpm = 120.0;
    clip->beats_per_bar = 4;
    clip->beat_unit = 4;
    clip->downbeat_frame = 0;
    clip->playback_rate = 1.0;
    clip->gain = 0.9f;
}

bool clip_init_from_wav(AudioClip *clip, const char *path) {
    memset(clip, 0, sizeof(*clip));
    SDL_AudioSpec src_spec; Uint8 *buf = NULL; Uint32 len = 0;
    if (!SDL_LoadWAV(path, &src_spec, &buf, &len)) return false;

    Uint8 *converted = NULL; int converted_len = 0;
    SDL_AudioSpec dst = { .format = SDL_AUDIO_F32, .channels = 2, .freq = src_spec.freq };
    if (!SDL_ConvertAudioSamples(&src_spec, buf, (int)len, &dst, &converted, &converted_len)) {
        SDL_free(buf); return false;
    }
    SDL_free(buf);

    clip->sample_rate = dst.freq;
    clip->channels = dst.channels;
    clip->frame_count = (size_t)converted_len / (sizeof(float) * (size_t)clip->channels);
    clip->samples = (float*)converted;
    SDL_strlcpy(clip->file_path, path, sizeof(clip->file_path));
    clip->loop_start_frame = 0;
    clip->loop_end_frame = clip->frame_count;
    clip_defaults(clip);
    return true;
}

void clip_init_generated(AudioClip *clip, int sample_rate, float seconds) {
    memset(clip, 0, sizeof(*clip));
    clip->sample_rate = sample_rate;
    clip->channels = 2;
    clip->frame_count = (size_t)(seconds * sample_rate);
    clip->samples = (float*)SDL_calloc(clip->frame_count * clip->channels, sizeof(float));
    SDL_strlcpy(clip->file_path, "generated://vaporplane", sizeof(clip->file_path));
    for (size_t i=0;i<clip->frame_count;i++) {
        float t = (float)i / (float)sample_rate;
        float env = fminf(1.0f, t * 4.0f) * fminf(1.0f, (seconds - t) * 4.0f);
        float s = 0.3f*sinf(2.0f*3.1415926f*220.0f*t) + 0.2f*sinf(2.0f*3.1415926f*330.0f*t);
        s += 0.15f*sinf(2.0f*3.1415926f*(110.0f+40.0f*sinf(2.0f*3.1415926f*0.25f*t))*t);
        s *= env;
        clip->samples[i*2] = s;
        clip->samples[i*2+1] = s;
    }
    clip->loop_start_frame = 0;
    clip->loop_end_frame = clip->frame_count;
    clip_defaults(clip);
}

void clip_destroy(AudioClip *clip) { SDL_free(clip->samples); memset(clip,0,sizeof(*clip)); }
void clip_reset_loop(AudioClip *clip){ clip->loop_start_frame=0; clip->loop_end_frame=clip->frame_count; }
void clip_clamp_loop(AudioClip *clip){ if(clip->loop_end_frame>clip->frame_count)clip->loop_end_frame=clip->frame_count; if(clip->loop_start_frame>=clip->loop_end_frame) clip->loop_start_frame=clip->loop_end_frame>1?clip->loop_end_frame-1:0; }
