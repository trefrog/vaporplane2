# Vaporplane Project Bundle Format

Vaporplane projects are folder bundles. The bundle keeps MIDI useful as a portable musical spine while leaving Vaporplane-specific identity, sample, and control-surface state in JSON sidecars.

```text
project_name.vapor/
  project.json
  timeline.mid
  surfaces.json
  samples/
```

This document describes the intended format shape only. Vaporplane does not implement full project save/load, audio conversion, MIDI learn, automation editing, envelope UI, or envelope playback yet.

## Authority Model

- `timeline.mid` is the intended authority for musical timing.
- `project.json` is the compact project manifest and identity map.
- `surfaces.json` is the Master page, FX, parameter, and control-surface sidecar.
- `samples/` contains bundled WAV files referenced from `project.json`.

MIDI is not the whole project. JSON sidecars are authoritative for Vaporplane-specific data that MIDI cannot naturally represent.

## Stable Identity Layers

Vaporplane project identity has three stable layers:

- `sample_id`: a bundled WAV/audio asset in `samples/`.
- `roster_clip_id`: a blessed loop/cut derived from a sample.
- `clip_instance_id`: a timeline placement of a roster clip.

Gremlin rule: runtime array order, roster order, MIDI note pitch, and MIDI channel are not permanent identity. Stable IDs in `project.json` are identity.

## project.json

`project.json` should stay compact. It identifies the project, names the bundle files, and maps stable Vaporplane IDs to bundled assets and MIDI events.

Expected responsibilities:

- Project format/version.
- Project ID and display name.
- PPQN used by `timeline.mid`.
- Relative references to `timeline.mid` and `surfaces.json`.
- `sample_id -> samples/*.wav` mappings.
- `roster_clip_id -> sample_id` mappings.
- `clip_instance_id -> roster_clip_id` mappings.
- MIDI event locator metadata for mapping note events back to `clip_instance_id`.

MIDI event locator metadata may include:

- track
- channel
- note pitch
- start tick
- duration ticks
- velocity
- occurrence index

Duplicated timing fields in `project.json` are identity and validation aids only. They are not a second timeline. If timing conflicts with `timeline.mid`, the MIDI file remains the intended musical timing authority.

MIDI note pitch may be arbitrary or sequential, and can be used as a clip/sample slot reference for MIDI compatibility. It is not the permanent identity of a sample or clip. Stable identity lives in `project.json` through `sample_id`, `roster_clip_id`, and `clip_instance_id`.

Example shape:

```json
{
  "format": "vaporplane.project",
  "version": 1,
  "project_id": "vp_2026_05_28_example",
  "name": "example",
  "ppqn": 960,
  "timeline": "timeline.mid",
  "surfaces": "surfaces.json",
  "samples": [
    {
      "sample_id": "sample_001",
      "path": "samples/sample_001.wav"
    }
  ],
  "roster_clips": [
    {
      "roster_clip_id": "roster_001",
      "sample_id": "sample_001",
      "name": "sample_001_cut_a",
      "source_start_frame": 1024,
      "source_end_frame": 193024
    }
  ],
  "clip_instances": [
    {
      "clip_instance_id": "clip_001",
      "roster_clip_id": "roster_001",
      "midi_locator": {
        "track": 1,
        "channel": 1,
        "note": 60,
        "start_tick": 0,
        "duration_ticks": 3840,
        "velocity": 100,
        "occurrence": 0
      }
    }
  ]
}
```

## timeline.mid

`timeline.mid` is the MIDI-friendly musical spine.

Expected responsibilities:

- Tempo events represent the tempo map.
- MIDI ticks represent musical placement at the project PPQN.
- Note events represent clip instances/triggers.
- Note pitch is an arbitrary/sequential clip or sample slot reference, not permanent identity.
- Note velocity represents instance level/intensity.
- Note duration should be stored even if the current engine does not fully use it yet.
- Note duration is reserved for future instance-bound nondestructive trimming/chopping workflows.
- CC events may represent parameter envelope changes over musical ticks.

The MIDI file should remain portable and legible to MIDI tools where practical. Vaporplane should not force all project metadata into MIDI events.

## surfaces.json

`surfaces.json` stores Vaporplane-specific Master page and control-surface state that does not belong in MIDI.

Expected responsibilities:

- Master page values such as master gain and future clip-protection values.
- FX chain unit list.
- FX unit stable parameter IDs.
- FX unit current parameter values in parameter-native units.
- Future external MIDI CC bindings.
- Timeline CC envelope mappings from MIDI CC lanes to stable Vaporplane parameter IDs.

The critical rule: a CC number is not itself a parameter identity. A CC number maps to a stable Vaporplane parameter ID through `surfaces.json`.

Example stable parameter IDs:

- `master.gain`
- `master.fx.reverb_1.send`
- `master.fx.reverb_1.return`
- `master.fx.reverb_1.decay`
- `master.fx.reverb_1.low_cut_hz`
- `master.fx.reverb_1.high_cut_hz`
- `master.fx.reverb_1.width`

Example shape:

```json
{
  "format": "vaporplane.surfaces",
  "version": 1,
  "master": {
    "gain": 0.90,
    "clip_protection": {
      "enabled": false
    }
  },
  "fx_chain": [
    {
      "unit_id": "reverb_1",
      "type": "reverb",
      "enabled": false,
      "parameters": {
        "master.fx.reverb_1.send": 0.55,
        "master.fx.reverb_1.return": 0.58,
        "master.fx.reverb_1.decay": 4.8,
        "master.fx.reverb_1.low_cut_hz": 90.0,
        "master.fx.reverb_1.high_cut_hz": 11500.0,
        "master.fx.reverb_1.width": 1.25
      }
    }
  ],
  "timeline_cc_envelopes": [
    {
      "track": 2,
      "channel": 1,
      "cc": 20,
      "parameter_id": "master.fx.reverb_1.send",
      "cc_value_range": "7bit_normalized_0_1"
    }
  ],
  "external_cc_bindings": [
    {
      "device_profile": "generic_midi_controller",
      "channel": 1,
      "cc": 74,
      "parameter_id": "master.fx.reverb_1.high_cut_hz",
      "cc_value_range": "7bit_normalized_0_1"
    }
  ]
}
```

## CC Mapping

MIDI CC events support two future uses:

- Timeline parameter envelopes stored as CC events in `timeline.mid`.
- External controller bindings from live MIDI input to Vaporplane parameters.

Timeline envelope identity is scoped by:

```text
track + channel + CC
```

External controller binding identity is scoped, where possible, by:

```text
device/profile + channel + CC
```

Initial CC values are 7-bit inputs in the range `0..127`. They map to normalized `0.0..1.0` parameter targets before being converted to parameter-specific units. Stored parameter values in `surfaces.json` may use parameter-native units such as seconds, hertz, decibels, booleans, or normalized amounts.

## samples/

The `samples/` directory contains bundled WAV files. `project.json` maps stable `sample_id` values to paths inside this directory.

This stage does not define audio conversion rules. Future save/export code may copy existing WAVs directly, render captured in-memory clips to WAV, or add conversion policy, but that is outside this scaffold.
