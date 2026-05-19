#include "audio_engine.h"
#include <math.h>
#include <string.h>

static float clip_sample(const AudioClip *c, uint64_t frame, int ch) {
    return c->samples[frame * c->channels + (ch % c->channels)];
}

static void SDLCALL audio_cb(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    (void)total_amount;
    AudioEngine *e = (AudioEngine *)userdata;
    int frames = additional_amount / (int)(sizeof(float) * 2);
    if (frames <= 0) return;
    float *out = (float *)SDL_stack_alloc(float, (size_t)frames * 2);
    if (!out) return;
    memset(out, 0, sizeof(float) * (size_t)frames * 2);

    SDL_LockMutex(e->lock);
    AudioClip *c = e->clip;
    Transport *t = e->transport;
    for (int i = 0; i < frames; ++i) {
        if (t->playing && c->samples && c->frame_count > 1) {
            uint64_t f0 = (uint64_t)e->playhead_frame;
            uint64_t loop_len = c->loop_end_frame - c->loop_start_frame + 1;
            if (f0 >= c->loop_end_frame) {
                f0 = c->loop_start_frame;
                e->playhead_frame = (double)f0;
            }
            uint64_t dist_to_end = c->loop_end_frame > f0 ? c->loop_end_frame - f0 : 0;
            float fade = 1.0f;
            if (dist_to_end < 64) fade = (float)dist_to_end / 64.0f;
            float l = clip_sample(c, f0, 0);
            float r = clip_sample(c, f0, 1);
            if (dist_to_end < 64 && loop_len > 64) {
                uint64_t wrap = c->loop_start_frame + (64 - dist_to_end - 1);
                float blend = 1.0f - fade;
                l = l * fade + clip_sample(c, wrap, 0) * blend;
                r = r * fade + clip_sample(c, wrap, 1) * blend;
            }
            out[i*2] += l * c->gain;
            out[i*2+1] += r * c->gain;

            e->playhead_frame += c->playback_rate;
            if ((uint64_t)e->playhead_frame > c->loop_end_frame) e->playhead_frame = (double)c->loop_start_frame;
        }

        if (e->metronome_enabled && t->playing) {
            uint64_t prev_tick = t->current_tick;
            t->current_seconds += 1.0 / (double)e->obtained.freq;
            t->current_tick = transport_seconds_to_tick(t, t->current_seconds);
            if ((t->current_tick / t->ppqn) != (prev_tick / t->ppqn)) {
                e->met_env = 1.0;
                e->met_phase = 0.0;
            }
            if (e->met_env > 0.0001) {
                int beat = (int)((t->current_tick / t->ppqn) % t->beats_per_bar);
                float freq = beat == 0 ? 1320.0f : 880.0f;
                float pip = (float)(sinf((float)e->met_phase) * e->met_env * 0.18);
                e->met_phase += 2.0 * M_PI * freq / (double)e->obtained.freq;
                e->met_env *= 0.992;
                out[i*2] += pip; out[i*2+1] += pip;
            }
        }
    }
    SDL_UnlockMutex(e->lock);

    SDL_PutAudioStreamData(stream, out, frames * (int)sizeof(float) * 2);
    SDL_stack_free(out);
}

bool audio_engine_init(AudioEngine *e, AudioClip *clip, Transport *transport) {
    memset(e, 0, sizeof(*e)); e->clip = clip; e->transport = transport; e->metronome_enabled = true;
    e->lock = SDL_CreateMutex();
    if (!e->lock) return false;
    SDL_AudioSpec want = {.format = SDL_AUDIO_F32, .channels = 2, .freq = clip->sample_rate > 0 ? clip->sample_rate : 48000};
    e->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, audio_cb, e);
    if (!e->stream) return false;
    e->device = SDL_GetAudioStreamDevice(e->stream);
    if (!e->device) return false;
    if (!SDL_GetAudioDeviceFormat(e->device, &e->obtained, NULL)) e->obtained = want;
    return SDL_ResumeAudioStreamDevice(e->stream);
}
void audio_engine_shutdown(AudioEngine *e) { if (e->stream) SDL_DestroyAudioStream(e->stream); if (e->lock) SDL_DestroyMutex(e->lock); }
void audio_engine_set_metronome(AudioEngine *e, bool enabled){ SDL_LockMutex(e->lock); e->metronome_enabled = enabled; SDL_UnlockMutex(e->lock);} 
void audio_engine_toggle_metronome(AudioEngine *e){ audio_engine_set_metronome(e,!e->metronome_enabled);} 
void audio_engine_set_playhead(AudioEngine *e, uint64_t frame){ SDL_LockMutex(e->lock); e->playhead_frame=(double)frame; SDL_UnlockMutex(e->lock);} 
uint64_t audio_engine_get_playhead_frame(AudioEngine *e){ SDL_LockMutex(e->lock); uint64_t f=(uint64_t)e->playhead_frame; SDL_UnlockMutex(e->lock); return f; }
void audio_engine_nudge_loop(AudioEngine *e, bool move_start, int64_t delta){ SDL_LockMutex(e->lock); AudioClip*c=e->clip; if(move_start){ int64_t ns=(int64_t)c->loop_start_frame+delta; if(ns<0)ns=0; if((uint64_t)ns>=c->loop_end_frame)ns=(int64_t)c->loop_end_frame-1; c->loop_start_frame=(uint64_t)ns; if(e->playhead_frame<c->loop_start_frame)e->playhead_frame=c->loop_start_frame;} else { int64_t ne=(int64_t)c->loop_end_frame+delta; if((uint64_t)ne>=c->frame_count)ne=(int64_t)c->frame_count-1; if((uint64_t)ne<=c->loop_start_frame)ne=(int64_t)c->loop_start_frame+1; c->loop_end_frame=(uint64_t)ne;} SDL_UnlockMutex(e->lock);} 
