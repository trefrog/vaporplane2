#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double bpm;
    uint16_t ppqn;
    int beats_per_bar;
    int beat_unit;

    double current_seconds;
    uint64_t current_tick;
    bool playing;

    bool metronome_enabled;
    uint64_t next_beat_tick;
    float metronome_env;
    double metronome_phase;
    bool metronome_downbeat;
} Transport;

void transport_init(Transport *t, double bpm, uint16_t ppqn, int beats_per_bar, int beat_unit);
void transport_update(Transport *t, double dt);
void transport_set_playing(Transport *t, bool playing);
double transport_tick_to_seconds(const Transport *t, uint64_t tick);
uint64_t transport_seconds_to_tick(const Transport *t, double seconds);
void transport_jump_to_seconds(Transport *t, double seconds);
float transport_next_metronome_sample(Transport *t, int sample_rate);
