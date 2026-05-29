# Vaporplane Project Browser, Keyboard, and Preview Roadmap

## Summary

This roadmap covers the next practical burst of `.vapor/` project-file work for Vaporplane.

The repo is already past the original "save first, load later" plan. Vaporplane can currently save and load self-contained project bundles from `exports/projects/`: the Project menu has `Save project...`, `Open project...`, and `Quit`; save writes the v3 manifest, MIDI timeline, surfaces sidecar, and canonical bundled sample WAVs; open loads the newest saved bundle from that directory.

The missing pieces are now narrower and more concrete:

- Extract validation out of the load path so browsing and loading share the same checks.
- Replace "open newest project" with a real gamepad-first project browser.
- Add `preview.wav` as a derived browser artifact.
- Add reusable text entry for named saves and later renames.
- Keep project loading deliberate and validated, rather than hidden behind preview or browsing.

Vaporplane is still a compact gamepad-first sample loop cutter and tick timeline sequencer. Do not turn this into a desktop DAW file manager.

## Current Bundle Shape

A Vaporplane project bundle is a folder:

```text
project_name.vapor/
  project.json
  timeline.mid
  surfaces.json
  samples/
```

Near-term bundle shape adds one optional derived artifact:

```text
project_name.vapor/
  preview.wav
```

Authoritative roles:

- `project.json` owns bundle identity, stable IDs, sample mappings, roster clip mappings, loop anchors, source lineage, and MIDI channel/note bindings.
- `timeline.mid` owns musical timing, tempo events, note events, and future CC envelope events.
- `surfaces.json` owns Master page state, FX unit state, parameter IDs, current parameter values, lane state, play range, tape speed, and future CC mappings.
- `samples/` owns bundled canonical WAV assets used for playback after load.
- `preview.wav` is a browser preview artifact only. It is never required for project validity or playback.

## Current Repo Reality

Already implemented:

- Bundle constants live in `include/project_format.h`.
- Project menu UI exists in Timeline view.
- `app_save_project_bundle()` writes new bundles and refuses to overwrite an existing path.
- Default save writes to the first available `exports/projects/vaporplane_project_###.vapor`.
- Save writes:
  - `project.json` format `vaporplane.project`, version `3`.
  - `timeline.mid` with tempo and lane note tracks.
  - `surfaces.json` format `vaporplane.surfaces`, version `1`.
  - `samples/*.wav` as canonical 48 kHz stereo 32-bit float WAVs.
- `app_load_project_bundle()` stages a bundle before applying it, then swaps roster, timeline, project ID/name, master gain, and Reverb 1 state.
- Default open finds the newest `.vapor` folder in `exports/projects/` and loads it.
- Load supports legacy project versions `1` through `3` enough to import and upgrade identity in memory.
- Existing roster preview playback can play loaded roster clips from memory.
- Existing WAV sample selector provides a simple list overlay pattern worth reusing.

Not implemented yet:

- No standalone project validator API.
- No browser list of project bundles.
- No project selection UI beyond "newest saved bundle".
- No `preview.wav` export.
- No browser-time preview playback of a project without loading it.
- No reusable text-entry modal or named Save As.
- No rename flow.
- No dirty-state prompt or overwrite UI.

Known risks in the current implementation:

- Load currently doubles as validation; browser code should not need to decode sample WAVs or mutate app state to know whether a project is usable.
- Relative sample paths from `project.json` are joined to the bundle path during load. They need an explicit "does not escape bundle root" check before the project browser makes arbitrary bundles convenient to select.
- JSON parsing is intentionally small and local, but validation should centralize its assumptions so save, load, and browser do not drift.
- Open currently loads the newest bundle, which is convenient for smoke testing but not a real user workflow.
- Save naming is automatic; the user cannot give a project a display name or bundle-safe folder name yet.

## Core Principle

Do not make the browser load whole projects just to preview or inspect them.

Instead:

- Validation checks project structure before load.
- Browser scans folders and runs cheap validation.
- Focused selection can run deeper validation.
- Save/export may write a short `preview.wav`.
- Browser preview plays `preview.wav` directly.
- Explicit open uses the existing project load path after validation passes.

This keeps browsing lightweight and prevents invisible timeline/audio-engine state from being created behind the user's back.

---

