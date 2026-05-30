#include "audio_engine.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_CALLBACK_CHUNK_FRAMES 512
#define WAVEFORM_LOOP_CROSSFADE_FRAMES 512
#define TIMELINE_DECLICK_FRAMES 512
#define MASTER_METER_CLIP_FLASH_SECONDS 0.35f
#define LANE_MONITOR_CLIP_HOLD_SECONDS 0.60f

static int timeline_total_instance_count(const MasterTimeline *timeline) {
    if(!timeline) return 0;
    int total = 0;
    for(int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        total += timeline->lanes[lane_index].instance_count;
    }
    return total;
}

static float velocity_to_gain(int velocity) {
    if(velocity < 1) velocity = 1;
    if(velocity > 127) velocity = 127;
    /* Simple perceptual taper: lower velocities fall away more naturally than raw amplitude. */
    return powf((float)velocity / 127.0f, 1.5f);
}

static double smoothstep01(double x) {
    if(x <= 0.0) return 0.0;
    if(x >= 1.0) return 1.0;
    return x * x * (3.0 - 2.0 * x);
}

static float clip_sample_at(const AudioClip *clip, double frame, int channel, size_t loop_start, size_t loop_end) {
    double loop_len = (double)(loop_end - loop_start);
    while (frame < (double)loop_start) frame += loop_len;
    while (frame >= (double)loop_end) frame -= loop_len;

    size_t i0 = (size_t)frame;
    size_t i1 = i0 + 1 < loop_end ? i0 + 1 : loop_start;
    double frac = frame - (double)i0;
    float s0 = clip->samples[i0 * (size_t)clip->channels + (size_t)channel];
    float s1 = clip->samples[i1 * (size_t)clip->channels + (size_t)channel];
    return (float)((1.0 - frac) * s0 + frac * s1);
}

static double clip_frame_step(const AudioEngine *a) {
    if(!a || !a->clip) return 1.0;
    double playback_rate = a->clip->playback_rate > 0.0 ? a->clip->playback_rate : 1.0;
    if(a->clip->sample_rate <= 0 || a->spec.freq <= 0) return playback_rate;
    return ((double)a->clip->sample_rate / (double)a->spec.freq) * playback_rate;
}

static float roster_sample_at(const RosterClip *clip, double frame, int channel) {
    if(!clip || !clip->samples || clip->frame_count == 0 || clip->channels <= 0) return 0.0f;
    if(frame < 0.0 || frame >= (double)clip->frame_count) return 0.0f;
    size_t i0 = (size_t)frame;
    size_t i1 = i0 + 1 < clip->frame_count ? i0 + 1 : i0;
    double frac = frame - (double)i0;
    int c = channel < clip->channels ? channel : clip->channels - 1;
    float s0 = clip->samples[i0 * (size_t)clip->channels + (size_t)c];
    float s1 = clip->samples[i1 * (size_t)clip->channels + (size_t)c];
    return (float)((1.0 - frac) * s0 + frac * s1);
}

static bool mix_preview(AudioEngine *a, float *left, float *right) {
    if(!a->preview_active || !a->roster || !a->roster_clip_count) return false;
    int roster_count = *a->roster_clip_count;
    if(a->preview_roster_clip_index < 0 || a->preview_roster_clip_index >= roster_count) {
        a->preview_active = false;
        return false;
    }

    RosterClip *clip = &a->roster[a->preview_roster_clip_index];
    if(!clip->samples || clip->frame_count == 0 || clip->sample_rate <= 0) {
        a->preview_active = false;
        return false;
    }
    if(a->preview_frame >= (double)clip->frame_count) {
        a->preview_active = false;
        return false;
    }

    *left += roster_sample_at(clip, a->preview_frame, 0);
    *right += roster_sample_at(clip, a->preview_frame, 1);

    double frame_step = a->spec.freq > 0 ? (double)clip->sample_rate / (double)a->spec.freq : 1.0;
    frame_step *= (double)timeline_effective_tape_speed(a->timeline);
    if(frame_step <= 0.0) frame_step = 1.0;
    a->preview_frame += frame_step;
    if(a->preview_frame >= (double)clip->frame_count) a->preview_active = false;
    return true;
}

static bool mix_file_preview(AudioEngine *a, float *left, float *right) {
    if(!a->file_preview_active || !a->file_preview_clip) return false;
    const AudioClip *clip = a->file_preview_clip;
    if(!clip->samples || clip->frame_count == 0 || clip->sample_rate <= 0) {
        a->file_preview_active = false;
        a->file_preview_clip = NULL;
        a->file_preview_frame = 0.0;
        return false;
    }
    if(a->file_preview_frame >= (double)clip->frame_count) {
        a->file_preview_active = false;
        a->file_preview_clip = NULL;
        a->file_preview_frame = 0.0;
        return false;
    }

    size_t i0 = (size_t)a->file_preview_frame;
    size_t i1 = i0 + 1 < clip->frame_count ? i0 + 1 : i0;
    double frac = a->file_preview_frame - (double)i0;
    int right_channel = clip->channels > 1 ? 1 : 0;
    float l0 = clip->samples[i0 * (size_t)clip->channels];
    float l1 = clip->samples[i1 * (size_t)clip->channels];
    float r0 = clip->samples[i0 * (size_t)clip->channels + (size_t)right_channel];
    float r1 = clip->samples[i1 * (size_t)clip->channels + (size_t)right_channel];
    *left += (float)((1.0 - frac) * l0 + frac * l1);
    *right += (float)((1.0 - frac) * r0 + frac * r1);

    double frame_step = a->spec.freq > 0 ? (double)clip->sample_rate / (double)a->spec.freq : 1.0;
    if(frame_step <= 0.0) frame_step = 1.0;
    a->file_preview_frame += frame_step;
    if(a->file_preview_frame >= (double)clip->frame_count) {
        a->file_preview_active = false;
        a->file_preview_clip = NULL;
        a->file_preview_frame = 0.0;
    }
    return true;
}

static double timeline_declik_gain(double elapsed_seconds, double source_duration_seconds, double instance_duration_seconds, int output_rate) {
    double audible_duration = fmin(source_duration_seconds, instance_duration_seconds);
    if(audible_duration <= 0.0) return 0.0;
    double fade_seconds = output_rate > 0 ? (double)TIMELINE_DECLICK_FRAMES / (double)output_rate : 0.0107;
    if(fade_seconds > audible_duration * 0.5) fade_seconds = audible_duration * 0.5;
    if(fade_seconds <= 0.0) return 1.0;

    double remaining = audible_duration - elapsed_seconds;
    double gain = 1.0;
    if(elapsed_seconds < fade_seconds) gain = smoothstep01(elapsed_seconds / fade_seconds);
    if(remaining < fade_seconds) {
        double out_gain = smoothstep01(remaining / fade_seconds);
        if(out_gain < gain) gain = out_gain;
    }
    if(gain < 0.0) gain = 0.0;
    if(gain > 1.0) gain = 1.0;
    return gain;
}

static int64_t metronome_beat_for_frame(const Transport *t, const AudioClip *clip, double frame, double frames_per_beat) {
    (void)clip;
    double rel = frame - (double)t->metronome_downbeat_frame;
    return (int64_t)floor(rel / frames_per_beat);
}

