/*
 * MIDI router bridge between decoded MIDI streams and the scheduler/audio
 * thread. It classifies channels, annotates events, and forwards them to the
 * MidiRouteSink supplied by the engine. All entry points are called from the
 * non-realtime side; the sink callbacks are responsible for deferring any heavy
 * work. Non-goal: per-voice synthesis details remain in the audio subsystem.
 */

#include "midi_router.h"
#include "audio/midi_debug_tap.h"
#include "audio/midi_tracks.h"
#include "SDL2/SDL.h"
#include <math.h>
#include <string.h> // memset

static struct {
    uint16_t ppqn;
    uint32_t usec_per_qn;
    MidiRouteSink sink;
    ChannelState chs[16];
} G;

static inline uint32_t now_ms(void){ return SDL_GetTicks(); }

static float portamento_time_from_cc(uint8_t val) {
    if (val == 0) return 0.0f;
    float norm = (float)val / 127.0f;
    float curved = norm * norm;
    return 10.0f + curved * 1990.0f; // 10ms .. 2000ms
}

static void reset_channel(ChannelState* cs, int ch) {
    *cs = (ChannelState){
        .program0 = -1,
        .notes_seen = 0,
        .min_note = 127,
        .max_note = 0,
        .is_drums = (ch == 9),
        .is_guitar = false,
        .is_bass = false,
        .drum_note_bias = 0
    };
    cs->sustain  = false;
    cs->cc7_vol  = 127;
    cs->cc11_expr= 127;
    cs->portamento_on = false;
    cs->portamento_time_ms = 120.0f;
    cs->portamento_last_note = -1;
    cs->portamento_control_pending = false;
    cs->portamento_control_note = -1;
    cs->pitch_bend_range = 2.0f;
    cs->current_pitch_bend = 0;
    cs->max_abs_pitch_bend = 0;
    cs->rpn_msb = 0x7F;
    cs->rpn_lsb = 0x7F;
    cs->data_entry_msb = 0;
    cs->data_entry_lsb = 0;
    cs->pitch_range_explicit = false;
    memset(cs->note_count, 0, sizeof(cs->note_count));
    memset(cs->sustain_latch, 0, sizeof(cs->sustain_latch));
    memset(cs->note_owner, -1, sizeof(cs->note_owner));
    memset(cs->sustain_owner, -1, sizeof(cs->sustain_owner));
}

void midi_router_init(uint16_t ppqn, uint32_t usec_per_qn, const MidiRouteSink* sink) {
    G.ppqn = ppqn;
    G.usec_per_qn = usec_per_qn ? usec_per_qn : 500000; // default 120 BPM
    if (sink) G.sink = *sink; else memset(&G.sink, 0, sizeof(G.sink));
    for (int c=0; c<16; ++c) reset_channel(&G.chs[c], c);
}

void midi_router_reset(void) {
    for (int c=0; c<16; ++c) reset_channel(&G.chs[c], c);
}

void midi_router_on_tempo(uint32_t /*tick*/, uint32_t usec_per_qn) {
    if (usec_per_qn) G.usec_per_qn = usec_per_qn;
}

static void classify_channel(ChannelState* cs, int ch, int8_t program0) {
    cs->program0 = program0;

    // Reset classification flags before re-evaluating
    cs->is_guitar = false;
    cs->is_bass = false;
    cs->drum_note_bias = 0;

    if (ch == 9) {
        cs->is_drums = true;
        return; // canonical drum channel stays put
    }

    // Some composers park kits on melodic channels using percussion GM patches.
    // Detect the common cases and remap their notes into the GM drum range.
    bool drums_by_program = false;
    int8_t bias = 0;
    switch (program0) {
        case 118: // GM: Synth Drum (often exported as pitched hits)
            drums_by_program = true;
            break;
        default:
            break;
    }

    if (drums_by_program) {
        cs->is_drums = true;
        cs->drum_note_bias = bias;
        return;
    }

    cs->is_drums = false;
    cs->is_guitar = (program0 >= 24 && program0 <= 31); // GM guitars
    cs->is_bass   = (program0 >= 32 && program0 <= 39); // GM basses
}