# Stage 1 - Validator Extraction and Load Hardening

## Goal

Create a reusable project validator and make the existing load path depend on it.

This is not a new feature stage. It is the foundation needed before the browser makes project loading easier to reach.

## Validator Shape

Add a small public project validation API, likely in new files such as:

```text
include/project_validation.h
src/project_validation.c
```

Suggested result:

```text
VALID
WARNING
INVALID
UNKNOWN
```

Each result should include:

- Status.
- Short display reason.
- Optional detailed reason for logs/status text.
- Best-effort project display name.
- Resolved bundle path.
- Whether `preview.wav` exists and appears playable.

Example reasons:

```text
missing project.json
unsupported project version
missing timeline.mid
missing surfaces.json
sample file missing
unsafe relative path
invalid MIDI header
preview.wav missing
noncanonical preview.wav
```

## Quick Validation

Runs when the project browser opens or refreshes.

Should check:

- Path is a directory.
- Directory has `.vapor` suffix.
- `project.json` exists.
- `project.json` parses enough to read format, version, display name, file references, sample paths, and roster mappings.
- Format is `vaporplane.project`.
- Version is supported by current load code.
- Referenced `timeline.mid` exists.
- Referenced `surfaces.json` exists.
- `samples/` exists.
- Referenced sample paths are relative, normalized enough for validation, and cannot escape the bundle root.
- Referenced sample files exist.
- Required manifest identity mappings are present.

Do not decode WAVs.
Do not parse full MIDI.
Do not load samples into the audio engine.
Do not mutate project folders.

## Full Validation

Runs when a project receives focus and immediately before load.

Should check everything quick validation checks, plus:

- `surfaces.json` exists and parses enough to read format/version.
- `surfaces.json` has supported format/version.
- `timeline.mid` has a plausible Standard MIDI File header.
- MIDI track chunks are structurally valid where practical.
- Required bundled WAV files have plausible WAV headers.
- Warn if bundled WAVs are not canonical:
  - 48000 Hz
  - stereo
  - 32-bit IEEE float WAV
- Warn if `preview.wav` is missing, invalid, or noncanonical.
- Validate ID references:
  - `roster_clip_id -> sample_id`
  - `sample_id -> samples/*.wav`
  - MIDI binding collisions are either rejected or clearly defined.
- Validate that paths do not escape the bundle root.

## Load Integration

Before `app_load_project_bundle()` stages a bundle:

- Run full validation.
- Refuse `INVALID`.
- Allow `WARNING`, but surface the warning in status text.
- Keep legacy version support aligned with validator support.
- Keep the staged-load behavior: do not partially replace the running project if validation or staging fails.

## Non-goals

- No browser UI yet.
- No preview rendering yet.
- No project migration system.
- No rewrite of the whole JSON layer unless validation becomes impossible without it.

## Definition of Done

- Browser-ready quick validation exists.
- Load-ready full validation exists.
- Existing save/load behavior still works.
- Unsafe project-relative paths are rejected before sample loading.
- Validation does not load audio into the engine or mutate the bundle.
- `app_load_project_bundle()` uses the same validator that the browser will use.

---

# Stage 2 - Project Browser Over Existing Load

## Goal

Replace "Open project loads newest bundle" with a real `.vapor/` project browser that uses the validator and existing load path.

This stage should not wait for preview WAVs or the keyboard. It can be useful immediately because real load already exists.

## Browser Behavior

On browser open/refresh:

- Scan the project export directory currently used by save/open.
- List folders ending in `.vapor`.
- Run quick validation on each project.
- Show project display name from `project.json` when available.
- Fall back to folder name if needed.
- Show validation state visually.

Suggested visual status:

```text
VALID    normal text
WARNING  caution color
INVALID  error color
UNKNOWN  dim text
```

When the selected project changes:

- Run full validation on the focused project.
- Update a compact reason/details area.
- Enable open for `VALID` and probably `WARNING`.
- Disable open for `INVALID` and `UNKNOWN`.

## UI Fit

Reuse the existing overlay/list patterns where clean:

- Sample selector list behavior.
- Timeline Project menu input conventions.
- `Up/Down` and d-pad selection.
- `A/Enter` to open.
- `B/Escape` to back out.

Suggested actions:

