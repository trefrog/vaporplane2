
Now that the tempo-anchor/seam work is in motion, I want to design the next layer: tape speed.

Please do not call this time-stretching. This is not pitch-preserving stretch, not Sync/Elastic, not warp, and not DSP reconstruction. It is only tape/varispeed playback: a transport/sample playback speed ratio where pitch naturally follows speed.

Core idea:

- Store one float ratio as the truth:
  - `tape_speed = 1.0f` by default
- This represents tape machine speed / motor speed.
- At `0.80f`, playback is slower and pitch drops naturally.
- At `1.25f`, playback is faster and pitch rises naturally.
- Do not store “false BPM” as canonical state.
- Do not add per-instance BPM.
- Do not add pitch-preserving stretching.
- Do not add a third timing mode.

The useful part is the control surface. Arbitrary ratios like `1.172f` are technically valid, but not very musical to adjust directly. I want two musical adjustment modes over the same underlying float:

1. BPM-step control

This treats tape speed as an audible BPM target relative to the canonical tempo at the cursor or playhead.

Formula:

```c
tape_speed = audible_bpm / canonical_bpm_here;
audible_bpm = canonical_bpm_here * tape_speed;
```

Example:

```text
canonical tempo at cursor = 100 BPM
audible target = 80 BPM
tape_speed = 0.80f
```

If a later tempo zone is 102 BPM, the same tape speed makes it sound like:

```text
102 * 0.80 = 81.6 BPM
```

That is intended. The tempo map remains the musical truth; tape speed is the transport motor layer.

2. 12-TET / transpose-step control

This treats tape speed as musical pitch transposition.

Formula:

```c
tape_speed = powf(2.0f, semitones / 12.0f);
semitones = 12.0f * log2f(tape_speed);
```

Examples:

```text
-12 semitones = 0.5x
 -7 semitones ≈ 0.667x
 -5 semitones ≈ 0.749x
  0 semitones = 1.0x
 +7 semitones ≈ 1.498x
+12 semitones = 2.0x
```

Please inspect the current tempo-map/timeline/audio code and propose a small implementation plan for tape speed.

Questions to answer before coding:

1. Where should `tape_speed` live?

   * I expect project/master timeline or transport-level, not per instance.
   * Recommend the least confusing location based on the current code.

2. What should tape speed affect in V1?

   * timeline playhead advancement?
   * metronome timing?
   * sample playback rate?
   * preview playback?
   * only timeline mode, or also source/roster preview?

3. How should it interact with existing tempo events?

   * canonical BPM comes from the tempo map
   * audible BPM is derived from `canonical_bpm_here * tape_speed`

4. How should UI/debug display show it?
   Suggested compact display:

   * `canon: 100.00`
   * `tape: 0.800x`
   * `hear: 80.00`
   * `pitch: -3.86 st`

5. How should adjustment modes work?

   * BPM mode: bracket keys or gamepad controls nudge audible BPM by whole BPM steps, recomputing `tape_speed` from canonical BPM at cursor/playhead.
   * 12-TET mode: controls nudge semitone distance, recomputing `tape_speed` from `pow(2, st / 12)`.
   * Recommend exact controls that fit the current input system and do not conflict with tempo-event BPM nudging.

6. What clamp range should be used?

   * Suggest a sane ratio range, maybe `0.25f..4.0f` or `0.05f..4.0f`.
   * Explain the tradeoff.

7. Should live changes during playback be allowed in this first slice?

   * If yes, keep it simple and safe.
   * If no, make the limitation explicit.
   * Avoid building a hidden complicated active-voice tracking system.

Non-goals:

* Do not implement pitch-preserving stretch.
* Do not use the term time-stretching in code/UI for this feature.
* Do not add Sync/Elastic mode.
* Do not add per-instance BPM.
* Do not make “audible BPM” a second source of truth.
* Do not add automatic BPM detection.
* Do not add warp markers.
* Do not overhaul placement semantics.
* Do not break existing tempo marker/seam behavior.

Important doctrine:

```text
Tempo map = composition clock.
Tape speed = transport motor speed.
Audible BPM = canonical BPM * tape_speed.
Pitch shift = 12 * log2(tape_speed).
```

Please start with a bounded implementation plan, likely files to touch, risks, and a manual test plan. Do not code until the plan is approved.