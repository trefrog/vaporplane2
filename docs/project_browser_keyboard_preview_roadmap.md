
# Vaporplane Project Browser, Validator, Keyboard, and Preview Roadmap

## Summary

This roadmap covers the next burst of project-file infrastructure for Vaporplane.

The goal is to make `.vapor/` project bundles feel real and browseable before full project loading becomes the center of gravity. The work should remain staged and boringly safe: validate bundles, browse them, preview them, name/save them, and only then load them.

Vaporplane is still a compact gamepad-first sample loop cutter and tick timeline sequencer. Do not turn this into a desktop DAW file manager.

## Current Bundle Shape

A Vaporplane project bundle is a folder:

```text
project_name.vapor/
  project.json
  timeline.mid
  surfaces.json
  preview.wav
  samples/
```

Authoritative roles:

* `project.json` owns bundle identity, stable IDs, sample mappings, roster clip mappings, and MIDI event locator metadata.
* `timeline.mid` owns musical timing, tempo events, note events, and future CC envelope events.
* `surfaces.json` owns Master page state, FX unit state, parameter IDs, current parameter values, and future CC mappings.
* `samples/` owns bundled canonical WAV assets.
* `preview.wav` is a derived browser preview artifact only. It is never required for project validity or playback.

## Core Principle

Do not make the browser secretly load whole projects just to preview them.

Instead:

* Save/export writes a short `preview.wav`.
* The project browser plays `preview.wav`.
* The validator checks project structure before load.
* Full project loading comes later.

This keeps the browser lightweight and avoids invisible timeline/audio-engine state getting created behind the user’s back.

---

# Stage 1 — Project Validator V2

## Goal

Expand the existing basic validator into a two-tier validator:

* Quick validation for browser refresh.
* Full validation for focused project / before load.

## Quick Validation

Runs when the project browser refreshes or opens.

Should check:

* Path is a directory.
* Directory has `.vapor` bundle suffix.
* `project.json` exists.
* `project.json` parses enough to read:

  * format
  * version
  * referenced filenames
* Format is `vaporplane.project`.
* Version is supported.
* Referenced `timeline.mid` exists.
* Referenced `surfaces.json` exists.
* `samples/` exists.
* Referenced sample paths are relative and safe.
* Referenced sample files exist.

Do not decode WAVs.
Do not parse full MIDI.
Do not load samples into the audio engine.
Do not mutate project folders.

## Full Validation

Runs when a project receives focus and before project load.

Should check everything quick validation checks, plus:

* `surfaces.json` exists and parses.
* `surfaces.json` has supported format/version.
* `timeline.mid` has a plausible Standard MIDI File header.
* MIDI file has structurally valid track chunks where practical.
* Referenced bundled WAV files have plausible WAV headers.
* Warn if bundled WAVs are not canonical:

  * 48000 Hz
  * stereo
  * 32-bit IEEE float WAV
* Warn if `preview.wav` is missing, invalid, or noncanonical.
* Validate ID references:

  * `clip_instance_id -> roster_clip_id`
  * `roster_clip_id -> sample_id`
  * `sample_id -> samples/*.wav`
* Validate that paths do not escape the bundle root.

## Status Levels

Validator should return a structured result:

```text
VALID
WARNING
INVALID
UNKNOWN
```

Each result should include a short reason string suitable for browser display:

```text
missing project.json
unsupported project version
missing timeline.mid
sample file missing
preview.wav missing
noncanonical WAV format
unsafe relative path
```

## Validity Rules

Invalid:

* Missing `project.json`.
* Broken or unparseable `project.json`.
* Unsupported project version.
* Missing required `timeline.mid`.
* Missing required `surfaces.json`.
* Missing required sample file.
* Unsafe relative paths.
* Broken required identity mappings.

Warning:

* Missing `preview.wav`.
* Broken `preview.wav`.
* Bundled WAV exists but is not canonical.
* Unknown optional fields.
* Missing optional source lineage.
* Future/unknown `surfaces.json` values that can be safely ignored.

## Non-goals

* No project load.
* No save/export changes unless required for constants.
* No audio decoding.
* No browser UI changes beyond exposing validation status if trivial.
* No migration system.

