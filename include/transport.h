#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double bpm;
    int ppqn;
    int beats_per_bar;
    int beat_unit;

    bool playing;
    bool metronome_on;

    double current_seconds;
    uint64_t current_tick;

    uint64_t last_beat_index;
    bool beat_pulse;
    bool downbeat_pulse;
} Transport;

void transport_init(Transport *t, double bpm, int ppqn, int beats_per_bar, int beat_unit);
void transport_set_playing(Transport *t, bool playing);
void transport_toggle_playing(Transport *t);
void transport_toggle_metronome(Transport *t);
void transport_update(Transport *t, double dt_seconds);
void transport_seek_seconds(Transport *t, double seconds);

double transport_tick_to_seconds(const Transport *t, uint64_t tick);
uint64_t transport_seconds_to_tick(const Transport *t, double seconds);
double transport_seconds_per_beat(const Transport *t);
