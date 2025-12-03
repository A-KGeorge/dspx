#ifndef DSP_LINEAR_REGRESSION_STAGE_H
#define DSP_LINEAR_REGRESSION_STAGE_H

#include "../IDspStage.h"
#include "../utils/SimdOps.h"
#include "../utils/Toon.h"
#include <vector>
#include <cmath>
#include <stdexcept>

namespace dsp
{
    namespace adapters
    {

        /**
         * @brief Policy-based Linear Regression Stage for DSP pipeline
         *
         * This stage fits a linear regression model (y = mx + b) to a sliding window
         * of samples and can output:
         * - Slope (m) - trend direction
         * - Intercept (b) - baseline offset
         * - Fitted values - detrended signal (residuals)
         * - Predictions - extrapolated values
         *
         * Uses SIMD-accelerated least squares fitting for optimal performance.
         *
         * Policy determines what to output:
         * - SlopePolicy: Output slope (trend)
         * - InterceptPolicy: Output intercept (baseline)
         * - ResidualsPolicy: Output residuals (detrended signal)
         * - PredictionsPolicy: Output fitted values
         */

        // Output policies
        struct SlopePolicy
        {
            static constexpr const char *name = "slope";
            static float compute(float slope, float intercept, float x, float y)
            {
                return slope;
            }
        };

        struct InterceptPolicy
        {
            static constexpr const char *name = "intercept";
            static float compute(float slope, float intercept, float x, float y)
            {
                return intercept;
            }
        };

        struct ResidualsPolicy
        {
            static constexpr const char *name = "residuals";
            static float compute(float slope, float intercept, float x, float y)
            {
                return y - (slope * x + intercept); // Actual - Predicted
            }
        };

        struct PredictionsPolicy
        {
            static constexpr const char *name = "predictions";
            static float compute(float slope, float intercept, float x, float y)
            {
                return slope * x + intercept; // Fitted value
            }
        };

        template <typename OutputPolicy = SlopePolicy>
        class LinearRegressionStage : public IDspStage
        {
        private:
            size_t m_windowSize;
            size_t m_numChannels;
            std::vector<std::vector<float>> m_buffers; // Per-channel circular buffers
            std::vector<size_t> m_writeIndices;        // Per-channel write positions
            std::vector<size_t> m_sampleCounts;        // Per-channel sample counts
            std::vector<float> m_xValues;              // Pre-computed x values (0, 1, 2, ...)

            // Cached statistics (recomputed per channel each time)
            float m_meanX;
            float m_meanY;
            float m_sumXY;
            float m_sumXX;

        public:
            explicit LinearRegressionStage(size_t windowSize)
                : m_windowSize(windowSize), m_numChannels(0), m_meanX(0.0f), m_meanY(0.0f), m_sumXY(0.0f), m_sumXX(0.0f)
            {
                if (windowSize < 2)
                {
                    throw std::invalid_argument("Linear regression window size must be at least 2");
                }

                // Pre-compute x values (0, 1, 2, ..., windowSize-1)
                m_xValues.resize(windowSize);
                for (size_t i = 0; i < windowSize; ++i)
                {
                    m_xValues[i] = static_cast<float>(i);
                }

                // Pre-compute mean of x values
                m_meanX = static_cast<float>(windowSize - 1) / 2.0f;

                // Pre-compute sum(x^2) for denominator
                m_sumXX = 0.0f;
                for (size_t i = 0; i < windowSize; ++i)
                {
                    float xCentered = m_xValues[i] - m_meanX;
                    m_sumXX += xCentered * xCentered;
                }
            }

            const char *getType() const override
            {
                return OutputPolicy::name;
            }

            void init(size_t numChannels)
            {
                m_numChannels = numChannels;
                m_buffers.resize(numChannels);
                m_writeIndices.resize(numChannels, 0);
                m_sampleCounts.resize(numChannels, 0);

                for (auto &buffer : m_buffers)
                {
                    buffer.resize(m_windowSize, 0.0f);
                }
            }