## Definition of Done

* Existing validator has quick/full modes or equivalent flags.
* Browser can call quick validation cheaply.
* Focus/open code can call full validation.
* Results include status and reason.
* No runtime playback behavior changes.

---

# Stage 2 — Save Preview WAV

## Goal

When saving a `.vapor/` bundle, write a short `preview.wav` that the project browser can play without loading the full project.

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

## Preview Length

Render approximately 7 seconds.

Suggested V1 selection rule:

1. Prefer first timeline region with clip activity.
2. If no timeline clips exist, fall back to first roster clip.
3. If no useful audio exists, skip preview and report a warning.

Do not implement preview marker UI yet.

## Surfaces / FX

If existing render plumbing can include Master page and FX state safely, include them.

If not, write a dry/basic preview and document the limitation.

Do not destabilize save/export to force perfect preview rendering.

## Implementation Rules

* No preview rendering inside audio callback.
* No filesystem work inside audio callback.
* No invisible project loading.
* No browser-time rendering.
* Preview rendering should happen during save/export only.
* If preview rendering fails, save may still succeed if all authoritative project files succeeded.

## Validator Interaction

Full validation should warn if:

* `preview.wav` is missing.
* `preview.wav` has an invalid WAV header.
* `preview.wav` is not canonical format.

Missing or broken preview should not invalidate the project.

## Non-goals

* No preview waveform UI.
* No preview marker.
* No preview cache invalidation system.
* No project browser changes in this stage unless trivial.
* No load implementation.

## Definition of Done

* Save/export may write `preview.wav`.
* `preview.wav` is canonical when written.
* Failed preview export does not corrupt the project.
* Validator can warn about preview status.
* Existing playback and save behavior remain intact.

---

# Stage 3 — Project Browser

## Goal

Add a browser for `.vapor/` project bundles, likely by reusing the existing WAV browser/list UI patterns where clean.

The browser should list project folders, show validation status, and eventually allow preview/open/rename actions.

## Browser Behavior

On browser open/refresh:

* Scan the chosen project directory.
* List folders ending in `.vapor`.
* Run quick validation on each project.
* Show project display name from `project.json` when available.
* Fall back to folder name if needed.
* Show validation state visually.

Suggested visual status:

```text
VALID    normal text
WARNING  yellow / caution style
INVALID  red text
UNKNOWN  dim text
```

## Focus Behavior

When the selected project changes:

* Run full validation on the focused project.
* Update reason/details panel.
* Enable/disable actions based on validation.

## Browser Actions

Initial intended actions:

```text
A / Enter       open project later, but may be disabled until load exists
X / key         preview project.wav
Y / key         rename later
B / Esc         back
Refresh key     rescan browser
```

Exact bindings should follow existing input conventions.

## Preview Button

If focused project has a valid `preview.wav`:

* Preview action plays it directly.
* Preview playback should not load the full project.
* Preview playback should not mutate current timeline/project state.

If preview is missing:

* Show warning.
* Preview action disabled or plays nothing with a status message.

## Non-goals

* No full project load yet unless explicitly part of a later stage.
* No invisible timeline loading.
* No folder mutation.
* No delete project.
* No project migration.
* No OS-native file dialog.
* No mouse-heavy workflow.

## Definition of Done

* Project browser lists `.vapor/` bundles.
* Quick validation runs on refresh.
* Full validation runs on focus.
* Invalid projects are marked red.
* Warning projects are marked distinctly.
* Preview action can play `preview.wav` if present and valid.
* Existing WAV browser remains intact.

---

# Stage 4 — Reusable On-Screen Keyboard Modal

## Goal

Add a reusable gamepad-first on-screen keyboard modal with physical keyboard capture.

This will support:

* Save As project naming.
* Project rename.
* Clip rename.
* Search/filter later.

## Design Requirements

The keyboard must be reusable, not hardcoded to project saving.

It should support:

* Initial text.
* Max length.
* Confirm.
* Cancel.
* Backspace/delete.
* Cursor movement.
* Physical keyboard input.
* Gamepad input.
* Field modes such as:

  * display name
  * filename-safe name
  * search text

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

