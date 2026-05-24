#include "timeline.h"
#include <math.h>
#include <string.h>

#define TIMELINE_DEFAULT_BPM 120.0
#define TIMELINE_MIN_BPM 30.0
#define TIMELINE_MAX_BPM 300.0
#define TIMELINE_MIN_TAPE_SPEED 0.05
#define TIMELINE_MAX_TAPE_SPEED 4.0

static double clamp_bpm(double bpm) {
    if (bpm < TIMELINE_MIN_BPM) return TIMELINE_MIN_BPM;
    if (bpm > TIMELINE_MAX_BPM) return TIMELINE_MAX_BPM;
    return bpm;
}

static int timeline_ppqn(const MasterTimeline *timeline) {
    return timeline && timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat : 960;
}

static int tempo_event_count(const MasterTimeline *timeline) {
    if (!timeline || timeline->tempo_event_count <= 0) return 0;
    if (timeline->tempo_event_count > TIMELINE_MAX_TEMPO_EVENTS) return TIMELINE_MAX_TEMPO_EVENTS;
    return timeline->tempo_event_count;
}

static double tempo_event_bpm_at_index(const MasterTimeline *timeline, int index) {
    int count = tempo_event_count(timeline);
    if (index < 0 || index >= count) return TIMELINE_DEFAULT_BPM;
    double bpm = timeline->tempo_events[index].bpm;
    return bpm > 0.0 ? clamp_bpm(bpm) : TIMELINE_DEFAULT_BPM;
}

void timeline_tempo_map_init(MasterTimeline *timeline, double bpm, bool explicit_tempo) {
    if (!timeline) return;
    timeline->tempo_event_count = 1;
    timeline->tempo_events[0].tick = 0;
    timeline->tempo_events[0].bpm = clamp_bpm(bpm > 0.0 ? bpm : TIMELINE_DEFAULT_BPM);
    timeline->project_tempo_explicit = explicit_tempo;
    if (timeline->tape_speed <= 0.0f) timeline->tape_speed = 1.0f;
}

bool timeline_tempo_event_index_at_tick(const MasterTimeline *timeline, int64_t tick, int *index_out) {
    int count = tempo_event_count(timeline);
    for (int i = 0; i < count; ++i) {
        if (timeline->tempo_events[i].tick == tick) {
            if (index_out) *index_out = i;
            return true;
        }
    }
    if (index_out) *index_out = -1;
    return false;
}

bool timeline_set_tempo_event(MasterTimeline *timeline, int64_t tick, double bpm, bool explicit_tempo) {
    if (!timeline) return false;
    if (tick < 0) tick = 0;
    if (timeline->tempo_event_count <= 0) {
        timeline_tempo_map_init(timeline, bpm, explicit_tempo);
        return true;
    }

    int count = tempo_event_count(timeline);
    int existing = -1;
    if (timeline_tempo_event_index_at_tick(timeline, tick, &existing)) {
        timeline->tempo_events[existing].bpm = clamp_bpm(bpm);
        if (explicit_tempo) timeline->project_tempo_explicit = true;
        return true;
    }

    if (count >= TIMELINE_MAX_TEMPO_EVENTS) return false;

    int insert_at = count;
    for (int i = 0; i < count; ++i) {
        if (tick < timeline->tempo_events[i].tick) {
            insert_at = i;
            break;
        }
    }
    for (int i = count; i > insert_at; --i) {
        timeline->tempo_events[i] = timeline->tempo_events[i - 1];
    }
    timeline->tempo_events[insert_at].tick = tick;
    timeline->tempo_events[insert_at].bpm = clamp_bpm(bpm);
    timeline->tempo_event_count = count + 1;
    if (explicit_tempo) timeline->project_tempo_explicit = true;
    return true;
}

bool timeline_remove_tempo_event(MasterTimeline *timeline, int index) {
    if (!timeline) return false;
    int count = tempo_event_count(timeline);
    if (index <= 0 || index >= count) return false;
    for (int i = index; i + 1 < count; ++i) {
        timeline->tempo_events[i] = timeline->tempo_events[i + 1];
    }
    timeline->tempo_event_count = count - 1;
    if (timeline->tempo_event_count < TIMELINE_MAX_TEMPO_EVENTS) {
        memset(&timeline->tempo_events[timeline->tempo_event_count], 0, sizeof(timeline->tempo_events[0]));
    }
    return true;
}

