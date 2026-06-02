#include "stretch_soundtouch.h"

#include <SoundTouch.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

enum {
    VP_SOUNDTOUCH_OK = 0,
    VP_SOUNDTOUCH_INVALID_ARGUMENT = -1,
    VP_SOUNDTOUCH_ALLOCATION_FAILED = -2,
    VP_SOUNDTOUCH_PROCESSING_FAILED = -3
};

static bool checked_sample_count(int64_t frames, int channels, size_t *out)
{
    if (frames < 0 || channels <= 0) return false;
    uint64_t frame_count = static_cast<uint64_t>(frames);
    uint64_t channel_count = static_cast<uint64_t>(channels);
    if (frame_count > std::numeric_limits<uint64_t>::max() / channel_count) return false;
    uint64_t samples = frame_count * channel_count;
    if (samples > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
    *out = static_cast<size_t>(samples);
    return true;
}

static bool checked_byte_count(size_t samples, size_t *out)
{
    if (samples > std::numeric_limits<size_t>::max() / sizeof(float)) return false;
    *out = samples * sizeof(float);
    return true;
}

static bool append_available(soundtouch::SoundTouch &processor,
                             std::vector<float> &output,
                             int channels)
{
    static const uint32_t receive_frames = 4096;
    float buffer[receive_frames * 32];
    if (channels > 32) return false;

    for (;;) {
        uint32_t received = processor.receiveSamples(buffer, receive_frames);
        if (received == 0) break;
        size_t sample_count = static_cast<size_t>(received) * static_cast<size_t>(channels);
        output.insert(output.end(), buffer, buffer + sample_count);
    }
    return true;
}

int vp_soundtouch_render_f32(const float *input_interleaved,
                             int64_t input_frames,
                             const VpStretchRequest *request,
                             float **output_interleaved,
                             int64_t *output_frames)
{
    if (!input_interleaved || !request || !output_interleaved || !output_frames) {
        return VP_SOUNDTOUCH_INVALID_ARGUMENT;
    }
    *output_interleaved = nullptr;
    *output_frames = 0;

    if (input_frames <= 0 || request->sample_rate <= 0 || request->channels <= 0 ||
        request->channels > 32) {
        return VP_SOUNDTOUCH_INVALID_ARGUMENT;
    }

    size_t input_samples = 0;
    if (!checked_sample_count(input_frames, request->channels, &input_samples)) {
        return VP_SOUNDTOUCH_INVALID_ARGUMENT;
    }

    try {
        soundtouch::SoundTouch processor;
        processor.setSampleRate(static_cast<uint>(request->sample_rate));
        processor.setChannels(static_cast<uint>(request->channels));

        switch (request->mode) {
            case VP_STRETCH_TEMPO_PERCENT:
                processor.setTempoChange(static_cast<double>(request->amount));
                break;
            case VP_STRETCH_PITCH_SEMITONES:
                processor.setPitchSemiTones(static_cast<double>(request->amount));
                break;
            case VP_STRETCH_RATE_PERCENT:
                processor.setRateChange(static_cast<double>(request->amount));
                break;
            default:
                return VP_SOUNDTOUCH_INVALID_ARGUMENT;
        }

        std::vector<float> output;
        double expected_ratio = processor.getInputOutputSampleRatio();
        if (expected_ratio > 0.0) {
            double expected_frames = static_cast<double>(input_frames) * expected_ratio + 8192.0;
            size_t expected_frame_count = expected_frames > 0.0 &&
                expected_frames < static_cast<double>(std::numeric_limits<size_t>::max()) ?
                static_cast<size_t>(expected_frames) : 0;
            if (expected_frame_count > 0 &&
                expected_frame_count <= std::numeric_limits<size_t>::max() / static_cast<size_t>(request->channels)) {
                output.reserve(expected_frame_count * static_cast<size_t>(request->channels));
            }
        }

        static const uint32_t chunk_frames = 4096;
        int64_t remaining = input_frames;
        int64_t frame_offset = 0;
        while (remaining > 0) {
            uint32_t frames = remaining > chunk_frames ? chunk_frames : static_cast<uint32_t>(remaining);
            const float *chunk = input_interleaved + frame_offset * request->channels;
            processor.putSamples(chunk, frames);
            if (!append_available(processor, output, request->channels)) {
                return VP_SOUNDTOUCH_INVALID_ARGUMENT;
            }
            frame_offset += frames;
            remaining -= frames;
        }

        processor.flush();
        if (!append_available(processor, output, request->channels)) {
            return VP_SOUNDTOUCH_INVALID_ARGUMENT;
        }

        if (output.empty() || output.size() % static_cast<size_t>(request->channels) != 0) {
            return VP_SOUNDTOUCH_PROCESSING_FAILED;
        }
        size_t output_byte_count = 0;
        if (!checked_byte_count(output.size(), &output_byte_count)) {
            return VP_SOUNDTOUCH_ALLOCATION_FAILED;
        }
        float *rendered = static_cast<float *>(std::malloc(output_byte_count));
        if (!rendered) return VP_SOUNDTOUCH_ALLOCATION_FAILED;
        std::memcpy(rendered, output.data(), output_byte_count);

        *output_interleaved = rendered;
        *output_frames = static_cast<int64_t>(output.size() / static_cast<size_t>(request->channels));
        (void)input_samples;
        return VP_SOUNDTOUCH_OK;
    } catch (const std::bad_alloc &) {
        return VP_SOUNDTOUCH_ALLOCATION_FAILED;
    } catch (...) {
        return VP_SOUNDTOUCH_PROCESSING_FAILED;
    }
}

void vp_soundtouch_free(float *buffer)
{
    std::free(buffer);
}
