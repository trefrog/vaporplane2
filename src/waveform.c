#include "waveform.h"
#include <math.h>

static void clamp_view_values(double *center, double *span) {
    if(*span < 0.01) *span = 0.01;
    if(*span > 1.0) *span = 1.0;
    double half = *span * 0.5;
    if(*center < half) *center = half;
    if(*center > 1.0 - half) *center = 1.0 - half;
}

void waveform_view_init(WaveformView *v){
    v->view_center=0.5;
    v->view_span=1.0;
    v->target_center=0.5;
    v->target_span=1.0;
}

void waveform_view_update(WaveformView *v, double dt) {
    clamp_view_values(&v->target_center, &v->target_span);
    double t = 1.0 - exp(-dt * 14.0);
    if(t < 0.0) t = 0.0;
    if(t > 1.0) t = 1.0;
    v->view_center += (v->target_center - v->view_center) * t;
    v->view_span += (v->target_span - v->view_span) * t;
    clamp_view_values(&v->view_center, &v->view_span);
}

void waveform_view_get_frame_bounds(const WaveformView *v, const AudioClip *clip, size_t *start_frame, size_t *end_frame) {
    double center = v->view_center;
    double span = v->view_span;
    clamp_view_values(&center, &span);
    double start = center - span * 0.5;
    double end = center + span * 0.5;
    if(start < 0.0) start = 0.0;
    if(end > 1.0) end = 1.0;
    size_t sf = clip && clip->frame_count > 0 ? (size_t)(start * (double)clip->frame_count) : 0;
    size_t ef = clip && clip->frame_count > 0 ? (size_t)ceil(end * (double)clip->frame_count) : 0;
    if(clip && ef > clip->frame_count) ef = clip->frame_count;
    if(clip && ef <= sf && clip->frame_count > 0) ef = sf + 1 <= clip->frame_count ? sf + 1 : clip->frame_count;
    if(start_frame) *start_frame = sf;
    if(end_frame) *end_frame = ef;
}

void waveform_render(SDL_Renderer *r,const AudioClip *clip,const WaveformView *v,size_t playhead,const TempoLockParams *tempo_params){
    int w,h; SDL_GetRenderOutputSize(r,&w,&h);
    SDL_SetRenderDrawColor(r, 14, 10, 20, 255); SDL_RenderClear(r);
    int ymid=h/2;
    SDL_SetRenderDrawColor(r, 45, 40, 70, 255);
    SDL_RenderLine(r, 0, ymid, w, ymid);
    if(!clip || !clip->samples || clip->frame_count<2) return;
    double start=v->view_center-v->view_span*0.5, end=v->view_center+v->view_span*0.5;
    if(start<0){end-=start;start=0;} if(end>1){start-=(end-1);end=1;} if(start<0)start=0;
    size_t sf=(size_t)(start*clip->frame_count), ef=(size_t)(end*clip->frame_count); if(ef<=sf+1) ef=sf+2;
    SDL_SetRenderDrawColor(r, 100, 230, 240, 255);
    for(int x=0;x<w;x++){
        size_t i0 = sf + (size_t)((double)x/(double)w*(double)(ef-sf));
        size_t i1 = sf + (size_t)((double)(x+1)/(double)w*(double)(ef-sf)); if(i1<=i0)i1=i0+1; if(i1>clip->frame_count)i1=clip->frame_count;
        float minv=1.f,maxv=-1.f;
        for(size_t i=i0;i<i1;i++){ float s=clip->samples[i*clip->channels]; if(s<minv)minv=s;if(s>maxv)maxv=s; }
        int y0=ymid-(int)(maxv*ymid*0.8f), y1=ymid-(int)(minv*ymid*0.8f);
        SDL_RenderLine(r,x,y0,x,y1);
    }
    if(tempo_params && tempo_params->bpm > 0.0 && clip->sample_rate > 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        double frames_per_beat = (60.0 / tempo_params->bpm) * (double)clip->sample_rate;
        int total_beats = (int)ceil(tempo_params->target_bars * (double)tempo_params->beats_per_bar);
        if(total_beats < 1) total_beats = tempo_params->beats_per_bar;
        for(int beat = 0; beat <= total_beats; ++beat) {
            double frame = (double)tempo_params->downbeat_frame + frames_per_beat * (double)beat;
            double fn = frame / (double)clip->frame_count;
            if(fn<start||fn>end) continue;
            int x=(int)(((fn-start)/(end-start))*w);
            if(beat == 0) {
                SDL_SetRenderDrawColor(r, 255, 80, 220, 255);
                SDL_RenderLine(r,x,0,x,h);
            } else if((beat % tempo_params->beats_per_bar) == 0) {
                SDL_SetRenderDrawColor(r, 160, 125, 220, 190);
                SDL_RenderLine(r,x,0,x,h);
            } else {
                SDL_SetRenderDrawColor(r, 100, 95, 140, 120);
                SDL_RenderLine(r,x,h/6,x,(h*5)/6);
            }
        }
    }
    size_t marks[3]={clip->loop_start_frame, clip->loop_end_frame, playhead}; SDL_Color cols[3]={{255,100,120,255},{255,200,110,255},{180,255,120,255}};
    for(int k=0;k<3;k++){ double fn=(double)marks[k]/(double)clip->frame_count; if(fn<start||fn>end) continue; int x=(int)(((fn-start)/(end-start))*w); SDL_SetRenderDrawColor(r,cols[k].r,cols[k].g,cols[k].b,255); SDL_RenderLine(r,x,0,x,h); }
}