static bool frame_is_close_to_beat(const Transport *t, double frame, double frames_per_beat, double tolerance_frames) {
    double rel = frame - (double)t->metronome_downbeat_frame;
    if(rel < 0.0) return false;
    double beat_pos = floor(rel / frames_per_beat) * frames_per_beat;
    return fabs(rel - beat_pos) <= tolerance_frames;
}

static void timeline_effective_play_range(const MasterTimeline *timeline, int64_t *start, int64_t *end);

static void update_frame_metronome(AudioEngine *a, double frame) {
    Transport *t = a->transport;
    const AudioClip *clip = a->clip;
    if(!t->playing || !t->metronome_enabled || !clip || clip->sample_rate <= 0 || t->bpm <= 0.0) return;

    double frames_per_beat = (60.0 / t->bpm) * (double)clip->sample_rate;
    if(frames_per_beat <= 1.0) return;

    int64_t beat = metronome_beat_for_frame(t, clip, frame, frames_per_beat);
    double rel = frame - (double)t->metronome_downbeat_frame;
    if(!a->metronome_beat_valid) {
        a->last_metronome_beat = beat;
        a->metronome_beat_valid = true;
        if(frame_is_close_to_beat(t, frame, frames_per_beat, fmax(1.0, clip_frame_step(a)))) {
            transport_trigger_metronome_beat(t, beat);
        }
        return;
    }

    if(rel >= 0.0 && beat != a->last_metronome_beat) {
        transport_trigger_metronome_beat(t, beat);
    }
    a->last_metronome_beat = beat;
}

static void update_timeline_metronome(AudioEngine *a, double tick, double tick_step) {
    Transport *t = a->transport;
    MasterTimeline *timeline = a->timeline;
    if(!timeline || !timeline->playing || !t->metronome_enabled || timeline->ticks_per_beat <= 0) return;

    if(tick < 0.0) return;
    int64_t beat = (int64_t)floor(tick / (double)timeline->ticks_per_beat);
    if(!a->metronome_beat_valid) {
        a->last_metronome_beat = beat;
        a->metronome_beat_valid = true;
        double tick_into_beat = tick - floor(tick / (double)timeline->ticks_per_beat) * (double)timeline->ticks_per_beat;
        if(tick_into_beat <= fmax(1.0, tick_step)) transport_trigger_metronome_beat(t, beat);
        return;
    }
    if(beat != a->last_metronome_beat) {
        transport_trigger_metronome_beat(t, beat);
    }
    a->last_metronome_beat = beat;
}

static void sync_transport_to_timeline(AudioEngine *a) {
    if(!a->timeline || !a->transport) return;
    MasterTimeline *timeline = a->timeline;
    double playhead_tick = a->timeline_playhead_tick > 0.0 ? a->timeline_playhead_tick :
        (timeline->playhead_tick > 0 ? (double)timeline->playhead_tick : 0.0);
    a->transport->bpm = timeline_effective_bpm_at_tick(timeline, playhead_tick);
    a->transport->beats_per_bar = timeline->timeline_beats_per_bar > 0 ? timeline->timeline_beats_per_bar : 4;
    a->transport->beat_unit = timeline->timeline_beat_unit > 0 ? timeline->timeline_beat_unit : 4;
    a->transport->playing = timeline->playing;
    a->transport->current_tick = timeline->playhead_tick > 0 ? (uint64_t)timeline->playhead_tick : 0;
    a->transport->current_seconds = timeline_seconds_at_tick(timeline, playhead_tick);
}

static void timeline_effective_play_range(const MasterTimeline *timeline, int64_t *start, int64_t *end) {
    int64_t length = timeline && timeline->length_ticks > 0 ? timeline->length_ticks : 0;
    if(length <= 0) {
        if(start) *start = 0;
        if(end) *end = 0;
        return;
    }

    int64_t s = timeline->play_range_start_tick;
    int64_t e = timeline->play_range_end_tick;
    if(s < 0 || e > length || e <= s) {
        s = 0;
        e = length;
    }
    if(s < 0) s = 0;
    if(s > length) s = length;
    if(e < 0) e = 0;
    if(e > length) e = length;
    if(e <= s) {
        s = 0;
        e = length;
    }
    if(start) *start = s;
    if(end) *end = e;
}

static void set_timeline_tick_unlocked(AudioEngine *a, int64_t tick) {
    if(!a->timeline) return;
    int64_t length = a->timeline->length_ticks > 0 ? a->timeline->length_ticks : 0;
    if(tick < 0) tick = 0;
    if(length > 0 && tick > length) tick = length;
    a->timeline->playhead_tick = tick;
    a->timeline_playhead_tick = (double)tick;
    a->metronome_beat_valid = false;
    sync_transport_to_timeline(a);
}

static bool audio_lane_index_valid(int lane_index) {
    return lane_index >= 0 && lane_index < TIMELINE_MAX_LANES;
}

