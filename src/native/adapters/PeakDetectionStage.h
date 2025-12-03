#pragma once

#include "../IDspStage.h"
#include "../core/PeakDetection.h"
#include "../utils/Toon.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace dsp::adapters
{
    /**
     * @brief Peak Detection Stage - Detects local maxima in a signal.
     *
     * ... (docs updated) ...
     *
     * **Parameters:**
     * - threshold: Minimum value for a peak.
     * - mode: 'moving' (stateful) or 'batch' (stateless).
     * - domain: 'time' or 'frequency'.
     * - windowSize: (Batch mode only) Local neighborhood size (odd, >= 3). Default 3.
     * - minPeakDistance: Minimum samples between peaks. Default 1.
     */
    class PeakDetectionStage : public IDspStage
    {
    public:
        explicit PeakDetectionStage(float threshold, std::string mode = "moving", std::string domain = "time", int windowSize = 3, int minPeakDistance = 1)
            : m_threshold(threshold),
              m_mode(std::move(mode)),
              m_domain(std::move(domain)),
              m_windowSize(windowSize),
              m_minPeakDistance(minPeakDistance),
              m_num_channels(0)
        {
            if (threshold < 0.0f)
            {
                throw std::invalid_argument("PeakDetection: threshold must be >= 0");
            }
            if (m_mode != "moving" && m_mode != "batch")
            {
                throw std::invalid_argument("PeakDetection: mode must be 'moving' or 'batch'");
            }
            if (m_domain != "time" && m_domain != "frequency")
            {
                throw std::invalid_argument("PeakDetection: domain must be 'time' or 'frequency'");
            }
            if (windowSize < 3 || windowSize % 2 == 0)
            {
                throw std::invalid_argument("PeakDetection: windowSize must be an odd number >= 3");
            }
            if (minPeakDistance < 1)
            {
                throw std::invalid_argument("PeakDetection: minPeakDistance must be >= 1");
            }

            // In moving mode, we only support the 3-point window
            if (m_mode == "moving")
            {
                m_windowSize = 3;
            }
        }

        const char *getType() const override
        {
            return "peakDetection";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == "moving")
            {
                // Note: processMoving implicitly uses m_windowSize = 3
                processMoving(buffer, numSamples, numChannels);
            }
            else // m_mode == "batch"
            {
                processBatch(buffer, numSamples, numChannels);
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("threshold", m_threshold);
            state.Set("numChannels", m_num_channels);
            state.Set("mode", m_mode);
            state.Set("domain", m_domain);
            state.Set("windowSize", m_windowSize);
            state.Set("minPeakDistance", m_minPeakDistance);

            if (m_mode == "moving" && !m_prev_sample.empty())
            {
                Napi::Array prevArray = Napi::Array::New(env, m_prev_sample.size());
                Napi::Array prevPrevArray = Napi::Array::New(env, m_prev_prev_sample.size());
                Napi::Array cooldownArray = Napi::Array::New(env, m_peakCooldown.size());

                for (size_t i = 0; i < m_prev_sample.size(); ++i)
                {
                    prevArray.Set(i, m_prev_sample[i]);
                    prevPrevArray.Set(i, m_prev_prev_sample[i]);
                    cooldownArray.Set(i, m_peakCooldown[i]);
                }

                state.Set("prevSample", prevArray);
                state.Set("prevPrevSample", prevPrevArray);
                state.Set("peakCooldown", cooldownArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("threshold"))
                m_threshold = state.Get("threshold").As<Napi::Number>().FloatValue();
            if (state.Has("numChannels"))
                m_num_channels = state.Get("numChannels").As<Napi::Number>().Int32Value();
            if (state.Has("mode"))
                m_mode = state.Get("mode").As<Napi::String>().Utf8Value();
            if (state.Has("domain"))
                m_domain = state.Get("domain").As<Napi::String>().Utf8Value();
            if (state.Has("windowSize"))
                m_windowSize = state.Get("windowSize").As<Napi::Number>().Int32Value();
            if (state.Has("minPeakDistance"))
                m_minPeakDistance = state.Get("minPeakDistance").As<Napi::Number>().Int32Value();

            if (m_mode == "moving" && state.Has("prevSample"))
            {
                Napi::Array prevArray = state.Get("prevSample").As<Napi::Array>();
                Napi::Array prevPrevArray = state.Get("prevPrevSample").As<Napi::Array>();
                Napi::Array cooldownArray = state.Get("peakCooldown").As<Napi::Array>();

                m_prev_sample.clear();
                m_prev_prev_sample.clear();
                m_peakCooldown.clear();

                for (size_t i = 0; i < prevArray.Length(); ++i)
                {
                    m_prev_sample.push_back(prevArray.Get(i).As<Napi::Number>().FloatValue());
                    m_prev_prev_sample.push_back(prevPrevArray.Get(i).As<Napi::Number>().FloatValue());
                    m_peakCooldown.push_back(cooldownArray.Get(i).As<Napi::Number>().Int32Value());
                }
            }
        }

        inline void serializeToon(dsp::toon::Serializer &s) const override
        {
            // Write configuration
            s.writeFloat(m_threshold);
            s.writeInt32(m_num_channels);
            s.writeString(m_mode);
            s.writeString(m_domain);
            s.writeInt32(m_windowSize);
            s.writeInt32(m_minPeakDistance);

            // Write state (only for moving mode)
            if (m_mode == "moving" && !m_prev_sample.empty())
            {
                s.writeFloatArray(m_prev_sample);
                s.writeFloatArray(m_prev_prev_sample);

                // Write peakCooldown as int array (convert to float array)
                std::vector<float> cooldown_float(m_peakCooldown.begin(), m_peakCooldown.end());
                s.writeFloatArray(cooldown_float);
            }
            else
            {
                // Write empty arrays
                s.writeFloatArray(std::vector<float>());
                s.writeFloatArray(std::vector<float>());
                s.writeFloatArray(std::vector<float>());
            }
        }

        inline void deserializeToon(dsp::toon::Deserializer &d) override
        {
            // Read configuration
            m_threshold = d.readFloat();
            m_num_channels = d.readInt32();
            m_mode = d.readString();
            m_domain = d.readString();
            m_windowSize = d.readInt32();
            m_minPeakDistance = d.readInt32();

            // Read state arrays
            m_prev_sample = d.readFloatArray();
            m_prev_prev_sample = d.readFloatArray();

            // Read peakCooldown (convert from float array to int)
            std::vector<float> cooldown_float = d.readFloatArray();
            m_peakCooldown.clear();
            for (float val : cooldown_float)
            {
                m_peakCooldown.push_back(static_cast<int>(val));
            }
        }

        void reset() override
        {
            std::fill(m_prev_sample.begin(), m_prev_sample.end(), 0.0f);
            std::fill(m_prev_prev_sample.begin(), m_prev_prev_sample.end(), 0.0f);
            std::fill(m_peakCooldown.begin(), m_peakCooldown.end(), 0);
        }

        bool isResizing() const override { return false; }

    private:
        /**
         * @brief Stateful ("moving") peak detection processing (WindowSize = 3 only).
         */
        void processMoving(float *buffer, size_t numSamples, int numChannels)
        {
            if (m_num_channels != numChannels || m_prev_sample.empty())
            {
                m_num_channels = numChannels;
                m_prev_sample.resize(numChannels, 0.0f);
                m_prev_prev_sample.resize(numChannels, 0.0f);
                m_peakCooldown.resize(numChannels, 0); // Resize cooldown state
            }

            size_t samplesPerChannel = numSamples / numChannels;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float prev_prev = m_prev_prev_sample[ch];
                float prev = m_prev_sample[ch];
                int &cooldown = m_peakCooldown[ch]; // Get reference to cooldown counter

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float current = buffer[idx];

                    // Decrement cooldown if active
                    if (cooldown > 0)
                    {
                        cooldown--;
                    }

                    // Check if `prev` (the previous sample) was a peak
                    bool prev_is_peak = (cooldown == 0) && // Must not be in cooldown
                                        (prev > prev_prev) &&
                                        (prev > current) &&
                                        (prev >= m_threshold);

                    if (i > 0)
                    {
                        buffer[idx - numChannels] = prev_is_peak ? 1.0f : 0.0f;
                    }
                    else
                    {
                        buffer[idx] = prev_is_peak ? 1.0f : 0.0f;
                    }

                    // If we found a peak, reset the cooldown
                    if (prev_is_peak)
                    {
                        cooldown = m_minPeakDistance - 1;
                    }

                    // Shift history forward
                    prev_prev = prev;
                    prev = current;
                }

                if (samplesPerChannel > 1)
                {
                    buffer[(samplesPerChannel - 1) * numChannels + ch] = 0.0f;
                }

                m_prev_prev_sample[ch] = prev_prev;
                m_prev_sample[ch] = prev;
            }
        }

        /**
         * @brief Stateless ("batch") peak detection processing.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            if (m_planar_input.size() < numSamples / numChannels)
            {
                m_planar_input.resize(numSamples / numChannels);
                m_planar_output.resize(numSamples / numChannels);
            }

            size_t samplesPerChannel = numSamples / numChannels;

            // Select the correct core function
            void (*peak_func)(const float *, float *, size_t, float, int, int) =
                (m_domain == "frequency")
                    ? dsp::core::find_freq_peaks_batch
                    : dsp::core::find_peaks_batch_delayed;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    m_planar_input[i] = buffer[i * numChannels + ch];
                }

                // Process using the core function, passing all parameters
                peak_func(m_planar_input.data(), m_planar_output.data(), samplesPerChannel, m_threshold, m_windowSize, m_minPeakDistance);

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    buffer[i * numChannels + ch] = m_planar_output[i];
                }
            }
        }

        // --- Member Variables ---
        float m_threshold;
        std::string m_mode;
        std::string m_domain;
        int m_windowSize;
        int m_minPeakDistance;
        int m_num_channels;

        // State for "moving" mode
        std::vector<float> m_prev_sample;
        std::vector<float> m_prev_prev_sample;
        std::vector<int> m_peakCooldown; // Cooldown counter per channel

        // Temporary planar buffers for "batch" mode
        std::vector<float> m_planar_input;
        std::vector<float> m_planar_output;
    };

} // namespace dsp::adapters