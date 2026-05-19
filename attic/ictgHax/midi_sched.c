/*
 * MIDI scheduler that converts routed events into wall-clock playback for the
 * audio thread. It tracks tempo segments, locks to transport state, and drains
 * timed note events into the MidiRouteSink supplied by the router. All entry
 * points expect to run off the realtime callback; the sink handles dispatch to
 * the audio engine. Non-goal: synthesizer voice management stays outside.
 */

#include "midi_sched.h"
#include "audio/midi_tracks.h"
#include <string.h>

typedef enum { EV_ON, EV_OFF, EV_PITCH, EV_RANGE } EvType;

typedef struct {
    double when_sec;
    EvType type;
    int ch, note;
    float vel;         // ON only
    int16_t bend;      // pitch bend amount
    float bend_range;  // semitones for range change
    MidiRouteExtras x;
} Ev;

typedef struct {
    uint32_t tick0;        // tick where this tempo takes effect
    uint32_t usec_per_qn;  // tempo value
    double   sec_at_tick0; // absolute seconds at tick0
} TempoSeg;

#ifndef MIDI_SCHED_MAX_EVENTS
#define MIDI_SCHED_MAX_EVENTS 16384
#endif
#define MIDI_SCHED_MAX_TEMPO  128

static struct {
    uint16_t ppqn;
    double   playback_origin_sec;
    double   playback_pos_sec;

    MidiRouteSink sink;

    // tempo map
    TempoSeg tmap[MIDI_SCHED_MAX_TEMPO];
    int tcount;

    uint32_t now_tick;   // current song tick (derived from wall clock)
    bool     locked;
    uint32_t lock_usec_per_qn;
    uint32_t lock_tick;   // tick at which lock engaged
    double   lock_sec;    // absolute seconds at lock_tick

    Ev q[MIDI_SCHED_MAX_EVENTS];
    int count;
    bool started;
} G;

static double tick_to_sec_with_map(uint32_t tick) {
    // Find last segment with tick0 <= tick
    int i = G.tcount - 1;
    while (i > 0 && G.tmap[i].tick0 > tick) i--;
    const TempoSeg *s = &G.tmap[i];
    double dticks = (double)(tick - s->tick0);
    double sec_per_tick = ((double)s->usec_per_qn / 1e6) / (double)G.ppqn;
    return s->sec_at_tick0 + dticks * sec_per_tick;
}

static uint32_t sec_to_tick_with_map(double secs_since_start) {
    if (secs_since_start <= 0.0 || G.tcount == 0) return 0;

    // Find last segment with sec_at_tick0 <= secs
    int i = 0;
    for (int j = 1; j < G.tcount; ++j) {
        if (G.tmap[j].sec_at_tick0 <= secs_since_start) i = j; else break;
    }
    const TempoSeg *s = &G.tmap[i];
    double sec_per_tick = ((double)s->usec_per_qn / 1e6) / (double)G.ppqn;
    double dt_sec = secs_since_start - s->sec_at_tick0;
    if (dt_sec < 0.0) dt_sec = 0.0;
    double dt_ticks = dt_sec / sec_per_tick;
    double tick = (double)s->tick0 + dt_ticks;
    if (tick < 0.0) tick = 0.0;
    if (tick > 4294967295.0) tick = 4294967295.0;
    return (uint32_t)(tick + 0.5); // round to nearest
}

double midi_sched_tick_to_seconds(uint32_t tick) {
    return tick_to_sec_with_map(tick);
}

static void rebuild_sec_at_tick0(void) {
    // Recompute sec_at_tick0 for all segments from start (tick 0 = 0 sec)
    double acc_sec = 0.0;
    for (int i = 0; i < G.tcount; ++i) {
        TempoSeg *s = &G.tmap[i];
        if (i == 0) { s->sec_at_tick0 = 0.0; continue; }
        TempoSeg *p = &G.tmap[i-1];
        double sec_per_tick_prev = ((double)p->usec_per_qn / 1e6) / (double)G.ppqn;
        uint32_t dticks = s->tick0 - p->tick0;
        acc_sec += dticks * sec_per_tick_prev;
        s->sec_at_tick0 = acc_sec;
    }
}

static double tick_to_sec_locked(uint32_t tick) {
    double spt = ((double)G.lock_usec_per_qn / 1e6) / (double)G.ppqn;
    if (!G.locked || tick <= G.lock_tick) return tick_to_sec_with_map(tick);
    return G.lock_sec + (double)(tick - G.lock_tick) * spt;
}

static uint32_t sec_to_tick_locked(double secs_since_start) {
    if (!G.locked || secs_since_start <= G.lock_sec)
        return sec_to_tick_with_map(secs_since_start);
    double spt = ((double)G.lock_usec_per_qn / 1e6) / (double)G.ppqn;
    double dt  = secs_since_start - G.lock_sec;
    double t   = (double)G.lock_tick + dt / spt;
    if (t < 0.0) t = 0.0;
    if (t > 4294967295.0) t = 4294967295.0;
    return (uint32_t)(t + 0.5);
}

