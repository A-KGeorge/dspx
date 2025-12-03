#pragma once

#include "../IDspStage.h"
#include "../core/RmsFilter.h"
#include "../utils/SimdOps.h"
#include "../utils/Toon.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string>
#include <algorithm>
#include <numeric>  // For std::accumulate
#include <iostream> // For optional debug logging

namespace dsp::adapters
{
    enum class RmsMode
    {
        Batch,
        Moving
    };

    class RmsStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new RMS Stage.
         * @param mode The RMS mode (Batch or Moving).
         * @param window_size The window size in samples (0 if using duration-based).
         * @param window_duration_ms The window duration in milliseconds (0 if using size-based).
         */
        explicit RmsStage(RmsMode mode, size_t window_size = 0, double window_duration_ms = 0.0)
            : m_mode(mode),
              m_window_size(window_size),
              m_window_duration_ms(window_duration_ms),
              m_is_initialized(window_size > 0)
        {
            if (m_mode == RmsMode::Moving && window_size == 0 && window_duration_ms == 0.0)
            {
                throw std::invalid_argument("RMS: either window size or window duration must be greater than 0 for 'moving' mode");
            }
        }

        // Return the type identifier for this stage
        const char *getType() const override
        {
            return "rms";
        }

        // Implementation of the interface method
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == RmsMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // RmsMode::Moving
            {
                processMoving(buffer, numSamples, numChannels, timestamps);
            }
        }

        // Serialize the stage's state
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == RmsMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);

            if (m_mode == RmsMode::Moving)
            {
                state.Set("windowSize", static_cast<uint32_t>(m_window_size));
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the filter's internal state
                    auto [bufferData, runningSumOfSquares] = m_filters[i].getState();

                    // Convert buffer data to JavaScript array
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t j = 0; j < bufferData.size(); ++j)
                    {
                        bufferArray.Set(j, Napi::Number::New(env, bufferData[j]));
                    }

                    channelState.Set("buffer", bufferArray);
                    // Store the running sum of squares
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
            RmsMode newMode = (modeStr == "moving") ? RmsMode::Moving : RmsMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("RMS mode mismatch during deserialization");
            }

            if (m_mode == RmsMode::Moving)
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

                    // Get running sum of squares
                    float runningSumOfSquares = channelState.Get("runningSumOfSquares").As<Napi::Number>().FloatValue();

                    // Validate runningSumOfSquares matches buffer contents
                    float actualSumOfSquares = 0.0f;
                    for (const auto &val : bufferData)
                    {
                        actualSumOfSquares += val * val;
                    }
                    const float tolerance = 0.001f * std::max(1.0f, std::abs(actualSumOfSquares));
                    if (std::abs(runningSumOfSquares - actualSumOfSquares) > tolerance)
                    {
                        throw std::runtime_error(
                            "Running sum of squares validation failed: expected " +
                            std::to_string(actualSumOfSquares) + " but got " +
                            std::to_string(runningSumOfSquares));
                    }

                    // Restore the filter's state
                    m_filters[i].setState(bufferData, runningSumOfSquares);
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
            // Serialize mode
            s.writeString(m_mode == RmsMode::Moving ? "moving" : "batch");

            if (m_mode == RmsMode::Moving)
            {
                // Serialize window configuration
                s.writeInt32(static_cast<int32_t>(m_window_size));
                s.writeDouble(m_window_duration_ms);
                s.writeBool(m_is_initialized);

                // Serialize filter states
                s.writeInt32(static_cast<int32_t>(m_filters.size()));
                for (const auto &filter : m_filters)
                {
                    auto [bufferData, runningSumOfSquares] = filter.getState();
                    s.writeFloatArray(bufferData); // CRITICAL: Direct binary copy
                    s.writeFloat(runningSumOfSquares);
                }
            }
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            // Deserialize and validate mode
            std::string modeStr = d.readString();
            RmsMode newMode = (modeStr == "moving") ? RmsMode::Moving : RmsMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("RMS TOON load: mode mismatch");
            }

            if (m_mode == RmsMode::Moving)
            {
                // Deserialize window configuration
                int32_t windowSize = d.readInt32();
                if (windowSize != static_cast<int32_t>(m_window_size))
                {
                    throw std::runtime_error("RMS TOON load: window size mismatch");
                }

                m_window_duration_ms = d.readDouble(); // restore duration
                m_is_initialized = d.readBool();       // restore init flag

                // Deserialize filter states
                int32_t numChannels = d.readInt32();
                m_filters.clear();
                for (int32_t i = 0; i < numChannels; ++i)
                {
                    // Create filter with correct window size (time-aware detection happens at runtime)
                    // preserve time-aware vs size-based based on restored flags
                    if (m_window_duration_ms > 0.0)
                    {
                        m_filters.emplace_back(m_window_size, m_window_duration_ms);
                    }
                    else
                    {
                        m_filters.emplace_back(m_window_size);
                    }

                    // Restore state
                    std::vector<float> bufferData = d.readFloatArray();
                    float runningSumOfSquares = d.readFloat();

                    m_filters[i].setState(bufferData, runningSumOfSquares);
                }

// Debug: emit simple checksum to verify restored state
#ifdef _WIN32
                if (std::getenv("DSPX_DEBUG_TOON") != nullptr)
                {
                    double sum0 = 0.0;
                    if (!m_filters.empty())
                    {
                        auto st = m_filters[0].getState();
                        for (float v : st.first)
                            sum0 += v;
                    }
                    std::cout << "[TOON] RMS restored: channels=" << numChannels
                              << ", win=" << m_window_size
                              << ", sum(ch0)=" << sum0 << std::endl;
                }
#endif
            }
        }

    private:
        /**
         * @brief Statelessly calculates the RMS for each channel
         * and overwrites all samples in that channel with the result.
         * Uses SIMD-optimized sum of squares for better performance.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                size_t numSamplesPerChannel = numSamples / numChannels;
                if (numSamplesPerChannel == 0)
                    continue;

                double sum_sq = 0.0;

                // For single-channel, use SIMD-optimized sum of squares
                if (numChannels == 1)
                {
                    sum_sq = dsp::simd::sum_of_squares(buffer, numSamples);
                }
                else
                {
                    // Multi-channel: strided access
                    for (size_t i = c; i < numSamples; i += numChannels)
                    {
                        double val = static_cast<double>(buffer[i]);
                        sum_sq += val * val;
                    }
                }

                // Calculate mean of squares and RMS
                double mean_sq = sum_sq / numSamplesPerChannel;
                float rms = static_cast<float>(std::sqrt(std::max(0.0, mean_sq)));

                // Fill this channel's buffer with the RMS value
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    buffer[i] = rms;
                }
            }
        }

        /**
         * @brief Statefully processes samples using the moving RMS filters.
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
                    throw std::runtime_error("RMS: windowDuration was set, but timestamps are not available to derive sample rate");
                }
            }

            // Lazily initialize filters, one for each channel
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

        RmsMode m_mode;
        size_t m_window_size;
        double m_window_duration_ms;
        bool m_is_initialized;
        // A separate RMS filter instance for each channel
        std::vector<dsp::core::RmsFilter<float>> m_filters;
    };

} // namespace dsp::adapters