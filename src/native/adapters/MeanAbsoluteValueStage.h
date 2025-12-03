#pragma once

#include "../IDspStage.h"
#include "../core/MovingAbsoluteValueFilter.h" // Include the new core filter
#include "../utils/Toon.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string>
#include <algorithm>
#include <numeric> // For std::accumulate

namespace dsp::adapters
{
    enum class MavMode
    {
        Batch,
        Moving
    };

    class MeanAbsoluteValueStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new Mean Absolute Value Stage.
         * @param mode The MAV mode (Batch or Moving).
         * @param window_size The window size in samples (0 if using duration-based).
         * @param window_duration_ms The window duration in milliseconds (0 if using size-based).
         */
        explicit MeanAbsoluteValueStage(MavMode mode, size_t window_size = 0, double window_duration_ms = 0.0)
            : m_mode(mode),
              m_window_size(window_size),
              m_window_duration_ms(window_duration_ms),
              m_is_initialized(window_size > 0)
        {
            if (m_mode == MavMode::Moving && window_size == 0 && window_duration_ms == 0.0)
            {
                throw std::invalid_argument("MeanAbsoluteValue: either window size or window duration must be greater than 0 for 'moving' mode");
            }
        }

        // Return the type identifier for this stage
        const char *getType() const override
        {
            return "meanAbsoluteValue";
        }

        // Implementation of the interface method
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == MavMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // MavMode::Moving
            {
                processMoving(buffer, numSamples, numChannels, timestamps);
            }
        }

        // Serialize the stage's state
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == MavMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);

            if (m_mode == MavMode::Moving)
            {
                state.Set("windowSize", static_cast<uint32_t>(m_window_size));
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the filter's internal state
                    auto [bufferData, runningSumOfAbs] = m_filters[i].getState();

                    // Convert buffer data to JavaScript array
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t j = 0; j < bufferData.size(); ++j)
                    {
                        bufferArray.Set(j, Napi::Number::New(env, bufferData[j]));
                    }

                    channelState.Set("buffer", bufferArray);
                    channelState.Set("runningSum", Napi::Number::New(env, runningSumOfAbs));

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
            MavMode newMode = (modeStr == "moving") ? MavMode::Moving : MavMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("MeanAbsoluteValue mode mismatch during deserialization");
            }

            if (m_mode == MavMode::Moving)
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
                    m_filters.emplace_back(m_window_size);
                }

                // Restore each channel's state
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

                    // Get running sum (of absolute values)
                    float runningSum = channelState.Get("runningSum").As<Napi::Number>().FloatValue();

                    // --- Validation ---
                    // We must re-calculate the sum of absolute values from the original buffer data
                    float actualSumOfAbs = 0.0f;
                    for (const auto &val : bufferData)
                    {
                        actualSumOfAbs += std::abs(val);
                    }

                    const float tolerance = 0.0001f * std::max(1.0f, std::abs(actualSumOfAbs));
                    if (std::abs(runningSum - actualSumOfAbs) > tolerance)
                    {
                        throw std::runtime_error(
                            "Running sum of absolute values validation failed: expected " +
                            std::to_string(actualSumOfAbs) + " but got " +
                            std::to_string(runningSum));
                    }
                    // --- End Validation ---

                    // Restore the filter's state
                    m_filters[i].setState(bufferData, runningSum);
                }
            }
        }

        inline void serializeToon(dsp::toon::Serializer &s) const override
        {
            // Write mode
            s.writeInt32(static_cast<int32_t>(m_mode));

            // Write configuration parameters
            s.writeInt32(static_cast<int32_t>(m_window_size));
            s.writeFloat(static_cast<float>(m_window_duration_ms));
            s.writeBool(m_is_initialized);

            // Write number of channels and filter states
            s.writeInt32(static_cast<int32_t>(m_filters.size()));

            // For Moving mode, serialize each filter's state
            if (m_mode == MavMode::Moving)
            {
                for (const auto &filter : m_filters)
                {
                    auto [bufferData, runningSum] = filter.getState();
                    s.writeFloatArray(bufferData);
                    s.writeFloat(runningSum);
                }
            }
        }

        inline void deserializeToon(dsp::toon::Deserializer &d) override
        {
            // Read mode
            int32_t mode_int = d.readInt32();
            if (mode_int < 0 || mode_int > 1)
                throw std::runtime_error("Invalid mode in MeanAbsoluteValueStage deserialization");
            m_mode = static_cast<MavMode>(mode_int);

            // Read configuration parameters
            int32_t window_size = d.readInt32();
            if (window_size < 0)
                throw std::runtime_error("Invalid window_size in MeanAbsoluteValueStage deserialization");
            m_window_size = static_cast<size_t>(window_size);

            m_window_duration_ms = static_cast<double>(d.readFloat());
            m_is_initialized = d.readBool();

            // Read number of channels
            int32_t num_channels = d.readInt32();
            if (num_channels < 0)
                throw std::runtime_error("Invalid num_channels in MeanAbsoluteValueStage deserialization");

            // Reconstruct filters
            m_filters.clear();

            if (m_mode == MavMode::Moving)
            {
                for (int32_t i = 0; i < num_channels; ++i)
                {
                    // Read buffer data
                    std::vector<float> bufferData = d.readFloatArray();

                    // Read running sum
                    float runningSum = d.readFloat();

                    // Create filter and restore state
                    if (m_window_duration_ms > 0.0)
                    {
                        m_filters.emplace_back(m_window_size, m_window_duration_ms);
                    }
                    else
                    {
                        m_filters.emplace_back(m_window_size);
                    }
                    m_filters.back().setState(bufferData, runningSum);
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

    private:
        /**
         * @brief Statelessly calculates the MAV for each channel
         * and overwrites all samples in that channel with the result.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                size_t numSamplesPerChannel = numSamples / numChannels;
                if (numSamplesPerChannel == 0)
                    continue;

                double sum_abs = 0.0;

                // First pass: Calculate sum of absolute values
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    sum_abs += static_cast<double>(std::abs(buffer[i]));
                }

                // Calculate MAV
                float mav = static_cast<float>(sum_abs / numSamplesPerChannel);

                // Second pass: Fill this channel's buffer with the single MAV value
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    buffer[i] = mav;
                }
            }
        }

        /**
         * @brief Statefully processes samples using the moving MAV filters.
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
                    throw std::runtime_error("MeanAbsoluteValue: windowDuration was set, but timestamps are not available to derive sample rate");
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
                        m_filters.emplace_back(m_window_size, m_window_duration_ms);
                    }
                    else
                    {
                        // Create regular filter
                        m_filters.emplace_back(m_window_size);
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

        MavMode m_mode;
        size_t m_window_size;
        double m_window_duration_ms;
        bool m_is_initialized;
        // We need a separate filter instance for each channel's state
        std::vector<dsp::core::MovingAbsoluteValueFilter<float>> m_filters;
    };

} // namespace dsp::adapters