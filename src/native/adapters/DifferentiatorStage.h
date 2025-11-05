#pragma once

#include "../IDspStage.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace dsp::adapters
{
    /**
     * @brief Differentiator Stage - Computes the discrete derivative of a signal.
     *
     * Implements finite difference: y[n] = x[n] - x[n-1]
     * Equivalent to FIR filter with coefficients [1, -1].
     *
     * **Use Cases:**
     * - Edge detection in signals
     * - Velocity from position data
     * - Change detection
     * - High-pass filtering (DC removal)
     *
     * **Note:** Amplifies high-frequency noise. Consider pre-filtering noisy signals.
     */
    class DifferentiatorStage : public IDspStage
    {
    public:
        DifferentiatorStage() : m_num_channels(0) {}

        const char *getType() const override
        {
            return "differentiator";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_num_channels != numChannels || m_prev_sample.empty())
            {
                m_num_channels = numChannels;
                m_prev_sample.resize(numChannels, 0.0f);
            }

            size_t samplesPerChannel = numSamples / numChannels;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float prev = m_prev_sample[ch];

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float current = buffer[idx];
                    buffer[idx] = current - prev;
                    prev = current;
                }

                m_prev_sample[ch] = prev;
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("numChannels", m_num_channels);

            if (!m_prev_sample.empty())
            {
                Napi::Array prevArray = Napi::Array::New(env, m_prev_sample.size());
                for (size_t i = 0; i < m_prev_sample.size(); ++i)
                    prevArray.Set(i, m_prev_sample[i]);
                state.Set("prevSample", prevArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("numChannels"))
                m_num_channels = state.Get("numChannels").As<Napi::Number>().Int32Value();

            if (state.Has("prevSample"))
            {
                Napi::Array prevArray = state.Get("prevSample").As<Napi::Array>();
                m_prev_sample.clear();
                for (size_t i = 0; i < prevArray.Length(); ++i)
                    m_prev_sample.push_back(prevArray.Get(i).As<Napi::Number>().FloatValue());
            }
        }

        void reset() override
        {
            std::fill(m_prev_sample.begin(), m_prev_sample.end(), 0.0f);
        }

        bool isResizing() const override { return false; }

    private:
        int m_num_channels;
        std::vector<float> m_prev_sample;
    };

} // namespace dsp::adapters
