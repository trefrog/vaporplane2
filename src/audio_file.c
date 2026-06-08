#include "audio_file.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#ifdef VAPORPLANE_HAVE_SNDFILE
#include <sndfile.h>
#endif

static bool checked_audio_byte_count(size_t frame_count, int channels, size_t *out) {
    if (!out || frame_count == 0 || channels <= 0) return false;
    size_t channel_count = (size_t)channels;
    if (frame_count > SIZE_MAX / channel_count) return false;
    size_t sample_count = frame_count * channel_count;
    if (sample_count > SIZE_MAX / sizeof(float)) return false;
    *out = sample_count * sizeof(float);
    return true;
}

static bool audio_file_decode_wav(AudioFileData *audio, const char *path) {
    SDL_AudioSpec src_spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &src_spec, &buf, &len)) return false;
    if (src_spec.channels <= 0 || src_spec.freq <= 0) {
        SDL_free(buf);
        return false;
    }

    Uint8 *converted = NULL;
    int converted_len = 0;
    SDL_AudioSpec dst = {
        .format = SDL_AUDIO_F32,
        .channels = src_spec.channels,
        .freq = src_spec.freq
    };
    if (!SDL_ConvertAudioSamples(&src_spec, buf, (int)len, &dst, &converted, &converted_len)) {
        SDL_free(buf);
        return false;
    }
    SDL_free(buf);

    size_t frame_bytes = sizeof(float) * (size_t)dst.channels;
    if (converted_len <= 0 || ((size_t)converted_len % frame_bytes) != 0) {
        SDL_free(converted);
        return false;
    }

    audio->sample_rate = dst.freq;
    audio->channels = dst.channels;
    audio->frame_count = (size_t)converted_len / frame_bytes;
    audio->samples = (float *)converted;
    return true;
}

#ifdef VAPORPLANE_HAVE_SNDFILE
static bool audio_file_decode_sndfile(AudioFileData *audio, const char *path) {
    SF_INFO info;
    SDL_memset(&info, 0, sizeof(info));
    SNDFILE *file = sf_open(path, SFM_READ, &info);
    if (!file) return false;
    if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0) {
        sf_close(file);
        return false;
    }
    if ((Uint64)info.frames > (Uint64)SIZE_MAX) {
        sf_close(file);
        return false;
    }

    size_t bytes = 0;
    if (!checked_audio_byte_count((size_t)info.frames, info.channels, &bytes)) {
        sf_close(file);
        return false;
    }

    float *samples = (float *)SDL_malloc(bytes);
    if (!samples) {
        sf_close(file);
        return false;
    }

    sf_count_t frames_read = sf_readf_float(file, samples, info.frames);
    sf_close(file);
    if (frames_read <= 0) {
        SDL_free(samples);
        return false;
    }

    audio->sample_rate = info.samplerate;
    audio->channels = info.channels;
    audio->frame_count = (size_t)frames_read;
    audio->samples = samples;
    return true;
}
#endif

bool audio_file_decode(AudioFileData *audio, const char *path) {
    if (!audio) return false;
    SDL_memset(audio, 0, sizeof(*audio));
    if (!path || !path[0]) return false;

#ifdef VAPORPLANE_HAVE_SNDFILE
    if (audio_file_decode_sndfile(audio, path)) return true;
    audio_file_data_destroy(audio);
#endif

    return audio_file_decode_wav(audio, path);
}

void audio_file_data_destroy(AudioFileData *audio) {
    if (!audio) return;
    SDL_free(audio->samples);
    SDL_memset(audio, 0, sizeof(*audio));
}