            void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
            {
                if (m_numChannels == 0)
                {
                    init(numChannels);
                }

                // Process interleaved multi-channel data
                // buffer[i] where channel = i % numChannels, sample_index = i / numChannels
                for (size_t i = 0; i < numSamples; ++i)
                {
                    int channel = i % numChannels;
                    float inputValue = buffer[i];

                    auto &circularBuffer = m_buffers[channel];
                    size_t &writeIdx = m_writeIndices[channel];
                    size_t &sampleCount = m_sampleCounts[channel];

                    // Add new sample to circular buffer
                    circularBuffer[writeIdx] = inputValue;
                    writeIdx = (writeIdx + 1) % m_windowSize;
                    if (sampleCount < m_windowSize)
                    {
                        sampleCount++;
                    }

                    // Need at least 2 samples for regression
                    if (sampleCount < 2)
                    {
                        buffer[i] = 0.0f;
                        continue;
                    }

                    // Fit linear regression: y = mx + b
                    float slope, intercept;
                    fitLinearRegression(circularBuffer, sampleCount, writeIdx, slope, intercept);

                    // Apply policy to determine output
                    // Use the most recent sample position as x value
                    float x = static_cast<float>(sampleCount - 1);
                    float y = inputValue;
                    buffer[i] = OutputPolicy::compute(slope, intercept, x, y);
                }
            }

        private:
            void fitLinearRegression(const std::vector<float> &buffer, size_t validSamples,
                                     size_t writeIdx, float &slope, float &intercept)
            {
                // Compute mean of y values using SIMD
                float sumY = 0.0f;

                if (validSamples == m_windowSize)
                {
                    // Buffer is full, can use SIMD directly
                    sumY = simd::sum(buffer.data(), validSamples);
                }
                else
                {
                    // Buffer not full, sum manually
                    for (size_t i = 0; i < validSamples; ++i)
                    {
                        sumY += buffer[i];
                    }
                }

                m_meanY = sumY / static_cast<float>(validSamples);

                // Adjust meanX for partial buffer
                float adjustedMeanX = static_cast<float>(validSamples - 1) / 2.0f;

                // Compute sum((x - meanX) * (y - meanY)) and sum((x - meanX)^2)
                float sumXY = 0.0f;
                float sumXX = 0.0f;

                if (validSamples == m_windowSize)
                {
                    // Full buffer - process in order from writeIdx
                    for (size_t i = 0; i < validSamples; ++i)
                    {
                        size_t idx = (writeIdx + i) % m_windowSize;
                        float y = buffer[idx];
                        float x = static_cast<float>(i);
                        float xCentered = x - adjustedMeanX;
                        float yCentered = y - m_meanY;
                        sumXY += xCentered * yCentered;
                        sumXX += xCentered * xCentered;
                    }
                }
                else
                {
                    // Partial buffer - process in order
                    for (size_t i = 0; i < validSamples; ++i)
                    {
                        float y = buffer[i];
                        float x = static_cast<float>(i);
                        float xCentered = x - adjustedMeanX;
                        float yCentered = y - m_meanY;
                        sumXY += xCentered * yCentered;
                        sumXX += xCentered * xCentered;
                    }
                }

                // Compute slope: m = sum((x - meanX)(y - meanY)) / sum((x - meanX)^2)
                if (std::abs(sumXX) < 1e-10f)
                {
                    // Avoid division by zero (all x values identical)
                    slope = 0.0f;
                }
                else
                {
                    slope = sumXY / sumXX;
                }

                // Compute intercept: b = meanY - m * meanX
                intercept = m_meanY - slope * adjustedMeanX;
            }

        public:
            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);
                state.Set("type", "linearRegression");
                state.Set("policy", OutputPolicy::name);
                state.Set("windowSize", static_cast<double>(m_windowSize));
                state.Set("numChannels", static_cast<double>(m_numChannels));

                // Save per-channel state
                Napi::Array channels = Napi::Array::New(env, m_numChannels);
                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Save circular buffer
                    Napi::Array buffer = Napi::Array::New(env, m_buffers[ch].size());
                    for (size_t i = 0; i < m_buffers[ch].size(); ++i)
                    {
                        buffer.Set(i, m_buffers[ch][i]);
                    }
                    channelState.Set("buffer", buffer);
                    channelState.Set("writeIndex", static_cast<double>(m_writeIndices[ch]));
                    channelState.Set("sampleCount", static_cast<double>(m_sampleCounts[ch]));

