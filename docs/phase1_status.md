# Vaporplane Phase 1 Vertical Slice Status

## Implemented
- C11 + SDL3 + CMake app split into modules: app, input, transport, audio engine, clip, waveform.
- WAV loading from `assets/samples/` with runtime generated fallback if no WAV is available.
- Optional `*.wav.json` sidecar metadata can define source BPM and related loop metadata.
- Waveform view can write source WAV tempo sidecar JSON from the active tempo state after confirmation.
- In-app sample selector backed by reusable `load_clip_from_path(App *, const char *)`.
- `AudioClip` metadata for path/rate/channels/frames/loop/music metadata/gain/playback rate.
- Transport with BPM, PPQN, beats-per-bar, beat-unit, play/pause, tick/second conversion.
- Metronome pip on beat with downbeat accent and toggle.
- Audible metronome clicks are driven from the same frame/downbeat grid as the visual beat guides.
- Looping sample playback with loop-start/loop-end edits while running.
- Waveform rendering with loop markers and playhead marker.
- Smooth target-based waveform panning/zooming.
- Zoom-relative keyboard and gamepad loop marker trimming.
- Dedicated Tempo Lock mode for manual BPM/downbeat/meter/length calibration.
- Loop capture roster with owned in-memory PCM clips and a first-pass tick-based master timeline.
- Roster clips can be opened as disposable waveform editing sources; captures from that view create new derived roster clips.
- Timeline roster preview plays the selected roster clip as a restartable one-shot.
- Timeline view has focus zones, a tick cursor, beat/bar grid, beat-snapped tempo markers, play range handles, and range playback.
- Timeline sequencing uses 8 lanes with same-lane overlap blocking and cross-lane overlap allowed.
- Timeline playback applies per-instance velocity and shows a compact master output meter/clipping indicator.
- Master Mix is a third main view for the master bus meter and future reverb, FX chain, and MIDI/control sections.
- Master audio owns a fixed-size built-in FX chain with one real built-in unit, `Reverb 1`, disabled by default.
- `Reverb 1` defaults to a more assertive long-room character when enabled, with depth/rate modulation available to soften metallic ringing; startup playback remains dry.
- Master FX process timeline/master playback only; waveform loop audition stays dry.
- First-pass Lane Inspector opens from the lane row number and shows lane identity, mute, post-lane analyzer, peak meter, and clip LED.
- Timeline playback starts from the play range and stops/rewinds or loops at the range end.
- Keyboard controls and first-pass gamepad editing controls with R2 chord support.