```text
A / Enter       validate and open selected project
B / Esc         back
Y / key         refresh/rescan
X / key         preview later, disabled until Stage 3
```

## Open Behavior

Opening a project should:

- Stop timeline playback and roster preview through existing app/audio functions.
- Run full validation again immediately before loading.
- Call `app_load_project_bundle()` only after validation allows it.
- Leave the current project untouched if validation or staging fails.
- Return to Timeline view on success.

## Non-goals

- No `preview.wav` playback yet.
- No rename/delete.
- No OS-native file dialog.
- No arbitrary filesystem browsing.
- No dirty-state prompt unless already easy to add safely.

## Definition of Done

- `Open project...` enters a project browser instead of loading the newest bundle.
- Browser lists saved `.vapor/` bundles.
- Quick validation runs on refresh.
- Full validation runs on focus and before open.
- Invalid projects are visibly unavailable.
- Existing save behavior remains intact.

---

# Stage 3 - Save and Play `preview.wav`

## Goal

Write a short `preview.wav` during bundle save and let the project browser play it without loading the full project.

## Preview Role

`preview.wav` is a derived artifact.

It is not authoritative.
It is not required for project validity.
It may be regenerated.
It may be missing.
It may become stale.
Project load must never depend on it.

## Format

Write canonical preview audio:

```text
preview.wav
48000 Hz
stereo
32-bit IEEE float WAV
WAV format code 3
```

Consider adding this constant:

```text
VAPORPLANE_PROJECT_PREVIEW_FILENAME "preview.wav"
```

## Preview Length

Render approximately 7 seconds.

Suggested V1 selection rule:

1. Prefer the first timeline span with clip activity.
2. If no timeline clips exist, fall back to the first roster clip.
3. If no useful audio exists, skip preview and report a warning.

Do not implement preview marker UI yet.

## Rendering Scope

Start conservative:

- It is acceptable for V1 preview rendering to be dry/basic if that avoids destabilizing the real-time engine.
- If existing offline render plumbing can include tape speed, master gain, and FX safely, include them.
- If not, document the limitation in code/status text and keep the file structurally valid.

## Implementation Rules

- No preview rendering inside the audio callback.
- No filesystem work inside the audio callback.
- No invisible project loading.
- No browser-time rendering.
- Preview rendering happens during save/export only.
- If preview rendering fails, save may still succeed if all authoritative project files succeeded.

## Browser Playback

If focused project has a valid `preview.wav`:

- Preview action plays it directly.
- Preview playback should not load the full project.
- Preview playback should not mutate current timeline/project state.

If preview is missing:

- Show warning.
- Preview action is disabled or produces a short status message.

Implementation can either:

- Add a small preview-file playback path to the audio engine, or
- Use an SDL audio stream owned by the browser/modal layer.

Keep it isolated from roster/timeline state.

## Validator Interaction

Full validation should warn if:

- `preview.wav` is missing.
- `preview.wav` has an invalid WAV header.
- `preview.wav` is not canonical format.

Missing or broken preview should not invalidate the project.

## Non-goals

- No preview waveform UI.
- No preview marker.
- No preview cache invalidation system.
- No project load changes beyond validation already done in Stage 1.

## Definition of Done

- Save/export may write `preview.wav`.
- `preview.wav` is canonical when written.
- Failed preview export does not corrupt or invalidate the project save.
- Validator reports preview warnings.
- Browser can play valid previews without loading projects.
- Existing timeline, roster preview, and sample selector playback remain intact.

---

# Stage 4 - Reusable Text Entry and Named Save As

## Goal

Add reusable gamepad-first text entry, then use it for named project saving.

The old roadmap split keyboard infrastructure and Save As into separate large stages. In this repo, they should be one focused feature because auto-numbered save already exists and the first real consumer is project naming.

## Text Entry Requirements

The modal must be reusable, not hardcoded to project saving.

It should support:

- Initial text.
- Max length.
- Confirm.
- Cancel.
- Backspace/delete.
- Cursor movement.
- Physical keyboard input.
- Gamepad input.
- Field modes:
  - display name
  - filename-safe name
  - search/filter text later

## Gamepad Controls

Suggested:

