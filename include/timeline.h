#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "clip.h"

#define APP_MAX_ROSTER_CLIPS 64
#define APP_MAX_TIMELINE_INSTANCES 128
#define APP_ROSTER_CLIP_NAME_MAX 128
#define APP_MIN_CAPTURE_FRAMES 64
#define APP_MAX_CAPTURE_FRAMES (48000 * 60 * 5)
#define APP_MAX_CAPTURE_BYTES (64u * 1024u * 1024u)

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

typedef struct {
    int roster_clip_index;
    int64_t start_tick;
    int64_t duration_ticks;
    int midi_note;
    int midi_channel;
    int midi_velocity;
} TimelineInstance;

typedef enum {
    TIMELINE_FOCUS_TRANSPORT,
    TIMELINE_FOCUS_RULER,
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
    double timeline_bpm;
    int timeline_beats_per_bar;
    int timeline_beat_unit;
    int ticks_per_beat;
    int64_t length_ticks;
    int64_t playhead_tick;
    int64_t timeline_cursor_tick;
    int64_t play_range_start_tick;
    int64_t play_range_end_tick;
    bool play_range_loop_enabled;
    bool play_range_custom;
    double view_center_tick;
    double view_span_ticks;
    TimelineInstance instances[APP_MAX_TIMELINE_INSTANCES];
    int instance_count;
} MasterTimeline;
