# Phase 1 Playable Vertical Slice Status

## Implemented
- C11 + SDL3 + CMake app with modular components (`app`, `transport`, `audio_engine`, `clip`, `waveform`, `input`).
- Clip metadata model with loop start/end, source rhythm metadata defaults, playback rate, and gain.
- WAV loading from `assets/samples/demo.wav` with runtime generated fallback sample when file is absent.
- Continuous loop playback with live loop-bound edits and soft edge fade to reduce clicks.
- Simple fixed-tempo transport (BPM/PPQN/time<->tick conversion/play-pause/current tick + seconds).
- Metronome pip mixed in audio with downbeat accent and keyboard toggle.
- Waveform render with loop start/end/playhead markers plus pan/zoom/focus controls.

## Build and Run
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build
./build/vaporplane
```

## Controls
- `Escape`: quit
- `Space`: play/pause
- `M`: toggle metronome
- `Left/Right`: pan view
- `Up/Down`: zoom in/out
- `A/D`: move loop start (hold Shift for coarse)
- `J/L`: move loop end (hold Shift for coarse)
- `1`: focus near loop start
- `2`: focus near loop end
- `R`: reset loop to full sample
- `Home`: jump playhead to loop start

## Known limitations
- No gamepad mapping yet.
- No tempo map (single fixed tempo only).
- Playback-rate modulation/time-stretch is not implemented.
- Waveform drawing is a minimal 2D view, not full pseudo-3D.

## Next
- Add gamepad loop editing and scrub controls.
- Add a tiny clip-slot layer so future MIDI note mapping is straightforward.
- Optionally add multiple clip loading and lane/bank abstraction.