void midi_router_on_program_change(int track, int ch, uint8_t program0) {
    (void)track;
    if (ch < 0 || ch > 15) return;

    {
        MidiDebugEvent e = {0};
        e.ts_ms  = now_ms();
        e.status = 0xC0 | (ch & 0x0F);
        e.ch     = (uint8_t)ch;
        e.type   = MIDIDEBUG_PROGRAM;
        e.d.pgm.prog = program0;
        midi_debug_tap_push(&e);
    }

    classify_channel(&G.chs[ch], ch, (int8_t)program0);
    midi_tracks_on_program_change(track, ch, (int)program0,
                                  G.chs[ch].is_drums,
                                  G.chs[ch].is_bass,
                                  G.chs[ch].is_guitar);
}

/* Build routing extras with sane defaults */
static MidiRouteExtras make_extras(int track, int ch, uint32_t tick, const ChannelState* cs) {
    MidiRouteExtras x;
    x.track = track; x.channel = ch; x.tick = tick;
    x.program0 = cs ? cs->program0 : -1;
    x.is_drums = cs ? cs->is_drums : (ch == 9);
    x.is_guitar = cs ? cs->is_guitar : false;
    x.is_bass   = cs ? cs->is_bass   : false;

    /* defaults */
    x.gain = 1.0f; x.pan = 0.0f;
    x.hpf_hz = 0.0f; x.lpf_hz = 20000.0f;
    x.mono_legato = false; x.glide_ms = 0.0f;
    x.is_five_string = false;
    x.portamento = false;
    x.portamento_time_ms = 0.0f;
    x.portamento_start_note = -1;
    x.pitch_bend_range = cs ? cs->pitch_bend_range : 2.0f;
    return x;
}

static float clamp_pitch_range(float semis) {
    if (!isfinite(semis)) semis = 0.0f;
    if (semis < 0.0f) semis = 0.0f;
    if (semis > 24.0f) semis = 24.0f; // generous but keeps sanity
    return semis;
}

static void dispatch_pitch_range(int track, int ch, uint32_t tick, ChannelState* cs) {
    if (!cs) return;
    if (G.sink.pitch_bend_range) {
        MidiRouteExtras x = make_extras(track, ch, tick, cs);
        x.pitch_bend_range = cs->pitch_bend_range;
        G.sink.pitch_bend_range(ch, cs->pitch_bend_range, &x);
    }
}

static void set_channel_pitch_range(ChannelState* cs, int track, int ch, uint32_t tick,
                                    float semis, bool mark_explicit) {
    if (!cs) return;
    float clamped = clamp_pitch_range(semis);
    if (fabsf(clamped - cs->pitch_bend_range) < 1e-4f && (!mark_explicit || cs->pitch_range_explicit)) {
        if (mark_explicit) cs->pitch_range_explicit = true;
        return;
    }
    cs->pitch_bend_range = clamped;
    if (mark_explicit) cs->pitch_range_explicit = true;
    if (mark_explicit) {
        if (clamped < 0.0f) clamped = 0.0f;
        int coarse = (int)floorf(clamped + 0.5f);
        if (coarse < 0) coarse = 0;
        if (coarse > 127) coarse = 127;
        cs->data_entry_msb = (uint8_t)coarse;
        float fractional = clamped - (float)coarse;
        if (fractional < 0.0f) fractional = 0.0f;
        int fine = (int)roundf(fractional * 128.0f);
        if (fine < 0) fine = 0;
        if (fine > 127) fine = 127;
        cs->data_entry_lsb = (uint8_t)fine;
    }
    dispatch_pitch_range(track, ch, tick, cs);
}

static bool rpn_is_pitch_bend(const ChannelState* cs) {
    if (!cs) return false;
    return cs->rpn_msb == 0 && cs->rpn_lsb == 0;
}

static void handle_rpn_data_entry(ChannelState* cs, int track, int ch, uint32_t tick) {
    if (!cs) return;
    if (!rpn_is_pitch_bend(cs)) return;
    float semis = (float)cs->data_entry_msb + ((float)cs->data_entry_lsb / 128.0f);
    set_channel_pitch_range(cs, track, ch, tick, semis, true);
}