bool timeline_has_tempo_event_strictly_between(const MasterTimeline *timeline, int64_t start_tick, int64_t end_tick) {
    if (!timeline || end_tick <= start_tick) return false;
    int count = tempo_event_count(timeline);
    for (int i = 0; i < count; ++i) {
        int64_t tick = timeline->tempo_events[i].tick;
        if (tick > start_tick && tick < end_tick) return true;
    }
    return false;
}

void timeline_shift_tempo_events(MasterTimeline *timeline, int64_t from_tick, int64_t delta_ticks) {
    if (!timeline || delta_ticks == 0) return;
    int count = tempo_event_count(timeline);
    for (int i = 0; i < count; ++i) {
        if (timeline->tempo_events[i].tick == 0) continue;
        if (timeline->tempo_events[i].tick >= from_tick) {
            timeline->tempo_events[i].tick += delta_ticks;
            if (timeline->tempo_events[i].tick < 0) timeline->tempo_events[i].tick = 0;
        }
    }
}

double timeline_bpm_at_tick(const MasterTimeline *timeline, double tick) {
    int count = tempo_event_count(timeline);
    if (count <= 0) return TIMELINE_DEFAULT_BPM;
    double bpm = tempo_event_bpm_at_index(timeline, 0);
    for (int i = 1; i < count; ++i) {
        if (tick < (double)timeline->tempo_events[i].tick) break;
        bpm = tempo_event_bpm_at_index(timeline, i);
    }
    return bpm;
}

double timeline_clamped_tape_speed(const MasterTimeline *timeline) {
    double speed = timeline ? (double)timeline->tape_speed : 1.0;
    if (speed < TIMELINE_MIN_TAPE_SPEED) return TIMELINE_MIN_TAPE_SPEED;
    if (speed > TIMELINE_MAX_TAPE_SPEED) return TIMELINE_MAX_TAPE_SPEED;
    return speed;
}

double timeline_tick_to_canonical_seconds(const MasterTimeline *timeline, double tick) {
    if (!timeline || tick <= 0.0) return 0.0;
    int count = tempo_event_count(timeline);
    int ppqn = timeline_ppqn(timeline);
    double seconds = 0.0;
    double segment_start = 0.0;
    double bpm = count > 0 ? tempo_event_bpm_at_index(timeline, 0) : TIMELINE_DEFAULT_BPM;

    for (int i = 1; i < count; ++i) {
        double event_tick = (double)timeline->tempo_events[i].tick;
        if (event_tick <= segment_start) {
            bpm = tempo_event_bpm_at_index(timeline, i);
            continue;
        }
        if (tick <= event_tick) break;
        seconds += ((event_tick - segment_start) / (double)ppqn) * 60.0 / bpm;
        segment_start = event_tick;
        bpm = tempo_event_bpm_at_index(timeline, i);
    }

    if (tick > segment_start) {
        seconds += ((tick - segment_start) / (double)ppqn) * 60.0 / bpm;
    }
    return seconds;
}

double timeline_canonical_seconds_to_tick(const MasterTimeline *timeline, double seconds) {
    if (!timeline || seconds <= 0.0) return 0.0;
    int count = tempo_event_count(timeline);
    int ppqn = timeline_ppqn(timeline);
    double remaining = seconds;
    double segment_start = 0.0;
    double bpm = count > 0 ? tempo_event_bpm_at_index(timeline, 0) : TIMELINE_DEFAULT_BPM;

    for (int i = 1; i < count; ++i) {
        double event_tick = (double)timeline->tempo_events[i].tick;
        double segment_ticks = event_tick - segment_start;
        if (segment_ticks <= 0.0) {
            bpm = tempo_event_bpm_at_index(timeline, i);
            continue;
        }
        double segment_seconds = (segment_ticks / (double)ppqn) * 60.0 / bpm;
        if (remaining <= segment_seconds) {
            return segment_start + remaining * bpm / 60.0 * (double)ppqn;
        }
        remaining -= segment_seconds;
        segment_start = event_tick;
        bpm = tempo_event_bpm_at_index(timeline, i);
    }

    return segment_start + remaining * bpm / 60.0 * (double)ppqn;
}

double timeline_canonical_seconds_between_ticks(const MasterTimeline *timeline,
                                                double start_tick,
                                                double end_tick) {
    if (end_tick >= start_tick) {
        return timeline_tick_to_canonical_seconds(timeline, end_tick) -
               timeline_tick_to_canonical_seconds(timeline, start_tick);
    }
    return -(timeline_tick_to_canonical_seconds(timeline, start_tick) -
             timeline_tick_to_canonical_seconds(timeline, end_tick));
}
