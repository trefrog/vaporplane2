We are working on Vaporplane, a C11 + SDL3 gamepad-first sample loop cutter and tick timeline sequencer.

Stage 3 only: implement the first real built-in FX unit: a serious but bounded master reverb.

Prerequisites:
Stage 1 added the Master Mix screen.
Stage 2 added a silent/no-op master FX chain.
Use that infrastructure. Do not bypass it with a one-off reverb hack.

Goal:
Add one shared master reverb unit as a built-in FX unit.

Important identity:
Vaporplane is not a conventional DAW, but the reverb should be taken seriously. The UI can remain compact and tactile, but the DSP should not be a flimsy tin can unless the user intentionally dials it that way.

Non-goals:
- No plugin hosting.
- No convolution reverb.
- No per-lane effect racks.
- No full mixer UI.
- No automation lanes.
- No pitch-preserving stretch.
- No automatic BPM detection.
- No dynamic allocation inside the audio callback.

DSP requirements:
- Implement a bounded algorithmic reverb suitable for real-time C audio.
- Freeverb-ish, Moorer-ish, or a small FDN design is acceptable. Choose based on codebase fit and clarity.
- One shared master send/return reverb, not one reverb per clip.
- Include damping in the feedback path.
- Include practical reverb tone shaping:
  - low cut on the reverb path
  - high cut on the reverb path
- Include denormal/tiny-value guards on persistent feedback/filter state.
- Smooth parameter changes to avoid zipper noise.
- Avoid metallic ringing as much as reasonable for a first pass.
- Avoid excessive CPU use, but do not make the algorithm fake or throwaway.

Minimum useful parameters:
- Reverb Enabled
- Reverb Send
- Reverb Return
- PreDelay
- Decay
- Size
- Diffusion
- Damping
- Low Cut
- High Cut
- Width

Optional only if still clean:
- Mod Depth
- Mod Rate

Master Mix screen:
- The Master Mix screen should expose the reverb unit’s real parameters.
- Keep it compact, grouped, and gamepad-friendly.
- Do not make it a DAW mixer.
- It should feel like a dedicated hardware-style master/reverb panel.

Audio architecture:
- Dry signal should remain clear.
- Wet return should be controllable.
- Reverb should be bypassable.
- Bypass should not cause clicks if easy to avoid.
- Include a reset/clear-tail helper internally if useful.
- Keep all buffers owned and initialized outside the audio callback.

Definition of done:
- The master FX chain has one real built-in reverb unit.
- The Master Mix screen can control the reverb.
- Reverb has low-cut and high-cut controls.
- Reverb sounds usable, not obviously placeholder.
- Reverb can be bypassed.
- No callback allocations.
- Existing sample playback/timeline behavior is preserved.

Stop after Stage 3. Report:
- files changed
- chosen reverb architecture
- parameter list and ranges
- how low cut/high cut are applied
- denormal protection used
- audio-thread safety notes
- subjective tuning notes / known limitations