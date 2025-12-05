#pragma once

#include "../IDspStage.h"
#include "../core/MovingZScoreFilter.h"
#include "../utils/Toon.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include <numeric>   // For std::accumulate
#include <algorithm> // For std::max

namespace dsp::adapters
{
    enum class ZScoreNormalizeMode
    {
        Batch,
        Moving
    };

    class ZScoreNormalizeStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new Z-Score Normalize Stage.
         * @param mode The variance mode (Batch or Moving).
         * @param window_size The window size in samples (0 if using duration-based).
         * @param window_duration_ms The window duration in milliseconds (0 if using size-based).
         * @param epsilon A small value to prevent division by zero (default 1e-6).
         */
        explicit ZScoreNormalizeStage(ZScoreNormalizeMode mode, size_t window_size = 0, double window_duration_ms = 0.0, float epsilon = 1e-6f)
            : m_mode(mode),
              m_window_size(window_size),
              m_window_duration_ms(window_duration_ms),
              m_epsilon(epsilon),
              m_is_initialized(window_size > 0)
        {
            if (m_mode == ZScoreNormalizeMode::Moving && window_size == 0 && window_duration_ms == 0.0)
            {
                throw std::invalid_argument("ZScoreNormalize: either window size or window duration must be greater than 0 for 'moving' mode");
            }
        }

        // Return the type identifier for this stage
        const char *getType() const override
        {
            return "zScoreNormalize";
        }

