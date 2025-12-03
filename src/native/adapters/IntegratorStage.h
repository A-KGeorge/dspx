#ifndef INTEGRATOR_STAGE_H
#define INTEGRATOR_STAGE_H

#include "../IDspStage.h"
#include "../utils/Toon.h"
#include <vector>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace dsp::adapters
{
    /**
     * @brief Leaky integrator stage using IIR filter.
     *
     * Implements a first-order IIR integrator with controllable leakage:
     *   y[n] = x[n] + α * y[n-1]
     *
     * Where α (alpha) is the leakage coefficient (0 < α <= 1):
     * - α = 1.0: Perfect integration (DC gain = ∞, no leakage)
     * - α = 0.99: Slight leakage (DC gain ≈ 100)
     * - α = 0.9: More leakage (DC gain = 10)
     *
     * Transfer function: H(z) = 1 / (1 - α*z^-1)
     *
     * Use cases:
     * - Accumulating sensor readings (accelerometer → velocity)
     * - Low-pass filtering with adjustable time constant
     * - Envelope detection after rectification
     * - Smoothing step responses
     *
     * Note: For α = 1.0, DC signals will grow without bound.
     * For streaming applications, α < 1.0 is recommended to prevent overflow.
     */
    class IntegratorStage : public IDspStage
    {
    private:
        float m_alpha;                    // Leakage coefficient (0 < α <= 1)
        int m_num_channels;               // Number of channels
        std::vector<float> m_prev_output; // Previous output y[n-1] per channel

    public:
        /**
         * @brief Construct integrator with leakage coefficient.
         * @param alpha Leakage coefficient (0 < α <= 1). Default 0.99.
         */
        explicit IntegratorStage(float alpha = 0.99f)
            : m_alpha(alpha), m_num_channels(0)
        {
            if (alpha <= 0.0f || alpha > 1.0f)
            {
                throw std::invalid_argument("Integrator alpha must be in range (0, 1]");
            }
        }

        const char *getType() const override
        {
            return "integrator";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Initialize state on first call or channel change
            if (m_num_channels != numChannels || m_prev_output.empty())
            {
                m_num_channels = numChannels;
                m_prev_output.resize(numChannels, 0.0f);
            }

            size_t samplesPerChannel = numSamples / numChannels;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float prev_out = m_prev_output[ch];

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float current_in = buffer[idx];

                    // Leaky integrator: y[n] = x[n] + α * y[n-1]
                    float current_out = current_in + m_alpha * prev_out;

                    buffer[idx] = current_out;
                    prev_out = current_out;
                }

                // Save state for next process call
                m_prev_output[ch] = prev_out;
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("alpha", m_alpha);
            state.Set("numChannels", m_num_channels);

            if (!m_prev_output.empty())
            {
                Napi::Array prevArray = Napi::Array::New(env, m_prev_output.size());
                for (size_t i = 0; i < m_prev_output.size(); ++i)
                    prevArray.Set(i, m_prev_output[i]);
                state.Set("prevOutput", prevArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("alpha"))
                m_alpha = state.Get("alpha").As<Napi::Number>().FloatValue();

            if (state.Has("numChannels"))
                m_num_channels = state.Get("numChannels").As<Napi::Number>().Int32Value();

            if (state.Has("prevOutput"))
            {
                Napi::Array prevArray = state.Get("prevOutput").As<Napi::Array>();
                m_prev_output.clear();
                for (size_t i = 0; i < prevArray.Length(); ++i)
                    m_prev_output.push_back(prevArray.Get(i).As<Napi::Number>().FloatValue());
            }
        }

        void reset() override
        {
            std::fill(m_prev_output.begin(), m_prev_output.end(), 0.0f);
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.writeFloat(m_alpha);
            s.writeInt32(m_num_channels);
            s.writeFloatArray(m_prev_output);
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            m_alpha = d.readFloat();
            m_num_channels = d.readInt32();
            m_prev_output = d.readFloatArray();
        }

        bool isResizing() const override
        {
            return false;
        }
    };

} // namespace dsp::adapters

#endif // INTEGRATOR_STAGE_H