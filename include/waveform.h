#pragma once
#include "audio_engine.h"
#include "clip.h"
#include <SDL3/SDL.h>

typedef struct {
    double view_center;
    double zoom_frames;
} WaveformView;

void waveform_init(WaveformView *v, const AudioClip *clip);
void waveform_pan(WaveformView *v, double delta_frames, const AudioClip *clip);
void waveform_zoom(WaveformView *v, double factor, const AudioClip *clip);
void waveform_focus(WaveformView *v, double frame, const AudioClip *clip);
void waveform_draw(SDL_Renderer *r, const AudioClip *clip, const WaveformView *view, uint64_t playhead);
