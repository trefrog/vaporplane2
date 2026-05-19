#include "audio_engine.h"
#include <math.h>

static void SDLCALL feed_audio(void *userdata, SDL_AudioStream *stream, int addl, int total) {
    (void)total;
    AudioEngine *a = userdata;
    int frames = addl / (int)sizeof(float) / a->out_spec.channels;
    float *mix = SDL_calloc((size_t)frames * a->out_spec.channels, sizeof(float));

    for (int i = 0; i < frames; ++i) {
        if (a->playing && a->clip && a->clip->samples) {
            uint64_t lo = a->clip->loop_start_frame, hi = a->clip->loop_end_frame;
            if (a->playhead_frame >= (double)hi) a->playhead_frame = (double)lo;
            uint64_t f = (uint64_t)a->playhead_frame;
            float s = a->clip->samples[f * a->clip->channels] * a->clip->gain;

            int dist_to_edge = (int)SDL_min((uint64_t)(f - lo), (uint64_t)(hi - f));
            if (dist_to_edge < 32) s *= (float)dist_to_edge / 32.0f;

            for (int ch = 0; ch < a->out_spec.channels; ++ch) mix[i * a->out_spec.channels + ch] = s;
            a->playhead_frame += a->clip->playback_rate;
        }

        if (a->transport->metronome_on && a->transport->playing) {
            double spb = transport_seconds_per_beat(a->transport);
            double beat_pos = fmod(a->transport->current_seconds + (double)i / a->out_spec.freq, spb);
            if (beat_pos < 1.0 / a->out_spec.freq) {
                uint64_t beat_idx = (uint64_t)((a->transport->current_seconds + (double)i / a->out_spec.freq) / spb);
                a->metro_freq = (beat_idx % a->transport->beats_per_bar == 0) ? 1320 : 880;
                a->metro_samples_left = a->out_spec.freq / 60;
                a->metro_phase = 0.0;
            }
        }
        if (a->metro_samples_left > 0) {
            float env = (float)a->metro_samples_left / (a->out_spec.freq / 60.0f);
            float pip = 0.2f * env * sinf((float)a->metro_phase);
            for (int ch = 0; ch < a->out_spec.channels; ++ch) mix[i * a->out_spec.channels + ch] += pip;
            a->metro_phase += 2.0 * M_PI * (double)a->metro_freq / a->out_spec.freq;
            a->metro_samples_left--;
        }
    }
    SDL_PutAudioStreamData(stream, mix, addl);
    SDL_free(mix);
}

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport) {
    SDL_zero(*a);
    a->clip = clip; a->transport = transport; a->playing = true; a->playhead_frame = clip->loop_start_frame;
    SDL_AudioSpec spec = {.format = SDL_AUDIO_F32, .channels = 2, .freq = 48000};
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_audio, a);
    if (!a->stream) return false;
    a->out_spec = spec;
    return SDL_ResumeAudioStreamDevice(a->stream);
}
void audio_engine_shutdown(AudioEngine *a) { if (a->stream) SDL_DestroyAudioStream(a->stream); }
void audio_engine_set_playing(AudioEngine *a, bool p) { a->playing = p; }
void audio_engine_jump_to_loop_start(AudioEngine *a) { a->playhead_frame = a->clip->loop_start_frame; }
uint64_t audio_engine_playhead_frame(const AudioEngine *a) { return (uint64_t)a->playhead_frame; }
