#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace AudioEffects {
    enum {
        EFF_NONE,
        EFF_BITCRUSH,
        EFF_DESAMPLE
    };

    void BitCrush(uint16_t* sampleBuffer, int samples, float quant, float gainFactor) {
        // Radio effect constants
        const float highPassAlpha = 0.95f;  // High-pass filter to cut bass
        const float lowPassAlpha = 0.1f;    // Low-pass filter to limit treble

        float prevSampleHighPass = 0.0f;
        float prevSampleLowPass = 0.0f;

        for (int i = 0; i < samples; i++) {
            float sample = static_cast<float>(sampleBuffer[i] - 32768) / 32768.0f;  // Normalize to -1.0 to 1.0

            // Apply high-pass filter to reduce bass
            float highPassSample = sample - highPassAlpha * prevSampleHighPass;
            prevSampleHighPass = sample;

            // Apply low-pass filter to reduce high frequencies
            float filteredSample = lowPassAlpha * highPassSample + (1.0f - lowPassAlpha) * prevSampleLowPass;
            prevSampleLowPass = filteredSample;

            // Re-quantize to 16-bit range
            sampleBuffer[i] = static_cast<uint16_t>(std::clamp(filteredSample * 32768.0f + 32768.0f, 0.0f, 65535.0f));
        }
    }

    static uint16_t tempBuf[10 * 1024];
    void Desample(uint16_t* inBuffer, int& samples, int desampleRate = 2) {
        assert(samples / desampleRate + 1 <= sizeof(tempBuf));
        int outIdx = 0;
        for (int i = 0; i < samples; i++) {
            if (i % desampleRate == 0) continue;

            tempBuf[outIdx] = inBuffer[i];
            outIdx++;
        }
        std::memcpy(inBuffer, tempBuf, outIdx * 2);
        samples = outIdx;
    }
}
