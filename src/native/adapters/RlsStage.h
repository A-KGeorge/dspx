#pragma once

#include "../IDspStage.h"
#include "../core/RlsFilter.h"
#include "../utils/SimdOps.h"
#include <napi.h>
#include <memory>
#include <vector>
#include <stdexcept>

namespace dsp
{
    namespace adapters
    {

        /**
         * @brief RLS (Recursive Least Squares) adaptive filter pipeline stage.
         *
         * RLS provides faster convergence than LMS/NLMS at the cost of O(N^2) complexity.
         * Maintains an N×N inverse covariance matrix for optimal weight updates.
         *
         * This stage REQUIRES exactly 2 channels:
         * - Channel 0: Primary signal x[n] (the signal to be filtered)
         * - Channel 1: Desired signal d[n] (the reference signal)
         *
         * Output: Error signal e[n] = d[n] - y[n] on both channels
         *
         * Parameters:
         * - numTaps: Filter length (number of coefficients)
         * - lambda: Forgetting factor (0 < λ ≤ 1, typically 0.98-0.9999)
         *   - Higher values (0.999): Longer memory, slower adaptation
         *   - Lower values (0.95): Shorter memory, faster tracking
         * - delta: Regularization parameter (typically 0.01-1.0)
         *   - Controls initial P matrix: P(0) = δ * I
         *   - Larger values: More initial uncertainty, faster initial convergence
         */
        class RlsStage : public IDspStage
        {
        public:
            RlsStage(size_t numTaps, float lambda, float delta = 0.01f)
                : m_numTaps(numTaps), m_lambda(lambda), m_delta(delta), m_initialized(false)
            {

                if (numTaps == 0)
                {
                    throw std::invalid_argument("RlsStage: numTaps must be > 0");
                }
                if (lambda <= 0.0f || lambda > 1.0f)
                {
                    throw std::invalid_argument("RlsStage: lambda must be in (0, 1]");
                }
                if (delta <= 0.0f)
                {
                    throw std::invalid_argument("RlsStage: delta must be > 0");
                }
            }

            const char *getType() const override
            {
                return "rlsFilter";
            }

            void process(float *buffer, size_t numSamples, int numChannels,
                         const float *timestamps = nullptr) override
            {

                if (numChannels != 2)
                {
                    throw std::invalid_argument(
                        "RlsStage requires exactly 2 channels: "
                        "Channel 0 = primary signal x[n], Channel 1 = desired signal d[n]");
                }

                if (!m_initialized)
                {
                    m_filter = std::make_unique<dsp::core::RlsFilter>(m_numTaps, m_lambda, m_delta);
                    m_initialized = true;
                }

                size_t samplesPerChannel = numSamples / numChannels;

                // Ensure scratch buffers (Phase 1 optimization)
                ensureScratchBuffers(samplesPerChannel);

                // Deinterleave using SIMD
                dsp::simd::deinterleave2Ch(buffer, m_scratch_ch0.data(), m_scratch_ch1.data(), samplesPerChannel);

                // Process each sample
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    // RLS adaptive filtering
                    float error = m_filter->processSample(m_scratch_ch0[i], m_scratch_ch1[i]);
                    m_scratch_ch0[i] = error;
                    m_scratch_ch1[i] = error;
                }

                // Interleave output using SIMD
                dsp::simd::interleave2Ch(m_scratch_ch0.data(), m_scratch_ch1.data(), buffer, samplesPerChannel);
            }

            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);

                state.Set("numTaps", Napi::Number::New(env, m_numTaps));
                state.Set("lambda", Napi::Number::New(env, m_lambda));
                state.Set("delta", Napi::Number::New(env, m_delta));
                state.Set("initialized", Napi::Boolean::New(env, m_initialized));

