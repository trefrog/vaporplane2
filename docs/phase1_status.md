# Vaporplane Phase 1 Vertical Slice Status

## Implemented
- C11 + SDL3 + CMake app split into modules: app, input, transport, audio engine, clip, waveform.
- WAV loading from `assets/samples/` with runtime generated fallback if no WAV is available.
- In-app sample selector backed by reusable `load_clip_from_path(App *, const char *)`.
- `AudioClip` metadata for path/rate/channels/frames/loop/music metadata/gain/playback rate.
- Transport with BPM, PPQN, beats-per-bar, beat-unit, play/pause, tick/second conversion.
- Metronome pip on beat with downbeat accent and toggle.
- Looping sample playback with loop-start/loop-end edits while running.
- Waveform rendering with loop markers and playhead marker.
- Smooth target-based waveform panning/zooming.
- Zoom-relative keyboard and gamepad loop marker trimming.
- Dedicated Tempo Lock mode for manual BPM/downbeat/meter/length calibration.
- Keyboard controls and first-pass gamepad editing controls with R2 chord support.

## Build / Run
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build
./build/vaporplane
```

## Controls
- `Escape`: quit
- `Space`: play/pause
- `M`: metronome on/off
- `T`: enter/exit Tempo Lock mode
- `[` / `]`: adjust manual transport BPM
- `Tab`: open/close sample selector
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

## Tempo Lock Mode
- Normal mode cuts/auditions loops; Tempo Lock mode calibrates the selected loop against musical time.
- `T` or gamepad `R2 + North`: enter/exit Tempo Lock mode.
- While active, loop anchor editing is disabled.
- `[` / `]` or d-pad left/right: adjust draft BPM.
- `,` / `.` or d-pad up/down: cycle target bars through `0.5`, `1`, `2`, `4`, `8`.
- `B` / `V`, `N` / `Shift+N`, left stick, or bumpers: nudge downbeat anchor.
- `M`, gamepad North, or gamepad West: cycle meter among `3/4`, `4/4`, `6/8`.
- `Return` or South: apply tempo lock.
- `Escape` or East: cancel without applying.
- `Ctrl+T`, `U`, Back, or `R2 + East`: clear/de-apply tempo lock.
- Clearing keeps the chosen params retained; if anchors move afterward, retained params are marked stale but can still seed re-entry.

## Gamepad Controls
- `South` / `Start`: play/pause
- `East`: jump playhead to loop start
- `Back`: metronome on/off
- `West` / left shoulder: select and focus loop start
- `North` / right shoulder: select and focus loop end
- Left stick: pan the waveform view
- D-pad up/down: zoom in/out
- Right stick left/right: trim the selected loop edge relative to visible zoom
- D-pad left/right: fine-trim the selected loop edge relative to visible zoom
- Left trigger: finer trimming
- Right trigger + South: set loop markers to the visible screen range
- Right trigger + North: enter/exit Tempo Lock mode
- Right trigger + East: clear/de-apply Tempo Lock
- Left stick click: reset loop to full sample
- Right stick click: open sample selector
- In selector: d-pad up/down choose, south loads, east closes, back refreshes

## Known limitations
- Gamepad support is first-pass only and needs tuning against real hardware.
- Loop playback has a very short boundary crossfade, but it still needs tuning by ear.
- No tempo map, no MIDI clips yet.

## Next
- Tune gamepad editing feel and add unobtrusive UX hints.
- Better stereo playback and smoother loop crossfade.
- Optional tempo map and clip quantized launch behavior.
