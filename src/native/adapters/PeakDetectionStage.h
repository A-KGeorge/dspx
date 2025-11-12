#pragma once

#include "../IDspStage.h"
#include "../core/Policies.h"
#include "../utils/SlidingWindowFilter.h"
#include <vector>
#include <stdexcept>
#include <string>

namespace dsp::adapters
{
    /**
     * @brief Peak Detection Stage - Unified time/frequency domain implementation.
     *
     * **Time Domain:**
     * - Uses SlidingWindowFilter with PeakDetectionPolicy (window size = 3)
     * - Batch mode: Process entire buffer independently
     * - Moving mode: Maintain state across process() calls
     *
     * **Frequency Domain:**
     * - Uses FrequencyPeakPolicy for batch spectral peak detection
     * - Always stateless (batch mode only)
     *
     * **Output:**
     * - Time domain: 1.0 at peak locations, 0.0 elsewhere
     * - Frequency domain: 1.0 at peak bins, 0.0 elsewhere
     */
    class PeakDetectionStage : public IDspStage
    {
    public:
        enum class Mode
        {
            Batch,
            Moving
        };

        enum class Domain
        {
            Time,
            Frequency
        };

        explicit PeakDetectionStage(
            float threshold,
            Mode mode = Mode::Moving,
            Domain domain = Domain::Time,
            size_t minPeakDistance = 1)
            : m_threshold(threshold),
              m_mode(mode),
              m_domain(domain),
              m_minPeakDistance(minPeakDistance),
              m_num_channels(0)
        {
            if (threshold < 0.0f)
            {
                throw std::invalid_argument("PeakDetection: threshold must be >= 0");
            }
        }

        const char *getType() const override
        {
            return "peakDetection";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Initialize filters on first call or channel count change
            if (m_num_channels != numChannels || m_time_filters.empty())
            {
                m_num_channels = numChannels;

                if (m_domain == Domain::Time)
                {
                    // Create sliding window filters (window size = 3 for time domain)
                    m_time_filters.clear();
                    for (int i = 0; i < numChannels; ++i)
                    {
                        m_time_filters.emplace_back(
                            3, // Window size for three-point comparison
                            dsp::core::PeakDetectionPolicy<float>(m_threshold));
                    }
                }
                else
                {
                    // Frequency domain: Initialize frequency peak policy
                    m_freq_policy = dsp::core::FrequencyPeakPolicy<float>(m_threshold, m_minPeakDistance);
                }
            }

            size_t samplesPerChannel = numSamples / numChannels;

            if (m_domain == Domain::Time)
            {
                processTimeDomain(buffer, samplesPerChannel, numChannels);
            }
            else
            {
                processFrequencyDomain(buffer, samplesPerChannel, numChannels);
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("threshold", m_threshold);
            state.Set("mode", m_mode == Mode::Batch ? "batch" : "moving");
            state.Set("domain", m_domain == Domain::Time ? "time" : "frequency");
            state.Set("minPeakDistance", static_cast<uint32_t>(m_minPeakDistance));
            state.Set("numChannels", m_num_channels);

            // Serialize per-channel state (time domain moving mode only)
            if (m_domain == Domain::Time && m_mode == Mode::Moving && !m_time_filters.empty())
            {
                Napi::Array channelsArray = Napi::Array::New(env, m_time_filters.size());

                for (size_t i = 0; i < m_time_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get buffer contents
                    auto bufferData = m_time_filters[i].getBufferContents();
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t j = 0; j < bufferData.size(); ++j)
                    {
                        bufferArray.Set(static_cast<uint32_t>(j), bufferData[j]);
                    }
                    channelState.Set("buffer", bufferArray);

                    // Get policy state (threshold)
                    channelState.Set("threshold", m_time_filters[i].getPolicy().getThreshold());

                    channelsArray.Set(static_cast<uint32_t>(i), channelState);
                }

                state.Set("channels", channelsArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("threshold"))
                m_threshold = state.Get("threshold").As<Napi::Number>().FloatValue();
            if (state.Has("minPeakDistance"))
                m_minPeakDistance = state.Get("minPeakDistance").As<Napi::Number>().Uint32Value();
            if (state.Has("numChannels"))
                m_num_channels = state.Get("numChannels").As<Napi::Number>().Int32Value();

            if (state.Has("mode"))
            {
                std::string modeStr = state.Get("mode").As<Napi::String>().Utf8Value();
                m_mode = (modeStr == "batch") ? Mode::Batch : Mode::Moving;
            }

            if (state.Has("domain"))
            {
                std::string domainStr = state.Get("domain").As<Napi::String>().Utf8Value();
                m_domain = (domainStr == "frequency") ? Domain::Frequency : Domain::Time;
            }

            // Restore per-channel state
            if (m_domain == Domain::Time && m_mode == Mode::Moving && state.Has("channels"))
            {
                Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
                m_time_filters.clear();

                for (size_t i = 0; i < channelsArray.Length(); ++i)
                {
                    Napi::Object channelState = channelsArray.Get(static_cast<uint32_t>(i)).As<Napi::Object>();

                    // Restore threshold
                    float threshold = channelState.Get("threshold").As<Napi::Number>().FloatValue();

                    // Create filter
                    dsp::utils::SlidingWindowFilter<float, dsp::core::PeakDetectionPolicy<float>> filter(
                        3,
                        dsp::core::PeakDetectionPolicy<float>(threshold));

                    // Restore buffer contents
                    if (channelState.Has("buffer"))
                    {
                        Napi::Array bufferArray = channelState.Get("buffer").As<Napi::Array>();
                        std::vector<float> bufferData;
                        for (uint32_t j = 0; j < bufferArray.Length(); ++j)
                        {
                            bufferData.push_back(bufferArray.Get(j).As<Napi::Number>().FloatValue());
                        }
                        filter.setBufferContents(bufferData);
                    }

                    m_time_filters.push_back(std::move(filter));
                }
            }
        }

        void reset() override
        {
            for (auto &filter : m_time_filters)
            {
                filter.clear();
            }
        }

        bool isResizing() const override { return false; }

    private:
        float m_threshold;
        Mode m_mode;
        Domain m_domain;
        size_t m_minPeakDistance;
        int m_num_channels;

        // Time domain: One sliding window filter per channel
        std::vector<dsp::utils::SlidingWindowFilter<float, dsp::core::PeakDetectionPolicy<float>>> m_time_filters;

        // Frequency domain: Single policy (stateless)
        dsp::core::FrequencyPeakPolicy<float> m_freq_policy{0.0f, 1};

        void processTimeDomain(float *buffer, size_t samplesPerChannel, int numChannels)
        {
            if (m_mode == Mode::Batch)
            {
                processTimeBatch(buffer, samplesPerChannel, numChannels);
            }
            else
            {
                processTimeMoving(buffer, samplesPerChannel, numChannels);
            }
        }

        void processTimeBatch(float *buffer, size_t samplesPerChannel, int numChannels)
        {
            // Stateless: Process entire buffer independently
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (samplesPerChannel < 3)
                {
                    // Not enough samples for peak detection
                    for (size_t i = 0; i < samplesPerChannel; ++i)
                    {
                        buffer[i * numChannels + ch] = 0.0f;
                    }
                    continue;
                }

                // Create temporary sliding window for batch processing
                dsp::utils::SlidingWindowFilter<float, dsp::core::PeakDetectionPolicy<float>> tempFilter(
                    3,
                    dsp::core::PeakDetectionPolicy<float>(m_threshold));

                // First two samples: fill the window (no output yet)
                tempFilter.addSample(buffer[0 * numChannels + ch]);
                buffer[0 * numChannels + ch] = 0.0f;

                tempFilter.addSample(buffer[1 * numChannels + ch]);
                buffer[1 * numChannels + ch] = 0.0f;

                // Process remaining samples
                for (size_t i = 2; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float sample = buffer[idx];

                    // Add sample and get peak detection result for the previous sample
                    float result = tempFilter.addSample(sample);

                    // Write result to previous position
                    buffer[(i - 1) * numChannels + ch] = result;
                }

                // Last sample can't be confirmed as peak
                buffer[(samplesPerChannel - 1) * numChannels + ch] = 0.0f;
            }
        }