static float clamp_float(float value, float min_value, float max_value) {
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static float zap_denormal(float value) {
    return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

static float one_pole_coeff(float hz, int sample_rate) {
    if(sample_rate <= 0) sample_rate = 48000;
    hz = clamp_float(hz, 1.0f, (float)sample_rate * 0.45f);
    return 1.0f - expf(-6.28318530717958647692f * hz / (float)sample_rate);
}

static float master_reverb_param_min(MasterReverbParamId param) {
    switch(param) {
        case MASTER_REVERB_PARAM_ENABLED: return 0.0f;
        case MASTER_REVERB_PARAM_SEND: return 0.0f;
        case MASTER_REVERB_PARAM_RETURN: return 0.0f;
        case MASTER_REVERB_PARAM_PREDELAY_MS: return 0.0f;
        case MASTER_REVERB_PARAM_DECAY_SECONDS: return 0.35f;
        case MASTER_REVERB_PARAM_SIZE: return 0.50f;
        case MASTER_REVERB_PARAM_DIFFUSION: return 0.0f;
        case MASTER_REVERB_PARAM_DAMPING: return 0.0f;
        case MASTER_REVERB_PARAM_LOW_CUT_HZ: return 20.0f;
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ: return 1200.0f;
        case MASTER_REVERB_PARAM_WIDTH: return 0.0f;
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS: return 0.0f;
        case MASTER_REVERB_PARAM_MOD_RATE_HZ: return 0.02f;
        case MASTER_REVERB_PARAM_COUNT:
        default: return 0.0f;
    }
}

static float master_reverb_param_max(MasterReverbParamId param) {
    switch(param) {
        case MASTER_REVERB_PARAM_ENABLED: return 1.0f;
        case MASTER_REVERB_PARAM_SEND: return 1.0f;
        case MASTER_REVERB_PARAM_RETURN: return 1.0f;
        case MASTER_REVERB_PARAM_PREDELAY_MS: return 200.0f;
        case MASTER_REVERB_PARAM_DECAY_SECONDS: return 12.0f;
        case MASTER_REVERB_PARAM_SIZE: return 1.50f;
        case MASTER_REVERB_PARAM_DIFFUSION: return 1.0f;
        case MASTER_REVERB_PARAM_DAMPING: return 1.0f;
        case MASTER_REVERB_PARAM_LOW_CUT_HZ: return 1000.0f;
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ: return 18000.0f;
        case MASTER_REVERB_PARAM_WIDTH: return 1.50f;
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS: return 12.0f;
        case MASTER_REVERB_PARAM_MOD_RATE_HZ: return 2.50f;
        case MASTER_REVERB_PARAM_COUNT:
        default: return 1.0f;
    }
}

static MasterReverbParams master_reverb_default_params(void) {
    MasterReverbParams params;
    params.enabled = false;
    params.send = 0.55f;
    params.return_gain = 0.58f;
    params.predelay_ms = 18.0f;
    params.decay_seconds = 4.8f;
    params.size = 1.18f;
    params.diffusion = 0.86f;
    params.damping = 0.32f;
    params.low_cut_hz = 90.0f;
    params.high_cut_hz = 11500.0f;
    params.width = 1.25f;
    params.mod_depth_ms = 1.80f;
    params.mod_rate_hz = 0.18f;
    return params;
}

static void master_reverb_set_param_unlocked(AudioEngine *a, MasterReverbParamId param, float value);

static void master_reverb_update_derived(AudioEngine *a) {
    if(!a) return;
    MasterReverbState *state = &a->master_reverb_state;
    const MasterReverbParams *params = &a->master_reverb_target;
    int sample_rate = a->spec.freq > 0 ? a->spec.freq : 48000;
    static const float base_delays[MASTER_REVERB_FDN_LINES] = {
        1499.0f, 2111.0f, 2633.0f, 2971.0f,
        3371.0f, 3793.0f, 4217.0f, 4789.0f
    };
    state->target_pre_delay_samples = clamp_float(params->predelay_ms * (float)sample_rate * 0.001f,
                                                  0.0f,
                                                  (float)(MASTER_REVERB_PREDELAY_MAX_FRAMES - 2));
    for(int i = 0; i < MASTER_REVERB_FDN_LINES; ++i) {
        float delay_samples = base_delays[i] * params->size;
        delay_samples = clamp_float(delay_samples, 4.0f, (float)(MASTER_REVERB_DELAY_MAX_FRAMES - 3));
        state->target_delay_samples[i] = delay_samples;
        float delay_seconds = delay_samples / (float)sample_rate;
        float feedback = powf(10.0f, (-3.0f * delay_seconds) / params->decay_seconds);
        state->target_feedback_gain[i] = clamp_float(feedback, 0.05f, 0.985f);
    }
    state->target_low_cut_coeff = one_pole_coeff(params->low_cut_hz, sample_rate);
    state->target_high_cut_coeff = one_pole_coeff(params->high_cut_hz, sample_rate);
    float damping_cutoff = 16000.0f - params->damping * 14500.0f;
    state->target_damping_coeff = one_pole_coeff(damping_cutoff, sample_rate);
}

static void master_reverb_clear_tail_unlocked(AudioEngine *a) {
    if(!a) return;
    MasterReverbState *state = &a->master_reverb_state;
    memset(state->pre_delay, 0, sizeof(state->pre_delay));
    memset(state->delay_lines, 0, sizeof(state->delay_lines));
    memset(state->damping_state, 0, sizeof(state->damping_state));
    state->pre_delay_write = 0;
    memset(state->delay_write, 0, sizeof(state->delay_write));
    state->low_cut_lp = 0.0f;
    state->high_cut_lp_l = 0.0f;
    state->high_cut_lp_r = 0.0f;
    state->mod_phase = 0.0f;
    state->tail_cleared = true;
}

static void master_reverb_init(AudioEngine *a) {
    if(!a) return;
    a->master_reverb_target = master_reverb_default_params();
    a->master_reverb_current = a->master_reverb_target;
    memset(&a->master_reverb_state, 0, sizeof(a->master_reverb_state));
    a->master_reverb_state.enabled_amount = 0.0f;
    master_reverb_update_derived(a);
    a->master_reverb_state.pre_delay_samples = a->master_reverb_state.target_pre_delay_samples;
    a->master_reverb_state.low_cut_coeff = a->master_reverb_state.target_low_cut_coeff;
    a->master_reverb_state.high_cut_coeff = a->master_reverb_state.target_high_cut_coeff;
    a->master_reverb_state.damping_coeff = a->master_reverb_state.target_damping_coeff;
    for(int i = 0; i < MASTER_REVERB_FDN_LINES; ++i) {
        a->master_reverb_state.delay_samples[i] = a->master_reverb_state.target_delay_samples[i];
        a->master_reverb_state.feedback_gain[i] = a->master_reverb_state.target_feedback_gain[i];
    }
    master_reverb_clear_tail_unlocked(a);
}

static void master_fx_chain_init(MasterFxChain *chain) {
    if(!chain) return;
    memset(chain, 0, sizeof(*chain));
    for(int i = 0; i < MASTER_FX_CHAIN_MAX_UNITS; ++i) {
        chain->units[i].type = MASTER_FX_UNIT_EMPTY;
        chain->units[i].enabled = false;
        chain->units[i].bypassed = true;
    }
    chain->units[0].type = MASTER_FX_UNIT_REVERB;
    chain->units[0].enabled = false;
    chain->units[0].bypassed = true;
    chain->unit_count = 1;
}

void audio_engine_init_master_fx(AudioEngine *a) {
    if(!a) return;
    master_fx_chain_init(&a->master_fx_chain);
    master_reverb_init(a);
}

static float read_fractional_delay(const float *buffer, int buffer_size, int write_index, float delay_samples) {
    if(!buffer || buffer_size <= 1) return 0.0f;
    if(delay_samples <= 0.5f) return 0.0f;
    float read_pos = (float)write_index - delay_samples;
    while(read_pos < 0.0f) read_pos += (float)buffer_size;
    while(read_pos >= (float)buffer_size) read_pos -= (float)buffer_size;
    int i0 = (int)floorf(read_pos);
    int i1 = i0 + 1;
    if(i1 >= buffer_size) i1 = 0;
    float frac = read_pos - (float)i0;
    return buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
}

static void master_reverb_process(AudioEngine *a, float *left, float *right) {
    if(!a || !left || !right) return;
    MasterReverbParams *current = &a->master_reverb_current;
    const MasterReverbParams *target = &a->master_reverb_target;
    MasterReverbState *state = &a->master_reverb_state;
    const int sample_rate = a->spec.freq > 0 ? a->spec.freq : 48000;
    const float smooth = 1.0f - expf(-1.0f / ((float)sample_rate * 0.020f));
    const float slow_smooth = 1.0f - expf(-1.0f / ((float)sample_rate * 0.060f));
    const float enabled_target = target->enabled ? 1.0f : 0.0f;

    state->enabled_amount += (enabled_target - state->enabled_amount) * smooth;
    if(!target->enabled && state->enabled_amount < 0.0001f && state->tail_cleared) return;

    current->send += (target->send - current->send) * smooth;
    current->return_gain += (target->return_gain - current->return_gain) * smooth;
    current->predelay_ms += (target->predelay_ms - current->predelay_ms) * slow_smooth;
    current->decay_seconds += (target->decay_seconds - current->decay_seconds) * smooth;
    current->size += (target->size - current->size) * slow_smooth;
    current->diffusion += (target->diffusion - current->diffusion) * smooth;
    current->damping += (target->damping - current->damping) * smooth;
    current->low_cut_hz += (target->low_cut_hz - current->low_cut_hz) * smooth;
    current->high_cut_hz += (target->high_cut_hz - current->high_cut_hz) * smooth;
    current->width += (target->width - current->width) * smooth;
    current->mod_depth_ms += (target->mod_depth_ms - current->mod_depth_ms) * smooth;
    current->mod_rate_hz += (target->mod_rate_hz - current->mod_rate_hz) * smooth;
    current->enabled = state->enabled_amount > 0.5f;

    state->pre_delay_samples += (state->target_pre_delay_samples - state->pre_delay_samples) * slow_smooth;
    state->low_cut_coeff += (state->target_low_cut_coeff - state->low_cut_coeff) * smooth;
    state->high_cut_coeff += (state->target_high_cut_coeff - state->high_cut_coeff) * smooth;
    state->damping_coeff += (state->target_damping_coeff - state->damping_coeff) * smooth;
    for(int i = 0; i < MASTER_REVERB_FDN_LINES; ++i) {
        state->delay_samples[i] += (state->target_delay_samples[i] - state->delay_samples[i]) * slow_smooth;
        state->feedback_gain[i] += (state->target_feedback_gain[i] - state->feedback_gain[i]) * smooth;
    }

    float mono = (*left + *right) * 0.5f * current->send * state->enabled_amount;
    if(state->pre_delay_samples <= 0.5f) {
        state->pre_delay[state->pre_delay_write] = mono;
        state->pre_delay_write = (state->pre_delay_write + 1) % MASTER_REVERB_PREDELAY_MAX_FRAMES;
    } else {
        float delayed = read_fractional_delay(state->pre_delay,
                                             MASTER_REVERB_PREDELAY_MAX_FRAMES,
                                             state->pre_delay_write,
                                             state->pre_delay_samples);
        state->pre_delay[state->pre_delay_write] = mono;
        state->pre_delay_write = (state->pre_delay_write + 1) % MASTER_REVERB_PREDELAY_MAX_FRAMES;
        mono = delayed;
    }

    state->low_cut_lp += state->low_cut_coeff * (mono - state->low_cut_lp);
    state->low_cut_lp = zap_denormal(state->low_cut_lp);
    float tank_input = mono - state->low_cut_lp;

    float delayed[MASTER_REVERB_FDN_LINES];
    float delayed_sum = 0.0f;
    float mod_depth_samples = current->mod_depth_ms * (float)sample_rate * 0.001f;
    if(mod_depth_samples > 0.0001f) {
        state->mod_phase += current->mod_rate_hz / (float)sample_rate;
        while(state->mod_phase >= 1.0f) state->mod_phase -= 1.0f;
        while(state->mod_phase < 0.0f) state->mod_phase += 1.0f;
    }
    static const float mod_phase_offset[MASTER_REVERB_FDN_LINES] = {
        0.00f, 0.13f, 0.29f, 0.41f,
        0.53f, 0.67f, 0.79f, 0.91f
    };
    for(int i = 0; i < MASTER_REVERB_FDN_LINES; ++i) {
        float delay_read = state->delay_samples[i];
        if(mod_depth_samples > 0.0001f) {
            float phase = state->mod_phase + mod_phase_offset[i];
            phase -= floorf(phase);
            delay_read += sinf(6.28318530717958647692f * phase) * mod_depth_samples;
            delay_read = clamp_float(delay_read, 4.0f, (float)(MASTER_REVERB_DELAY_MAX_FRAMES - 3));
        }
        delayed[i] = read_fractional_delay(state->delay_lines[i],
                                           MASTER_REVERB_DELAY_MAX_FRAMES,
                                           state->delay_write[i],
                                           delay_read);
        delayed[i] = zap_denormal(delayed[i]);
        delayed_sum += delayed[i];
    }

    static const float input_sign[MASTER_REVERB_FDN_LINES] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f };
    static const float left_sign[MASTER_REVERB_FDN_LINES] = { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };
    static const float right_sign[MASTER_REVERB_FDN_LINES] = { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f };
    float wet_l = 0.0f;
    float wet_r = 0.0f;
    float householder_scale = 2.0f / (float)MASTER_REVERB_FDN_LINES;
    float input_gain = 0.58f;
    for(int i = 0; i < MASTER_REVERB_FDN_LINES; ++i) {
        float householder = householder_scale * delayed_sum - delayed[i];
        float diffused = delayed[i] + (householder - delayed[i]) * current->diffusion;
        state->damping_state[i] += state->damping_coeff * (diffused - state->damping_state[i]);
        state->damping_state[i] = zap_denormal(state->damping_state[i]);
        float write_value = tank_input * input_gain * input_sign[i] +
                            state->damping_state[i] * state->feedback_gain[i];
        write_value = zap_denormal(write_value);
        state->delay_lines[i][state->delay_write[i]] = write_value;
        state->delay_write[i] = (state->delay_write[i] + 1) % MASTER_REVERB_DELAY_MAX_FRAMES;
        wet_l += delayed[i] * left_sign[i];
        wet_r += delayed[i] * right_sign[i];
    }

    wet_l *= 0.24f;
    wet_r *= 0.24f;
    state->high_cut_lp_l += state->high_cut_coeff * (wet_l - state->high_cut_lp_l);
    state->high_cut_lp_r += state->high_cut_coeff * (wet_r - state->high_cut_lp_r);
    state->high_cut_lp_l = zap_denormal(state->high_cut_lp_l);
    state->high_cut_lp_r = zap_denormal(state->high_cut_lp_r);
    wet_l = state->high_cut_lp_l;
    wet_r = state->high_cut_lp_r;

    float mid = (wet_l + wet_r) * 0.5f;
    float side = (wet_l - wet_r) * 0.5f * current->width;
    wet_l = zap_denormal(mid + side);
    wet_r = zap_denormal(mid - side);

    float wet_gain = current->return_gain * state->enabled_amount;
    *left += wet_l * wet_gain;
    *right += wet_r * wet_gain;
    state->tail_cleared = false;
    if(!target->enabled && state->enabled_amount < 0.0001f) {
        master_reverb_clear_tail_unlocked(a);
    }
}

static void master_fx_chain_process(AudioEngine *a, float *left, float *right) {
    if(!a || !left || !right) return;
    const MasterFxChain *chain = &a->master_fx_chain;
    int count = chain->unit_count;
    if(count < 0) count = 0;
    if(count > MASTER_FX_CHAIN_MAX_UNITS) count = MASTER_FX_CHAIN_MAX_UNITS;
    for(int i = 0; i < count; ++i) {
        const MasterFxUnit *unit = &chain->units[i];
        if(unit->type == MASTER_FX_UNIT_EMPTY) continue;
        switch(unit->type) {
            case MASTER_FX_UNIT_REVERB:
                master_reverb_process(a, left, right);
                break;
            case MASTER_FX_UNIT_LOW_HIGH_CUT:
            case MASTER_FX_UNIT_DELAY:
            case MASTER_FX_UNIT_SOFT_CLIP_LIMITER:
                if(!unit->enabled || unit->bypassed) break;
                break;
            case MASTER_FX_UNIT_EMPTY:
            default:
                break;
        }
    }
}

static void write_lane_analyzer_sample(AudioEngine *a, int lane_index, float left, float right) {
    if(!a->lane_analyzer_active || a->active_analyzer_lane != lane_index) return;
    float mono = (left + right) * 0.5f;
    if(mono > 1.5f) mono = 1.5f;
    if(mono < -1.5f) mono = -1.5f;
    a->lane_analyzer_samples[a->lane_analyzer_write_index] = mono;
    a->lane_analyzer_write_index = (a->lane_analyzer_write_index + 1u) % (unsigned int)LANE_ANALYZER_WINDOW_SIZE;
    if(a->lane_analyzer_sample_count < (unsigned int)LANE_ANALYZER_WINDOW_SIZE) a->lane_analyzer_sample_count++;
}

static void update_lane_monitor(AudioEngine *a, int lane_index, float left, float right) {
    if(!audio_lane_index_valid(lane_index)) return;
    LaneMonitorState *meter = &a->lane_meters[lane_index];
    float abs_l = fabsf(left);
    float abs_r = fabsf(right);
    meter->peak_l = fmaxf(meter->peak_l * 0.995f, abs_l);
    meter->peak_r = fmaxf(meter->peak_r * 0.995f, abs_r);
    meter->rms_l = sqrtf(meter->rms_l * meter->rms_l * 0.995f + left * left * 0.005f);
    meter->rms_r = sqrtf(meter->rms_r * meter->rms_r * 0.995f + right * right * 0.005f);

    float peak = fmaxf(abs_l, abs_r);
    if(peak > 1.0f) {
        meter->clip_count++;
        meter->clip_hold_seconds = LANE_MONITOR_CLIP_HOLD_SECONDS;
    } else if(meter->clip_hold_seconds > 0.0f && a->spec.freq > 0) {
        meter->clip_hold_seconds -= 1.0f / (float)a->spec.freq;
        if(meter->clip_hold_seconds < 0.0f) meter->clip_hold_seconds = 0.0f;
    }

    write_lane_analyzer_sample(a, lane_index, left, right);
}

static void decay_lane_monitors(AudioEngine *a) {
    for(int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        update_lane_monitor(a, lane_index, 0.0f, 0.0f);
    }
}

static int mix_timeline(AudioEngine *a, float *left, float *right) {
    MasterTimeline *timeline = a->timeline;
    if(!timeline || !a->roster || !a->roster_clip_count || !timeline->playing ||
       timeline->length_ticks <= 0 || timeline_total_instance_count(timeline) <= 0 ||
       timeline->ticks_per_beat <= 0) {
        decay_lane_monitors(a);
        a->metronome_beat_valid = false;
        return 0;
    }

    int64_t range_start_tick = 0, range_end_tick = 0;
    timeline_effective_play_range(timeline, &range_start_tick, &range_end_tick);
    if(range_end_tick <= range_start_tick) {
        timeline->playing = false;
        set_timeline_tick_unlocked(a, range_start_tick);
        a->transport->playing = false;
        a->transport->metronome_env = 0.0f;
        a->metronome_beat_valid = false;
        return 0;
    }
    if(a->timeline_playhead_tick < (double)range_start_tick ||
       a->timeline_playhead_tick >= (double)range_end_tick) {
        a->timeline_playhead_tick = (double)range_start_tick;
        timeline->playhead_tick = range_start_tick;
        a->metronome_beat_valid = false;
    }

    double ticks_per_second = timeline_ticks_per_second_at_tick(timeline, a->timeline_playhead_tick) *
                              (double)timeline_effective_tape_speed(timeline);
    double tick_step = ticks_per_second / (double)a->spec.freq;
    if(tick_step <= 0.0) return 0;

    sync_transport_to_timeline(a);
    update_timeline_metronome(a, a->timeline_playhead_tick, tick_step);

    int roster_count = *a->roster_clip_count;
    int active_count = 0;
    float lane_left[TIMELINE_MAX_LANES] = {0};
    float lane_right[TIMELINE_MAX_LANES] = {0};
    for(int lane_index = 0; lane_index < TIMELINE_MAX_LANES; ++lane_index) {
        const TimelineLane *lane = &timeline->lanes[lane_index];
        if(!lane->muted) {
            float lane_gain = lane->gain > 0.0f ? lane->gain : 1.0f;
            for(int i = 0; i < lane->instance_count; ++i) {
                const TimelineInstance *instance = &lane->instances[i];
                if(instance->roster_clip_index < 0 || instance->roster_clip_index >= roster_count) continue;
                if(instance->duration_ticks <= 0) continue;
                double instance_start = (double)instance->start_tick;
                double instance_end = (double)(instance->start_tick + instance->duration_ticks);
                if(a->timeline_playhead_tick < instance_start || a->timeline_playhead_tick >= instance_end) continue;

                const RosterClip *clip = &a->roster[instance->roster_clip_index];
                if(!clip->samples || clip->frame_count == 0 || clip->sample_rate <= 0) continue;
                double elapsed_seconds = timeline_seconds_between_ticks(timeline, instance_start, a->timeline_playhead_tick);
                double source_frame = elapsed_seconds * (double)clip->sample_rate;
                if(source_frame >= (double)clip->frame_count) continue;
                double source_duration_seconds = (double)clip->frame_count / (double)clip->sample_rate;
                double instance_duration_seconds = timeline_seconds_between_ticks(timeline, instance_start, instance_end);
                double gain = timeline_declik_gain(elapsed_seconds, source_duration_seconds, instance_duration_seconds, a->spec.freq);
                double range_elapsed_seconds = timeline_seconds_between_ticks(timeline, (double)range_start_tick, a->timeline_playhead_tick);
                double range_duration_seconds = timeline_seconds_between_ticks(timeline, (double)range_start_tick, (double)range_end_tick);
                double range_gain = timeline_declik_gain(range_elapsed_seconds, range_duration_seconds, range_duration_seconds, a->spec.freq);
                if(range_gain < gain) gain = range_gain;
                if(gain <= 0.0) continue;

                float instance_gain = velocity_to_gain(instance->midi_velocity) * lane_gain * (float)gain;
                lane_left[lane_index] += roster_sample_at(clip, source_frame, 0) * instance_gain;
                lane_right[lane_index] += roster_sample_at(clip, source_frame, 1) * instance_gain;
                active_count++;
            }
        }
        update_lane_monitor(a, lane_index, lane_left[lane_index], lane_right[lane_index]);
        *left += lane_left[lane_index];
        *right += lane_right[lane_index];
    }

    a->timeline_playhead_tick += tick_step;
    if(a->timeline_playhead_tick >= (double)range_end_tick) {
        if(timeline->play_range_loop_enabled) {
            a->timeline_playhead_tick = (double)range_start_tick;
            timeline->playhead_tick = range_start_tick;
            a->metronome_beat_valid = false;
            sync_transport_to_timeline(a);
        } else {
            timeline->playing = false;
            timeline->playhead_tick = range_start_tick;
            a->timeline_playhead_tick = (double)range_start_tick;
            a->transport->playing = false;
            a->transport->metronome_env = 0.0f;
            a->metronome_beat_valid = false;
            sync_transport_to_timeline(a);
        }
    } else {
        timeline->playhead_tick = (int64_t)floor(a->timeline_playhead_tick);
    }
    return active_count;
}

static void update_master_meter(AudioEngine *a, float left, float right) {
    float abs_l = fabsf(left);
    float abs_r = fabsf(right);
    a->meter.peak_l = fmaxf(a->meter.peak_l * 0.995f, abs_l);
    a->meter.peak_r = fmaxf(a->meter.peak_r * 0.995f, abs_r);
    a->meter.rms_l = sqrtf(a->meter.rms_l * a->meter.rms_l * 0.995f + left * left * 0.005f);
    a->meter.rms_r = sqrtf(a->meter.rms_r * a->meter.rms_r * 0.995f + right * right * 0.005f);

    float peak = fmaxf(abs_l, abs_r);
    int bucket = peak < 0.20f ? 0 : (peak < 0.70f ? 1 : (peak <= 1.0f ? 2 : 3));
    a->meter.histogram[bucket]++;
    if(peak > 1.0f) {
        a->meter.clip_count++;
        a->meter.clip_flash_seconds = MASTER_METER_CLIP_FLASH_SECONDS;
    } else if(a->meter.clip_flash_seconds > 0.0f && a->spec.freq > 0) {
        a->meter.clip_flash_seconds -= 1.0f / (float)a->spec.freq;
        if(a->meter.clip_flash_seconds < 0.0f) a->meter.clip_flash_seconds = 0.0f;
    }
}

static float clamp_output(float sample) {
    if(sample > 1.0f) return 1.0f;
    if(sample < -1.0f) return -1.0f;
    return sample;
}

static void render_audio_frame(AudioEngine *a, float *out_left, float *out_right) {
    float left = 0.f, right = 0.f;
    int active_clips = 0;
    if (a->playback_mode == AUDIO_PLAYBACK_TIMELINE) {
        active_clips += mix_timeline(a, &left, &right);
    } else if (a->clip && a->clip->samples && a->clip->frame_count>1 && a->transport->playing) {
        active_clips++;
        size_t loop_start=a->clip->loop_start_frame, loop_end=a->clip->loop_end_frame;
        if(loop_end > a->clip->frame_count) loop_end = a->clip->frame_count;
        if(loop_start + 1 >= loop_end) loop_start = 0;
        if (a->playhead_frame >= loop_end) a->playhead_frame = (double)loop_start;
        if (a->playhead_frame < loop_start) a->playhead_frame = (double)loop_start;
        update_frame_metronome(a, a->playhead_frame);

        left = clip_sample_at(a->clip, a->playhead_frame, 0, loop_start, loop_end);
        right = clip_sample_at(a->clip, a->playhead_frame, 1, loop_start, loop_end);

        size_t loop_len = loop_end - loop_start;
        size_t fade_frames = loop_len / 2 < WAVEFORM_LOOP_CROSSFADE_FRAMES ? loop_len / 2 : WAVEFORM_LOOP_CROSSFADE_FRAMES;
        double distance_to_end = (double)loop_end - a->playhead_frame;
        if(fade_frames > 0 && distance_to_end < (double)fade_frames) {
            double blend = smoothstep01(1.0 - distance_to_end / (double)fade_frames);
            double wrap_frame = (double)loop_start + ((double)fade_frames - distance_to_end);
            float wrap_left = clip_sample_at(a->clip, wrap_frame, 0, loop_start, loop_end);
            float wrap_right = clip_sample_at(a->clip, wrap_frame, 1, loop_start, loop_end);
            left = (float)(left * (1.0 - blend) + wrap_left * blend);
            right = (float)(right * (1.0 - blend) + wrap_right * blend);
        }

        left *= a->clip->gain;
        right *= a->clip->gain;
        a->playhead_frame += clip_frame_step(a);
        while (a->playhead_frame >= (double)loop_end) a->playhead_frame -= (double)(loop_end - loop_start);
        if (a->playhead_frame < (double)loop_start) a->playhead_frame = (double)loop_start;
    } else {
        a->metronome_beat_valid = false;
    }
    if(mix_preview(a, &left, &right)) active_clips++;
    if(mix_file_preview(a, &left, &right)) active_clips++;
    a->debug_stats.active_clips = active_clips;
    if(a->playback_mode == AUDIO_PLAYBACK_WAVEFORM) transport_update(a->transport, 1.0/(double)a->spec.freq);
    float m = transport_next_metronome_sample(a->transport, a->spec.freq);
    float final_left = (left + m) * a->master_gain;
    float final_right = (right + m) * a->master_gain;
    if(a->playback_mode == AUDIO_PLAYBACK_TIMELINE) {
        master_fx_chain_process(a, &final_left, &final_right);
    }
    update_master_meter(a, final_left, final_right);
    *out_left = clamp_output(final_left);
    *out_right = clamp_output(final_right);
}

static void SDLCALL feed_audio(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount){
    (void)total_amount;
    AudioEngine *a = (AudioEngine*)userdata;
    int frames = additional_amount / (int)(sizeof(float)*2);
    if(frames <= 0) return;
    Uint64 callback_start = SDL_GetTicksNS();
    while(frames > 0) {
        int chunk_frames = frames > AUDIO_CALLBACK_CHUNK_FRAMES ? AUDIO_CALLBACK_CHUNK_FRAMES : frames;
        float mix[AUDIO_CALLBACK_CHUNK_FRAMES * 2];
        for(int i=0;i<chunk_frames;i++){
            render_audio_frame(a, &mix[i*2], &mix[i*2+1]);
        }
        SDL_PutAudioStreamData(stream, mix, chunk_frames * (int)sizeof(float) * 2);
        frames -= chunk_frames;
    }
    Uint64 callback_end = SDL_GetTicksNS();
    int buffer_frames = additional_amount / (int)(sizeof(float) * 2);
    double callback_ms = (double)(callback_end - callback_start) / 1000000.0;
    double budget_ms = a->spec.freq > 0 ? ((double)buffer_frames / (double)a->spec.freq) * 1000.0 : 0.0;
    if(a->debug_stats.callback_ms_avg <= 0.0) a->debug_stats.callback_ms_avg = callback_ms;
    else a->debug_stats.callback_ms_avg = a->debug_stats.callback_ms_avg * 0.92 + callback_ms * 0.08;
    double decayed_max = a->debug_stats.callback_ms_max * 0.985;
    a->debug_stats.callback_ms_max = callback_ms > decayed_max ? callback_ms : decayed_max;
    a->debug_stats.buffer_frames = buffer_frames;
    a->debug_stats.sample_rate = a->spec.freq;
    a->debug_stats.audio_budget_ms = budget_ms;
    a->debug_stats.audio_load = budget_ms > 0.0 ? callback_ms / budget_ms : 0.0;
    if(budget_ms > 0.0 && callback_ms > budget_ms) a->debug_stats.over_budget_count++;
}

bool audio_engine_init(AudioEngine *a, AudioClip *clip, Transport *transport){
    memset(a,0,sizeof(*a)); a->clip=clip;a->transport=transport;a->master_gain=0.9f; a->playhead_frame=0; a->playback_mode=AUDIO_PLAYBACK_WAVEFORM; a->active_analyzer_lane=-1; a->lane_analyzer_active=false;
    audio_engine_init_master_fx(a);
    a->spec.format=SDL_AUDIO_F32; a->spec.channels=2; a->spec.freq=48000;
    a->debug_stats.sample_rate = a->spec.freq;
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &a->spec, feed_audio, a);
    if(!a->stream){ fprintf(stderr,"SDL_OpenAudioDeviceStream failed: %s\n",SDL_GetError()); return false; }
    if (!SDL_ResumeAudioStreamDevice(a->stream)){ fprintf(stderr,"SDL_ResumeAudioStreamDevice failed: %s\n",SDL_GetError()); return false; }
    return true;
}
void audio_engine_shutdown(AudioEngine *a){ if(a->stream) SDL_DestroyAudioStream(a->stream); memset(a,0,sizeof(*a)); }
void audio_engine_set_playhead(AudioEngine *a,size_t frame){
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->playhead_frame=(double)frame;
    a->metronome_beat_valid=false;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}
