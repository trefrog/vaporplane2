We are working on Vaporplane, a C11 + SDL3 gamepad-first sample loop cutter and tick timeline sequencer.

Stage 2 only: add internal infrastructure for a master FX chain, with no audible changes.

Prerequisite:
Assume Stage 1 already added a dedicated Master Mix screen. Preserve it.

Goal:
Create a small, bounded internal architecture for master FX units so later reverb/filter units can live in a proper chain.

Important:
Do not implement reverb yet.
Do not implement audible filters yet.
Do not change the sound.
Do not add plugin hosting.
Do not add per-lane effects racks.
Do not add a full mixer.
Do not add automation.
Do not add MIDI learn yet.
Do not allocate memory inside the audio callback.

Architecture goal:
Add a lightweight internal concept of an FX unit, suitable for built-in DSP units only.

The chain should support future built-in units such as:
- Reverb
- Low cut / high cut
- Delay
- Soft clip / limiter
But only infrastructure should be added in this stage.

Suggested direction, adapt to the existing codebase:
- A stable enum of built-in FX unit types.
- A small FX unit struct with enabled/bypass state.
- A process function pointer or switch-based processor.
- A master FX chain object owned by the audio engine or master bus.
- A no-op/pass-through unit or empty chain for now.
- Parameter IDs or a parameter registry foundation if clean.
- Parameter values should be stable and future MIDI-mappable.
- Any future audio parameter changes should be smoothable, but do not overbuild.

Critical audio rule:
The output should sound identical before and after this stage. If a no-op FX chain is called, it must pass audio through unchanged except for unavoidable float roundoff. No wet/dry, no filters, no gain changes.

Master Mix screen update:
- Show the FX chain section as real infrastructure now.
- It can list something like:
  FX CHAIN
  [empty]
  or
  Slot 1: Empty
- Do not expose fake controls that do nothing unless clearly marked as placeholders.

Definition of done:
- There is a master FX chain scaffold.
- It is integrated at the correct master-bus point.
- It produces no audible change.
- No reverb unit exists yet.
- No real filter unit exists yet.
- Master Mix screen reflects the chain’s existence.
- No audio callback allocations.

Stop after Stage 2. Report:
- files changed
- where the FX chain lives
- how audio passes through it
- why it should be audibly unchanged
- how Stage 3 should add the first real unit