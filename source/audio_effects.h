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
        // Constants for simulating radio-like audio
        const float bitDepthReduction = 4.0f;  // Aggressive bit depth reduction
        const float highPassCutoff = 0.1f;  // High-pass filter weight
        const float lowPassCutoff = 0.8f;  // Low-pass filter weight
        const float scale = (1 << static_cast<int>(bitDepthReduction)) - 1;

        float prevSample = 0.0f;
        for (int i = 0; i < samples; i++) {
            // Normalize to range -1.0 to 1.0
            float sample = static_cast<float>(sampleBuffer[i]) / 32768.0f;

            // High-pass filter to reduce bass
            sample = sample - highPassCutoff * prevSample;
            prevSample = sample;

            // Bit reduction for grainy effect
            sample = std::round(sample * scale) / scale;

            // Low-pass filter to limit sharpness
            sample = lowPassCutoff * sample + (1 - lowPassCutoff) * prevSample;

            // Apply gain factor to adjust overall level
            sample *= gainFactor;

            // Clamp and re-quantize to 16-bit range
            sampleBuffer[i] = static_cast<uint16_t>(std::clamp(sample * 32768.0f, -32768.0f, 32767.0f));
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
