We are working on Vaporplane, a C11 + SDL3 gamepad-first sample loop cutter and tick timeline sequencer. Treat it as a compact musical instrument / sample archaeology tool, not a conventional DAW.

Stage 1 only: create the new Master Mix screen/mode and make it conceptually solid.

Goal:
Add a dedicated Master Mix screen that can eventually house master bus controls, meters, reverb controls, and future FX-unit controls.

Important:
Do not implement FX chaining yet.
Do not implement reverb yet.
Do not add audible DSP changes.
Do not add plugin hosting, track effects racks, a full mixer, automation lanes, or DAW bloat.

The Master Mix screen should establish the UI home for:
- Master level
- Master peak / clip state if currently available or easy to expose
- Reverb section placeholder
- FX chain section placeholder
- Future MIDI mapping hints/placeholders if clean

It is okay for some controls to be placeholders in this stage, but they should feel intentionally placed, not like random debug text.

Design priorities:
- Gamepad-first navigation.
- Keyboard-compatible navigation.
- Compact, tactile, instrument-like.
- This should feel like a focused master panel, not a spreadsheet.
- Preserve the current main workflow and playback behavior.

Implementation requirements:
- Add a screen/mode enum/state if needed.
- Add input handling to enter/leave the Master Mix screen using whatever screen navigation pattern already exists.
- Render a clear title/header: Master Mix.
- Render grouped sections, likely:
  MASTER
  REVERB
  FX CHAIN
  MIDI / CONTROL, maybe placeholder only
- If master peak/clipping data already exists, display it.
- If it does not exist yet, add only a minimal safe placeholder or TODO comment. Do not do a big audio-engine refactor in this stage.
- Avoid touching audio code unless necessary for read-only status display.

Definition of done:
- Vaporplane has a reachable Master Mix screen.
- It is visually and navigationally coherent.
- No audible behavior changes.
- No reverb.
- No FX chain infrastructure.
- Existing screens and playback still work.

Stop after Stage 1. Report:
- files changed
- how to open/close/navigate the Master Mix screen
- what is real vs placeholder
- any architecture notes for Stage 2