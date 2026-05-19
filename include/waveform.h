#pragma once
#include <SDL3/SDL.h>
#include "clip.h"

typedef struct {
    double view_center_norm;
    double zoom;
} WaveformView;

void waveform_init_view(WaveformView *view);
void waveform_pan(WaveformView *view, double amount);
void waveform_zoom(WaveformView *view, double factor);
void waveform_focus(WaveformView *view, double norm_pos);
void waveform_draw(SDL_Renderer *r, const SDL_FRect *rect, const AudioClip *clip,
                   uint64_t playhead, const WaveformView *view);
