# Phase 1 vertical-slice status

## Implemented
- C11 + SDL3 modular demo app with transport, clip loader/generator, waveform rendering, input handling, and audio engine.
- Audio clip metadata model with loop start/end, gain, playback rate, and source timing defaults.
- WAV load attempt from `assets/samples/demo.wav`; fallback procedural sample generation if missing.
- Real-time loop playback with adjustable loop bounds while running and a short crossfade near loop wrap.
- Keyboard controls for play/pause, loop edits, zoom/pan, focus, reset, and playhead jump.
- Basic gamepad hook (button south toggles play/pause), intentionally minimal for this slice.
- Metronome pip mixed in audio output and toggle with `M`.

## Build and run
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build
./build/vaporplane
```

## Controls
- Escape: quit
- Space: play/pause
- M: toggle metronome
- Left/Right: pan waveform view
- Up/Down: zoom in/out
- A/D: move loop start left/right (Shift = coarse)
- J/L: move loop end left/right (Shift = coarse)
- 1: focus loop start
- 2: focus loop end
- R: reset loop to full clip
- Home: jump playhead to loop start

## Known limitations
- Gamepad mapping is intentionally incomplete for this phase.
- Transport is fixed-tempo (single BPM, no tempo map UI).
- Procedural fallback clip is used when no WAV exists.

## Next
- Expand gamepad editing controls (trigger-focus + dpad marker nudging).
- Add HUD labels for BPM/loop frames/metronome state.
- Optional clip slots and MIDI note-to-clip trigger skeleton.
