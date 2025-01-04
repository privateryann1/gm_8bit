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
        // Constants to simulate radio-like effect
        const float bitDepthReduction = 6.0f;  // Reduce bit depth to simulate grainy sound
        const float scale = (1 << static_cast<int>(bitDepthReduction)) - 1;  // Scale to new bit range

        for (int i = 0; i < samples; i++) {
            // Normalize to range -1.0 to 1.0 for processing
            float sample = static_cast<float>(sampleBuffer[i]) / 32768.0f;

            // Apply simple high-pass filter to simulate radio frequency response
            if (i > 0) {
                sample -= 0.95f * static_cast<float>(sampleBuffer[i - 1]) / 32768.0f;
            }

            // Bit reduction effect
            sample = std::round(sample * scale) / scale;

            // Apply gain factor
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