                    channels.Set(ch, channelState);
                }
                state.Set("channels", channels);

                return state;
            }

            void deserializeState(const Napi::Object &state) override
            {
                if (!state.Has("type") || state.Get("type").ToString().Utf8Value() != "linearRegression")
                {
                    throw std::runtime_error("Invalid state type for LinearRegressionStage");
                }

                if (!state.Has("policy") || state.Get("policy").ToString().Utf8Value() != OutputPolicy::name)
                {
                    throw std::runtime_error("Policy mismatch in LinearRegressionStage state");
                }

                size_t windowSize = static_cast<size_t>(state.Get("windowSize").ToNumber().DoubleValue());
                if (windowSize != m_windowSize)
                {
                    throw std::runtime_error("Window size mismatch in LinearRegressionStage state");
                }

                size_t numChannels = static_cast<size_t>(state.Get("numChannels").ToNumber().DoubleValue());
                init(numChannels);

                // Restore per-channel state
                Napi::Array channels = state.Get("channels").As<Napi::Array>();
                for (size_t ch = 0; ch < numChannels; ++ch)
                {
                    Napi::Object channelState = channels.Get(ch).As<Napi::Object>();

                    // Restore circular buffer
                    Napi::Array buffer = channelState.Get("buffer").As<Napi::Array>();
                    m_buffers[ch].resize(buffer.Length());
                    for (size_t i = 0; i < buffer.Length(); ++i)
                    {
                        m_buffers[ch][i] = buffer.Get(i).ToNumber().FloatValue();
                    }

                    m_writeIndices[ch] = static_cast<size_t>(channelState.Get("writeIndex").ToNumber().DoubleValue());
                    m_sampleCounts[ch] = static_cast<size_t>(channelState.Get("sampleCount").ToNumber().DoubleValue());
                }
            }

            inline void serializeToon(dsp::toon::Serializer &s) const override
            {
                // Write configuration
                s.writeInt32(static_cast<int32_t>(m_windowSize));
                s.writeInt32(static_cast<int32_t>(m_numChannels));

                // Write per-channel state
                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    s.writeFloatArray(m_buffers[ch]);
                    s.writeInt32(static_cast<int32_t>(m_writeIndices[ch]));
                    s.writeInt32(static_cast<int32_t>(m_sampleCounts[ch]));
                }
            }

            inline void deserializeToon(dsp::toon::Deserializer &d) override
            {
                // Read configuration
                int32_t windowSize = d.readInt32();
                if (windowSize < 2)
                    throw std::runtime_error("Invalid windowSize in LinearRegressionStage deserialization");
                if (static_cast<size_t>(windowSize) != m_windowSize)
                    throw std::runtime_error("Window size mismatch in LinearRegressionStage deserialization");

                int32_t numChannels = d.readInt32();
                if (numChannels < 0)
                    throw std::runtime_error("Invalid numChannels in LinearRegressionStage deserialization");

                init(static_cast<size_t>(numChannels));

                // Read per-channel state
                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    m_buffers[ch] = d.readFloatArray();
                    m_writeIndices[ch] = static_cast<size_t>(d.readInt32());
                    m_sampleCounts[ch] = static_cast<size_t>(d.readInt32());
                }
            }

            void reset() override
            {
                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    std::fill(m_buffers[ch].begin(), m_buffers[ch].end(), 0.0f);
                    m_writeIndices[ch] = 0;
                    m_sampleCounts[ch] = 0;
                }
            }
        };

        // Type aliases for convenience
        using LinearRegressionSlope = LinearRegressionStage<SlopePolicy>;
        using LinearRegressionIntercept = LinearRegressionStage<InterceptPolicy>;
        using LinearRegressionResiduals = LinearRegressionStage<ResidualsPolicy>;
        using LinearRegressionPredictions = LinearRegressionStage<PredictionsPolicy>;

    } // namespace adapters
} // namespace dsp

#endif // DSP_LINEAR_REGRESSION_STAGE_H