## Build / Run
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build
./build/vaporplane
```

## Controls
- `Escape`: close active panel/mode, then press twice within 2s to quit
- `F1`: show/hide controls legend
- `F2`: cycle waveform/timeline/master mix view
- `Space`: play/pause
- `M`: metronome on/off
- `T`: enter/exit Tempo Lock mode
- `[` / `]`: adjust manual transport BPM
- `Tab`: open/close sample selector in waveform view; cycle timeline focus in timeline view
- `Left/Right`: smoothly pan view
- `Up/Down`: smoothly zoom in/out
- `A/D`: move loop start relative to visible zoom (`Shift` = larger step)
- `J/L`: move loop end relative to visible zoom (`Shift` = larger step)
- `1`: focus loop start
- `2`: focus loop end
- `R`: reset loop to full sample
- `Home`: jump playhead to loop start

## Sample Selector
- Lists case-insensitive `*.wav` files from `assets/samples/`.
- `Up/Down`: choose sample
- `Return`: load selected sample
- `Tab` / `Escape`: close selector
- `R`: refresh the sample list while selector is open
- Future file-dialog results should call `load_clip_from_path()` directly.

## Loop Capture / Timeline
- In normal waveform mode, `L2 + R2 + South` captures the current loop into the roster as an owned in-memory PCM clip.
- Captured clips are in-memory only; they retain source path, source loop frames, copied PCM, color, source BPM, meter, target bars/beats, and downbeat offset.
- Captures from roster-derived waveform sources preserve original source-path/frame lineage while creating a new roster item.
- The first captured clip initializes the master timeline BPM/meter and base tempo event from that clip, then creates one timeline instance at tick `0` in lane 1.
- Later captures add roster entries only for now.
- Timeline has 8 fixed lanes. Each lane owns fixed-capacity timeline instances.
- Timeline instances use musical ticks internally: `roster_clip_index`, `start_tick`, `duration_ticks`, MIDI-friendly note/channel, and velocity.
- Timeline owns a fixed-capacity tempo map with an anchored event at tick `0` and beat-snapped tempo changes.
- Nonzero tempo events act as structural ruler seams; the timeline cursor/edit ghost can sit on the before or after side of the same seam tick.
- Same-lane instance overlap is blocked; different-lane overlap is allowed and mixed together.
- Instance velocity is `1..127`, defaults to `100`, and uses a simple perceptual gain curve: `(velocity / 127)^1.5`.
- Lanes are abstract numbered rows `1..8` with fixed palette identities; there are no lane names or roles.
- Lane mute is exposed only through the one-lane Lane Inspector; there is no solo and no lane mixer UI.
- The master output monitor shows peak level and a `CLIP` flash/count when output exceeds the safe range.
- The Lane Inspector analyzer is an isolated 64-bar post-lane spectrum view fed by a fixed 1024-sample active-lane snapshot.
- Timeline context menus are vertical stacks navigated with Up/Down; South/Enter applies and East/Escape backs out.
- Ruler context menus can mark/remove beat-snapped tempo events; tick `0` is anchored and cannot be removed.
- Ruler/track context menus can insert one bar before the cursor's current bar, stopping transport first and shifting later instances and later tempo events.
- Roster context menus can open clips in waveform view, place clips freely, place clips with source pulse metadata, insert clips with source pulse metadata, export WAVs, or delete roster clips with confirmation; deleting a roster clip also removes its timeline instances and compacts references.
- Captures fail cleanly with status text for a full roster, very short loops, oversized clips, or memory allocation failure.
- Timeline playback is separate from waveform loop playback: switching to timeline view stops waveform playback and waits silently.
- Timeline focus zones are `TRANSPORT`, `RULER`, `LANE INDEX`, `PLAY RANGE`, `TRACK AREA`, and `ROSTER`.
- `Tab` / `Shift+Tab` cycle timeline focus. Gamepad bumpers cycle focus in timeline view.
- Timeline cursor movement snaps to one beat by default, clamps to the timeline length, and respects tempo seams as two-step before/after barriers.
- Timeline beat/bar grid is drawn from timeline ticks and meter; bar lines are stronger than beat lines, and tempo event BPM labels render in the ruler.
- Timeline playback and metronome timing follow the tempo map; downbeat accents follow the master timeline grid, not play range starts or source clip anchors.
- Play range defaults to the full timeline. `PLAY RANGE` focus can adjust start/end handles on the beat grid or reset to full timeline.
- `Space` starts/stops timeline playback from the keyboard. Gamepad timeline transport uses the `R2` layer.
- Timeline playback starts at `play_range_start_tick`.
- If range looping is off, playback stops at `play_range_end_tick`, rewinds to `play_range_start_tick`, remains in timeline view, and stays silent.
- If range looping is on, playback wraps back to `play_range_start_tick`.
- Empty timelines do not play and show `timeline empty`.
- Timeline clips play owned PCM at natural speed; no time-stretching or pitch correction is applied yet.
- If natural PCM duration and instance tick duration disagree, playback stops at the earlier of audio end or instance end.
- Metronome enable/disable is global, but waveform mode follows waveform/transport tempo and timeline mode follows the master timeline tempo map.
- Timeline audio mixes as instance -> lane sum -> master sum -> output.
- The audio callback avoids file IO and heap allocation; structural timeline/roster mutations stop transport/preview first, then mutate under the audio stream lock.

## Timeline Controls
- Keyboard `Tab` / `Shift+Tab`: cycle focus forward/back.
- Keyboard `Space`: play/pause timeline transport.
- Keyboard `Enter`: activate focused zone.
- Keyboard `Escape`: exit play-range adjustment, otherwise guarded quit.
- Keyboard `C` or gamepad Start/Plus opens a focus-aware context menu.
- Context menus are vertical overlay lists with a drop shadow: Up/Down changes the highlighted item, South/Enter applies it, and East/Escape backs out.
- `RULER` menus include `Mark tempo` and, when the cursor is on a removable tempo event, `Remove tempo`.
- `RULER` and `TRACK AREA` menus include `Insert bar`, which inserts one full bar before the cursor's containing bar.
- `TRACK AREA` menus include instance actions when an instance is selected.
- `ROSTER` menus include `Open waveform`, `Place free`, `Place pulse`, `Insert pulse`, `Export WAV`, and confirmed `Delete roster clip`.
- In timeline view, left stick pans horizontally and zooms vertically across focus zones; hold L2 for 3x faster viewport movement.
- In `RULER` or `TRACK AREA`: `Left/Right` move cursor by one beat; tempo seams take two presses to cross; `Shift+Left/Right` pans.
- In `RULER`, `[` / `]` adjusts the tempo event at the cursor by `0.5` BPM.
- In `RULER`, gamepad `R2 + d-pad up/down` adjusts the tempo event at the cursor by `1.0` BPM, and `R2 + d-pad left/right` adjusts by `0.1` BPM.
- In `LANE INDEX`: `Up/Down` chooses lane `1..8`, and `Enter`/South opens the Lane Inspector for that lane.
- In `PLAY RANGE`: `Enter` enters adjustment, `1` selects start handle, `2` selects end handle, `Left/Right` nudges the selected handle, `R` resets to full timeline; armed handles render with a padded outline and the timeline view keeps both anchors visible while adjusting.
- In `TRACK AREA`: `Up/Down` moves the lane cursor, and `Enter`/South selects the instance under the cursor in that lane; pressing it again enters `MOVE INSTANCE`.
- In `TRACK AREA`, gamepad `L2 + left stick X` glides the cursor horizontally with hold acceleration.
- In `RULER` or `TRACK AREA`, gamepad `L2 + d-pad left/right` or `R2 + d-pad left/right` moves the cursor by one bar and may skip tempo seam barriers.
- In `TRACK AREA`, `[` / `]` decreases/increases selected instance velocity. Gamepad `L2 + d-pad up/down` also adjusts velocity.
- In `MOVE INSTANCE`: d-pad or arrow left/right moves a lifted ghost by snap units, d-pad or arrow up/down moves it between lanes, South/Enter confirms, East/Escape cancels and keeps the original lane/start tick.
- In `ROSTER`: `Up/Down` selects a roster row; South/Enter arms it, and pressing again enters `PLACE CLIP` at the snapped cursor.
- In `PLACE CLIP`: d-pad or arrow left/right moves a lifted ghost by snap units and respects tempo seams, d-pad or arrow up/down chooses lane 1..8, South/Enter confirms, East/Escape cancels.
- `Place free` does not alter ruler tempo. `Place pulse` writes/conforms a tempo event at the placement tick from the roster clip source BPM. `Insert pulse` shifts later timeline material and later tempo events before placing the source pulse.
- Moving shows the original instance as a dim origin block until the ghost is dropped.
- Armed roster rows, selected instances, valid ghosts, and invalid overlap ghosts use distinct depth-ready 2D cues.
- Timeline move/place drops reject same-lane overlaps with status text and do not commit. Cross-lane overlaps are allowed.
- Gamepad bumpers: cycle focus.
- Gamepad plain South: activate focused zone.
- Gamepad plain East: back/cancel focused adjustment.
- Gamepad plain Start/Plus: opens the focus-aware context menu.
- Gamepad Back/Minus: metronome on/off.
- Gamepad `R2 + South`: play/pause timeline.
- Gamepad `R2 + East`: stop preview, stop timeline playback, and rewind to play range start.
- Gamepad `R2 + West`: jump playhead and cursor to play range start without changing play state.
- Gamepad `R2 + North`: toggle play range loop.
- Gamepad `R2 + Start`: cycle waveform/timeline/master mix view.
- In `ROSTER`, gamepad right-stick click previews the selected roster clip without changing placement state.

## Master Mix
- Master Mix is reached with `F2` or gamepad `R2 + Start` as part of the main view cycle.
- It shows the existing read-only master gain, peak meter, and clip state.
- FX Chain reflects `Slot 1: Reverb 1`. Reverb controls are real, including modulation depth/rate; MIDI/Control remains an intentional placeholder.
- Keyboard `Tab`/`Shift+Tab` and gamepad bumpers change focused section; outside `REVERB`, Up/Down also changes section focus.
- Keyboard `Esc` is consumed and does not enter the quit flow while on Master Mix. Gamepad East returns to Timeline.
- In `REVERB`, keyboard `Up`/`Down` or gamepad d-pad up/down selects a `Reverb 1` parameter; `Left`/`Right` or d-pad left/right adjusts it; `Shift`/L2 uses fine steps.
- `Enter`/South toggles `Reverb 1` when `Enabled` is selected, and `R`/left-stick click clears the reverb tail.

## Lane Inspector
- Opens from timeline `LANE INDEX` focus with `Enter` or South.
- Shows only `LANE N`, lane palette styling/swatches, mute, compact peak/clip state, a post-lane analyzer, a compact peak meter, and a red `CLIP` column.
- The analyzer sits to the right of a small lane data/settings column; the inspector remains one lane at a time.
- The analyzer uses a small UI-thread FFT; the audio callback only writes fixed-size post-lane samples for the active inspected lane.
- The view is visually exclusive: global sample/BPM/status/debug overlay text is hidden while the inspector is open.
- `Return` or South toggles mute for the inspected lane.
- Left/Right changes the inspected lane palette.
- `Escape` or East returns to the timeline.
- Gamepad `R2 + South/East/West/North` keeps timeline transport behavior while the inspector is open.
- Muted lanes do not contribute to the master output and render dim/desaturated in the timeline.
- The inspector is intentionally a lane magnifier, not a mixer screen: no names, roles, solo, sends, routing, inserts, pan, EQ, effects, automation, compression, or faders.

## Tempo Lock Mode
- Normal mode cuts/auditions loops; Tempo Lock mode calibrates the selected loop against musical time.
- `T` or gamepad `R2 + North`: enter Tempo Lock mode; `T` cancels while active.
- While active, loop anchor editing is disabled.
- `[` / `]` or d-pad left/right: adjust draft BPM; gamepad uses fine nudges with a slow hold-repeat, and `L2 + d-pad left/right` uses faster coarse nudges.
- `,` / `.` or d-pad up/down: cycle target bars through `0.25`, `0.5`, `0.75`, `1`, `2`, `4`, `8`.
- `B` / `V`, `N` / `Shift+N`, left stick, or bumpers: nudge downbeat anchor.
- Gamepad `R2 + North`: snap downbeat anchor to the left loop marker while Tempo Lock is active.
- Right stick: pan/zoom waveform view while calibrating.
- `M`, gamepad North, or gamepad West: cycle meter among `3/4`, `4/4`, `6/8`.
- `Return` or South: apply tempo lock.
- Gamepad `L2 + R2 + South`: apply tempo lock and capture the calibrated loop to the roster.
- Gamepad `L2 + R2 + Back`: opens a confirmation modal to write source WAV tempo sidecar JSON.
- `Escape`, `T`, or East: cancel without applying.
- `Ctrl+T`, `U`, or `R2 + East`: clear/de-apply tempo lock.
- Clearing keeps the chosen params retained; if anchors move afterward, retained params are marked stale but can still seed re-entry.

## Gamepad Waveform Controls
- `South` / `Start`: play/pause
- `East`: jump playhead to loop start
- `Back`: metronome on/off
- `West` / left shoulder: select and focus loop start
- `North` / right shoulder: select and focus loop end
- Left stick: pan the waveform view
- D-pad up/down: zoom in/out
- D-pad left/right: trim the selected loop edge relative to visible zoom
- Left trigger: finer trimming
- Left trigger + right trigger + South: capture the current loop to the roster
- Left trigger + right trigger + Back: confirm writing source WAV tempo sidecar JSON
- Right trigger + South: set loop markers to the visible screen range
- Right trigger + North: enter Tempo Lock mode; while active, snap downbeat to loop start
- Right trigger + East: clear/de-apply Tempo Lock
- Right trigger + Start: cycle waveform/timeline/master mix view
- Left stick click: reset loop to full sample
- Right stick click: open sample selector in waveform view
- In selector: d-pad up/down choose, south loads, east closes
- In timeline view: left stick pans/zooms, L2 accelerates viewport movement, bumpers cycle focus, South activates focus, East cancels, R2+face buttons control transport
- In timeline lane-index focus: d-pad up/down chooses a lane, South opens Lane Inspector
- In Lane Inspector: South toggles mute, East returns to timeline, R2+face buttons control timeline transport
- In timeline move/place: d-pad left/right moves by snap units and d-pad up/down chooses lane
- In timeline track focus: L2 + left stick X glides the cursor, L2/R2 + d-pad left/right moves by bar, and L2 + d-pad up/down adjusts selected instance velocity
- In timeline roster focus: right stick click previews the selected roster clip

## Known limitations
- Gamepad support is first-pass only and needs tuning against real hardware.
- Loop playback has a very short boundary crossfade, but it still needs tuning by ear.
- Timeline has no mouse drag/drop editing, clip stretching, tempo ramps, MIDI clips, SMF save/load, project persistence, routing, effects, pan, sends, solo, or mixer UI.

## Next
- Tune gamepad editing feel and add unobtrusive UX hints.
- Better stereo playback and smoother loop crossfade.
- Optional clip quantized launch behavior.
