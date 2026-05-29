# Vaporplane Project Bundle Format

Vaporplane projects are folder bundles. The bundle keeps MIDI useful as a portable musical spine while leaving Vaporplane-specific identity, sample, and control-surface state in JSON sidecars.

```text
project_name.vapor/
  project.json
  timeline.mid
  surfaces.json
  samples/
```

This document describes the project bundle shape. Vaporplane implements save/load for the core bundle files; MIDI learn, automation editing, envelope UI, and envelope playback are not installed yet.

## Authority Model

- `timeline.mid` is the intended authority for musical timing.
- `project.json` is the compact project manifest and identity map.
- `surfaces.json` is the Master page, FX, parameter, and control-surface sidecar.
- `samples/` contains bundled WAV files referenced from `project.json`.

MIDI is not the whole project. JSON sidecars are authoritative for Vaporplane-specific data that MIDI cannot naturally represent.

## Stable Identity Layers

Vaporplane project identity has two stable JSON identity layers plus MIDI-owned timeline placements:

- `sample_id`: a bundled WAV/audio asset in `samples/`.
- `roster_clip_id`: a blessed loop/cut derived from a sample.
- MIDI note events in `timeline.mid`: timeline placements of roster clips.

Gremlin rule: runtime array order, roster order, MIDI note pitch, and MIDI channel are not permanent identity. Stable `sample_id` and `roster_clip_id` values in `project.json` are identity. MIDI note timing remains the timeline authority.

Format v3 generates stable IDs once when a roster clip enters the project, then preserves those IDs across save-as bundles, deletion, and reordering. IDs are not derived from current array position. No sample content hashes are stored; bundled WAVs are authoritative and may be externally edited as long as filenames and manifest references remain valid.

## project.json

`project.json` should stay compact. It identifies the project, names the bundle files, and maps stable Vaporplane IDs to bundled assets and MIDI events.

Expected responsibilities:

- Project format/version.
- Project ID and display name.
- PPQN used by `timeline.mid`.
- Relative references to `timeline.mid` and `surfaces.json`.
- `sample_id -> samples/*.wav` mappings.
- `roster_clip_id -> sample_id` mappings.
- `roster_clip_id -> MIDI binding` mappings, currently channel + note.
- Roster clip loop anchors: `loop_start_frame`, `loop_end_frame`, and `downbeat_offset_frames`.
- Optional source lineage/provenance: source path, source bounds, and source sample rate.

`project.json` must not store a second list of timeline placements. MIDI note events in `timeline.mid` are clip instances. The manifest only explains which roster clip a MIDI channel/note refers to.

MIDI note pitch may be arbitrary or sequential, and can be used as a clip/sample slot reference for MIDI compatibility. It is not the permanent identity of a sample or clip. Stable identity lives in `project.json` through `sample_id` and `roster_clip_id`.

Example shape:

```json
{
  "format": "vaporplane.project",
  "version": 3,
  "project_id": "project_ab12cd34",
  "name": "example",
  "ppqn": 960,
  "timeline": "timeline.mid",
  "surfaces": "surfaces.json",
  "samples": [
    {
      "sample_id": "sample_4f91c2aa",
      "path": "samples/sample_4f91c2aa.wav"
    }
  ],
  "roster_clips": [
    {
      "roster_clip_id": "roster_c87e120d",
      "sample_id": "sample_4f91c2aa",
      "name": "sample_cut_a",
      "midi_binding": {
        "channel": 1,
        "note": 60
      },
      "source_bpm": 114.0,
      "beats_per_bar": 4,
      "beat_unit": 4,
      "target_bars": 4.0,
      "target_beats": 16.0,
      "loop_start_frame": 0,
      "loop_end_frame": 768000,
      "downbeat_offset_frames": 0,
      "source_lineage": {
        "path": "original/source.wav",
        "start_frame": 1024,
        "end_frame": 193024,
        "sample_rate": 48000
      }
    }
  ]
}
```

Legacy format v2 bundles may contain positional-looking IDs such as `sample_001` and `roster_001`. Once loaded, those strings are accepted as stable IDs for that project and preserved by later v3 saves rather than renumbered.

Legacy format v1 bundles may contain `clip_instances[]` locator objects. Loaders may use those only to recover channel/note to `roster_clip_id` mappings. Their timing fields are not authoritative and must not override `timeline.mid`.

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
- Timeline surface state: tape speed, play range start/end ticks, and play range loop enabled.
- Lane surface state: lane index, muted, gain, palette, and future lane FX chain placeholders.
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
  "timeline_surface": {
    "tape_speed": 1.0,
    "play_range": {
      "start_tick": 0,
      "end_tick": 15360,
      "loop_enabled": false
    }
  },
  "lanes": [
    {
      "lane": 1,
      "muted": false,
      "gain": 1.0,
      "palette": 0,
      "fx_chain": []
    }
  ],
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

Project bundle save writes roster clips as canonical bundled WAV assets: 48 kHz, stereo, 32-bit float. Filenames are based on stable `sample_id` values, not roster order. These bundled WAVs are authoritative for future playback. Source lineage in `project.json` is provenance only and is not required to reconstruct playback.
