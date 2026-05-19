#include "transport.h"
#include <math.h>

void transport_init(Transport *t, double bpm, uint16_t ppqn, int beats_per_bar, int beat_unit){
    t->bpm=bpm;t->ppqn=ppqn;t->beats_per_bar=beats_per_bar;t->beat_unit=beat_unit;
    t->current_seconds=0;t->current_tick=0;t->playing=false;t->metronome_enabled=true;t->next_beat_tick=0;t->metronome_env=0;t->metronome_phase=0;t->metronome_downbeat=true;
}

double transport_tick_to_seconds(const Transport *t, uint64_t tick){ return (60.0/t->bpm)*((double)tick/(double)t->ppqn); }
uint64_t transport_seconds_to_tick(const Transport *t,double s){ return (uint64_t)llround((s*t->bpm/60.0)*(double)t->ppqn); }

void transport_update(Transport *t,double dt){ if(!t->playing)return; t->current_seconds += dt; t->current_tick = transport_seconds_to_tick(t,t->current_seconds);} 
void transport_set_playing(Transport *t,bool p){ t->playing=p; }
void transport_jump_to_seconds(Transport *t,double s){ if(s<0)s=0; t->current_seconds=s; t->current_tick=transport_seconds_to_tick(t,s); t->next_beat_tick=(t->current_tick/t->ppqn)*t->ppqn; }
float transport_next_metronome_sample(Transport *t, int sample_rate){
    if(!t->metronome_enabled) return 0.f;
    if (t->current_tick >= t->next_beat_tick) { t->metronome_env = 1.0f; uint64_t beat=t->next_beat_tick/t->ppqn; t->metronome_downbeat=(beat%t->beats_per_bar)==0; t->next_beat_tick += t->ppqn; }
    if (t->metronome_env < 0.0005f) return 0.f;
    float freq=t->metronome_downbeat?1400.f:1000.f; t->metronome_phase += 2.0*M_PI*freq/(double)sample_rate; if(t->metronome_phase>2.0*M_PI) t->metronome_phase -= 2.0*M_PI;
    float out = sinf((float)t->metronome_phase)*t->metronome_env*0.2f; t->metronome_env*=0.995f; return out;
}
