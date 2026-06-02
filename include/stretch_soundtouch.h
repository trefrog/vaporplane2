#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VpStretchMode {
    VP_STRETCH_TEMPO_PERCENT,
    VP_STRETCH_PITCH_SEMITONES,
    VP_STRETCH_RATE_PERCENT
} VpStretchMode;

typedef struct VpStretchRequest {
    int sample_rate;
    int channels;
    float amount;
    VpStretchMode mode;
} VpStretchRequest;

int vp_soundtouch_render_f32(const float *input_interleaved,
                             int64_t input_frames,
                             const VpStretchRequest *request,
                             float **output_interleaved,
                             int64_t *output_frames);

void vp_soundtouch_free(float *buffer);

#ifdef __cplusplus
}
#endif