                if (m_initialized && m_filter)
                {
                    // Serialize weights
                    const auto &weights = m_filter->getWeights();
                    Napi::Array weightsArray = Napi::Array::New(env, weights.size());
                    for (size_t i = 0; i < weights.size(); ++i)
                    {
                        weightsArray.Set(i, Napi::Number::New(env, weights[i]));
                    }
                    state.Set("weights", weightsArray);

                    // Serialize inverse covariance matrix (N×N)
                    const auto &inverseCov = m_filter->getInverseCov();
                    Napi::Array pMatrixArray = Napi::Array::New(env, inverseCov.size());
                    for (size_t i = 0; i < inverseCov.size(); ++i)
                    {
                        pMatrixArray.Set(i, Napi::Number::New(env, inverseCov[i]));
                    }
                    state.Set("inverseCov", pMatrixArray);

                    // Serialize buffer contents
                    const auto &buffer = m_filter->getBuffer();
                    std::vector<float> bufferData = buffer.toVector();
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t i = 0; i < bufferData.size(); ++i)
                    {
                        bufferArray.Set(i, Napi::Number::New(env, bufferData[i]));
                    }
                    state.Set("buffer", bufferArray);
                }

                return state;
            }

            void deserializeState(const Napi::Object &state) override
            {
                m_numTaps = state.Get("numTaps").As<Napi::Number>().Uint32Value();
                m_lambda = state.Get("lambda").As<Napi::Number>().FloatValue();
                m_delta = state.Get("delta").As<Napi::Number>().FloatValue();
                m_initialized = state.Get("initialized").As<Napi::Boolean>().Value();

                if (m_initialized)
                {
                    // Recreate filter
                    m_filter = std::make_unique<dsp::core::RlsFilter>(m_numTaps, m_lambda, m_delta);

                    // Restore weights
                    if (state.Has("weights"))
                    {
                        Napi::Array weightsArray = state.Get("weights").As<Napi::Array>();
                        std::vector<float> weights(weightsArray.Length());
                        for (size_t i = 0; i < weightsArray.Length(); ++i)
                        {
                            weights[i] = weightsArray.Get(i).As<Napi::Number>().FloatValue();
                        }
                        m_filter->setWeights(weights);
                    }

                    // Restore inverse covariance matrix
                    if (state.Has("inverseCov"))
                    {
                        Napi::Array pMatrixArray = state.Get("inverseCov").As<Napi::Array>();
                        std::vector<float> inverseCov(pMatrixArray.Length());
                        for (size_t i = 0; i < pMatrixArray.Length(); ++i)
                        {
                            inverseCov[i] = pMatrixArray.Get(i).As<Napi::Number>().FloatValue();
                        }
                        m_filter->setInverseCov(inverseCov);
                    }

                    // Restore buffer
                    if (state.Has("buffer"))
                    {
                        Napi::Array bufferArray = state.Get("buffer").As<Napi::Array>();
                        std::vector<float> bufferData(bufferArray.Length());
                        for (size_t i = 0; i < bufferArray.Length(); ++i)
                        {
                            bufferData[i] = bufferArray.Get(i).As<Napi::Number>().FloatValue();
                        }
                        m_filter->setBuffer(bufferData);
                    }
                }
            }

            void reset() override
            {
                if (m_initialized && m_filter)
                {
                    m_filter->reset();
                }
            }

            void serializeToon(dsp::toon::Serializer &s) const override
            {
                s.startObject();

                s.writeString("numTaps");
                s.writeInt32(static_cast<int32_t>(m_numTaps));

                s.writeString("lambda");
                s.writeFloat(m_lambda);

                s.writeString("delta");
                s.writeFloat(m_delta);

                s.writeString("initialized");
                s.writeBool(m_initialized);

                if (m_initialized && m_filter)
                {
                    // Serialize weights
                    s.writeString("weights");
                    s.writeFloatArray(m_filter->getWeights());

                    // Serialize inverse covariance matrix
                    s.writeString("inverseCov");
                    s.writeFloatArray(m_filter->getInverseCov());

                    // Serialize buffer
                    const auto &buffer = m_filter->getBuffer();
                    std::vector<float> bufferData = buffer.toVector();
                    s.writeString("buffer");
                    s.writeFloatArray(bufferData);
                }

                s.endObject();
            }

            void deserializeToon(dsp::toon::Deserializer &d) override
            {
                d.consumeToken(dsp::toon::T_OBJECT_START);

                std::string key = d.readString(); // "numTaps"
                m_numTaps = d.readInt32();

                key = d.readString(); // "lambda"
                m_lambda = d.readFloat();

                key = d.readString(); // "delta"
                m_delta = d.readFloat();

                key = d.readString(); // "initialized"
                m_initialized = d.readBool();

                if (m_initialized)
                {
                    // Recreate filter
                    m_filter = std::make_unique<dsp::core::RlsFilter>(m_numTaps, m_lambda, m_delta);

                    // Restore weights
                    key = d.readString(); // "weights"
                    std::vector<float> weights = d.readFloatArray();
                    m_filter->setWeights(weights);

                    // Restore inverse covariance matrix
                    key = d.readString(); // "inverseCov"
                    std::vector<float> inverseCov = d.readFloatArray();
                    m_filter->setInverseCov(inverseCov);

                    // Restore buffer
                    key = d.readString(); // "buffer"
                    std::vector<float> bufferData = d.readFloatArray();
                    m_filter->setBuffer(bufferData);
                }

                d.consumeToken(dsp::toon::T_OBJECT_END);
            }

        private:
            void ensureScratchBuffers(size_t samplesPerChannel)
            {
                if (m_scratch_ch0.capacity() < samplesPerChannel)
                {
                    size_t new_capacity = samplesPerChannel * 2;
                    m_scratch_ch0.reserve(new_capacity);
                    m_scratch_ch1.reserve(new_capacity);
                }
                m_scratch_ch0.resize(samplesPerChannel);
                m_scratch_ch1.resize(samplesPerChannel);
            }

            size_t m_numTaps;
            float m_lambda;
            float m_delta;
            bool m_initialized;
            std::unique_ptr<dsp::core::RlsFilter> m_filter;

            // Pre-allocated scratch buffers
            std::vector<float> m_scratch_ch0;
            std::vector<float> m_scratch_ch1;
        };

    } // namespace adapters
} // namespace dsp