static bool program_is_voice(int program0) {
    if (program0 < 0) return false;
    if (program0 >= 52 && program0 <= 55) return true;   // Choir/voice patches
    if (program0 == 85) return true;                     // Lead voice (silk)
    return false;
}

static uint8_t remap_drum_note(const ChannelState* cs, uint8_t note) {
    if (!cs) return note;

    if (cs->program0 == 118) {
        switch (note) {
            case 64: return 36; // kick
            case 68: return 38; // snare
            case 73: return 43; // floor tom
            case 76: return 45; // low tom
            case 80: return 49; // crash
            case 85: return 57; // crash 2
            default: break;
        }
    }

    if (cs->drum_note_bias == 0) return note;
    int val = (int)note + (int)cs->drum_note_bias;
    if (val < 0) val = 0;
    if (val > 127) val = 127;
    return (uint8_t)val;
}

void midi_router_on_note_on(int track, int ch, uint8_t note, uint8_t vel, uint32_t tick) {
    if (ch < 0 || ch > 15) return;
    ChannelState* cs = &G.chs[ch];
    midi_tracks_on_channel_activity(track, ch, cs->is_drums, cs->is_bass, cs->is_guitar, cs->program0);
    cs->notes_seen++;
    if (note < cs->min_note) cs->min_note = note;
    if (note > cs->max_note) cs->max_note = note;

    {
        MidiDebugEvent e = {0};
        e.ts_ms  = now_ms();
        e.status = 0x90 | (ch & 0x0F);
        e.ch     = (uint8_t)ch;
        e.type   = MIDIDEBUG_NOTE_ON;
        e.d.note.note = note;
        e.d.note.vel  = vel;
        midi_debug_tap_push(&e);
    }

    MidiRouteExtras x = make_extras(track, ch, tick, cs);
    if (!cs->is_drums) {
        int start_note = -1;
        if (cs->portamento_control_pending && cs->portamento_control_note >= 0) {
            start_note = cs->portamento_control_note;
        } else if (cs->portamento_last_note >= 0) {
            start_note = cs->portamento_last_note;
        }
        bool do_portamento = cs->portamento_on && start_note >= 0 && start_note != note;
        if (do_portamento) {
            x.portamento = true;
            x.portamento_time_ms = (cs->portamento_time_ms > 0.0f) ? cs->portamento_time_ms : 0.0f;
            x.portamento_start_note = start_note;
        }
        cs->portamento_control_pending = false;
        cs->portamento_control_note = -1;
        cs->portamento_last_note = note;
    }

    // velocity scaling by CC7*CC11
    float amp = (vel / 127.0f)
            * ((float)cs->cc7_vol  / 127.0f)
            * ((float)cs->cc11_expr/ 127.0f);
    if (amp > 1.f) amp = 1.f;

    cs->note_count[note]++; // refcount
    cs->sustain_latch[note] = 0;
    cs->note_owner[note] = (track >= 0 && track <= 127) ? (int8_t)track : -1;
    cs->sustain_owner[note] = -1;

    if (cs->is_drums) {
        x.is_drums = true;
        uint8_t routed_note = remap_drum_note(cs, note);
        if (G.sink.drum_on) G.sink.drum_on(ch, routed_note, amp, &x);
        else if (G.sink.note_on) G.sink.note_on(ch, routed_note, amp, &x);
        return;
    }
    /* Bass hints (5-string, filters, mono/legato) */
    if (cs->is_bass) {
        if (cs->min_note <= 35) x.is_five_string = true; // B0 or below seen
        x.mono_legato = true;
        x.glide_ms    = 12.0f;
        x.hpf_hz      = x.is_five_string ? 32.0f : 40.0f;
        x.lpf_hz      = 3800.0f;
    }

    /* Guitar hint: strummable (actual staggering happens in your scheduler/audio) */
    if (cs->is_guitar) {
        // mark via extras flag if you like (no field here); or your scheduler can query channel state
        // (you can extend MidiRouteExtras to carry a 'strummable' bool if that helps)
    }

    if (G.sink.note_on) G.sink.note_on(ch, note, amp, &x);
}

