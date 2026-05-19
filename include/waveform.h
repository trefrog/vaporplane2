#pragma once
#include <SDL3/SDL.h>
#include "clip.h"

typedef struct {
    double view_center; // normalized 0..1
    double view_span;   // normalized width
    double target_center;
    double target_span;
} WaveformView;

void waveform_view_init(WaveformView *v);
void waveform_view_update(WaveformView *v, double dt);
void waveform_view_get_frame_bounds(const WaveformView *v, const AudioClip *clip, size_t *start_frame, size_t *end_frame);
void waveform_render(SDL_Renderer *r, const AudioClip *clip, const WaveformView *v, size_t playhead);
