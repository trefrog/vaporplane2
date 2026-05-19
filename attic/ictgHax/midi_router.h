#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Channel state (queriable for UI/debug) */
typedef struct {
    int8_t  program0;     // 0..127, -1 unknown
    uint32_t notes_seen;
    uint8_t min_note, max_note;
    bool is_drums;        // true for ch==9
    bool is_guitar;       // GM 24..31
    bool is_bass;         // GM 32..39
    int8_t drum_note_bias; // transpose drums that live on melodic channels

    bool sustain;      // CC64 >= 64
    uint8_t cc7_vol;   // channel volume (default 127)
    uint8_t cc11_expr; // expression (default 127)
    bool portamento_on;
   float portamento_time_ms;
   int   portamento_last_note;
   bool  portamento_control_pending;
   int   portamento_control_note;
    float pitch_bend_range;   // semitone span currently in effect
    int16_t current_pitch_bend;
    int16_t max_abs_pitch_bend;
    uint8_t rpn_msb;
    uint8_t rpn_lsb;
    uint8_t data_entry_msb;
    uint8_t data_entry_lsb;
    bool    pitch_range_explicit; // set when RPN updates range

    // simple note refcount for sustain release
    uint8_t note_count[128];
    uint8_t sustain_latch[128]; // 1 if pedal is holding the released note
    int8_t  note_owner[128];     // originating track index for active note_on
    int8_t  sustain_owner[128];  // track index that latched sustain for note
} ChannelState;

/* Extra routing hints passed downstream to your synth/voices */
typedef struct {
    int track;
    int channel;
    uint32_t tick;

    int  program0;        // GM program on this channel (-1 if unknown)
    bool is_drums;
    bool is_guitar;
    bool is_bass;
    bool is_five_string;  // bass has seen note <= 35 (B0)

    float gain;           // mix trim (1.0 default)
    float pan;            // -1..+1
    float hpf_hz;         // simple filtering hints
    float lpf_hz;
    bool  mono_legato;
    float glide_ms;
    bool  portamento;
    float portamento_time_ms;
    int   portamento_start_note;
    float pitch_bend_range;
} MidiRouteExtras;

/* Where routed notes go (you implement these in your audio layer) */
typedef struct {
    /* Non-drum instruments */
    void (*note_on)(int ch, int note, float vel, const MidiRouteExtras* x);
    void (*note_off)(int ch, int note, const MidiRouteExtras* x);
    void (*pitch_bend)(int ch, int16_t bend, const MidiRouteExtras* x);
    void (*pitch_bend_range)(int ch, float semitones, const MidiRouteExtras* x);

    /* Drums on channel 10 (optional). If NULL, router will call note_on/off with is_drums=true */
    void (*drum_on)(int ch, int note, float vel, const MidiRouteExtras* x);
    void (*drum_off)(int ch, int note, const MidiRouteExtras* x);
} MidiRouteSink;

/* Config (tempo for ms↔tick if you need it later) */
void midi_router_init(uint16_t ppqn, uint32_t usec_per_qn, const MidiRouteSink* sink);
void midi_router_reset(void);
void midi_router_finalize(void);
void midi_router_on_tempo(uint32_t tick, uint32_t usec_per_qn);

/* SMF callback adapters — wire these directly into smf.c callbacks */
void midi_router_on_program_change(int track, int ch, uint8_t program0);
void midi_router_on_note_on     (int track, int ch, uint8_t note, uint8_t vel, uint32_t tick);
void midi_router_on_note_off    (int track, int ch, uint8_t note, uint8_t vel, uint32_t tick);
void midi_router_on_end_of_track(int track, uint32_t tick);

/* Optional: CC / PB passthrough (no-op by default) */
void midi_router_on_control_change(int track, int ch, uint8_t cc, uint8_t val, uint32_t tick);
void midi_router_on_pitch_bend    (int track, int ch, int16_t bend, uint32_t tick);

/* Debug / query */
const ChannelState* midi_router_get_channel_state(int ch);
