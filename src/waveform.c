#include "waveform.h"

void waveform_init(WaveformView *v, const AudioClip *clip) { v->view_center = clip->frame_count * 0.5; v->zoom_frames = clip->frame_count; }
void waveform_pan(WaveformView *v, double d, const AudioClip *clip) { v->view_center += d; if (v->view_center < 0) v->view_center = 0; if (v->view_center > clip->frame_count) v->view_center = clip->frame_count; }
void waveform_zoom(WaveformView *v, double f, const AudioClip *clip) { v->zoom_frames *= f; if (v->zoom_frames < 256) v->zoom_frames = 256; if (v->zoom_frames > clip->frame_count) v->zoom_frames = clip->frame_count; }
void waveform_focus(WaveformView *v, double frame, const AudioClip *clip) { (void)clip; v->view_center = frame; }

static int frame_to_x(const WaveformView *v, uint64_t frame, int w) {
    double left = v->view_center - v->zoom_frames * 0.5;
    return (int)(((double)frame - left) / v->zoom_frames * w);
}

void waveform_draw(SDL_Renderer *r, const AudioClip *clip, const WaveformView *view, uint64_t playhead) {
    int w, h; SDL_GetRenderOutputSize(r, &w, &h);
    int mid = h / 2;
    SDL_SetRenderDrawColor(r, 20, 18, 30, 255); SDL_RenderLine(r, 0, mid, w, mid);
    SDL_SetRenderDrawColor(r, 100, 220, 255, 255);
    for (int x = 0; x < w; ++x) {
        double left = view->view_center - view->zoom_frames * 0.5;
        uint64_t f = (uint64_t)(left + (double)x / w * view->zoom_frames);
        if (f >= clip->frame_count) continue;
        float s = clip->samples[f * clip->channels];
        int y = mid + (int)(s * (h * 0.35));
        SDL_RenderPoint(r, x, y);
    }
    int sx = frame_to_x(view, clip->loop_start_frame, w), ex = frame_to_x(view, clip->loop_end_frame, w), px = frame_to_x(view, playhead, w);
    SDL_SetRenderDrawColor(r, 255, 120, 200, 255); SDL_RenderLine(r, sx, 0, sx, h);
    SDL_SetRenderDrawColor(r, 255, 190, 80, 255); SDL_RenderLine(r, ex, 0, ex, h);
    SDL_SetRenderDrawColor(r, 140, 255, 140, 255); SDL_RenderLine(r, px, 0, px, h);
}
