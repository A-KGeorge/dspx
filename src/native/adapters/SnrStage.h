#pragma once

#include "../IDspStage.h"
#include "../core/RmsFilter.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string>

namespace dsp::adapters
{
    /**
     * @brief SNR (Signal-to-Noise Ratio) Stage - Computes SNR in dB.
     *
     * Requires exactly 2 channels:
     * - Channel 0: Signal (clean or signal+noise)
     * - Channel 1: Noise reference
     *
     * Output: Single channel containing SNR in dB
     * Formula: SNR_dB = 10 * log10(signal_power / noise_power)
     *
     * Uses dual RMS filters to compute running power estimates.
     *
     * **Use Cases:**
     * - Audio quality assessment
     * - Speech enhancement validation
     * - Adaptive filter performance monitoring
     * - Real-time SNR tracking in communications
     *
     * **Note:** Output is clamped to [-100, 100] dB to avoid infinities.
     * Very low noise results in high SNR (approaching 100 dB).
     */
    class SnrStage : public IDspStage
    {
    private:
        size_t m_window_size;                     // Window size for RMS computation
        dsp::core::RmsFilter<float> m_signal_rms; // RMS filter for signal channel
        dsp::core::RmsFilter<float> m_noise_rms;  // RMS filter for noise channel
        bool m_initialized;                       // Track if filters are initialized

    public:
        /**
         * @brief Construct SNR stage with specified window size.
         * @param window_size Window size in samples for RMS computation.
         */
        explicit SnrStage(size_t window_size)
            : m_window_size(window_size),
              m_signal_rms(window_size),
              m_noise_rms(window_size),
              m_initialized(false)
        {
            if (window_size == 0)
            {
                throw std::invalid_argument("SNR window_size must be greater than 0");
            }
        }

        const char *getType() const override
        {
            return "snr";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Validate 2-channel requirement
            if (numChannels != 2)
            {
                throw std::invalid_argument("SNR stage requires exactly 2 channels (signal, noise)");
            }

            size_t samplesPerChannel = numSamples / 2;

            // Constants for SNR computation
            const float epsilon = 1e-10f; // Prevent division by zero
            const float min_db = -100.0f;
            const float max_db = 100.0f;

            // Process sample by sample
            for (size_t i = 0; i < samplesPerChannel; ++i)
            {
                float signal_sample = buffer[i * 2 + 0]; // Channel 0 (signal)
                float noise_sample = buffer[i * 2 + 1];  // Channel 1 (noise)

                // Update RMS filters with new samples (sample-count based, not time-aware)
                float signal_rms = m_signal_rms.addSample(signal_sample);
                float noise_rms = m_noise_rms.addSample(noise_sample);

                // Compute signal and noise power (RMS^2)
                float signal_power = signal_rms * signal_rms;
                float noise_power = noise_rms * noise_rms;

                // Compute SNR in dB with clamping
                float snr_db;
                if (noise_power < epsilon)
                {
                    snr_db = max_db; // Very low noise -> high SNR
                }
                else
                {
                    snr_db = 10.0f * std::log10f((signal_power + epsilon) / (noise_power + epsilon));
                    snr_db = std::max(min_db, std::min(max_db, snr_db)); // Clamp [-100, 100]
                }

                buffer[i] = snr_db; // Write SNR to output (single channel)
            }

            // Output is now single-channel (SNR values in dB)
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("windowSize", Napi::Number::New(env, static_cast<uint32_t>(m_window_size)));

            // Serialize signal RMS filter state
            auto signal_state = m_signal_rms.getState();
            const auto &signal_buffer = signal_state.first;
            float signal_sum = signal_state.second;

            Napi::Array signalBufferArray = Napi::Array::New(env, signal_buffer.size());
            for (size_t i = 0; i < signal_buffer.size(); ++i)
            {
                signalBufferArray.Set(i, Napi::Number::New(env, signal_buffer[i]));
            }
            state.Set("signalBuffer", signalBufferArray);
            state.Set("signalSum", Napi::Number::New(env, signal_sum));

            // Serialize noise RMS filter state
            auto noise_state = m_noise_rms.getState();
            const auto &noise_buffer = noise_state.first;
            float noise_sum = noise_state.second;

            Napi::Array noiseBufferArray = Napi::Array::New(env, noise_buffer.size());
            for (size_t i = 0; i < noise_buffer.size(); ++i)
            {
                noiseBufferArray.Set(i, Napi::Number::New(env, noise_buffer[i]));
            }
            state.Set("noiseBuffer", noiseBufferArray);
            state.Set("noiseSum", Napi::Number::New(env, noise_sum));

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("windowSize"))
                m_window_size = state.Get("windowSize").As<Napi::Number>().Uint32Value();

            // Deserialize signal RMS filter state
            if (state.Has("signalBuffer") && state.Has("signalSum"))
            {
                Napi::Array signalBufferArray = state.Get("signalBuffer").As<Napi::Array>();
                std::vector<float> signal_buffer;
                for (uint32_t i = 0; i < signalBufferArray.Length(); ++i)
                    signal_buffer.push_back(signalBufferArray.Get(i).As<Napi::Number>().FloatValue());

                float signal_sum = state.Get("signalSum").As<Napi::Number>().FloatValue();
                m_signal_rms.setState(signal_buffer, signal_sum);
            }

            // Deserialize noise RMS filter state
            if (state.Has("noiseBuffer") && state.Has("noiseSum"))
            {
                Napi::Array noiseBufferArray = state.Get("noiseBuffer").As<Napi::Array>();
                std::vector<float> noise_buffer;
                for (uint32_t i = 0; i < noiseBufferArray.Length(); ++i)
                    noise_buffer.push_back(noiseBufferArray.Get(i).As<Napi::Number>().FloatValue());

                float noise_sum = state.Get("noiseSum").As<Napi::Number>().FloatValue();
                m_noise_rms.setState(noise_buffer, noise_sum);
            }

            m_initialized = true;
        }

        void reset() override
        {
            m_signal_rms.clear();
            m_noise_rms.clear();
        }

        bool isResizing() const override
        {
            return true; // SNR reduces 2 channels to 1 channel
        }

        int getOutputChannels() const override
        {
            return 1; // SNR outputs single channel (dB values)
        }
    };

} // namespace dsp::adapters
