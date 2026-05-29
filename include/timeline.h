#pragma once
#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "clip.h"

#define APP_MAX_ROSTER_CLIPS 64
#define TIMELINE_MAX_LANES 8
#define APP_MAX_TIMELINE_INSTANCES_PER_LANE 32
#define TIMELINE_MAX_TEMPO_EVENTS 64
#define APP_ROSTER_CLIP_NAME_MAX 128
#define APP_MIN_CAPTURE_FRAMES 64
#define APP_MAX_CAPTURE_FRAMES (48000 * 60 * 5)
#define APP_MAX_CAPTURE_BYTES (64u * 1024u * 1024u)
#define APP_STABLE_ID_MAX 48
#define TIMELINE_MIN_BPM 30.0
#define TIMELINE_MAX_BPM 300.0
#define TIMELINE_DEFAULT_BPM 120.0
#define TIMELINE_TAPE_SPEED_DEFAULT 1.0f
#define TIMELINE_TAPE_SPEED_MIN 0.25f
#define TIMELINE_TAPE_SPEED_MAX 4.0f

typedef struct {
    char sample_id[APP_STABLE_ID_MAX];
    char roster_clip_id[APP_STABLE_ID_MAX];
    char name[APP_ROSTER_CLIP_NAME_MAX];
    char source_path[CLIP_MAX_PATH];
    size_t source_loop_start_frame;
    size_t source_loop_end_frame;
    int source_sample_rate;
    size_t loop_start_frame;
    size_t loop_end_frame;
    int sample_rate;
    int channels;
    size_t frame_count;
    float *samples;
    double source_bpm;
    int beats_per_bar;
    int beat_unit;
    double target_bars;
    double target_beats;
    size_t downbeat_offset_frames;
    int midi_note;
    int midi_channel;
    int midi_velocity;
    SDL_Color color;
} RosterClip;

typedef struct {
    int roster_clip_index;
    int64_t start_tick;
    int64_t duration_ticks;
    int midi_note;
    int midi_channel;
    int midi_velocity;
} TimelineInstance;

typedef struct {
    int lane_index;
    int instance_index;
} TimelineInstanceRef;

typedef struct {
    int palette_index;
    float gain;
    bool muted;
    TimelineInstance instances[APP_MAX_TIMELINE_INSTANCES_PER_LANE];
    int instance_count;
} TimelineLane;

typedef struct {
    int64_t tick;
    double bpm;
} TimelineTempoEvent;

typedef enum {
    TIMELINE_FOCUS_TRANSPORT,
    TIMELINE_FOCUS_RULER,
    TIMELINE_FOCUS_LANE_INDEX,
    TIMELINE_FOCUS_PLAY_RANGE,
    TIMELINE_FOCUS_TRACK_AREA,
    TIMELINE_FOCUS_ROSTER,
    TIMELINE_FOCUS_COUNT
} TimelineFocusZone;

typedef enum {
    TIMELINE_RANGE_HANDLE_START,
    TIMELINE_RANGE_HANDLE_END
} TimelineRangeHandle;

typedef enum {
    TIMELINE_SEAM_NONE,
    TIMELINE_SEAM_BEFORE,
    TIMELINE_SEAM_AFTER
} TimelineSeamSide;

typedef struct {
    bool initialized;
    bool playing;
    double timeline_bpm;
    int timeline_beats_per_bar;
    int timeline_beat_unit;
    int ticks_per_beat;
    int64_t length_ticks;
    int64_t playhead_tick;
    int64_t timeline_cursor_tick;
    TimelineSeamSide timeline_cursor_seam_side;
    int64_t play_range_start_tick;
    int64_t play_range_end_tick;
    bool play_range_loop_enabled;
    bool play_range_custom;
    double view_center_tick;
    double view_span_ticks;
    float tape_speed;
    TimelineTempoEvent tempo_events[TIMELINE_MAX_TEMPO_EVENTS];
    int tempo_event_count;
    TimelineLane lanes[TIMELINE_MAX_LANES];
} MasterTimeline;

static inline double timeline_clamp_bpm(double bpm) {
    if (bpm < TIMELINE_MIN_BPM) return TIMELINE_MIN_BPM;
    if (bpm > TIMELINE_MAX_BPM) return TIMELINE_MAX_BPM;
    return bpm;
}

static inline int timeline_valid_tempo_event_count(const MasterTimeline *timeline) {
    if (!timeline || timeline->tempo_event_count <= 0) return 0;
    if (timeline->tempo_event_count > TIMELINE_MAX_TEMPO_EVENTS) return TIMELINE_MAX_TEMPO_EVENTS;
    return timeline->tempo_event_count;
}

