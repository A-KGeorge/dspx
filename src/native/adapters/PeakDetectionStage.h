#pragma once

#include "../IDspStage.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace dsp::adapters
{
    /**
     * @brief Peak Detection Stage - Detects local maxima in a signal.
     *
     * This stage identifies peaks (local maxima) in the input signal using a simple
     * three-point comparison method. A peak is detected when:
     * 1. The previous sample is greater than the sample before it
     * 2. The previous sample is greater than the current sample
     * 3. The previous sample exceeds the threshold
     *
     * **Use Cases:**
     * - Heart rate detection (R-peaks in ECG)
     * - Event detection in sensor data
     * - Tempo detection in audio
     * - Spike detection in neural recordings
     *
     * **Output:**
     * - 1.0 at peak locations
     * - 0.0 elsewhere
     *
     * **State:** Maintains 2 previous samples per channel for continuity
     */
    class PeakDetectionStage : public IDspStage
    {
    public:
        explicit PeakDetectionStage(float threshold)
            : m_threshold(threshold), m_num_channels(0)
        {
            if (threshold < 0.0f)
            {
                throw std::invalid_argument("PeakDetection: threshold must be >= 0");
            }
        }

        const char *getType() const override
        {
            return "peakDetection";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_num_channels != numChannels || m_prev_sample.empty())
            {
                m_num_channels = numChannels;
                m_prev_sample.resize(numChannels, 0.0f);
                m_prev_prev_sample.resize(numChannels, 0.0f);
            }

            size_t samplesPerChannel = numSamples / numChannels;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float prev_prev = m_prev_prev_sample[ch];
                float prev = m_prev_sample[ch];

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float current = buffer[idx];

                    // Check if `prev` (the previous sample) was a peak
                    // We can now confirm this since we have the current sample
                    bool prev_is_peak = (prev > prev_prev) && (prev > current) && (prev >= m_threshold);

                    if (i > 0)
                    {
                        // Write peak marker BACK to the previous sample's position
                        buffer[idx - numChannels] = prev_is_peak ? 1.0f : 0.0f;
                    }
                    else
                    {
                        // For the first sample, we mark it if the prev (from previous process call) was a peak
                        buffer[idx] = prev_is_peak ? 1.0f : 0.0f;
                    }

                    // Shift history forward
                    prev_prev = prev;
                    prev = current;
                }

                // The last sample in the buffer can't be confirmed as a peak yet
                // (we need to see the next sample), so set it to 0
                // BUT: Don't overwrite if there's only 1 sample and we just wrote the pending result at i==0
                if (samplesPerChannel > 1)
                {
                    buffer[(samplesPerChannel - 1) * numChannels + ch] = 0.0f;
                }

                // Save state for next process call
                m_prev_prev_sample[ch] = prev_prev;
                m_prev_sample[ch] = prev;
            }
        }
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("threshold", m_threshold);
            state.Set("numChannels", m_num_channels);

            if (!m_prev_sample.empty())
            {
                Napi::Array prevArray = Napi::Array::New(env, m_prev_sample.size());
                Napi::Array prevPrevArray = Napi::Array::New(env, m_prev_prev_sample.size());

                for (size_t i = 0; i < m_prev_sample.size(); ++i)
                {
                    prevArray.Set(i, m_prev_sample[i]);
                    prevPrevArray.Set(i, m_prev_prev_sample[i]);
                }

                state.Set("prevSample", prevArray);
                state.Set("prevPrevSample", prevPrevArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("threshold"))
                m_threshold = state.Get("threshold").As<Napi::Number>().FloatValue();
            if (state.Has("numChannels"))
                m_num_channels = state.Get("numChannels").As<Napi::Number>().Int32Value();

            if (state.Has("prevSample"))
            {
                Napi::Array prevArray = state.Get("prevSample").As<Napi::Array>();
                Napi::Array prevPrevArray = state.Get("prevPrevSample").As<Napi::Array>();

                m_prev_sample.clear();
                m_prev_prev_sample.clear();

                for (size_t i = 0; i < prevArray.Length(); ++i)
                {
                    m_prev_sample.push_back(prevArray.Get(i).As<Napi::Number>().FloatValue());
                    m_prev_prev_sample.push_back(prevPrevArray.Get(i).As<Napi::Number>().FloatValue());
                }
            }
        }

        void reset() override
        {
            std::fill(m_prev_sample.begin(), m_prev_sample.end(), 0.0f);
            std::fill(m_prev_prev_sample.begin(), m_prev_prev_sample.end(), 0.0f);
        }

        bool isResizing() const override { return false; }

    private:
        float m_threshold;
        int m_num_channels;
        std::vector<float> m_prev_sample;
        std::vector<float> m_prev_prev_sample;
    };

} // namespace dsp::adapters
