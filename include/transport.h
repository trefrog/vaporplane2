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

    uint64_t beat_counter;
    bool beat_pulse;
    bool downbeat_pulse;
} Transport;

void transport_init(Transport *t, double bpm, uint16_t ppqn, int beats_per_bar, int beat_unit);
void transport_set_playing(Transport *t, bool playing);
void transport_toggle_play(Transport *t);
void transport_update(Transport *t, double dt_sec);
void transport_seek_seconds(Transport *t, double seconds);
double transport_tick_to_seconds(const Transport *t, uint64_t tick);
uint64_t transport_seconds_to_tick(const Transport *t, double sec);