        // Implementation of the interface method
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == ZScoreNormalizeMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // ZScoreNormalizeMode::Moving
            {
                processMoving(buffer, numSamples, numChannels, timestamps);
            }
        }

        // Serialize the stage's state
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == ZScoreNormalizeMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);
            state.Set("epsilon", Napi::Number::New(env, m_epsilon));

            if (m_mode == ZScoreNormalizeMode::Moving)
            {
                state.Set("windowSize", static_cast<uint32_t>(m_window_size));
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the filter's internal state
                    auto [bufferData, sums] = m_filters[i].getState();
                    auto [runningSum, runningSumOfSquares] = sums;

                    // Convert buffer data to JavaScript array
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t j = 0; j < bufferData.size(); ++j)
                    {
                        bufferArray.Set(j, Napi::Number::New(env, bufferData[j]));
                    }

                    channelState.Set("buffer", bufferArray);
                    channelState.Set("runningSum", Napi::Number::New(env, runningSum));
                    channelState.Set("runningSumOfSquares", Napi::Number::New(env, runningSumOfSquares));

                    channelsArray.Set(static_cast<uint32_t>(i), channelState);
                }
                state.Set("channels", channelsArray);
            }

            return state;
        }

        // Deserialize and restore the stage's state
        void deserializeState(const Napi::Object &state) override
        {
            std::string modeStr = state.Get("mode").As<Napi::String>().Utf8Value();
            ZScoreNormalizeMode newMode = (modeStr == "moving") ? ZScoreNormalizeMode::Moving : ZScoreNormalizeMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("ZScoreNormalize mode mismatch during deserialization");
            }

            m_epsilon = state.Get("epsilon").As<Napi::Number>().FloatValue();

            if (m_mode == ZScoreNormalizeMode::Moving)
            {
                // Get window size and validate
                size_t windowSize = state.Get("windowSize").As<Napi::Number>().Uint32Value();
                if (windowSize != m_window_size)
                {
                    throw std::runtime_error("Window size mismatch during deserialization");
                }

                // Get number of channels
                uint32_t numChannels = state.Get("channels").As<Napi::Array>().Length();

                // Recreate filters
                m_filters.clear();
                for (uint32_t i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back(m_window_size, m_epsilon);
                }

                // Restore each channel's state (identical logic to VarianceStage)
                Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
                for (uint32_t i = 0; i < numChannels; ++i)
                {
                    Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                    // Get buffer data
                    Napi::Array bufferArray = channelState.Get("buffer").As<Napi::Array>();
                    std::vector<float> bufferData;
                    bufferData.reserve(bufferArray.Length());
                    for (uint32_t j = 0; j < bufferArray.Length(); ++j)
                    {
                        bufferData.push_back(bufferArray.Get(j).As<Napi::Number>().FloatValue());
                    }

                    // Get running sums
                    float runningSum = channelState.Get("runningSum").As<Napi::Number>().FloatValue();
                    float runningSumOfSquares = channelState.Get("runningSumOfSquares").As<Napi::Number>().FloatValue();

                    // --- Validation (identical to VarianceStage) ---
                    float actualSum = 0.0f;
                    float actualSumOfSquares = 0.0f;
                    for (const auto &val : bufferData)
                    {
                        actualSum += val;
                        actualSumOfSquares += val * val;
                    }

                    const float toleranceSum = 0.0001f * std::max(1.0f, std::abs(actualSum));
                    if (std::abs(runningSum - actualSum) > toleranceSum)
                    {
                        throw std::runtime_error(
                            "Running sum validation failed: expected " +
                            std::to_string(actualSum) + " but got " +
                            std::to_string(runningSum));
                    }

                    const float toleranceSq = 0.001f * std::max(1.0f, std::abs(actualSumOfSquares));
                    if (std::abs(runningSumOfSquares - actualSumOfSquares) > toleranceSq)
                    {
                        throw std::runtime_error(
                            "Running sum of squares validation failed: expected " +
                            std::to_string(actualSumOfSquares) + " but got " +
                            std::to_string(runningSumOfSquares));
                    }
                    // --- End Validation ---

                    // Restore the filter's state
                    m_filters[i].setState(bufferData, runningSum, runningSumOfSquares);
                }
            }
        }

        // Reset all filters to initial state
        void reset() override
        {
            for (auto &filter : m_filters)
            {
                filter.clear();
            }
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.writeString(m_mode == ZScoreNormalizeMode::Moving ? "moving" : "batch");

            if (m_mode == ZScoreNormalizeMode::Moving)
            {
                s.writeInt32(static_cast<int32_t>(m_window_size));
                s.writeDouble(m_window_duration_ms);
                s.writeBool(m_is_initialized);
                s.writeFloat(m_epsilon);

                s.writeInt32(static_cast<int32_t>(m_filters.size()));
                for (const auto &filter : m_filters)
                {
                    auto [bufferData, sums] = filter.getState();
                    auto [runningSum, runningSumOfSquares] = sums;
                    s.writeFloatArray(bufferData);
                    s.writeFloat(runningSum);
                    s.writeFloat(runningSumOfSquares);
                    s.writeFloat(m_epsilon); // Store epsilon explicitly
                }
            }
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            std::string modeStr = d.readString();
            ZScoreNormalizeMode newMode = (modeStr == "moving") ? ZScoreNormalizeMode::Moving : ZScoreNormalizeMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("ZScore TOON load: mode mismatch");
            }

            if (m_mode == ZScoreNormalizeMode::Moving)
            {
                int32_t windowSize = d.readInt32();
                if (windowSize != static_cast<int32_t>(m_window_size))
                {
                    throw std::runtime_error("ZScore TOON load: window size mismatch");
                }

                m_window_duration_ms = d.readDouble(); // restore duration
                m_is_initialized = d.readBool();       // restore init flag
                m_epsilon = d.readFloat();             // restore epsilon

                int32_t numChannels = d.readInt32();
                m_filters.clear();
                for (int32_t i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back(m_window_size, m_epsilon);

                    std::vector<float> bufferData = d.readFloatArray();
                    float runningSum = d.readFloat();
                    float runningSumOfSquares = d.readFloat();
                    d.readFloat(); // epsilon (already restored)

                    // setState takes (buffer, sum, sumOfSquares) - 3 separate params
                    m_filters[i].setState(bufferData, runningSum, runningSumOfSquares);
                }
            }
        }

    private:
        /**
         * @brief Statelessly calculates the Z-Score for each sample
         * based on the entire buffer's stats for that channel.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                size_t numSamplesPerChannel = numSamples / numChannels;
                if (numSamplesPerChannel == 0)
                    continue;

                double sum = 0.0;
                double sum_sq = 0.0;

                // First pass: Calculate sums
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    double val = static_cast<double>(buffer[i]);
                    sum += val;
                    sum_sq += val * val;
                }

                // Calculate mean and variance
                double mean = sum / numSamplesPerChannel;
                double mean_sq = sum_sq / numSamplesPerChannel;
                double variance = std::max(0.0, mean_sq - (mean * mean));
                float stddev = static_cast<float>(std::sqrt(variance));

                // Second pass: Apply Z-Score normalization in-place
                if (stddev < m_epsilon)
                {
                    // StdDev is zero, all values are the mean, Z-Score is 0
                    for (size_t i = c; i < numSamples; i += numChannels)
                    {
                        buffer[i] = 0.0f;
                    }
                }
                else
                {
                    float mean_f = static_cast<float>(mean);
                    for (size_t i = c; i < numSamples; i += numChannels)
                    {
                        buffer[i] = (buffer[i] - mean_f) / stddev;
                    }
                }
            }
        }

        /**
         * @brief Statefully processes samples using the moving z-score filters.
         */
        void processMoving(float *buffer, size_t numSamples, int numChannels, const float *timestamps)
        {
            // Determine if we're in time-aware mode
            bool useTimeAware = (m_window_duration_ms > 0.0) && timestamps != nullptr;

            // Lazy initialization: convert windowDuration to windowSize if needed
            if (!m_is_initialized && m_window_duration_ms > 0.0)
            {
                if (timestamps != nullptr && numSamples > 1)
                {
                    // Estimate sample rate from timestamps
                    size_t samples_to_check = std::min(numSamples, size_t(10));
                    double total_time_ms = timestamps[samples_to_check - 1] - timestamps[0];
                    double avg_sample_period_ms = total_time_ms / (samples_to_check - 1);
                    double estimated_sample_rate = 1000.0 / avg_sample_period_ms;

                    // Use 3x the estimated size for time-aware mode
                    size_t estimated_size = static_cast<size_t>((m_window_duration_ms / 1000.0) * estimated_sample_rate);
                    m_window_size = std::max(size_t(1), estimated_size * 3);

                    m_is_initialized = true;
                }
                else
                {
                    throw std::runtime_error("ZScoreNormalize: windowDuration was set, but timestamps are not available to derive sample rate");
                }
            }

            // Lazily initialize our filters, one for each channel
            if (m_filters.size() != static_cast<size_t>(numChannels))
            {
                m_filters.clear();
                for (int i = 0; i < numChannels; ++i)
                {
                    if (useTimeAware)
                    {
                        // Create time-aware filter
                        m_filters.emplace_back(m_window_size, m_window_duration_ms, m_epsilon);
                    }
                    else
                    {
                        // Create regular filter
                        m_filters.emplace_back(m_window_size, m_epsilon);
                    }
                }
            }

            // Process the buffer sample by sample, de-interleaving
            for (size_t i = 0; i < numSamples; ++i)
            {
                int channel = i % numChannels;
                size_t sample_index = i / numChannels;

                if (useTimeAware)
                {
                    // Use time-aware processing
                    buffer[i] = m_filters[channel].addSampleWithTimestamp(buffer[i], timestamps[sample_index]);
                }
                else
                {
                    // Use sample-count processing
                    buffer[i] = m_filters[channel].addSample(buffer[i]);
                }
            }
        }

        ZScoreNormalizeMode m_mode;
        size_t m_window_size;
        double m_window_duration_ms;
        float m_epsilon;
        bool m_is_initialized;
        // We need a separate filter instance for each channel's state
        std::vector<dsp::core::MovingZScoreFilter<float>> m_filters;
    };

} // namespace dsp::adapters