```text
D-pad / left stick    select character
A                     insert selected character
B                     backspace
X                     space / underscore depending on mode
Y                     shift / page toggle
Left shoulder         cursor left
Right shoulder        cursor right
Start/Menu            confirm
Esc / Back            cancel
```

## Physical Keyboard Capture

When the modal is open:

- Text input should go to the modal, not the main app.
- Printable characters insert text.
- Backspace deletes.
- Delete removes forward if supported.
- Left/right arrows move cursor.
- Enter confirms.
- Esc cancels.

Use SDL text input APIs for text entry rather than interpreting every printable key manually.

## Save As Flow

Replace or complement the current auto-numbered save menu item:

```text
Project Menu
  -> Save project...
    -> Text entry asks for project display name
      -> bundle-safe folder name is generated
        -> app_save_project_bundle() writes .vapor bundle
```

Save rules:

- Use `.vapor` bundle suffix.
- Generate a filesystem-safe folder name from the display name.
- Do not overwrite an existing bundle unless a later overwrite UI exists.
- If the folder exists, show a clear error.
- On success, set `app->project_name` to the display name before writing.
- On failure, current project state remains playable and unchanged except for status text.

Example:

```text
Project name: Mall Fog 01
Bundle folder: Mall_Fog_01.vapor
```

## Non-goals

- No overwrite UI.
- No autosave.
- No recent projects list.
- No OS file picker.
- No clipboard/IME complexity beyond basic SDL text input.
- No project rename yet unless it falls out naturally and safely.

## Definition of Done

- Text entry can be opened and closed cleanly.
- Physical keyboard and gamepad entry both work.
- Input focus returns cleanly after closing.
- `Save project...` can create a named `.vapor/` bundle.
- Existing auto-numbered save path can remain as a fallback/dev path if useful.

---

# Stage 5 - Rename, Polish, and Project-File UX

## Goal

Round out the project browser once validation, open, preview, and named save are real.

## Candidate Work

- Rename project display name and/or bundle folder.
- Refresh action.
- Sort by modified time or name.
- Show compact metadata:
  - display name
  - bundle folder
  - modified date if easy
  - validation status
  - preview availability
- Add a current-project marker if the loaded bundle path is tracked.
- Add dirty-state tracking and an open-project confirmation.
- Add overwrite confirmation for Save As.
- Add a "save current project" path once current bundle path is tracked.

## Rename Rules

Keep rename deliberately modest:

- Renaming display name edits `project.json`.
- Renaming the bundle folder is a separate operation or explicitly confirmed.
- Do not rewrite samples, MIDI, or surfaces during a display-name-only rename.
- Validate after rename.

## Non-goals

- No delete project until there is a strong reason.
- No migrations hidden inside browser actions.
- No full filesystem manager.
- No arbitrary project repair.

---

# Implementation Safety Rules

## Identity

Runtime array order is not identity.

These are identity:

```text
sample_id
roster_clip_id
clip_instance_id
parameter_id
```

These are locators or UI state, not permanent identity:

```text
array index
roster order
MIDI note pitch
MIDI channel
filename
display name
```

## Audio Thread

Never do these in the audio callback:

```text
filesystem I/O
JSON parsing/writing
MIDI parsing/writing
WAV conversion
preview rendering
malloc/free
locks
printf/logging
project validation
browser scanning
```

## Validation

Prefer clear warnings/errors over fake success.

Do not repair projects silently.

Do not rewrite project bundles during validation.

The validator should be the shared gate for:

- Browser display.
- Browser open action.
- Direct `app_load_project_bundle()` calls.
- Future rename or metadata edits.

## Browser

The project browser should feel like a compact shelf of saved sets, not a general-purpose file manager.

Keep it:

- Gamepad-first.
- Fast to scan.
- Clear about validity.
- Clear about whether pressing open will replace the current project.

## Keyboard

The keyboard is infrastructure.

Do not tie it only to project naming.

It should become the reusable text-entry layer for project names, clip names, rename operations, and future search/filter fields.

---

# Suggested Order of Codex Sessions

1. Extract validator and harden `app_load_project_bundle()`.
2. Build project browser and replace newest-project open.
3. Add `preview.wav` save and browser playback.
4. Add reusable text entry and named Save As.
5. Add rename/overwrite/dirty-state polish as needed.

Each session should stop at its stated stage unless the next step is tiny and already naturally in hand.
