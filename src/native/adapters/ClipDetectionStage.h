#pragma once

#include "../IDspStage.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace dsp::adapters
{
    /**
     * @brief Clip Detection Stage - Detects when samples exceed a threshold.
     * 
     * This stage analyzes the input signal and outputs a binary (0.0 or 1.0) indicator
     * showing where clipping occurs. This is useful for:
     * - Audio clipping detection (overload prevention)
     * - Saturation detection in ADC signals
     * - Quality control in data acquisition
     * 
     * **Output:**
     * - 1.0 when |sample| >= threshold (clipping detected)
     * - 0.0 when |sample| < threshold (no clipping)
     * 
     * **Processing:** Stateless, processes each sample independently
     * 
     * @example
     * ```cpp
     * // Detect audio clipping at 0.95 (5% headroom)
     * ClipDetectionStage clipDetector(0.95f);
     * clipDetector.process(buffer, numSamples, numChannels);
     * // Output: 1.0 where clipping occurs, 0.0 elsewhere
     * ```
     */
    class ClipDetectionStage : public IDspStage
    {
    public:
        /**
         * @brief Construct a ClipDetectionStage with a threshold.
         * @param threshold Absolute amplitude threshold for clipping (must be > 0)
         * @throws std::invalid_argument if threshold <= 0
         */
        explicit ClipDetectionStage(float threshold)
            : m_threshold(threshold)
        {
            if (threshold <= 0.0f)
            {
                throw std::invalid_argument("ClipDetection: threshold must be > 0");
            }
        }

        const char *getType() const override
        {
            return "clipDetection";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Process each sample independently
            for (size_t i = 0; i < numSamples; ++i)
            {
                float absVal = std::abs(buffer[i]);
                buffer[i] = (absVal >= m_threshold) ? 1.0f : 0.0f;
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("threshold", m_threshold);
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("threshold"))
            {
                m_threshold = state.Get("threshold").As<Napi::Number>().FloatValue();
            }
        }

        void reset() override
        {
            // No state to reset (stateless stage)
        }

        bool isResizing() const override
        {
            return false; // Does not change buffer size
        }

    private:
        float m_threshold;
    };

} // namespace dsp::adapters