void midi_router_on_note_off(int track, int ch, uint8_t note, uint8_t vel, uint32_t tick) {
    (void)vel;
    if (ch < 0 || ch > 15) return;
    ChannelState* cs = &G.chs[ch];

    {
        MidiDebugEvent e = {0};
        e.ts_ms  = now_ms();
        e.status = 0x80 | (ch & 0x0F);
        e.ch     = (uint8_t)ch;
        e.type   = MIDIDEBUG_NOTE_OFF;
        e.d.note.note = note;
        e.d.note.vel  = vel;
        midi_debug_tap_push(&e);
    }    
    // refcount
    if (cs->note_count[note] > 0) cs->note_count[note]--;
    if (cs->note_count[note] == 0) cs->note_owner[note] = -1;

    MidiRouteExtras x = make_extras(track, ch, tick, cs);

    if (cs->is_drums) {
        uint8_t routed_note = remap_drum_note(cs, note);
        if (G.sink.drum_off) G.sink.drum_off(ch, routed_note, &x);
        else if (G.sink.note_off) { x.is_drums = true; G.sink.note_off(ch, routed_note, &x); }
        return;
    }

    // sustain pedal held? defer release until pedal up
    if (cs->sustain) {
        cs->sustain_latch[note] = 1;
        cs->sustain_owner[note] = (track >= 0 && track <= 127) ? (int8_t)track : -1;
        return;
    }

    cs->sustain_latch[note] = 0;
    cs->sustain_owner[note] = -1;

    if (G.sink.note_off) G.sink.note_off(ch, note, &x);
}

void midi_router_on_control_change(int track, int ch, uint8_t cc, uint8_t val, uint32_t tick) {
    if (ch < 0 || ch > 15) return;
    ChannelState* cs = &G.chs[ch];

    {
        MidiDebugEvent e = {0};
        e.ts_ms  = now_ms();
        e.status = 0xB0 | (ch & 0x0F);
        e.ch     = (uint8_t)ch;
        e.type   = MIDIDEBUG_CC;
        e.d.cc.cc  = cc;
        e.d.cc.val = val;
        midi_debug_tap_push(&e);
    }

    if (cc == 101) { // RPN MSB select
        cs->rpn_msb = val;
        if (val == 127 && cs->rpn_lsb == 127) {
            cs->rpn_msb = 0x7F;
            cs->rpn_lsb = 0x7F;
        }
        return;
    } else if (cc == 100) { // RPN LSB select
        cs->rpn_lsb = val;
        if (val == 127 && cs->rpn_msb == 127) {
            cs->rpn_msb = 0x7F;
            cs->rpn_lsb = 0x7F;
        }
        return;
    } else if (cc == 6) { // Data Entry MSB
        cs->data_entry_msb = val;
        handle_rpn_data_entry(cs, track, ch, tick);
        return;
    } else if (cc == 38) { // Data Entry LSB
        cs->data_entry_lsb = val;
        handle_rpn_data_entry(cs, track, ch, tick);
        return;
    } else if (cc == 96) { // Data Increment
        if (rpn_is_pitch_bend(cs) && cs->data_entry_msb < 127) {
            cs->data_entry_msb += 1;
            handle_rpn_data_entry(cs, track, ch, tick);
        }
        return;
    } else if (cc == 97) { // Data Decrement
        if (rpn_is_pitch_bend(cs) && cs->data_entry_msb > 0) {
            cs->data_entry_msb -= 1;
            handle_rpn_data_entry(cs, track, ch, tick);
        }
        return;
    } else if (cc == 64) { // sustain
        bool down = (val >= 64);
        if (!cs->is_drums) {
            if (!cs->sustain && down) cs->sustain = true;
            else if (cs->sustain && !down) {
                cs->sustain = false;
                // release any notes whose logical count is zero
                for (int n=0;n<128;n++){
                    if (cs->sustain_latch[n] && cs->note_count[n]==0 && G.sink.note_off) {
                        MidiRouteExtras x = make_extras(track, ch, tick, cs);
                        G.sink.note_off(ch, n, &x);
                    }
                    if (!cs->sustain) {
                        cs->sustain_latch[n] = 0;
                        cs->sustain_owner[n] = -1;
                    }
                }
            }
        }
    } else if (cc == 65) { // Portamento On/Off
        if (!cs->is_drums) {
            cs->portamento_on = (val >= 64);
        }
    } else if (cc == 5) { // Portamento Time
        if (!cs->is_drums) {
            cs->portamento_time_ms = portamento_time_from_cc(val);
        }
    } else if (cc == 84) { // Portamento Control (start note)
        if (!cs->is_drums) {
            cs->portamento_control_pending = true;
            cs->portamento_control_note = val;
        }
    } else if (cc == 120 || cc == 123) { // All Sound Off / All Notes Off
        if (G.sink.note_off) {
            MidiRouteExtras x = make_extras(track, ch, tick, cs);
            for (int n = 0; n < 128; ++n) {
                if ((cs->note_count[n] > 0 || cs->sustain_latch[n]) && G.sink.note_off) {
                    G.sink.note_off(ch, n, &x);
                }
                cs->note_count[n] = 0;
                cs->sustain_latch[n] = 0;
                cs->note_owner[n] = -1;
                cs->sustain_owner[n] = -1;
            }
        }
        cs->sustain = false;
    } else if (cc == 7) {
        cs->cc7_vol = val;
    } else if (cc == 11) {
        cs->cc11_expr = val;
    }
}

