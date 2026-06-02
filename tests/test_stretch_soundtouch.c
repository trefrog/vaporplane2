#include "stretch_soundtouch.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

int main(void)
{
    const int sample_rate = 48000;
    const int channels = 2;
    const int64_t frames = sample_rate / 2;
    float *input = (float *)malloc((size_t)frames * (size_t)channels * sizeof(float));
    assert(input);

    for (int64_t i = 0; i < frames; ++i) {
        float t = (float)i / (float)sample_rate;
        float sample = 0.25f * sinf(2.0f * 3.14159265358979323846f * 220.0f * t);
        input[i * channels] = sample;
        input[i * channels + 1] = sample;
    }

    VpStretchRequest request;
    request.sample_rate = sample_rate;
    request.channels = channels;
    request.amount = -25.0f;
    request.mode = VP_STRETCH_TEMPO_PERCENT;

    float *output = NULL;
    int64_t output_frames = 0;
    int result = vp_soundtouch_render_f32(input, frames, &request, &output, &output_frames);

    assert(result == 0);
    assert(output != NULL);
    assert(output_frames > 0);

    vp_soundtouch_free(output);
    free(input);
    return 0;
}
