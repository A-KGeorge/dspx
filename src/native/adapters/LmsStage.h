#pragma once
#include "../IDspStage.h"
#include "../core/DifferentiableFilter.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <cmath>

namespace dsp
{
    /**
     * @brief Adaptive LMS Filter implemented as an IDspStage.
     *
     * This stage REQUIRES numChannels == 2:
     * - Channel 0: Primary signal x[n] (the signal to be processed)
     * - Channel 1: Desired signal d[n] (the reference signal)
     *
     * The stage outputs the error signal e[n] = d[n] - y[n] in-place,
     * which for noise cancellation represents the cleaned signal.
     *
     * The filter adapts its weights using the LMS algorithm:
     * - y[n] = w^T * x[n]  (filter output)
     * - e[n] = d[n] - y[n]  (error/cleaned signal)
     * - w[n+1] = w[n] + mu * e[n] * x[n]  (weight update)
     *
     * Use cases:
     * - Noise cancellation: x[n] = noisy signal, d[n] = noise reference
     * - Echo cancellation: x[n] = far-end signal, d[n] = microphone signal
     * - System identification: x[n] = input, d[n] = system output
     */
    class LmsStage : public IDspStage
    {
    public:
        /**
         * @brief Construct an LMS filter stage.
         *
         * @param numTaps Number of filter taps (filter order)
         * @param learningRate Learning rate (mu), typically 0.001 to 0.1
         * @param normalized If true, use NLMS (Normalized LMS) algorithm
         * @param lambda Regularization parameter for leaky LMS (0.0 = standard LMS)
         */
        LmsStage(size_t numTaps, float learningRate, bool normalized = false, float lambda = 0.0f)
            : m_numTaps(numTaps),
              m_learningRate(learningRate),
              m_normalized(normalized),
              m_lambda(lambda),
              m_filter(nullptr) // Initialize as nullptr, will be created on first use
        {
            if (numTaps == 0)
            {
                throw std::invalid_argument("LmsStage: numTaps must be > 0");
            }
            if (learningRate <= 0.0f || learningRate > 1.0f)
            {
                throw std::invalid_argument("LmsStage: learningRate must be in (0, 1]");
            }
            if (lambda < 0.0f || lambda >= 1.0f)
            {
                throw std::invalid_argument("LmsStage: lambda must be in [0, 1)");
            }
        }

        const char *getType() const override { return "lmsFilter"; }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // CRITICAL: This stage REQUIRES exactly 2 channels
            if (numChannels != 2)
            {
                throw std::invalid_argument(
                    "LmsStage requires exactly 2 channels: "
                    "Channel 0 = primary signal x[n], Channel 1 = desired signal d[n]. "
                    "Got " +
                    std::to_string(numChannels) + " channels.");
            }

            // Initialize filter if needed (first call)
            if (!m_initialized)
            {
                m_filter = std::make_unique<dsp::core::DifferentiableFilter<float>>(m_numTaps, m_learningRate, m_lambda, m_normalized);
                m_filter->init(1); // Single "channel" in DifferentiableFilter (we handle interleaving here)
                m_initialized = true;
            }

            size_t samplesPerChannel = numSamples / numChannels;

            // Temporary buffers for deinterleaving
            std::vector<float> primarySignal(samplesPerChannel); // x[n] - channel 0
            std::vector<float> desiredSignal(samplesPerChannel); // d[n] - channel 1
            std::vector<float> outputSignal(samplesPerChannel);  // y[n] - filter output
            std::vector<float> errorSignal(samplesPerChannel);   // e[n] - error (cleaned signal)

            // Deinterleave: Extract channel 0 (primary) and channel 1 (desired)
            for (size_t i = 0; i < samplesPerChannel; ++i)
            {
                primarySignal[i] = buffer[i * numChannels + 0]; // Channel 0: x[n]
                desiredSignal[i] = buffer[i * numChannels + 1]; // Channel 1: d[n]
            }

            // Run adaptive LMS filter
            // Note: DifferentiableFilter uses planar layout internally, but that's abstracted away
            m_filter->process(
                primarySignal.data(), // input x[n]
                desiredSignal.data(), // desired d[n]
                outputSignal.data(),  // output y[n] (filter prediction)
                errorSignal.data(),   // error e[n] = d[n] - y[n]
                samplesPerChannel,
                true // adapt = true (update weights)
            );

            // Write back interleaved result
            // For noise cancellation, the output is the error signal e[n]
            // For system identification, you might want y[n] instead (see below)
            for (size_t i = 0; i < samplesPerChannel; ++i)
            {
                // Output the cleaned signal (error) to both channels
                // This allows downstream stages to process it
                buffer[i * numChannels + 0] = errorSignal[i]; // Channel 0: e[n]
                buffer[i * numChannels + 1] = errorSignal[i]; // Channel 1: e[n] (duplicate for consistency)
            }

            // Alternative output modes (could be configurable):
            // 1. Noise cancellation (current): output = e[n] = d[n] - y[n]
            // 2. System identification: output = y[n] (predicted signal)
            // 3. Dual output: channel 0 = e[n], channel 1 = y[n]
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);

            // Save filter parameters
            state.Set("numTaps", Napi::Number::New(env, m_numTaps));
            state.Set("learningRate", Napi::Number::New(env, m_learningRate));
            state.Set("normalized", Napi::Boolean::New(env, m_normalized));
            state.Set("lambda", Napi::Number::New(env, m_lambda));
            state.Set("initialized", Napi::Boolean::New(env, m_initialized));

            // Save filter weights and state
            if (m_initialized)
            {
                auto weights = m_filter->getWeights(0); // Single "channel" (we handle interleaving)
                Napi::Array weightsArray = Napi::Array::New(env, weights.size());
                for (size_t i = 0; i < weights.size(); ++i)
                {
                    weightsArray.Set(i, Napi::Number::New(env, weights[i]));
                }
                state.Set("weights", weightsArray);

                // Note: DifferentiableFilter's internal circular buffer state is preserved
                // through the weights, as the filter is designed for stateful operation
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            // Restore filter parameters
            m_numTaps = state.Get("numTaps").As<Napi::Number>().Uint32Value();
            m_learningRate = state.Get("learningRate").As<Napi::Number>().FloatValue();
            m_normalized = state.Get("normalized").As<Napi::Boolean>().Value();
            m_lambda = state.Get("lambda").As<Napi::Number>().FloatValue();
            m_initialized = state.Get("initialized").As<Napi::Boolean>().Value();

            // Recreate filter with restored parameters
            m_filter = std::make_unique<dsp::core::DifferentiableFilter<float>>(m_numTaps, m_learningRate, m_lambda, m_normalized);

            // Restore weights
            if (m_initialized && state.Has("weights"))
            {
                m_filter->init(1); // Reinitialize filter

                Napi::Array weightsArray = state.Get("weights").As<Napi::Array>();
                std::vector<float> weights(weightsArray.Length());
                for (size_t i = 0; i < weightsArray.Length(); ++i)
                {
                    weights[i] = weightsArray.Get(i).As<Napi::Number>().FloatValue();
                }
                m_filter->setWeights(0, weights);
            }
        }

        void reset() override
        {
            if (m_initialized)
            {
                m_filter->reset();
            }
        }

    private:
        size_t m_numTaps;
        float m_learningRate;
        bool m_normalized;
        float m_lambda;
        bool m_initialized = false;
        std::unique_ptr<dsp::core::DifferentiableFilter<float>> m_filter;
    };

} // namespace dsp
