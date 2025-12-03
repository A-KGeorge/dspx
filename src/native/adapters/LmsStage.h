#pragma once
#include "../IDspStage.h"
#include "../core/DifferentiableFilter.h"
#include "../utils/SimdOps.h"
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

            // Ensure scratch buffers are large enough (grows as needed, never shrinks)
            ensureScratchBuffers(samplesPerChannel);

            // Deinterleave using SIMD-optimized function
            dsp::simd::deinterleave2Ch(buffer, m_scratch_primary.data(), m_scratch_desired.data(), samplesPerChannel);

            // Run adaptive LMS filter
            // Note: DifferentiableFilter uses planar layout internally, but that's abstracted away
            m_filter->process(
                m_scratch_primary.data(), // input x[n]
                m_scratch_desired.data(), // desired d[n]
                m_scratch_output.data(),  // output y[n] (filter prediction)
                m_scratch_error.data(),   // error e[n] = d[n] - y[n]
                samplesPerChannel,
                true // adapt = true (update weights)
            );

            // Interleave error signal back to buffer using SIMD
            // For noise cancellation, output is the error signal e[n] on both channels
            dsp::simd::interleave2Ch(m_scratch_error.data(), m_scratch_error.data(), buffer, samplesPerChannel);

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

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.startObject();

            s.writeString("numTaps");
            s.writeInt32(static_cast<int32_t>(m_numTaps));

            s.writeString("learningRate");
            s.writeFloat(m_learningRate);

            s.writeString("normalized");
            s.writeBool(m_normalized);

            s.writeString("lambda");
            s.writeFloat(m_lambda);

            s.writeString("initialized");
            s.writeBool(m_initialized);

            if (m_initialized)
            {
                auto weights = m_filter->getWeights(0);
                s.writeString("weights");
                s.writeFloatArray(weights);
            }

            s.endObject();
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            d.consumeToken(dsp::toon::T_OBJECT_START);

            std::string key = d.readString(); // "numTaps"
            m_numTaps = d.readInt32();

            key = d.readString(); // "learningRate"
            m_learningRate = d.readFloat();

            key = d.readString(); // "normalized"
            m_normalized = d.readBool();

            key = d.readString(); // "lambda"
            m_lambda = d.readFloat();

            key = d.readString(); // "initialized"
            m_initialized = d.readBool();

            // Recreate filter with restored parameters
            m_filter = std::make_unique<dsp::core::DifferentiableFilter<float>>(m_numTaps, m_learningRate, m_lambda, m_normalized);

            if (m_initialized)
            {
                m_filter->init(1); // Reinitialize filter

                key = d.readString(); // "weights"
                std::vector<float> weights = d.readFloatArray();
                m_filter->setWeights(0, weights);
            }

            d.consumeToken(dsp::toon::T_OBJECT_END);
        }

    private:
        void ensureScratchBuffers(size_t samplesPerChannel)
        {
            if (m_scratch_primary.capacity() < samplesPerChannel)
            {
                // Over-allocate to reduce future resizes (2x growth strategy)
                size_t new_capacity = samplesPerChannel * 2;
                m_scratch_primary.reserve(new_capacity);
                m_scratch_desired.reserve(new_capacity);
                m_scratch_output.reserve(new_capacity);
                m_scratch_error.reserve(new_capacity);
            }
            // Resize to exact size needed (doesn't reallocate if within capacity)
            m_scratch_primary.resize(samplesPerChannel);
            m_scratch_desired.resize(samplesPerChannel);
            m_scratch_output.resize(samplesPerChannel);
            m_scratch_error.resize(samplesPerChannel);
        }

        size_t m_numTaps;
        float m_learningRate;
        bool m_normalized;
        float m_lambda;
        bool m_initialized = false;
        std::unique_ptr<dsp::core::DifferentiableFilter<float>> m_filter;

        // Pre-allocated scratch buffers (Phase 1 optimization)
        std::vector<float> m_scratch_primary;
        std::vector<float> m_scratch_desired;
        std::vector<float> m_scratch_output;
        std::vector<float> m_scratch_error;
    };

} // namespace dsp
