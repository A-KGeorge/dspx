#pragma once

#include "../IDspStage.h"
#include "../core/MovingAverageFilter.h"
#include "../utils/SimdOps.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string>
#include <algorithm>
#include <numeric> // For std::accumulate

namespace dsp::adapters
{
    enum class AverageMode
    {
        Batch,
        Moving
    };

    class MovingAverageStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new Moving Average Stage.
         * @param mode The averaging mode (Batch or Moving).
         * @param window_size The window size in samples (0 if using duration-based).
         * @param window_duration_ms The window duration in milliseconds (0 if using size-based).
         */
        explicit MovingAverageStage(AverageMode mode, size_t window_size = 0, double window_duration_ms = 0.0)
            : m_mode(mode),
              m_window_size(window_size),
              m_window_duration_ms(window_duration_ms),
              m_is_initialized(window_size > 0) // Initialized if windowSize was provided
        {
            if (m_mode == AverageMode::Moving && window_size == 0 && window_duration_ms == 0.0)
            {
                throw std::invalid_argument("MovingAverage: either window size or window duration must be greater than 0 for 'moving' mode");
            }
        }

        // Return the type identifier for this stage
        const char *getType() const override
        {
            return "movingAverage";
        }

        // This is the implementation of the interface method
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == AverageMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // AverageMode::Moving
            {
                processMoving(buffer, numSamples, numChannels, timestamps);
            }
        }

        // Serialize the stage's state to a Napi::Object
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == AverageMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);

            if (m_mode == AverageMode::Moving)
            {
                state.Set("windowSize", static_cast<uint32_t>(m_window_size));
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the filter's internal state
                    auto [bufferData, runningSum] = m_filters[i].getState();

                    // Convert buffer data to JavaScript array
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t j = 0; j < bufferData.size(); ++j)
                    {
                        bufferArray.Set(j, Napi::Number::New(env, bufferData[j]));
                    }

                    channelState.Set("buffer", bufferArray);
                    channelState.Set("runningSum", Napi::Number::New(env, runningSum));

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
            AverageMode newMode = (modeStr == "moving") ? AverageMode::Moving : AverageMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("MovingAverage mode mismatch during deserialization");
            }

            if (m_mode == AverageMode::Moving)
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

                    // Get running sum
                    float runningSum = channelState.Get("runningSum").As<Napi::Number>().FloatValue();

                    // Validate that runningSum matches the actual sum of buffer values
                    float actualSum = 0.0f;
                    for (const auto &val : bufferData)
                    {
                        actualSum += val;
                    }

                    // Allow small floating-point tolerance
                    const float tolerance = 0.0001f * std::max(1.0f, std::abs(actualSum));
                    if (std::abs(runningSum - actualSum) > tolerance)
                    {
                        throw std::runtime_error(
                            "Running sum validation failed: expected " +
                            std::to_string(actualSum) + " but got " +
                            std::to_string(runningSum));
                    }

                    // Restore the filter's state
                    m_filters[i].setState(bufferData, runningSum);
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

        // TOON Binary Serialization - CRITICAL OPTIMIZATION for large buffers
        void serializeToon(dsp::toon::Serializer &serializer) const override
        {
            serializer.startObject();

            // 1. Mode
            serializer.writeString("mode");
            serializer.writeString((m_mode == AverageMode::Moving) ? "moving" : "batch");

            if (m_mode == AverageMode::Moving)
            {
                // 2. Config
                serializer.writeString("windowSize");
                serializer.writeInt32(static_cast<int32_t>(m_window_size));

                serializer.writeString("windowDuration");
                serializer.writeDouble(m_window_duration_ms);

                serializer.writeString("initialized");
                serializer.writeBool(m_is_initialized);

                // 3. Channels - Zero-copy-like binary arrays
                serializer.writeString("channels");
                serializer.startArray();

                for (const auto &filter : m_filters)
                {
                    auto [bufferData, runningSum] = filter.getState();

                    serializer.startObject();

                    // CRITICAL: Direct binary copy of float buffer (not text!)
                    serializer.writeString("buffer");
                    serializer.writeFloatArray(bufferData);

                    serializer.writeString("sum");
                    serializer.writeFloat(runningSum);

                    serializer.endObject();
                }
                serializer.endArray();
            }

            serializer.endObject();
        }

        // TOON Binary Deserialization - CRITICAL OPTIMIZATION for large buffers
        void deserializeToon(dsp::toon::Deserializer &deserializer) override
        {
            deserializer.consumeToken(dsp::toon::T_OBJECT_START);

            // 1. Mode
            std::string key = deserializer.readString(); // "mode"
            std::string modeStr = deserializer.readString();

            AverageMode newMode = (modeStr == "moving") ? AverageMode::Moving : AverageMode::Batch;
            if (newMode != m_mode)
            {
                throw std::runtime_error("MovingAverage mode mismatch during TOON deserialization");
            }

            if (modeStr == "moving")
            {
                // 2. Config
                key = deserializer.readString(); // "windowSize"
                int32_t windowSize = deserializer.readInt32();

                key = deserializer.readString(); // "windowDuration"
                double windowDuration = deserializer.readDouble();

                key = deserializer.readString(); // "initialized"
                bool initialized = deserializer.readBool();

                // Validate window size
                if (windowSize != static_cast<int32_t>(m_window_size))
                {
                    throw std::runtime_error("MovingAverage window size mismatch during TOON deserialization");
                }

                m_window_duration_ms = windowDuration;
                m_is_initialized = initialized;

                // 3. Channels
                key = deserializer.readString(); // "channels"
                deserializer.consumeToken(dsp::toon::T_ARRAY_START);

                m_filters.clear();

                while (deserializer.peekToken() != dsp::toon::T_ARRAY_END)
                {
                    deserializer.consumeToken(dsp::toon::T_OBJECT_START);

                    // Buffer - CRITICAL: Direct binary memcpy (not parsing text!)
                    deserializer.readString(); // "buffer"
                    std::vector<float> bufferData = deserializer.readFloatArray();

                    // Sum
                    deserializer.readString(); // "sum"
                    float runningSum = deserializer.readFloat();

                    // Reconstruct filter with time-aware support
                    if (m_window_duration_ms > 0.0)
                    {
                        m_filters.emplace_back(m_window_size, m_window_duration_ms);
                    }
                    else
                    {
                        m_filters.emplace_back(m_window_size);
                    }
                    m_filters.back().setState(bufferData, runningSum);

                    deserializer.consumeToken(dsp::toon::T_OBJECT_END);
                }
                deserializer.consumeToken(dsp::toon::T_ARRAY_END);
            }

            deserializer.consumeToken(dsp::toon::T_OBJECT_END);
        }

    private:
        /**
         * @brief Statelessly calculates the average for each channel
         * and overwrites all samples in that channel with the result.
         * Uses SIMD-optimized summation for better performance.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                size_t numSamplesPerChannel = numSamples / numChannels;
                if (numSamplesPerChannel == 0)
                    continue;

                double sum = 0.0;

                // For single-channel or well-aligned data, use SIMD sum
                if (numChannels == 1)
                {
                    // Fast path: contiguous memory
                    sum = dsp::simd::sum(buffer, numSamples);
                }
                else
                {
                    // Multi-channel: strided access (compiler can still auto-vectorize)
                    for (size_t i = c; i < numSamples; i += numChannels)
                    {
                        sum += static_cast<double>(buffer[i]);
                    }
                }

                // Calculate average
                float average = static_cast<float>(sum / numSamplesPerChannel);

                // Fill this channel's buffer with the single average value
                // For single channel, memset equivalent is very fast
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    buffer[i] = average;
                }
            }
        }

        /**
         * @brief Statefully processes samples using the moving average filters.
         * @param buffer The interleaved audio buffer.
         * @param numSamples The total number of samples.
         * @param numChannels The number of channels.
         * @param timestamps Optional timestamps for deriving sample rate on first call.
         */
        void processMoving(float *buffer, size_t numSamples, int numChannels, const float *timestamps)
        {
            // Determine if we're in time-aware mode (pure duration without size, or both)
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
                    double estimated_sample_rate = 1000.0 / avg_sample_period_ms; // Hz

                    // Calculate window size: (duration_ms / 1000) * sample_rate
                    // For time-aware mode, use 3x the estimated size to ensure we never
                    // run out of buffer space before time-based expiration happens
                    size_t estimated_size = static_cast<size_t>((m_window_duration_ms / 1000.0) * estimated_sample_rate);
                    m_window_size = std::max(size_t(1), estimated_size * 3);

                    m_is_initialized = true;
                }
                else
                {
                    throw std::runtime_error("MovingAverage: windowDuration was set, but timestamps are not available to derive sample rate");
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

        AverageMode m_mode;
        size_t m_window_size;
        double m_window_duration_ms;
        bool m_is_initialized;
        // We need a separate filter instance for each channel's state
        std::vector<dsp::core::MovingAverageFilter<float>> m_filters;
    };

} // namespace dsp::adapters