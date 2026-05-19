#include "transport.h"

static double sec_per_tick(const Transport *t) {
    return (60.0 / t->bpm) / (double)t->ppqn;
}

void transport_init(Transport *t, double bpm, uint16_t ppqn, int beats_per_bar, int beat_unit) {
    *t = (Transport){0};
    t->bpm = bpm;
    t->ppqn = ppqn;
    t->beats_per_bar = beats_per_bar;
    t->beat_unit = beat_unit;
}

void transport_set_playing(Transport *t, bool playing) { t->playing = playing; }
void transport_toggle_play(Transport *t) { t->playing = !t->playing; }

double transport_tick_to_seconds(const Transport *t, uint64_t tick) { return (double)tick * sec_per_tick(t); }
uint64_t transport_seconds_to_tick(const Transport *t, double sec) {
    if (sec <= 0.0) return 0;
    return (uint64_t)(sec / sec_per_tick(t));
}

void transport_seek_seconds(Transport *t, double seconds) {
    t->current_seconds = seconds < 0.0 ? 0.0 : seconds;
    t->current_tick = transport_seconds_to_tick(t, t->current_seconds);
}

void transport_update(Transport *t, double dt_sec) {
    t->beat_pulse = false;
    t->downbeat_pulse = false;
    if (!t->playing || dt_sec <= 0.0) return;
    uint64_t prev_beat = t->current_tick / t->ppqn;
    t->current_seconds += dt_sec;
    t->current_tick = transport_seconds_to_tick(t, t->current_seconds);
    uint64_t cur_beat = t->current_tick / t->ppqn;
    if (cur_beat != prev_beat) {
        t->beat_pulse = true;
        t->beat_counter = cur_beat;
        t->downbeat_pulse = ((cur_beat % (uint64_t)t->beats_per_bar) == 0);
    }
}
