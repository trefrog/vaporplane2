#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "clip.h"

#define APP_MAX_ROSTER_CLIPS 64
#define TIMELINE_MAX_LANES 8
#define APP_MAX_TIMELINE_INSTANCES_PER_LANE 32
#define APP_ROSTER_CLIP_NAME_MAX 128
#define APP_MIN_CAPTURE_FRAMES 64
#define APP_MAX_CAPTURE_FRAMES (48000 * 60 * 5)
#define APP_MAX_CAPTURE_BYTES (64u * 1024u * 1024u)
#define TIMELINE_MAX_TEMPO_EVENTS 128

typedef struct {
    int64_t tick;
    double bpm;
} TempoEvent;

typedef struct {
    char name[APP_ROSTER_CLIP_NAME_MAX];
    char source_path[CLIP_MAX_PATH];
    size_t source_loop_start_frame;
    size_t source_loop_end_frame;
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

typedef enum {
    TIMELINE_INSTANCE_FREE,
    TIMELINE_INSTANCE_TAPE
} TimelineInstanceTiming;

typedef struct {
    int roster_clip_index;
    int64_t start_tick;
    int64_t duration_ticks;
    /* First source frame rendered by this instance; non-zero when pickup audio is clipped at tick 0. */
    size_t source_start_frame;
    /* Musical downbeat anchor on the canonical timeline, which may fall after start_tick for pickups. */
    int64_t downbeat_tick;
    TimelineInstanceTiming timing;
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

typedef struct {
    bool initialized;
    bool playing;
    int timeline_beats_per_bar;
    int timeline_beat_unit;
    int ticks_per_beat;
    TempoEvent tempo_events[TIMELINE_MAX_TEMPO_EVENTS];
    int tempo_event_count;
    bool project_tempo_explicit;
    float tape_speed;
    int64_t length_ticks;
    int64_t playhead_tick;
    int64_t timeline_cursor_tick;
    int64_t play_range_start_tick;
    int64_t play_range_end_tick;
    bool play_range_loop_enabled;
    bool play_range_custom;
    double view_center_tick;
    double view_span_ticks;
    TimelineLane lanes[TIMELINE_MAX_LANES];
} MasterTimeline;

void timeline_tempo_map_init(MasterTimeline *timeline, double bpm, bool explicit_tempo);
bool timeline_set_tempo_event(MasterTimeline *timeline, int64_t tick, double bpm, bool explicit_tempo);
bool timeline_remove_tempo_event(MasterTimeline *timeline, int index);
bool timeline_tempo_event_index_at_tick(const MasterTimeline *timeline, int64_t tick, int *index_out);
bool timeline_has_tempo_event_strictly_between(const MasterTimeline *timeline, int64_t start_tick, int64_t end_tick);
void timeline_shift_tempo_events(MasterTimeline *timeline, int64_t from_tick, int64_t delta_ticks);
double timeline_bpm_at_tick(const MasterTimeline *timeline, double tick);
double timeline_clamped_tape_speed(const MasterTimeline *timeline);

/*
 * Canonical seconds are derived from the tempo map at tape_speed == 1.0.
 * These helpers intentionally do not include the timeline tape_speed layer.
 */
double timeline_tick_to_canonical_seconds(const MasterTimeline *timeline, double tick);
double timeline_canonical_seconds_to_tick(const MasterTimeline *timeline, double seconds);
double timeline_canonical_seconds_between_ticks(const MasterTimeline *timeline,
                                                double start_tick,
                                                double end_tick);