void midi_sched_init(uint16_t ppqn, uint32_t usec_per_qn, const MidiRouteSink* real_sink) {
    memset(&G, 0, sizeof(G));
    G.ppqn = ppqn ? ppqn : 480;
    if (real_sink) G.sink = *real_sink;
    // default tempo segment at tick 0
    G.tmap[0] = (TempoSeg){ .tick0 = 0, .usec_per_qn = usec_per_qn ? usec_per_qn : 500000, .sec_at_tick0 = 0.0 };
    G.tcount = 1;
}


// Recompute when_sec for all queued events at/after change_tick, then resort.
static void retime_queue_from_tick(uint32_t change_tick) {
    for (int i = 0; i < G.count; ++i) {
        uint32_t ev_tick = G.q[i].x.tick;
        if (ev_tick >= change_tick) {
            G.q[i].when_sec = G.locked ? tick_to_sec_locked(ev_tick)
                                       : tick_to_sec_with_map(ev_tick);
        }
    }
    // Resort by when_sec (stable-ish is nice but not required here)
    // Simple insertion sort is fine for small queues; qsort is fine too.
    for (int i = 1; i < G.count; ++i) {
        Ev key = G.q[i];
        int j = i - 1;
        while (j >= 0) {
            bool swap = false;
            if (G.q[j].when_sec > key.when_sec) {
                swap = true;
            } else if (G.q[j].when_sec == key.when_sec) {
                if (key.type == EV_RANGE && G.q[j].type != EV_RANGE) {
                    swap = true;
                }
            }
            if (!swap) break;
            G.q[j + 1] = G.q[j];
            --j;
        }
        G.q[j + 1] = key;
    }
}

void midi_sched_retime_from(uint32_t tick){ retime_queue_from_tick(tick); }


void midi_sched_set_tempo(uint32_t usec_per_qn) {
    // Append a change at "now" tick? No—scheduler doesn’t know current tick.
    // Called from parser with exact tick via router shim below, so we keep this
    // legacy signature only for initial set; real changes go through midi_sched_set_tempo_at().
    if (G.tcount == 0) {
        G.tmap[0] = (TempoSeg){ .tick0=0, .usec_per_qn = usec_per_qn ? usec_per_qn : 500000, .sec_at_tick0=0.0 };
        G.tcount = 1;
    } else {
        G.tmap[0].usec_per_qn = usec_per_qn ? usec_per_qn : 500000;
    }
    rebuild_sec_at_tick0();
}

// New: tempo change at specific tick
void midi_sched_set_tempo_at(uint32_t tick, uint32_t usec_per_qn) {
    // If locked, force the locked value regardless of caller
    // if (G.locked) usec_per_qn = G.lock_usec_per_qn;
    
    // MIDI tempo 0 is invalid and would cause divide-by-zero later.
    // Clamp to a sane default (120 BPM = 500000 usec per quarter note).
    if (usec_per_qn == 0) usec_per_qn = 500000;

    // If last segment is at the same tick, just update it (avoid spam)
    if (G.tcount > 0 && G.tmap[G.tcount-1].tick0 == tick) {
        G.tmap[G.tcount-1].usec_per_qn = usec_per_qn;
        rebuild_sec_at_tick0();
        retime_queue_from_tick(tick);
        return;
    }

    if (G.tcount < MIDI_SCHED_MAX_TEMPO) {
        G.tmap[G.tcount++] = (TempoSeg){ .tick0 = tick, .usec_per_qn = usec_per_qn, .sec_at_tick0 = 0.0 };
        // keep segments sorted
        for (int i = G.tcount-1; i > 0 && G.tmap[i-1].tick0 > G.tmap[i].tick0; --i) {
            TempoSeg tmp = G.tmap[i-1]; G.tmap[i-1] = G.tmap[i]; G.tmap[i] = tmp;
        }
        rebuild_sec_at_tick0();
        retime_queue_from_tick(tick);
    }
}

void midi_sched_start(double start_time_sec) {
    G.playback_origin_sec = start_time_sec;
    G.playback_pos_sec = start_time_sec;
    G.started = true;
    G.now_tick = 0;
}

static void enqueue_ev(const Ev* e) {
    if (G.count >= MIDI_SCHED_MAX_EVENTS) return;
    int i = G.count++;
    G.q[i] = *e;
    while (i > 0) {
        bool swap = false;
        if (G.q[i-1].when_sec > G.q[i].when_sec) {
            swap = true;
        } else if (G.q[i-1].when_sec == G.q[i].when_sec) {
            if (G.q[i].type == EV_RANGE && G.q[i-1].type != EV_RANGE) {
                swap = true;
            }
        }
        if (!swap) break;
        Ev tmp = G.q[i-1];
        G.q[i-1] = G.q[i];
        G.q[i] = tmp;
        --i;
    }
}

