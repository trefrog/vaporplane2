We have a stronger design direction for tempo events and tempo transplanting, but I want this implemented in at most two phases. Do not fragment it into a dozen micro-branches, but also do not repeat the previous mistake of mixing every timing idea together.

Core concept:

Tempo events are not just BPM metadata. They are structural anchor seams on the beat grid.

A tempo event at a beat-snapped tick creates a seam between musical zones. Cursor/grid navigation should respect these seams generally, not only for one special paste command. The seam creates a before/after ambiguity that the cursor must be able to express.

The goal is to eventually support tempo transplanting like this:

```text
Before paste:
bar 9 seam = 100 BPM

Incoming material:
starts at 92 BPM

After insert-paste at seam:
bar 9 seam = 92 BPM
```

If an insert-paste with tempo metadata lands on an existing seam, it should reuse/conform that seam rather than create another tempo event nearby or stacked on top. Tempo events are strictly grid-aligned and unique per beat-snapped tick.

Important doctrine:

* Ticks are truth.
* Tempo belongs to the ruler, not to timeline instances.
* Roster clips may carry source tempo metadata.
* Timeline instances do not own BPM.
* When tempo metadata is explicitly brought into the timeline, it becomes ruler data.
* Cursor navigation respects structural seams.
* Moving/placing across a seam should be an intentional zone-crossing action.
* No pitch-preserving stretch.
* No Sync/Elastic mode.
* No automatic BPM detection.
* No DAW-style warp markers.
* No per-instance BPM.

Please inspect the current branch/code and propose a two-phase implementation plan before coding.

Phase 1: Anchor seam navigation and ruler behavior

Implement tempo events as beat-snapped structural seams.

Desired behavior:

* Tempo events remain unique per beat-snapped tick.
* Tick 0 remains the anchored base tempo event.
* Cursor/grid motion should notice tempo seams.
* If moving from one beat cell to the next would cross a tempo seam, the first press should stop at the near side of the seam, and the next press should cross to the far side.
* The cursor should be able to represent “before seam” and “after seam” at the same tick.
* This should apply as a general grid-navigation paradigm, not as a one-off paste hack.
* For now, only tempo events should create these navigation barriers. Do not make every marker type into a barrier.
* Ruler rendering should make the seam visible and show which side/focus is active if practical.
* Existing tempo event insert/remove/nudge behavior should keep working.
* Existing playback behavior should remain stable.
* Normal cursor movement should respect tempo seams as barriers.
* held-R2 cursor hops may skip over seams 

Suggested internal model:

```c
typedef enum TimelineSeamSide {
    TIMELINE_SEAM_NONE,
    TIMELINE_SEAM_BEFORE,
    TIMELINE_SEAM_AFTER
} TimelineSeamSide;
```

or similar.

The cursor may need to become conceptually:

```text
tick + seam_side
```

Same tick, different edit intent.

Phase 1 non-goals:

* Do not implement insert-paste yet unless there is already an obvious existing paste path.
* Do not implement tempo transplanting yet.
* Do not add per-instance BPM.
* Do not add time-stretching.
* Do not add source tempo landmarks beyond existing roster metadata.
* Do not add a large tempo editor.

Phase 2: Tempo-bearing insert/transplant behavior

After Phase 1 is stable, add the first minimal tempo transplant operation.

This is not a general DAW warp system. It is a ruler splice operation.

Desired behavior:

* Roster/source material may provide tempo metadata, such as source BPM and musical length.
* Timeline instances themselves should not store their own BPM.
* If the user explicitly chooses an operation like “bring pulse,” “place with tempo,” or “insert with tempo,” the source tempo metadata becomes timeline ruler data.
* If inserting tempo-bearing material at a normal beat position, create a tempo event at the insertion tick if needed.
* If inserting tempo-bearing material at an existing seam, reuse/conform the existing seam to the incoming starting BPM.
* Do not create duplicate tempo events at the same beat-snapped tick.
* For insert-style operations, later timeline material and later tempo events should shift as needed.
* For overlay-style placement, do not rewrite existing tempo unless the user explicitly chose a tempo-bearing operation.
* Cursor seam side should decide whether the operation attaches before or after the seam.

Example behavior:

```text
Existing timeline:
bar 1 = 100 BPM
bar 9 seam = 100 BPM

Incoming tempo-bearing material:
starts at 92 BPM

User insert-pastes at bar 9 seam, after-side:
bar 9 seam is reused and becomes 92 BPM
incoming material begins in the new 92 BPM zone
later material shifts right if this is an insert operation
```

Moving existing instances across seams:

* Since instances do not own BPM, moving an instance across a seam simply places it under the destination ruler zone.
* The seam navigation should make this intentional:

  * first movement reaches the seam/near side
  * second movement crosses into the next zone
* Do not invent per-instance tempo ownership to solve this.

Please recommend names for the user-facing actions, but keep them compact and gamepad-friendly. Candidate language:

* Mark tempo
* Bring pulse
* Place with pulse
* Insert with pulse
* Place free

I care more about the architecture than the exact label right now.

Implementation constraints:

* Keep fixed-size tempo event storage if that is already the current direction.
* No allocation in the audio callback.
* Tempo mutations must be safe relative to audio playback.
* Clamp BPM to the existing valid range.
* Preserve buildability after Phase 1 before starting Phase 2.
* Prefer small helper functions over scattered tempo logic.
* Add comments around seam-side cursor behavior, because this is the conceptual core.

Please start by reading the relevant files and reporting:

1. Whether the current tempo event implementation is a safe base for this.
2. What files need changes for Phase 1.
3. What files need changes for Phase 2.
4. The riskiest ambiguity in cursor movement.
5. A concrete test plan for both phases.

Do not start coding until you’ve given the plan.

Please be especially careful not to turn Phase 2 into a hidden stretch engine. This is tempo ruler surgery, not audio warping.

## Implementation status

Implemented in two phases:

### Phase 1

* Added `TimelineSeamSide` and made the timeline cursor/edit ghost conceptually `tick + seam_side`.
* Nonzero tempo events are structural navigation seams. Normal beat movement stops on the near side of a seam, then crosses to the far side on the next press.
* Bar-sized cursor hops can skip the seam barrier.
* Tempo event insert/remove/nudge behavior still uses the fixed-size, beat-snapped, unique-per-tick tempo map.
* Ruler rendering highlights tempo seams and shows the active before/after side.
* Playback timing and metronome timing continue to use the tempo map; no stretch behavior was added.

### Phase 2

* Added explicit roster placement modes:
  * `Place free`: old behavior; places the clip without changing the tempo map.
  * `Place pulse`: places the clip and writes/conforms a tempo event at the placement tick using the roster clip source BPM.
  * `Insert pulse`: shifts later timeline material and later tempo events, then writes/conforms the insertion tempo seam using the roster clip source BPM.
* Timeline instances still do not store BPM.
* Roster clips remain the source of tempo metadata until an explicit pulse operation writes that metadata into the ruler.
* Existing seams are reused on the after-side. The before-side of an existing seam inserts before the old seam by shifting that seam later and creating the incoming pulse seam at the insertion tick.
* Tempo event storage remains fixed-capacity, tempo mutations are guarded by the audio stream lock, and the audio callback does not allocate.
