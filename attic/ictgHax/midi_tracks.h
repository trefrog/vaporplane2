#ifndef MIDI_TRACKS_H
#define MIDI_TRACKS_H

#include <stdbool.h>

typedef enum {
    MIDI_TRACK_MODE_PLAY = 0,
    MIDI_TRACK_MODE_MUTE = 1,
    MIDI_TRACK_MODE_SOLO = 2,
} MidiTrackMode;

typedef struct {
    char name[64];
    MidiTrackMode mode;
    float meter; // 0..1 smoothed activity
    int channel;
    int program0; // last seen GM program, -1 unknown
    int first_program0; // first non-negative program observed, -1 unknown
    bool program_conflict; // true if multiple distinct programs observed
    bool is_drums;
    bool is_bass;
    bool is_guitar;
} MidiTrackState;

void midi_tracks_reset(int track_count);
void midi_tracks_set_name(int track, const char* name);
void midi_tracks_on_track_name(int track, const char* name);

void midi_tracks_on_program_change(int track, int channel, int program0, bool is_drums, bool is_bass, bool is_guitar);
void midi_tracks_on_channel_activity(int track, int channel, bool is_drums, bool is_bass, bool is_guitar, int program0);

void midi_tracks_note_activity(int track, float normalized_velocity);
void midi_tracks_update(float dt);

bool midi_tracks_should_play(int track);
void midi_tracks_set_mode(int track, MidiTrackMode mode);
MidiTrackMode midi_tracks_get_mode(int track);

int midi_tracks_count(void);
const MidiTrackState* midi_tracks_states(void);

void midi_tracks_set_active_input(int track);
int  midi_tracks_get_active_input(void);
int  midi_tracks_resolve_program(int track); // returns GM program or -1 for fallback
bool midi_tracks_program_is_ambiguous(int track);
int  midi_tracks_pick_default_active(void);

#endif