void midi_sched_enqueue_note_on(int ch, int note, float vel, const MidiRouteExtras* x) {
    if (!x) return;
    Ev e; e.type=EV_ON; e.ch=ch; e.note=note; e.vel=vel; e.bend=0; e.x=*x;
    e.when_sec = G.locked ? tick_to_sec_locked(x->tick)
                          : tick_to_sec_with_map(x->tick);
    enqueue_ev(&e);
}
void midi_sched_enqueue_note_off(int ch, int note, const MidiRouteExtras* x) {
    if (!x) return;
    Ev e; e.type=EV_OFF; e.ch=ch; e.note=note; e.vel=0.f; e.bend=0; e.x=*x;
    e.when_sec = G.locked ? tick_to_sec_locked(x->tick)
                          : tick_to_sec_with_map(x->tick);
    enqueue_ev(&e);
}

void midi_sched_enqueue_pitch_bend(int ch, int16_t bend, const MidiRouteExtras* x) {
    if (!x) return;
    Ev e;
    e.type = EV_PITCH;
    e.ch = ch;
    e.note = -1;
    e.vel = 0.0f;
    e.bend = bend;
    e.bend_range = 0.0f;
    e.x = *x;
    e.when_sec = G.locked ? tick_to_sec_locked(x->tick)
                          : tick_to_sec_with_map(x->tick);
    enqueue_ev(&e);
}

void midi_sched_enqueue_pitch_range(int ch, float semitones, const MidiRouteExtras* x) {
    if (!x) return;
    Ev e;
    e.type = EV_RANGE;
    e.ch = ch;
    e.note = -1;
    e.vel = 0.0f;
    e.bend = 0;
    e.bend_range = semitones;
    e.x = *x;
    e.when_sec = G.locked ? tick_to_sec_locked(x->tick)
                          : tick_to_sec_with_map(x->tick);
    enqueue_ev(&e);
}

void midi_sched_drain(double playback_time_sec) {
    if (!G.started) return;

    // If we've been asked to start in the future (quantized launches), wait until
    // the playback clock reaches that origin instead of clamping forward.
    if (playback_time_sec < G.playback_origin_sec) {
        G.playback_pos_sec = playback_time_sec;
        G.now_tick = 0;
        return;
    }

    G.playback_pos_sec = playback_time_sec;

    double elapsed = playback_time_sec - G.playback_origin_sec;
    if (elapsed < 0.0) elapsed = 0.0;

    G.now_tick = G.locked ? sec_to_tick_locked(elapsed)
                          : sec_to_tick_with_map(elapsed);

    int i = 0;
    while (i < G.count) {
        if (G.q[i].when_sec > elapsed) break;
        Ev e = G.q[i];
        memmove(&G.q[i], &G.q[i+1], (size_t)(G.count - (i+1)) * sizeof(Ev));
        G.count--;
        if (e.x.track >= 0 && !midi_tracks_should_play(e.x.track)) {
            continue;
        }
        if (e.type == EV_ON) {
            if (e.x.is_drums && G.sink.drum_on) G.sink.drum_on(e.ch, e.note, e.vel, &e.x);
            else if (G.sink.note_on) G.sink.note_on(e.ch, e.note, e.vel, &e.x);
        } else if (e.type == EV_OFF) {
            if (e.x.is_drums && G.sink.drum_off) G.sink.drum_off(e.ch, e.note, &e.x);
            else if (G.sink.note_off) G.sink.note_off(e.ch, e.note, &e.x);
        } else if (e.type == EV_PITCH) {
            if (G.sink.pitch_bend) G.sink.pitch_bend(e.ch, e.bend, &e.x);
        } else if (e.type == EV_RANGE) {
            if (G.sink.pitch_bend_range) G.sink.pitch_bend_range(e.ch, e.bend_range, &e.x);
        }
    }
}

uint32_t midi_sched_current_tick(void) {
    return G.now_tick;
}

bool midi_sched_has_pending(void) {
    return G.count > 0;
}

void midi_sched_set_tempo_lock(bool on, uint32_t usec_per_qn_if_on) {
    if (on && !G.locked) {
        G.lock_tick = G.now_tick;
        G.lock_sec  = tick_to_sec_with_map(G.lock_tick);
        G.lock_usec_per_qn = usec_per_qn_if_on ? usec_per_qn_if_on : 500000;
        G.locked = true;
    } else if (!on && G.locked) {
        G.locked = false;
        G.lock_tick = 0;  // clear wall
        G.lock_sec  = 0.0;
    }
}
bool midi_sched_is_tempo_locked(void) { return G.locked; }
