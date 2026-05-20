# Vaporplane Phase 1 Vertical Slice Status

## Implemented
- C11 + SDL3 + CMake app split into modules: app, input, transport, audio engine, clip, waveform.
- WAV loading from `assets/samples/` with runtime generated fallback if no WAV is available.
- Optional `*.wav.json` sidecar metadata can define source BPM and related loop metadata.
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
- Timeline view has focus zones, a tick cursor, beat/bar grid, play range handles, and range playback.
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
- `F2`: toggle waveform/timeline view
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
- `L2 + R2 + South` captures the current loop into the roster as an owned in-memory PCM clip.
- Captured clips are in-memory only; they retain source path, source loop frames, copied PCM, color, source BPM, meter, target bars/beats, and downbeat offset.
- The first captured clip initializes the master timeline BPM/meter from that clip and creates one timeline instance at tick `0`.
- Later captures add roster entries only for now.
- Timeline instances use musical ticks internally: `roster_clip_index`, `start_tick`, and `duration_ticks`.
- Captures fail cleanly with status text for a full roster, very short loops, oversized clips, or memory allocation failure.
- Timeline playback is separate from waveform loop playback: switching to timeline view stops waveform playback and waits silently.
- Timeline focus zones are `TRANSPORT`, `RULER`, `PLAY RANGE`, `TRACK AREA`, and `ROSTER`.
- `Tab` / `Shift+Tab` cycle timeline focus. Gamepad bumpers cycle focus in timeline view.
- Timeline cursor movement snaps to one beat by default and clamps to the timeline length.
- Timeline beat/bar grid is drawn from timeline ticks, BPM, and meter; bar lines are stronger than beat lines.
- Play range defaults to the full timeline. `PLAY RANGE` focus can adjust start/end handles on the beat grid or reset to full timeline.
- `Space` starts/stops timeline playback from the keyboard. Gamepad timeline transport uses the `R2` layer.
- Timeline playback starts at `play_range_start_tick`.
- If range looping is off, playback stops at `play_range_end_tick`, rewinds to `play_range_start_tick`, remains in timeline view, and stays silent.
- If range looping is on, playback wraps back to `play_range_start_tick`.
- Empty timelines do not play and show `timeline empty`.
- Timeline clips play owned PCM at natural speed; no time-stretching or pitch correction is applied yet.
- If natural PCM duration and instance tick duration disagree, playback stops at the earlier of audio end or instance end.
- Metronome enable/disable is global, but waveform mode follows waveform/transport tempo and timeline mode follows master timeline tempo.
- The audio callback avoids file IO and heap allocation; timeline/roster mutations are locked or rejected during timeline playback.

## Timeline Controls
- Keyboard `Tab` / `Shift+Tab`: cycle focus forward/back.
- Keyboard `Space`: play/pause timeline transport.
- Keyboard `Enter`: activate focused zone.
- Keyboard `Escape`: exit play-range adjustment, otherwise guarded quit.
- In `RULER` or `TRACK AREA`: `Left/Right` move cursor by one beat; `Shift+Left/Right` pans; `Up/Down` zoom.
- In `PLAY RANGE`: `Enter` enters adjustment, `1` selects start handle, `2` selects end handle, `Left/Right` nudges the selected handle, `R` resets to full timeline.
- In `ROSTER`: `Up/Down` selects a roster clip; `Enter` marks it selected.
- Gamepad bumpers: cycle focus.
- Gamepad plain South: activate focused zone.
- Gamepad plain East: back/cancel focused adjustment.
- Gamepad plain Start/Plus: reserved, no-op for now.
- Gamepad Back/Minus: metronome on/off.
- Gamepad `R2 + South`: play/pause timeline.
- Gamepad `R2 + East`: stop and rewind to play range start.
- Gamepad `R2 + West`: jump playhead and cursor to play range start without changing play state.
- Gamepad `R2 + North`: toggle play range loop.
- Gamepad `R2 + Start`: toggle waveform/timeline view.
- Gamepad right-stick click: open sample selector.

## Tempo Lock Mode
- Normal mode cuts/auditions loops; Tempo Lock mode calibrates the selected loop against musical time.
- `T` or gamepad `R2 + North`: enter/exit Tempo Lock mode.
- While active, loop anchor editing is disabled.
- `[` / `]` or d-pad left/right: adjust draft BPM; gamepad uses fine nudges with a slow hold-repeat.
- `,` / `.` or d-pad up/down: cycle target bars through `0.5`, `1`, `2`, `4`, `8`.
- `B` / `V`, `N` / `Shift+N`, left stick, or bumpers: nudge downbeat anchor.
- Right stick: pan/zoom waveform view while calibrating.
- `M`, gamepad North, or gamepad West: cycle meter among `3/4`, `4/4`, `6/8`.
- `Return` or South: apply tempo lock.
- `Escape` or East: cancel without applying.
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
- Right trigger + South: set loop markers to the visible screen range
- Right trigger + North: enter/exit Tempo Lock mode
- Right trigger + East: clear/de-apply Tempo Lock
- Right trigger + Start: toggle waveform/timeline view
- Left stick click: reset loop to full sample
- Right stick click: open sample selector
- In selector: d-pad up/down choose, south loads, east closes
- In timeline view: bumpers cycle focus, South activates focus, East cancels, R2+face buttons control transport

## Known limitations
- Gamepad support is first-pass only and needs tuning against real hardware.
- Loop playback has a very short boundary crossfade, but it still needs tuning by ear.
- Timeline has no drag/drop clip placement, clip stretching, tempo map, MIDI clips, SMF save/load, or project persistence yet.

## Next
- Tune gamepad editing feel and add unobtrusive UX hints.
- Better stereo playback and smoother loop crossfade.
- Optional tempo map and clip quantized launch behavior.
