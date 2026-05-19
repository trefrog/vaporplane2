# Vaporplane Phase 1 Vertical Slice Status

## Implemented
- C11 + SDL3 + CMake app split into modules: app, input, transport, audio engine, clip, waveform.
- WAV loading from `assets/samples/demo_loop.wav` with runtime generated fallback if missing.
- `AudioClip` metadata for path/rate/channels/frames/loop/music metadata/gain/playback rate.
- Transport with BPM, PPQN, beats-per-bar, beat-unit, play/pause, tick/second conversion.
- Metronome pip on beat with downbeat accent and toggle.
- Looping sample playback with loop-start/loop-end edits while running.
- Waveform rendering with loop markers and playhead marker.
- Keyboard controls and first-pass gamepad editing controls.

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
- `Left/Right`: pan view
- `Up/Down`: zoom in/out
- `A/D`: move loop start (`Shift` = coarse)
- `J/L`: move loop end (`Shift` = coarse)
- `1`: focus loop start
- `2`: focus loop end
- `R`: reset loop to full sample
- `Home`: jump playhead to loop start

## Gamepad Controls
- `South` / `Start`: play/pause
- `East`: jump playhead to loop start
- `Back`: metronome on/off
- `West` / left shoulder: select and focus loop start
- `North` / right shoulder: select and focus loop end
- Left stick: pan the waveform view
- D-pad up/down: zoom in/out
- Right stick left/right: trim the selected loop edge
- D-pad left/right: fine-trim the selected loop edge
- Right trigger: faster trimming
- Left trigger: finer trimming
- Left stick click: reset loop to full sample

## Known limitations
- Gamepad support is first-pass only and needs tuning against real hardware.
- Playback output is mono-summed from the source clip.
- No tempo map, no MIDI clips yet.

## Next
- Tune gamepad editing feel and add unobtrusive UX hints.
- Better stereo playback and smoother loop crossfade.
- Optional tempo map and clip quantized launch behavior.
