#include "audio_engine.h"
#include <math.h>
#include <string.h>

static float clip_sample_at(const AudioClip *clip, double frame, int channel, size_t loop_start, size_t loop_end) {
    double loop_len = (double)(loop_end - loop_start);
    while (frame < (double)loop_start) frame += loop_len;
    while (frame >= (double)loop_end) frame -= loop_len;

    size_t i0 = (size_t)frame;
    size_t i1 = i0 + 1 < loop_end ? i0 + 1 : loop_start;
    double frac = frame - (double)i0;
    float s0 = clip->samples[i0 * (size_t)clip->channels + (size_t)channel];
    float s1 = clip->samples[i1 * (size_t)clip->channels + (size_t)channel];
    return (float)((1.0 - frac) * s0 + frac * s1);
}

static int64_t metronome_beat_for_frame(const Transport *t, const AudioClip *clip, double frame, double frames_per_beat) {
    (void)clip;
    double rel = frame - (double)t->metronome_downbeat_frame;
    return (int64_t)floor(rel / frames_per_beat);
}

static bool frame_is_close_to_beat(const Transport *t, double frame, double frames_per_beat, double tolerance_frames) {
    double rel = frame - (double)t->metronome_downbeat_frame;
    if(rel < 0.0) return false;
    double beat_pos = floor(rel / frames_per_beat) * frames_per_beat;
    return fabs(rel - beat_pos) <= tolerance_frames;
}

static void update_frame_metronome(AudioEngine *a, double frame) {
    Transport *t = a->transport;
    const AudioClip *clip = a->clip;
    if(!t->playing || !t->metronome_enabled || !clip || clip->sample_rate <= 0 || t->bpm <= 0.0) return;

    double frames_per_beat = (60.0 / t->bpm) * (double)clip->sample_rate;
    if(frames_per_beat <= 1.0) return;

    int64_t beat = metronome_beat_for_frame(t, clip, frame, frames_per_beat);
    double rel = frame - (double)t->metronome_downbeat_frame;
    if(!a->metronome_beat_valid) {
        a->last_metronome_beat = beat;
        a->metronome_beat_valid = true;
        if(frame_is_close_to_beat(t, frame, frames_per_beat, fmax(1.0, a->clip->playback_rate))) {
            transport_trigger_metronome_beat(t, beat);
        }
        return;
    }

    if(rel >= 0.0 && beat != a->last_metronome_beat) {
        transport_trigger_metronome_beat(t, beat);
    }
    a->last_metronome_beat = beat;
}

static void SDLCALL feed_audio(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount){
    (void)total_amount;
    AudioEngine *a = (AudioEngine*)userdata;
    int frames = additional_amount / (int)(sizeof(float)*2);
    if(frames <= 0) return;
    float *mix = (float*)SDL_malloc((size_t)frames * sizeof(float) * 2);
    if(!mix) return;
    for(int i=0;i<frames;i++){
        float left = 0.f, right = 0.f;
        if (a->clip && a->clip->samples && a->clip->frame_count>1 && a->transport->playing) {
            size_t loop_start=a->clip->loop_start_frame, loop_end=a->clip->loop_end_frame;
            if(loop_end > a->clip->frame_count) loop_end = a->clip->frame_count;
            if(loop_start + 1 >= loop_end) loop_start = 0;
            if (a->playhead_frame >= loop_end) a->playhead_frame = (double)loop_start;
            if (a->playhead_frame < loop_start) a->playhead_frame = (double)loop_start;
            update_frame_metronome(a, a->playhead_frame);

            left = clip_sample_at(a->clip, a->playhead_frame, 0, loop_start, loop_end);
            right = clip_sample_at(a->clip, a->playhead_frame, 1, loop_start, loop_end);

            size_t loop_len = loop_end - loop_start;
            size_t fade_frames = loop_len / 2 < 64 ? loop_len / 2 : 64;
            double distance_to_end = (double)loop_end - a->playhead_frame;
            if(fade_frames > 0 && distance_to_end < (double)fade_frames) {
                double blend = 1.0 - distance_to_end / (double)fade_frames;
                double wrap_frame = (double)loop_start + ((double)fade_frames - distance_to_end);
                float wrap_left = clip_sample_at(a->clip, wrap_frame, 0, loop_start, loop_end);
                float wrap_right = clip_sample_at(a->clip, wrap_frame, 1, loop_start, loop_end);
                left = (float)(left * (1.0 - blend) + wrap_left * blend);
                right = (float)(right * (1.0 - blend) + wrap_right * blend);
            }

            left *= a->clip->gain;
            right *= a->clip->gain;
            a->playhead_frame += a->clip->playback_rate;
            while (a->playhead_frame >= (double)loop_end) a->playhead_frame -= (double)(loop_end - loop_start);
            if (a->playhead_frame < (double)loop_start) a->playhead_frame = (double)loop_start;
        } else {
            a->metronome_beat_valid = false;
        }
        transport_update(a->transport, 1.0/(double)a->spec.freq);
        float m = transport_next_metronome_sample(a->transport, a->spec.freq);
        mix[i*2]=(left + m) * a->master_gain;
        mix[i*2+1]=(right + m) * a->master_gain;
    }
    SDL_PutAudioStreamData(stream, mix, frames * (int)sizeof(float) * 2);
    SDL_free(mix);
}

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport){
    memset(a,0,sizeof(*a)); a->clip=clip;a->transport=transport;a->master_gain=0.9f; a->playhead_frame=0;
    a->spec.format=SDL_AUDIO_F32; a->spec.channels=2; a->spec.freq=48000;
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &a->spec, feed_audio, a);
    if(!a->stream) return false;
    if (!SDL_ResumeAudioStreamDevice(a->stream)) return false;
    return true;
}
void audio_engine_shutdown(AudioEngine *a){ if(a->stream) SDL_DestroyAudioStream(a->stream); memset(a,0,sizeof(*a)); }
void audio_engine_set_playhead(AudioEngine *a,size_t frame){
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->playhead_frame=(double)frame;
    a->metronome_beat_valid=false;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}
size_t audio_engine_get_playhead_frame(const AudioEngine *a){
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    size_t frame = (size_t)a->playhead_frame;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
    return frame;
}
