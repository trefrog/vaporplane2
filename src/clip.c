#include "clip.h"
#include "audio_file.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static void clip_defaults(AudioClip *clip) {
    clip->source_bpm = 120.0;
    clip->has_clip_metadata_bpm = false;
    clip->clip_metadata_bpm = 120.0;
    clip->clip_tempo_locked = false;
    clip->tempo_lock.bpm = 120.0;
    clip->tempo_lock.downbeat_frame = clip->loop_start_frame;
    clip->tempo_lock.beats_per_bar = 4;
    clip->tempo_lock.beat_unit = 4;
    clip->tempo_lock.target_bars = 4.0;
    clip->has_full_source_tempo_metadata = false;
    clip->full_source_tempo_metadata = clip->tempo_lock;
    clip->beats_per_bar = 4;
    clip->beat_unit = 4;
    clip->downbeat_frame = clip->loop_start_frame;
    clip->playback_rate = 1.0;
    clip->gain = 1.0f;
}

static bool json_number_value(const char *json, const char *key, double *out) {
    char needle[64];
    SDL_snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *p = SDL_strstr(json, needle);
    if (!p) return false;
    p = SDL_strchr(p, ':');
    if (!p) return false;
    p++;
    char *end = NULL;
    double value = strtod(p, &end);
    if (end == p) return false;
    *out = value;
    return true;
}

static void clip_load_sidecar_metadata(AudioClip *clip, const char *path) {
    char sidecar[CLIP_MAX_PATH + 6];
    SDL_snprintf(sidecar, sizeof(sidecar), "%s.json", path);

    size_t size = 0;
    void *bytes = SDL_LoadFile(sidecar, &size);
    if (!bytes) return;

    char *json = (char *)SDL_malloc(size + 1);
    if (!json) {
        SDL_free(bytes);
        return;
    }
    SDL_memcpy(json, bytes, size);
    json[size] = '\0';
    SDL_free(bytes);

    double value = 0.0;
    bool has_sidecar_bpm = json_number_value(json, "bpm", &value) && value > 0.0;
    if (has_sidecar_bpm) {
        clip->has_clip_metadata_bpm = true;
        clip->clip_metadata_bpm = value;
        clip->source_bpm = value;
        clip->tempo_lock.bpm = value;
    }
    if (json_number_value(json, "beats_per_bar", &value) && value >= 1.0) {
        clip->beats_per_bar = (int)value;
        clip->tempo_lock.beats_per_bar = (int)value;
    }
    if (json_number_value(json, "beat_unit", &value) && value >= 1.0) {
        clip->beat_unit = (int)value;
        clip->tempo_lock.beat_unit = (int)value;
    }
    bool has_sidecar_target_bars = json_number_value(json, "target_bars", &value) && value > 0.0;
    if (has_sidecar_target_bars) {
        clip->tempo_lock.target_bars = value;
    }
    if (json_number_value(json, "downbeat_frame", &value) && value >= 0.0) {
        size_t frame = (size_t)value;
        if (frame < clip->frame_count) {
            clip->downbeat_frame = frame;
            clip->tempo_lock.downbeat_frame = frame;
        }
    }

    if (clip->has_clip_metadata_bpm) {
        clip->clip_tempo_locked = true;
    }
    if (has_sidecar_bpm && has_sidecar_target_bars &&
        clip->tempo_lock.beats_per_bar > 0 &&
        clip->tempo_lock.beat_unit > 0 &&
        clip->tempo_lock.target_bars > 0.0) {
        clip->has_full_source_tempo_metadata = true;
        clip->full_source_tempo_metadata = clip->tempo_lock;
    }

    SDL_free(json);
}

bool clip_init_from_audio_file(AudioClip *clip, const char *path) {
    memset(clip, 0, sizeof(*clip));
    AudioFileData decoded;
    if (!audio_file_decode(&decoded, path)) return false;
    if (decoded.sample_rate <= 0 || decoded.channels <= 0 ||
        decoded.frame_count == 0 || !decoded.samples) {
        audio_file_data_destroy(&decoded);
        return false;
    }

    Uint8 *converted = NULL; int converted_len = 0;
    Uint64 decoded_bytes = (Uint64)decoded.frame_count * (Uint64)decoded.channels * sizeof(float);
    if (decoded_bytes > (Uint64)INT_MAX) {
        audio_file_data_destroy(&decoded);
        return false;
    }
    SDL_AudioSpec src = { .format = SDL_AUDIO_F32, .channels = decoded.channels, .freq = decoded.sample_rate };
    SDL_AudioSpec dst = { .format = SDL_AUDIO_F32, .channels = 2, .freq = decoded.sample_rate };
    if (!SDL_ConvertAudioSamples(&src, (const Uint8 *)decoded.samples, (int)decoded_bytes, &dst, &converted, &converted_len)) {
        audio_file_data_destroy(&decoded); return false;
    }
    audio_file_data_destroy(&decoded);
    if (converted_len <= 0 || (converted_len % (int)(sizeof(float) * (size_t)dst.channels)) != 0) {
        SDL_free(converted);
        return false;
    }

    clip->sample_rate = dst.freq;
    clip->channels = dst.channels;
    clip->frame_count = (size_t)converted_len / (sizeof(float) * (size_t)clip->channels);
    clip->samples = (float*)converted;
    SDL_strlcpy(clip->file_path, path, sizeof(clip->file_path));
    clip->loop_start_frame = 0;
    clip->loop_end_frame = clip->frame_count;
    clip_defaults(clip);
    clip_load_sidecar_metadata(clip, path);
    return true;
}

bool clip_init_from_wav(AudioClip *clip, const char *path) {
    return clip_init_from_audio_file(clip, path);
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
