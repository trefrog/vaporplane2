#include "waveform.h"
#include <math.h>

void waveform_init_view(WaveformView *view){ view->view_center_norm=0.5; view->zoom=1.0; }
void waveform_pan(WaveformView *view,double amount){ view->view_center_norm += amount / view->zoom; if(view->view_center_norm<0)view->view_center_norm=0; if(view->view_center_norm>1)view->view_center_norm=1; }
void waveform_zoom(WaveformView *view,double factor){ view->zoom*=factor; if(view->zoom<1.0)view->zoom=1.0; if(view->zoom>80.0)view->zoom=80.0; }
void waveform_focus(WaveformView *view,double norm_pos){ view->view_center_norm=norm_pos; if(view->view_center_norm<0)view->view_center_norm=0; if(view->view_center_norm>1)view->view_center_norm=1; }

void waveform_draw(SDL_Renderer *r, const SDL_FRect *rect, const AudioClip *clip, uint64_t playhead, const WaveformView *view){
    SDL_SetRenderDrawColor(r, 18, 12, 32, 255); SDL_RenderFillRect(r, rect);
    float mid = rect->y + rect->h*0.5f;
    SDL_SetRenderDrawColor(r, 60, 45, 90, 255); SDL_RenderLine(r, rect->x, mid, rect->x+rect->w, mid);
    if(!clip->samples||clip->frame_count<2)return;
    double window = 1.0 / view->zoom;
    double start = view->view_center_norm - window*0.5; if(start<0)start=0; if(start+window>1)start=1-window;
    for (int x=0;x<(int)rect->w;x++) {
        double n0 = start + window * ((double)x / rect->w);
        double n1 = start + window * ((double)(x+1) / rect->w);
        uint64_t f0=(uint64_t)(n0*(clip->frame_count-1)); uint64_t f1=(uint64_t)(n1*(clip->frame_count-1));
        if(f1<=f0)f1=f0+1; if(f1>=clip->frame_count)f1=clip->frame_count-1;
        float minv=1,maxv=-1;
        for(uint64_t f=f0; f<=f1; ++f){ float s=clip->samples[f*clip->channels]; if(s<minv)minv=s; if(s>maxv)maxv=s; }
        SDL_SetRenderDrawColor(r, 120, 240, 220, 255);
        SDL_RenderLine(r, rect->x+x, mid-maxv*rect->h*0.4f, rect->x+x, mid-minv*rect->h*0.4f);
    }
    uint64_t markers[3]={clip->loop_start_frame,clip->loop_end_frame,playhead}; SDL_Color cols[3]={{255,100,210,255},{255,210,100,255},{255,255,255,255}};
    for(int i=0;i<3;i++){ double n=(double)markers[i]/(double)(clip->frame_count-1); double screen=(n-start)/window; if(screen>=0&&screen<=1){ float x=rect->x+(float)(screen*rect->w); SDL_SetRenderDrawColor(r,cols[i].r,cols[i].g,cols[i].b,255); SDL_RenderLine(r,x,rect->y,x,rect->y+rect->h);} }
}
