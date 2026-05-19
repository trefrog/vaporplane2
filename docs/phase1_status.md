# Vaporplane Phase 1 Vertical Slice Status

## Implemented
- C11 + SDL3 + CMake app split into modules: app, input, transport, audio engine, clip, waveform.
- WAV loading from `assets/samples/demo_loop.wav` with runtime generated fallback if missing.
- `AudioClip` metadata for path/rate/channels/frames/loop/music metadata/gain/playback rate.
- Transport with BPM, PPQN, beats-per-bar, beat-unit, play/pause, tick/second conversion.
- Metronome pip on beat with downbeat accent and toggle.
- Looping sample playback with loop-start/loop-end edits while running.
- Waveform rendering with loop markers and playhead marker.
- Keyboard controls and partial gamepad stub.

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

## Known limitations
- Gamepad support is minimal in this phase and needs full control mapping.
- Playback output is mono-summed from the source clip.
- No tempo map, no MIDI clips yet.

## Next
- Full gamepad editing mode and UX hints.
- Better stereo playback and smoother loop crossfade.
- Optional tempo map and clip quantized launch behavior.
