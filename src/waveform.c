#include "waveform.h"

void waveform_view_init(WaveformView *v){ v->view_center=0.5; v->view_span=1.0; }

void waveform_render(SDL_Renderer *r,const AudioClip *clip,const WaveformView *v,size_t playhead){
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
    size_t marks[3]={clip->loop_start_frame, clip->loop_end_frame, playhead}; SDL_Color cols[3]={{255,100,120,255},{255,200,110,255},{180,255,120,255}};
    for(int k=0;k<3;k++){ double fn=(double)marks[k]/(double)clip->frame_count; if(fn<start||fn>end) continue; int x=(int)(((fn-start)/(end-start))*w); SDL_SetRenderDrawColor(r,cols[k].r,cols[k].g,cols[k].b,255); SDL_RenderLine(r,x,0,x,h); }
}
