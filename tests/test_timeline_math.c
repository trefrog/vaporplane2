#include "timeline.h"

#include <assert.h>

int main(void)
{
    MasterTimeline timeline = {0};
    timeline.ticks_per_beat = 960;

    int64_t sixteenth = timeline_sixteenth_ticks(&timeline);
    assert(sixteenth == 240);
    assert(timeline_micro_offset_limit(sixteenth) == 119);

    assert(timeline_nearest_tick_anchor(0, sixteenth) == 0);
    assert(timeline_nearest_tick_anchor(119, sixteenth) == 0);
    assert(timeline_nearest_tick_anchor(121, sixteenth) == 240);
    assert(timeline_nearest_tick_anchor(250, sixteenth) == 240);

    int64_t min_start = 0;
    int64_t max_start = 0;
    timeline_micro_offset_bounds(240, sixteenth, &min_start, &max_start);
    assert(min_start == 121);
    assert(max_start == 359);

    assert(timeline_clamp_start_to_anchor_offset_bounds(240, 80, sixteenth) == 121);
    assert(timeline_clamp_start_to_anchor_offset_bounds(240, 240, sixteenth) == 240);
    assert(timeline_clamp_start_to_anchor_offset_bounds(240, 480, sixteenth) == 359);

    sixteenth = timeline_sixteenth_ticks_for_ppqn(964);
    assert(sixteenth == 241);
    assert(timeline_micro_offset_limit(sixteenth) == 120);
    timeline_micro_offset_bounds(241, sixteenth, &min_start, &max_start);
    assert(min_start == 121);
    assert(max_start == 361);

    return 0;
}
