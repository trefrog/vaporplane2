#include "transport.h"

void transport_init(Transport *t, double bpm, int ppqn, int beats_per_bar, int beat_unit) {
    t->bpm = bpm;
    t->ppqn = ppqn;
    t->beats_per_bar = beats_per_bar;
    t->beat_unit = beat_unit;
    t->playing = true;
    t->metronome_on = true;
    t->current_seconds = 0.0;
    t->current_tick = 0;
    t->last_beat_index = 0;
    t->beat_pulse = false;
    t->downbeat_pulse = false;
}

double transport_seconds_per_beat(const Transport *t) { return 60.0 / t->bpm; }

double transport_tick_to_seconds(const Transport *t, uint64_t tick) {
    return (double)tick * (60.0 / t->bpm) / (double)t->ppqn;
}

uint64_t transport_seconds_to_tick(const Transport *t, double seconds) {
    if (seconds <= 0.0) return 0;
    return (uint64_t)(seconds * (double)t->ppqn * t->bpm / 60.0 + 0.5);
}

void transport_set_playing(Transport *t, bool playing) { t->playing = playing; }
void transport_toggle_playing(Transport *t) { t->playing = !t->playing; }
void transport_toggle_metronome(Transport *t) { t->metronome_on = !t->metronome_on; }

void transport_seek_seconds(Transport *t, double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    t->current_seconds = seconds;
    t->current_tick = transport_seconds_to_tick(t, seconds);
    t->last_beat_index = (uint64_t)(seconds / transport_seconds_per_beat(t));
}

void transport_update(Transport *t, double dt_seconds) {
    t->beat_pulse = false;
    t->downbeat_pulse = false;
    if (!t->playing) return;

    double before = t->current_seconds;
    t->current_seconds += dt_seconds;
    t->current_tick = transport_seconds_to_tick(t, t->current_seconds);

    double spb = transport_seconds_per_beat(t);
    uint64_t beat_before = (uint64_t)(before / spb);
    uint64_t beat_after = (uint64_t)(t->current_seconds / spb);
    if (beat_after > beat_before) {
        t->beat_pulse = true;
        t->last_beat_index = beat_after;
        t->downbeat_pulse = ((beat_after % (uint64_t)t->beats_per_bar) == 0);
    }
}
