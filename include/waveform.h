#pragma once
#include <SDL3/SDL.h>
#include "clip.h"

typedef struct {
    double view_center; // normalized 0..1
    double view_span;   // normalized width
} WaveformView;

void waveform_view_init(WaveformView *v);
void waveform_render(SDL_Renderer *r, const AudioClip *clip, const WaveformView *v, size_t playhead);
