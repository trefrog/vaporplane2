#include "audio_engine.h"
#include <string.h>

static void SDLCALL feed_audio(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount){
    (void)total_amount;
    AudioEngine *a = (AudioEngine*)userdata;
    int frames = additional_amount / (int)(sizeof(float)*2);
    float *mix = (float*)SDL_malloc((size_t)frames * sizeof(float) * 2);
    for(int i=0;i<frames;i++){
        float s = 0.f;
        if (a->clip && a->clip->samples && a->clip->frame_count>1 && a->transport->playing) {
            size_t loop_start=a->clip->loop_start_frame, loop_end=a->clip->loop_end_frame;
            if (a->playhead_frame >= loop_end) a->playhead_frame = (double)loop_start;
            size_t frame=(size_t)a->playhead_frame;
            size_t next=frame+1<loop_end?frame+1:loop_start;
            double frac=a->playhead_frame-(double)frame;
            float l0=a->clip->samples[frame*2], l1=a->clip->samples[next*2];
            s = (float)((1.0-frac)*l0 + frac*l1) * a->clip->gain;
            a->playhead_frame += a->clip->playback_rate;
        }
        transport_update(a->transport, 1.0/(double)a->spec.freq);
        float m = transport_next_metronome_sample(a->transport, a->spec.freq);
        float out = (s + m) * a->master_gain;
        mix[i*2]=out; mix[i*2+1]=out;
    }
    SDL_PutAudioStreamData(stream, mix, additional_amount);
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
void audio_engine_set_playhead(AudioEngine *a,size_t frame){ a->playhead_frame=(double)frame; }
size_t audio_engine_get_playhead_frame(const AudioEngine *a){ return (size_t)a->playhead_frame; }
