#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "midi_router.h"   // for MidiRouteExtras & MidiRouteSink

// Initialize scheduler with timing and the *real* audio sink (your voice calls).
void midi_sched_init(uint16_t ppqn, uint32_t usec_per_qn, const MidiRouteSink* real_sink);

// Update tempo (called from SMF tempo callback).
void midi_sched_set_tempo(uint32_t usec_per_qn);
void midi_sched_set_tempo_at(uint32_t tick, uint32_t usec_per_qn);

void midi_sched_retime_from(uint32_t tick);

// Start a playback run; 'start_time_sec' is the playback clock value the
// audio thread will advance from (typically 0.0 after a reset). If this value
// lies in the future relative to the playback clock, events will wait until
// that time before draining.
void midi_sched_start(double start_time_sec);

// Enqueue events (called by the router-facing sink wrappers).
void midi_sched_enqueue_note_on (int ch, int note, float vel, const MidiRouteExtras* x);
void midi_sched_enqueue_note_off(int ch, int note, const MidiRouteExtras* x);
void midi_sched_enqueue_pitch_bend(int ch, int16_t bend, const MidiRouteExtras* x);
void midi_sched_enqueue_pitch_range(int ch, float semitones, const MidiRouteExtras* x);

// Pump due events from the realtime audio thread. Supply the playback clock
// position (seconds) that corresponds to the current sample before rendering
// it, and the scheduler will dispatch any events whose timestamps are <= that
// position.
void midi_sched_drain(double playback_time_sec);

// Utility for router: convert ticks→seconds using current tempo.
double midi_sched_tick_to_seconds(uint32_t tick);

uint32_t midi_sched_current_tick(void);
bool midi_sched_has_pending(void);
void midi_sched_set_tempo_lock(bool on, uint32_t usec_per_qn_if_on);
bool midi_sched_is_tempo_locked(void);