size_t audio_engine_get_playhead_frame(const AudioEngine *a){
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    size_t frame = (size_t)a->playhead_frame;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
    return frame;
}

void audio_engine_set_timeline(AudioEngine *a, RosterClip *roster, int *roster_clip_count, MasterTimeline *timeline) {
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->roster = roster;
    a->roster_clip_count = roster_clip_count;
    a->timeline = timeline;
    a->timeline_playhead_tick = timeline ? (double)timeline->playhead_tick : 0.0;
    a->metronome_beat_valid = false;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_set_playback_mode(AudioEngine *a, AudioPlaybackMode mode) {
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->playback_mode = mode;
    a->metronome_beat_valid = false;
    if(mode == AUDIO_PLAYBACK_TIMELINE) {
        if(a->timeline) {
            int64_t range_start = 0;
            timeline_effective_play_range(a->timeline, &range_start, NULL);
            a->timeline->playing = false;
            a->timeline->playhead_tick = range_start;
            a->timeline_playhead_tick = (double)range_start;
        }
        if(a->transport) {
            a->transport->playing = false;
            a->transport->metronome_env = 0.0f;
        }
    } else if(a->transport) {
        a->transport->playing = false;
        a->transport->metronome_env = 0.0f;
    }
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_start_timeline(AudioEngine *a) {
    if(!a->stream) return;
    if(a->stream) SDL_LockAudioStream(a->stream);
    int64_t range_start = 0, range_end = 0;
    if(a->timeline) timeline_effective_play_range(a->timeline, &range_start, &range_end);
    if(a->timeline && timeline_total_instance_count(a->timeline) > 0 && range_end > range_start) {
        a->playback_mode = AUDIO_PLAYBACK_TIMELINE;
        a->timeline_playhead_tick = (double)range_start;
        a->timeline->playhead_tick = range_start;
        a->timeline->playing = true;
        sync_transport_to_timeline(a);
        if(a->transport) {
            a->transport->current_tick = range_start > 0 ? (uint64_t)range_start : 0;
            a->transport->current_seconds = timeline_seconds_at_tick(a->timeline, (double)range_start);
            a->transport->playing = true;
        }
        a->metronome_beat_valid = false;
    }
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_stop_timeline(AudioEngine *a, bool rewind) {
    if(a->stream) SDL_LockAudioStream(a->stream);
    if(a->timeline) {
        a->timeline->playing = false;
        if(rewind) {
            int64_t range_start = 0;
            timeline_effective_play_range(a->timeline, &range_start, NULL);
            a->timeline->playhead_tick = range_start;
            a->timeline_playhead_tick = (double)range_start;
        }
    }
    if(a->transport) {
        a->transport->playing = false;
        a->transport->metronome_env = 0.0f;
    }
    a->metronome_beat_valid = false;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

bool audio_engine_preview_roster_clip(AudioEngine *a, int roster_index) {
    if(!a->stream) return false;
    if(a->stream) SDL_LockAudioStream(a->stream);
    bool ok = false;
    if(a->roster && a->roster_clip_count &&
       roster_index >= 0 && roster_index < *a->roster_clip_count) {
        RosterClip *clip = &a->roster[roster_index];
        if(clip->samples && clip->frame_count > 0 && clip->sample_rate > 0) {
            a->preview_roster_clip_index = roster_index;
            a->preview_frame = 0.0;
            a->preview_active = true;
            ok = true;
        }
    }
    if(!ok) {
        a->preview_active = false;
        a->preview_roster_clip_index = -1;
        a->preview_frame = 0.0;
    }
    if(a->stream) SDL_UnlockAudioStream(a->stream);
    return ok;
}

void audio_engine_stop_preview(AudioEngine *a) {
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->preview_active = false;
    a->preview_roster_clip_index = -1;
    a->preview_frame = 0.0;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

bool audio_engine_preview_file_clip(AudioEngine *a, const AudioClip *clip) {
    if(!a || !clip || !clip->samples || clip->frame_count == 0 || clip->sample_rate <= 0) return false;
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->file_preview_clip = clip;
    a->file_preview_frame = 0.0;
    a->file_preview_active = true;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
    return true;
}

void audio_engine_stop_file_preview(AudioEngine *a) {
    if(!a) return;
    if(a->stream) SDL_LockAudioStream(a->stream);
    a->file_preview_active = false;
    a->file_preview_clip = NULL;
    a->file_preview_frame = 0.0;
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_set_timeline_playhead(AudioEngine *a, int64_t tick) {
    if(a->stream) SDL_LockAudioStream(a->stream);
    set_timeline_tick_unlocked(a, tick);
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

bool audio_engine_timeline_is_playing(const AudioEngine *a) {
    if(!a->stream) return false;
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    bool playing = a->timeline && a->timeline->playing;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
    return playing;
}

int64_t audio_engine_get_timeline_playhead_tick(const AudioEngine *a) {
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    int64_t tick = a->timeline ? a->timeline->playhead_tick : 0;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
    return tick;
}

void audio_engine_get_master_meter(const AudioEngine *a, MasterMeterState *meter) {
    if(!meter) return;
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    *meter = a->meter;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
}

void audio_engine_get_master_fx_chain(const AudioEngine *a, MasterFxChain *chain) {
    if(!chain) return;
    master_fx_chain_init(chain);
    if(!a) return;
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    *chain = a->master_fx_chain;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
}

const char *audio_engine_master_fx_unit_label(MasterFxUnitType type) {
    switch(type) {
        case MASTER_FX_UNIT_EMPTY: return "Empty";
        case MASTER_FX_UNIT_REVERB: return "Reverb 1";
        case MASTER_FX_UNIT_LOW_HIGH_CUT: return "Low/High Cut";
        case MASTER_FX_UNIT_DELAY: return "Delay";
        case MASTER_FX_UNIT_SOFT_CLIP_LIMITER: return "Soft Clip / Limiter";
        default: return "Unknown";
    }
}

const char *audio_engine_master_reverb_param_label(MasterReverbParamId param) {
    switch(param) {
        case MASTER_REVERB_PARAM_ENABLED: return "Enabled";
        case MASTER_REVERB_PARAM_SEND: return "Send";
        case MASTER_REVERB_PARAM_RETURN: return "Return";
        case MASTER_REVERB_PARAM_PREDELAY_MS: return "PreDelay";
        case MASTER_REVERB_PARAM_DECAY_SECONDS: return "Decay";
        case MASTER_REVERB_PARAM_SIZE: return "Size";
        case MASTER_REVERB_PARAM_DIFFUSION: return "Diffusion";
        case MASTER_REVERB_PARAM_DAMPING: return "Damping";
        case MASTER_REVERB_PARAM_LOW_CUT_HZ: return "Low Cut";
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ: return "High Cut";
        case MASTER_REVERB_PARAM_WIDTH: return "Width";
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS: return "Mod Depth";
        case MASTER_REVERB_PARAM_MOD_RATE_HZ: return "Mod Rate";
        case MASTER_REVERB_PARAM_COUNT:
        default: return "Unknown";
    }
}

static void master_reverb_set_param_unlocked(AudioEngine *a, MasterReverbParamId param, float value) {
    if(!a || param < 0 || param >= MASTER_REVERB_PARAM_COUNT) return;
    MasterReverbParams *params = &a->master_reverb_target;
    value = clamp_float(value, master_reverb_param_min(param), master_reverb_param_max(param));
    switch(param) {
        case MASTER_REVERB_PARAM_ENABLED:
            params->enabled = value >= 0.5f;
            a->master_fx_chain.units[0].type = MASTER_FX_UNIT_REVERB;
            a->master_fx_chain.units[0].enabled = params->enabled;
            a->master_fx_chain.units[0].bypassed = !params->enabled;
            if(a->master_fx_chain.unit_count < 1) a->master_fx_chain.unit_count = 1;
            if(params->enabled) a->master_reverb_state.tail_cleared = false;
            break;
        case MASTER_REVERB_PARAM_SEND: params->send = value; break;
        case MASTER_REVERB_PARAM_RETURN: params->return_gain = value; break;
        case MASTER_REVERB_PARAM_PREDELAY_MS: params->predelay_ms = value; break;
        case MASTER_REVERB_PARAM_DECAY_SECONDS: params->decay_seconds = value; break;
        case MASTER_REVERB_PARAM_SIZE: params->size = value; break;
        case MASTER_REVERB_PARAM_DIFFUSION: params->diffusion = value; break;
        case MASTER_REVERB_PARAM_DAMPING: params->damping = value; break;
        case MASTER_REVERB_PARAM_LOW_CUT_HZ: params->low_cut_hz = value; break;
        case MASTER_REVERB_PARAM_HIGH_CUT_HZ: params->high_cut_hz = value; break;
        case MASTER_REVERB_PARAM_WIDTH: params->width = value; break;
        case MASTER_REVERB_PARAM_MOD_DEPTH_MS: params->mod_depth_ms = value; break;
        case MASTER_REVERB_PARAM_MOD_RATE_HZ: params->mod_rate_hz = value; break;
        case MASTER_REVERB_PARAM_COUNT:
        default: break;
    }
    master_reverb_update_derived(a);
}

void audio_engine_get_master_reverb_params(const AudioEngine *a, MasterReverbParams *current, MasterReverbParams *target) {
    if(current) *current = master_reverb_default_params();
    if(target) *target = master_reverb_default_params();
    if(!a) return;
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    if(current) *current = a->master_reverb_current;
    if(target) *target = a->master_reverb_target;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
}

void audio_engine_set_master_reverb_param(AudioEngine *a, MasterReverbParamId param, float value) {
    if(!a) return;
    if(a->stream) SDL_LockAudioStream(a->stream);
    master_reverb_set_param_unlocked(a, param, value);
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_set_master_reverb_enabled(AudioEngine *a, bool enabled) {
    audio_engine_set_master_reverb_param(a, MASTER_REVERB_PARAM_ENABLED, enabled ? 1.0f : 0.0f);
}

void audio_engine_toggle_master_reverb(AudioEngine *a) {
    if(!a) return;
    if(a->stream) SDL_LockAudioStream(a->stream);
    bool enabled = !a->master_reverb_target.enabled;
    master_reverb_set_param_unlocked(a, MASTER_REVERB_PARAM_ENABLED, enabled ? 1.0f : 0.0f);
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_clear_master_reverb_tail(AudioEngine *a) {
    if(!a) return;
    if(a->stream) SDL_LockAudioStream(a->stream);
    master_reverb_clear_tail_unlocked(a);
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_get_debug_stats(const AudioEngine *a, AudioDebugStats *stats) {
    if(!stats) return;
    SDL_memset(stats, 0, sizeof(*stats));
    if(!a) return;
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    *stats = a->debug_stats;
    if(stats->sample_rate <= 0) stats->sample_rate = a->spec.freq;
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
}

void audio_engine_set_active_lane_analyzer(AudioEngine *a, int lane_index) {
    if(!a) return;
    if(a->stream) SDL_LockAudioStream(a->stream);
    if(audio_lane_index_valid(lane_index)) {
        a->active_analyzer_lane = lane_index;
        a->lane_analyzer_active = true;
        SDL_memset(a->lane_analyzer_samples, 0, sizeof(a->lane_analyzer_samples));
        a->lane_analyzer_write_index = 0;
        a->lane_analyzer_sample_count = 0;
    } else {
        a->active_analyzer_lane = -1;
        a->lane_analyzer_active = false;
        SDL_memset(a->lane_analyzer_samples, 0, sizeof(a->lane_analyzer_samples));
        a->lane_analyzer_write_index = 0;
        a->lane_analyzer_sample_count = 0;
    }
    if(a->stream) SDL_UnlockAudioStream(a->stream);
}

void audio_engine_get_lane_monitor(const AudioEngine *a, int lane_index, LaneMonitorState *meter) {
    if(!meter) return;
    SDL_memset(meter, 0, sizeof(*meter));
    if(!a || !audio_lane_index_valid(lane_index)) return;
    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    *meter = a->lane_meters[lane_index];
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
}

void audio_engine_get_lane_analyzer_snapshot(const AudioEngine *a,
                                             int lane_index,
                                             float *samples,
                                             int sample_count,
                                             int *sample_rate,
                                             bool *active) {
    if(sample_rate) *sample_rate = a ? a->spec.freq : 0;
    if(active) *active = false;
    if(samples && sample_count > 0) SDL_memset(samples, 0, (size_t)sample_count * sizeof(float));
    if(!a || !samples || sample_count <= 0 || !audio_lane_index_valid(lane_index)) return;

    AudioEngine *mutable_audio = (AudioEngine *)a;
    if(mutable_audio->stream) SDL_LockAudioStream(mutable_audio->stream);
    bool is_active = a->lane_analyzer_active && a->active_analyzer_lane == lane_index;
    if(active) *active = is_active;
    if(is_active) {
        unsigned int available = a->lane_analyzer_sample_count;
        if(available > (unsigned int)LANE_ANALYZER_WINDOW_SIZE) available = (unsigned int)LANE_ANALYZER_WINDOW_SIZE;
        int copy_count = sample_count < (int)available ? sample_count : (int)available;
        int pad_count = sample_count - copy_count;
        if(pad_count > 0) SDL_memset(samples, 0, (size_t)pad_count * sizeof(float));
        unsigned int start = (a->lane_analyzer_write_index + (unsigned int)LANE_ANALYZER_WINDOW_SIZE - (unsigned int)copy_count) %
                             (unsigned int)LANE_ANALYZER_WINDOW_SIZE;
        for(int i = 0; i < copy_count; ++i) {
            unsigned int src = (start + (unsigned int)i) % (unsigned int)LANE_ANALYZER_WINDOW_SIZE;
            samples[pad_count + i] = a->lane_analyzer_samples[src];
        }
    }
    if(mutable_audio->stream) SDL_UnlockAudioStream(mutable_audio->stream);
}