* Text input should go to the modal, not the main app.
* Printable characters insert text.
* Backspace deletes.
* Delete removes forward if supported.
* Left/right arrows move cursor.
* Enter confirms.
* Esc cancels.
* Modifier shortcuts may be deferred.

Use SDL text input APIs if already available in the codebase.

## Character Pages

Suggested pages:

```text
lowercase
uppercase
numbers
symbols
filename-safe
```

For filename-safe mode:

* Restrict or sanitize unsafe filesystem characters.
* Avoid path separators.
* Avoid path traversal.
* Display the resulting bundle-safe name separately when relevant.

Example:

```text
Project name: Mall Fog 01
Bundle folder: Mall_Fog_01.vapor
```

## Non-goals

* No IME/composition complexity beyond what SDL text input provides simply.
* No full rich text editing.
* No clipboard support unless trivial.
* No mouse-first keyboard.
* No Save As workflow yet unless explicitly part of the next stage.

## Definition of Done

* Modal can be opened from a test/dev path or placeholder menu action.
* Text can be entered with physical keyboard.
* Text can be entered with gamepad.
* Modal returns accepted/cancelled result.
* Input focus returns cleanly after closing.
* Existing input modes remain intact.

---

# Stage 5 — Save As Uses Keyboard + Bundle Export

## Goal

Connect the system menu’s Save As action to the on-screen keyboard and save-only bundle export.

## Flow

Suggested flow:

```text
System Menu
  → Save Project As
    → On-screen keyboard asks for project display name
      → sanitized bundle folder name is generated
        → save-only bundle export writes .vapor bundle
```

## Save Rules

* Use `.vapor` bundle suffix.
* Generate filesystem-safe folder name.
* Do not overwrite existing bundle unless safe overwrite exists.
* If folder exists, show clear error.
* On success, current project path may be set.
* On failure, current project state must remain unchanged.

## Non-goals

* No load.
* No overwrite UI.
* No autosave.
* No recent projects list.
* No OS file picker.

## Definition of Done

* Save As can be triggered from the UI.
* User can name the project with gamepad or physical keyboard.
* A `.vapor/` bundle is exported.
* Existing project remains playable after save.
* Failure is visible and non-destructive.

---

# Stage 6 — Open Project Prep, Not Full Load

## Goal

Prepare the Open Project flow using the project browser and validator, without implementing full project load unless separately requested.

## Behavior

System Menu:

```text
Open Project
  → Project Browser
    → select project
    → full validation
    → preview available
    → load action disabled or placeholder until load stage
```

## Non-goals

* No actual project load in this stage.
* No current-project dirty state prompts.
* No migrations.
* No invisible timeline hydration.

## Definition of Done

* Open Project enters the project browser.
* Browser uses validator and preview.
* Load action is clearly not implemented or safely disabled.
* Existing current project remains untouched.

---

# Later Stage — Real Project Load

Full load should only happen after:

* Save-only bundle export works.
* Validator is reusable.
* Browser can display validation.
* Keyboard can name/save projects.
* Preview works independently.

Load must use the same validator as the browser.

Before loading:

* Run full validation.
* Refuse invalid projects.
* Warn for nonfatal issues.
* Clear or replace current project state deliberately.
* Load bundled WAVs from `samples/`, not original source paths.
* Treat `source_lineage` as archaeology only.
* Restore roster clips, clip instances, tempo map, and surfaces state.
* Do not silently remap missing IDs.

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

Do not “repair” projects silently.

Do not rewrite project bundles during validation.

## Browser

The project browser should be a crate-digging shelf, not a full desktop file manager.

Keep it compact, tactile, gamepad-first, and visually obvious.

## Keyboard

The keyboard is infrastructure.

Do not tie it only to project naming.

It should become the reusable text-entry spell for the whole instrument.

---

# Suggested Order of Codex Sessions

1. Project Validator V2
2. Save `preview.wav` during bundle export
3. Project Browser with validation states
4. Preview playback from browser
5. Reusable on-screen keyboard modal
6. Save As flow using keyboard + bundle export
7. Open Project browser flow
8. Real project load

Each session should stop at its stated stage.
Do not one-shot the whole roadmap.
