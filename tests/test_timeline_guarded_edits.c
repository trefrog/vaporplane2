#include "app.h"

#include <assert.h>
#include <string.h>

static void setup_app(App *app)
{
    memset(app, 0, sizeof(*app));
    app->timeline.initialized = true;
    app->timeline.timeline_bpm = 120.0;
    app->timeline.timeline_beats_per_bar = 4;
    app->timeline.timeline_beat_unit = 4;
    app->timeline.ticks_per_beat = 960;
    app->timeline.length_ticks = 480;
    app->timeline.view_span_ticks = 3840.0;
    app->timeline.lanes[0].type = TIMELINE_LANE_AUDIO;
    app->timeline.lanes[0].gain = 1.0f;
    app->timeline.lanes[0].instance_count = 1;
    app->timeline.lanes[0].instances[0].kind = TIMELINE_INSTANCE_AUDIO_CLIP;
    app->timeline.lanes[0].instances[0].start_tick = 240;
    app->timeline.lanes[0].instances[0].duration_ticks = 240;
    app->timeline.lanes[0].instances[0].midi_velocity = 100;
    app->selected_timeline_lane = 0;
    app->selected_timeline_instance = (TimelineInstanceRef){ 0, 0 };
    app->timeline_focus_zone = TIMELINE_FOCUS_TRACK_AREA;
    app->timeline_edit_mode = TIMELINE_EDIT_NONE;
    app->timeline_value_bubble_mode = TIMELINE_VALUE_BUBBLE_NONE;
    app->timeline_value_bubble_instance = (TimelineInstanceRef){ -1, -1 };
}

int main(void)
{
    App app;
    setup_app(&app);

    app_timeline_guarded_value_edit(&app, TIMELINE_VALUE_BUBBLE_OFFSET, 1);
    assert(app.timeline.lanes[0].instances[0].start_tick == 240);
    assert(app.timeline_value_bubble_mode == TIMELINE_VALUE_BUBBLE_OFFSET);

    app_timeline_guarded_value_edit(&app, TIMELINE_VALUE_BUBBLE_OFFSET, 1);
    assert(app.timeline.lanes[0].instances[0].start_tick == 241);

    app_timeline_dismiss_value_bubble(&app);
    assert(app.timeline_value_bubble_mode == TIMELINE_VALUE_BUBBLE_NONE);

    app_timeline_guarded_value_edit(&app, TIMELINE_VALUE_BUBBLE_LENGTH, 1);
    assert(app.timeline.lanes[0].instances[0].duration_ticks == 240);
    assert(app.timeline_value_bubble_mode == TIMELINE_VALUE_BUBBLE_LENGTH);

    app_timeline_guarded_value_edit(&app, TIMELINE_VALUE_BUBBLE_LENGTH, 1);
    assert(app.timeline.lanes[0].instances[0].duration_ticks == 241);

    app_timeline_guarded_value_edit(&app, TIMELINE_VALUE_BUBBLE_VELOCITY, 1);
    assert(app.timeline.lanes[0].instances[0].midi_velocity == 100);
    assert(app.timeline_value_bubble_mode == TIMELINE_VALUE_BUBBLE_VELOCITY);

    app_timeline_guarded_value_edit(&app, TIMELINE_VALUE_BUBBLE_VELOCITY, 1);
    assert(app.timeline.lanes[0].instances[0].midi_velocity == 101);

    return 0;
}
