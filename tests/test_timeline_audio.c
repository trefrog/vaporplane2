#include "audio_engine.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void setup_timeline(MasterTimeline *timeline, int64_t duration_ticks)
{
    memset(timeline, 0, sizeof(*timeline));
    timeline->initialized = true;
    timeline->timeline_bpm = 60.0;
    timeline->timeline_beats_per_bar = 4;
    timeline->timeline_beat_unit = 4;
    timeline->ticks_per_beat = 1000;
    timeline->length_ticks = duration_ticks;
    timeline->tape_speed = 1.0f;
    timeline->lanes[0].type = TIMELINE_LANE_AUDIO;
    timeline->lanes[0].gain = 1.0f;
    timeline->lanes[0].instance_count = 1;
    timeline->lanes[0].instances[0].kind = TIMELINE_INSTANCE_AUDIO_CLIP;
    timeline->lanes[0].instances[0].roster_clip_index = 0;
    timeline->lanes[0].instances[0].pattern_index = -1;
    timeline->lanes[0].instances[0].start_tick = 0;
    timeline->lanes[0].instances[0].duration_ticks = duration_ticks;
    timeline->lanes[0].instances[0].midi_velocity = 127;
}

static void setup_engine(AudioEngine *engine,
                         RosterClip *clip,
                         int *clip_count,
                         MasterTimeline *timeline)
{
    memset(engine, 0, sizeof(*engine));
    engine->roster = clip;
    engine->roster_clip_count = clip_count;
    engine->timeline = timeline;
    engine->master_gain = 1.0f;
}

static void setup_clip(RosterClip *clip, float *samples, size_t frames)
{
    memset(clip, 0, sizeof(*clip));
    clip->samples = samples;
    clip->frame_count = frames;
    clip->sample_rate = 1000;
    clip->channels = 2;
    for (size_t i = 0; i < frames; ++i) {
        samples[i * 2] = 0.75f;
        samples[i * 2 + 1] = 0.75f;
    }
}

static int has_nonzero_before(const float *out, int frame_count, int stop_frame)
{
    for (int frame = 0; frame < frame_count && frame < stop_frame; ++frame) {
        if (fabsf(out[frame * 2]) > 0.0001f || fabsf(out[frame * 2 + 1]) > 0.0001f) return 1;
    }
    return 0;
}

static void assert_silent_from(const float *out, int frame_count, int start_frame)
{
    for (int frame = start_frame; frame < frame_count; ++frame) {
        assert(fabsf(out[frame * 2]) < 0.000001f);
        assert(fabsf(out[frame * 2 + 1]) < 0.000001f);
    }
}

int main(void)
{
    {
        MasterTimeline timeline;
        RosterClip clip;
        AudioEngine engine;
        AudioTimelineRenderState state;
        int clip_count = 1;
        float samples[16];
        float out[16];

        setup_timeline(&timeline, 4);
        setup_clip(&clip, samples, 8);
        setup_engine(&engine, &clip, &clip_count, &timeline);
        audio_timeline_render_state_init(&state, 0, 8);
        audio_engine_render_timeline_block(&engine, &state, out, 8, 1000);

        assert(has_nonzero_before(out, 8, 4));
        assert_silent_from(out, 8, 4);
    }

    {
        MasterTimeline timeline;
        RosterClip clip;
        AudioEngine engine;
        AudioTimelineRenderState state;
        int clip_count = 1;
        float samples[8];
        float out[16];

        setup_timeline(&timeline, 8);
        setup_clip(&clip, samples, 4);
        setup_engine(&engine, &clip, &clip_count, &timeline);
        audio_timeline_render_state_init(&state, 0, 8);
        audio_engine_render_timeline_block(&engine, &state, out, 8, 1000);

        assert(has_nonzero_before(out, 8, 4));
        assert_silent_from(out, 8, 4);
    }

    return 0;
}