        void processTimeMoving(float *buffer, size_t samplesPerChannel, int numChannels)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto &filter = m_time_filters[ch];

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float current = buffer[idx];

                    // Before adding current sample, check buffer state
                    size_t count_before = filter.getCount();

                    // If we have at least 2 samples already, we can check for a peak
                    // after adding the current sample (which gives us 3 total)
                    if (count_before >= 2)
                    {
                        // Get the last 2 samples before adding current
                        auto bufferData = filter.getBufferContents();
                        float prev_prev = bufferData[count_before - 2];
                        float prev = bufferData[count_before - 1];

                        // Now add the current sample
                        filter.addSample(current);

                        // Check if 'prev' is a peak: prev_prev < prev > current
                        bool is_peak = (prev > prev_prev) && (prev > current) && (prev >= m_threshold);

                        // Write result
                        if (i == 0)
                        {
                            // First sample of batch: the peak check is for a sample from previous batch
                            buffer[idx] = is_peak ? 1.0f : 0.0f;
                        }
                        else
                        {
                            // Not first sample: write to previous position
                            buffer[(i - 1) * numChannels + ch] = is_peak ? 1.0f : 0.0f;
                            // Current position will be evaluated later
                            buffer[idx] = 0.0f;
                        }
                    }
                    else
                    {
                        // Not enough history yet, just add the sample
                        filter.addSample(current);

                        if (i > 0)
                        {
                            buffer[(i - 1) * numChannels + ch] = 0.0f;
                        }
                        buffer[idx] = 0.0f;
                    }
                }

                // Last sample always gets 0 (unless it was the only sample and we wrote a result)
                if (samplesPerChannel > 1)
                {
                    buffer[(samplesPerChannel - 1) * numChannels + ch] = 0.0f;
                }
            }
        }

        void processFrequencyDomain(float *buffer, size_t samplesPerChannel, int numChannels)
        {
            // Frequency domain: buffer contains magnitude/power spectrum
            // Output: 1.0 at peak frequencies, 0.0 elsewhere

            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Extract channel data
                std::vector<float> channelData(samplesPerChannel);
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    channelData[i] = buffer[i * numChannels + ch];
                }

                // Detect peaks using frequency policy
                auto peakIndices = m_freq_policy.detectPeaks(channelData.data(), samplesPerChannel);

                // Zero the buffer first
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    buffer[i * numChannels + ch] = 0.0f;
                }

                // Mark peaks
                for (size_t peakIdx : peakIndices)
                {
                    buffer[peakIdx * numChannels + ch] = 1.0f;
                }
            }
        }
    };

} // namespace dsp::adapters