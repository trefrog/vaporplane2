# vaporplane

Vaporplane is a small C11 + SDL3 sample-loop instrument for vaporwave-style
editing, loop archaeology, and gamepad-first timeline sketching.

It is built around a waveform-as-playfield workflow: load an audio file, trim a
loop, calibrate it against musical time, capture it into a roster, and place
those captured clips on a compact tick-based timeline. The target is an
immediate, tactile instrument rather than a conventional DAW.

[⬇️ see it in action on youtube 🔴]
[![Vaporplane demo](https://img.youtube.com/vi/MDcz9ypPbWk/maxresdefault.jpg)](https://www.youtube.com/watch?v=MDcz9ypPbWk)

Not a DAW.

## Current Shape

- Waveform editor with smooth pan/zoom, loop start/end markers, playhead, and
  live loop audition.
- `assets/samples/` browser for WAV files, plus FLAC, MP3, AIFF, and AIF when
  libsndfile is available, with generated fallback audio if no sample is
  available.
- Optional audio-file sidecars such as `*.wav.json`, `*.flac.json`, or
  `*.mp3.json` for source BPM, meter, target length, and downbeat metadata.
- Tempo Lock mode for calibrating loops by BPM, downbeat, meter, and target
  bars before capture.
- Roster capture: the current loop becomes an owned in-memory PCM clip with
  source lineage and musical metadata.
- Tick-based master timeline with 8 lanes, tempo markers, beat/bar grid,
  play-range handles, clip placement, movement, velocity, and same-lane overlap
  blocking.
- Timeline tape speed control for audible BPM/pitch-style transport changes
  while keeping the tempo map as the musical authority.
- Master Mix view with master meter, clipping indicator, and a real built-in
  `Reverb 1` master FX unit.
- Lane Inspector view with per-lane mute, palette selection, peak/clip state,
  and a small post-lane analyzer.
- Project save/load as self-contained `.vapor/` bundles containing
  `project.json`, `timeline.mid`, `surfaces.json`, and bundled samples.

## Build

Requirements:

- CMake 3.20+
- C11 compiler
- SDL3
- libsndfile optional, for FLAC/MP3/AIFF import

On macOS with Homebrew SDL3 and optional libsndfile:

```bash
brew install sdl3 libsndfile
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3);$(brew --prefix libsndfile)"
cmake --build build
./build/vaporplane
```

To force the WAV-only fallback build, configure with
`-DVAPORPLANE_ENABLE_SNDFILE=OFF`.

On Windows, the supported local path is MSYS2 MINGW64. Install the needed
MINGW64 packages first, then build from a normal Windows shell with:

```bat
scripts\build-win-mingw.cmd
```

To create an unsigned Windows tester zip:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Local
```

The package script defaults to `C:\msys64`, Release, Ninja,
`build-windows-release`, and `dist\Vaporplane-windows-x64.zip`. Use
`-Msys2Root`, `-BuildDir`, `-DistDir`, `-Sdl3Dll`, or `-Generator` to override
those defaults.

If SDL3 is installed somewhere else, point `CMAKE_PREFIX_PATH` at that install
prefix or make sure CMake can find the SDL3 package config.

## Samples

Put audio files in:

```text
assets/samples/
```

Vaporplane always lists WAV files. If libsndfile is found at build time, it also
lists FLAC, MP3, AIFF, and AIF files. Sidecar metadata can live next to a sample
as:

```text
sample.wav.json
sample.flac.json
sample.mp3.json
```

The sidecar parser currently understands simple tempo-lock fields such as BPM,
meter, target bars, and downbeat frame. The waveform view can also write a tempo
sidecar for the active source after confirmation.

## Core Workflow

1. Open the app and load an audio file from the sample selector.
2. Trim the loop start/end markers in the waveform view.
3. Use Tempo Lock when the loop needs musical calibration.
4. Capture the loop into the roster.
5. Switch to the timeline and place roster clips across the 8 lanes.
6. Use the Project menu to save a `.vapor/` bundle under `exports/projects/`.

Captured clips are copied into memory, so later edits can derive new roster
clips from existing roster clips without losing the original source lineage.

## Keyboard Controls

Global and waveform:

- `F1`: show/hide controls legend
- `F2`: cycle Waveform, Timeline, and Master Mix views
- `Space`: play/pause
- `M`: metronome on/off
- `Escape`: close panel/mode; from Waveform or Master Mix, return to Timeline;
  from idle Timeline, open the Project menu
- `Tab`: sample selector in Waveform; focus cycling in Timeline and Master Mix
- `Left` / `Right`: pan waveform or move timeline cursor, depending on focus
- `Up` / `Down`: zoom waveform or move within focused timeline controls
- `A` / `D`: move loop start
- `J` / `L`: move loop end
- `1` / `2`: focus loop start/end, or select play-range handles in Timeline
- `R`: reset loop or focused timeline control where applicable
- `Home`: jump waveform playhead to loop start

Tempo Lock:

- `T`: enter/cancel Tempo Lock mode
- `[` / `]`: adjust draft BPM
- `,` / `.`: cycle target bar length
- `B` / `V` / `N`: nudge or adjust downbeat controls
- `M`: cycle meter
- `Return`: apply Tempo Lock
- `Escape`: cancel without applying
- `Ctrl+T` or `U`: clear/de-apply Tempo Lock

Timeline:

- `Tab` / `Shift+Tab`: cycle focus zones
- `Enter`: activate focused zone, select/drop clips, or apply menu item
- `C`: open the focus-aware context menu
- `[` / `]`: adjust selected instance velocity, or adjust a tempo event when the
  ruler cursor is on one
- `0`: reset tape speed from transport focus

## Gamepad Notes

The app is intentionally gamepad-first, with keyboard controls kept available
for development and precise editing.

Common mappings:

- South / Start: play or activate
- East: back/cancel or jump to loop start in waveform view
- Back/Minus: metronome
- Bumpers: select loop edges in Waveform, cycle focus in Timeline/Master Mix
- Left stick: pan waveform or timeline view
- D-pad: trim, zoom, move cursor, edit ghosts, or adjust focused controls
- Left trigger + Start: open the Timeline Project menu
- Right trigger + Start: cycle main views
- Right trigger + South: timeline play/pause or waveform frame-grip commit
- Right trigger + West: set timeline playhead to cursor
- Right trigger + East: rewind timeline to play range start
- Left trigger + Right trigger + South: capture the calibrated/current loop
- Right stick click: open sample selector in Waveform or preview roster clip in
  Timeline roster focus

Gamepad support is still being tuned against real hardware, so the in-app
controls legend is the best source of truth while editing.

## Project Bundles

Vaporplane projects are folder bundles:

```text
project_name.vapor/
  project.json
  timeline.mid
  surfaces.json
  samples/
```

`timeline.mid` is the portable musical spine. `project.json` keeps stable
project, sample, and roster identities. `surfaces.json` stores Vaporplane-only
state such as master settings, tape speed, play range, lane state, and FX
parameters.

See [docs/project_format.md](docs/project_format.md) for the format contract.

## Repository Map

- `src/`: app, input, audio engine, transport, waveform, clip, and project
  validation implementation.
- `include/`: public headers for the app modules.
- `assets/samples/`: local WAV sample library and sidecar metadata.
- `docs/`: design notes, status, project format, and feature roadmaps.
- `attic/`: older experiments kept out of the main build.

## Limitations

- No mouse drag/drop editing yet.
- No time-stretching or pitch correction for timeline clips.
- No tempo ramps, MIDI clip editor, routing matrix, lane FX, sends, solo, pan,
  EQ, compression, or full mixer UI.
- Project loading currently targets the app's bundle format and newest saved
  project flow, not a general-purpose file picker.
- The audio callback is kept allocation-free; structural timeline edits stop or
  lock playback before mutating shared state.

## Further Reading

- [docs/phase1_status.md](docs/phase1_status.md): current feature inventory and
  detailed controls.
- [docs/project_format.md](docs/project_format.md): `.vapor/` bundle design.
- [docs/tape_speed.md](docs/tape_speed.md): tape-speed design notes.
- [docs/new_timeline_tempo_paradigm.md](docs/new_timeline_tempo_paradigm.md):
  timeline and tempo-map thinking.

## License

Vaporplane's original source code is licensed under the MIT License.

Third-party code, libraries, and bundled assets may have their own licenses.
See `THIRD_PARTY_NOTICES.md` for details. Sample libraries and user-provided
WAV files are not automatically covered by Vaporplane's code license.
