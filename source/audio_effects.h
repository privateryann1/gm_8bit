#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace AudioEffects {
    enum {
        EFF_NONE,
        EFF_BITCRUSH,
        EFF_DESAMPLE
    };

    void BitCrush(uint16_t* sampleBuffer, int samples, float quant, float gainFactor) {
        // Constants to simulate radio-like effect (based on analyzed radio_fx.wav)
        const float bitDepthReduction = 8.0f;  // Reduces bit depth to simulate grainy sound
        const float scale = (1 << (16 - (int)bitDepthReduction)) - 1;  // Scale to new bit range

        for (int i = 0; i < samples; i++) {
            float sample = static_cast<float>(sampleBuffer[i]) / 32768.0f;  // Normalize to -1.0 to 1.0

            // Apply bit reduction effect
            sample = std::round(sample * scale) / scale;

            // Apply gain
            sample *= gainFactor;

            // Re-quantize back to 16-bit range
            sampleBuffer[i] = static_cast<uint16_t>(std::max(-32768.0f, std::min(32767.0f, sample * 32768.0f)));
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