static inline double timeline_base_bpm(const MasterTimeline *timeline) {
    double bpm = timeline && timeline->timeline_bpm > 0.0 ? timeline->timeline_bpm : TIMELINE_DEFAULT_BPM;
    int count = timeline_valid_tempo_event_count(timeline);
    if (count > 0 && timeline->tempo_events[0].tick == 0) bpm = timeline->tempo_events[0].bpm;
    return timeline_clamp_bpm(bpm);
}

static inline int timeline_tempo_event_index_at_tick(const MasterTimeline *timeline, int64_t tick) {
    int count = timeline_valid_tempo_event_count(timeline);
    for (int i = 0; i < count; ++i) {
        int64_t event_tick = timeline->tempo_events[i].tick;
        if (event_tick == tick) return i;
        if (event_tick > tick) break;
    }
    return -1;
}

static inline int timeline_tempo_event_index_before_or_at_tick(const MasterTimeline *timeline, double tick) {
    int count = timeline_valid_tempo_event_count(timeline);
    int index = -1;
    for (int i = 0; i < count; ++i) {
        double event_tick = (double)timeline->tempo_events[i].tick;
        if (event_tick <= tick) index = i;
        else break;
    }
    return index;
}

static inline double timeline_effective_bpm_at_tick(const MasterTimeline *timeline, double tick) {
    int index = timeline_tempo_event_index_before_or_at_tick(timeline, tick);
    if (index >= 0) return timeline_clamp_bpm(timeline->tempo_events[index].bpm);
    return timeline_base_bpm(timeline);
}

static inline double timeline_ticks_per_second_at_tick(const MasterTimeline *timeline, double tick) {
    int ticks_per_beat = timeline && timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat : 960;
    double bpm = timeline_effective_bpm_at_tick(timeline, tick);
    return bpm * (double)ticks_per_beat / 60.0;
}

static inline float timeline_clamp_tape_speed(float speed) {
    if (speed < TIMELINE_TAPE_SPEED_MIN) return TIMELINE_TAPE_SPEED_MIN;
    if (speed > TIMELINE_TAPE_SPEED_MAX) return TIMELINE_TAPE_SPEED_MAX;
    return speed;
}

static inline float timeline_effective_tape_speed(const MasterTimeline *timeline) {
    if (!timeline || timeline->tape_speed <= 0.0f) return TIMELINE_TAPE_SPEED_DEFAULT;
    return timeline_clamp_tape_speed(timeline->tape_speed);
}

static inline double timeline_audible_bpm_at_tick(const MasterTimeline *timeline, double tick) {
    return timeline_effective_bpm_at_tick(timeline, tick) * (double)timeline_effective_tape_speed(timeline);
}

static inline float timeline_tape_speed_from_audible_bpm(const MasterTimeline *timeline, double tick, double audible_bpm) {
    double canonical_bpm = timeline_effective_bpm_at_tick(timeline, tick);
    if (canonical_bpm <= 0.0) canonical_bpm = TIMELINE_DEFAULT_BPM;
    return timeline_clamp_tape_speed((float)(audible_bpm / canonical_bpm));
}

static inline double timeline_tape_pitch_semitones(float speed) {
    return 12.0 * log2((double)timeline_clamp_tape_speed(speed));
}

static inline float timeline_tape_speed_from_pitch_semitones(double semitones) {
    return timeline_clamp_tape_speed((float)pow(2.0, semitones / 12.0));
}

static inline double timeline_seconds_between_ticks(const MasterTimeline *timeline, double start_tick, double end_tick) {
    if (!timeline || start_tick == end_tick) return 0.0;

    bool negative = false;
    if (end_tick < start_tick) {
        double tmp = start_tick;
        start_tick = end_tick;
        end_tick = tmp;
        negative = true;
    }

    int ticks_per_beat = timeline->ticks_per_beat > 0 ? timeline->ticks_per_beat : 960;
    double seconds = 0.0;
    double cursor = start_tick;
    double bpm = timeline_effective_bpm_at_tick(timeline, cursor);
    int count = timeline_valid_tempo_event_count(timeline);

    for (int i = 0; i < count; ++i) {
        double event_tick = (double)timeline->tempo_events[i].tick;
        if (event_tick <= cursor) continue;
        if (event_tick >= end_tick) break;
        seconds += (event_tick - cursor) * 60.0 / (bpm * (double)ticks_per_beat);
        cursor = event_tick;
        bpm = timeline_clamp_bpm(timeline->tempo_events[i].bpm);
    }

    if (end_tick > cursor) {
        seconds += (end_tick - cursor) * 60.0 / (bpm * (double)ticks_per_beat);
    }

    return negative ? -seconds : seconds;
}

static inline double timeline_seconds_at_tick(const MasterTimeline *timeline, double tick) {
    return timeline_seconds_between_ticks(timeline, 0.0, tick);
}