void midi_router_on_pitch_bend(int track, int ch, int16_t bend, uint32_t tick) {
    if (ch < 0 || ch > 15) return;
    ChannelState* cs = &G.chs[ch];

    int abs_bend = bend >= 0 ? bend : -bend;
    if (abs_bend > cs->max_abs_pitch_bend) cs->max_abs_pitch_bend = (int16_t)abs_bend;
    cs->current_pitch_bend = bend;

    {
        MidiDebugEvent e = {0};
        e.ts_ms  = now_ms();
        e.status = 0xE0 | (ch & 0x0F);
        e.ch     = (uint8_t)ch;
        e.type   = MIDIDEBUG_PITCHBEND;
        e.d.pb.bend = bend;   // already -8192..+8191 here
        midi_debug_tap_push(&e);
    }

    if (G.sink.pitch_bend) {
        MidiRouteExtras x = make_extras(track, ch, tick, cs);
        G.sink.pitch_bend(ch, bend, &x);
    }
}

void midi_router_finalize(void) {
    for (int ch = 0; ch < 16; ++ch) {
        ChannelState* cs = &G.chs[ch];
        if (!cs) continue;
        if (cs->is_drums) continue;
        if (cs->pitch_range_explicit) continue;
        if (!program_is_voice(cs->program0)) continue;
        if (cs->max_abs_pitch_bend < 4096) continue; // needs > ~1 semitone

        int note_range = 0;
        if (cs->notes_seen > 0 && cs->max_note >= cs->min_note) {
            note_range = (int)cs->max_note - (int)cs->min_note;
        }
        if (note_range > 6) continue; // voice already spans a wide range

        float normalized = (float)cs->max_abs_pitch_bend / 8192.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        float target = 2.0f + normalized * 2.0f; // 2..4 semitones

        set_channel_pitch_range(cs, -1, ch, 0, target, true);
    }
}

const ChannelState* midi_router_get_channel_state(int ch) {
    if (ch < 0 || ch > 15) return NULL;
    return &G.chs[ch];
}

void midi_router_on_end_of_track(int track, uint32_t tick) {
    if (track < 0) return;
    for (int ch = 0; ch < 16; ++ch) {
        ChannelState* cs = &G.chs[ch];
        bool released_any = false;
        for (int n = 0; n < 128; ++n) {
            if (!cs->sustain_latch[n]) continue;
            if (cs->sustain_owner[n] != track) continue;
            if (cs->note_count[n] > 0) continue;
            if (G.sink.note_off) {
                MidiRouteExtras x = make_extras(track, ch, tick, cs);
                G.sink.note_off(ch, n, &x);
            }
            cs->sustain_latch[n] = 0;
            cs->sustain_owner[n] = -1;
            released_any = true;
        }
        if (released_any) cs->sustain = false;
    }
}
