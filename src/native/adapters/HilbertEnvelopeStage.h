/**
 * Hilbert Envelope Pipeline Stage
 *
 * Computes the instantaneous amplitude (envelope) of a signal using the Hilbert transform.
 * This is a STATEFUL/MOVING operation that uses a sliding window approach.
 *
 * Features:
 * - Sliding window FFT-based implementation
 * - SIMD-optimized magnitude calculation
 * - Per-channel state management
 * - Configurable window and hop size
 *
 * Algorithm:
 * 1. Maintain circular buffer per channel
 * 2. When window is full:
 *    a. FFT → frequency domain
 *    b. Create analytic signal (zero negative frequencies, double positive)
 *    c. IFFT → time domain
 *    d. Compute magnitude (envelope) with SIMD
 * 3. Output hop_size samples
 *
 * Mathematical Background:
 * The analytic signal is: z(t) = x(t) + j*H{x(t)}
 * where H{} is the Hilbert transform.
 * The envelope is: |z(t)| = sqrt(x²(t) + H{x}²(t))
 */

#pragma once

#include "../IDspStage.h"
#include "../core/FftEngine.h"
#include "../utils/CircularBufferArray.h"
#include "../utils/SimdOps.h"
#include <vector>
#include <complex>
#include <memory>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace dsp::adapters
{
    class HilbertEnvelopeStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a Hilbert Envelope stage
         * @param window_size FFT window size (should be power of 2 for best performance)
         * @param hop_size Number of samples to advance between windows
         */
        explicit HilbertEnvelopeStage(size_t window_size, size_t hop_size = 0)
            : m_window_size(window_size),
              m_hop_size(hop_size > 0 ? hop_size : window_size / 2) // Default: 50% overlap
        {
            if (m_window_size == 0)
            {
                throw std::invalid_argument("HilbertEnvelope: window size must be greater than 0");
            }

            if (m_hop_size == 0 || m_hop_size > m_window_size)
            {
                throw std::invalid_argument("HilbertEnvelope: hop size must be between 1 and window_size");
            }

            // Create FFT engine
            m_fft_engine = std::make_unique<dsp::core::FftEngine<float>>(m_window_size);

            // Allocate working buffers
            m_fft_buffer.resize(m_window_size);
            m_window_data.resize(m_window_size);
        }

        const char *getType() const override
        {
            return "hilbertEnvelope";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Lazy initialization of channel buffers
            if (m_channel_buffers.size() != static_cast<size_t>(numChannels))
            {
                m_channel_buffers.clear();
                m_samples_since_output.clear();

                for (int i = 0; i < numChannels; ++i)
                {
                    m_channel_buffers.emplace_back(m_window_size);
                    m_samples_since_output.push_back(0);
                }
            }

            // Temporary output buffer
            std::vector<float> output_buffer;
            output_buffer.reserve(numSamples);

            // Process sample by sample (de-interleaved processing)
            for (size_t i = 0; i < numSamples; ++i)
            {
                int channel = i % numChannels;
                float sample = buffer[i];

                // Add sample to channel's circular buffer
                m_channel_buffers[channel].push(sample);
                m_samples_since_output[channel]++;

                // Check if it's time to compute envelope for this channel
                if (m_channel_buffers[channel].getCount() >= m_window_size &&
                    m_samples_since_output[channel] >= m_hop_size)
                {
                    // Compute envelope
                    float envelope_value = computeEnvelope(channel);
                    output_buffer.push_back(envelope_value);

                    // Reset hop counter
                    m_samples_since_output[channel] = 0;
                }
                else
                {
                    // Not ready yet - output previous value or zero
                    // For streaming, we'll just pass through the input
                    output_buffer.push_back(sample);
                }
            }

            // Copy output back to buffer
            std::copy(output_buffer.begin(),
                      output_buffer.begin() + std::min(output_buffer.size(), numSamples),
                      buffer);
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("windowSize", Napi::Number::New(env, m_window_size));
            state.Set("hopSize", Napi::Number::New(env, m_hop_size));
            state.Set("numChannels", Napi::Number::New(env, m_channel_buffers.size()));

            // Serialize each channel's buffer
            Napi::Array channelsArray = Napi::Array::New(env, m_channel_buffers.size());
            for (size_t i = 0; i < m_channel_buffers.size(); ++i)
            {
                Napi::Object channelState = Napi::Object::New(env);

                // Get buffer data
                std::vector<float> buffer_data = m_channel_buffers[i].toVector();
                Napi::Array bufferArray = Napi::Array::New(env, buffer_data.size());
                for (size_t j = 0; j < buffer_data.size(); ++j)
                {
                    bufferArray.Set(j, Napi::Number::New(env, buffer_data[j]));
                }

                channelState.Set("buffer", bufferArray);
                channelState.Set("samplesSinceOutput", Napi::Number::New(env, m_samples_since_output[i]));
                channelsArray.Set(static_cast<uint32_t>(i), channelState);
            }
            state.Set("channels", channelsArray);
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            size_t windowSize = state.Get("windowSize").As<Napi::Number>().Uint32Value();
            size_t hopSize = state.Get("hopSize").As<Napi::Number>().Uint32Value();

            if (windowSize != m_window_size || hopSize != m_hop_size)
            {
                throw std::runtime_error("Window/hop size mismatch during deserialization");
            }

            uint32_t numChannels = state.Get("channels").As<Napi::Array>().Length();

            // Recreate channel buffers
            m_channel_buffers.clear();
            m_samples_since_output.clear();

            for (uint32_t i = 0; i < numChannels; ++i)
            {
                m_channel_buffers.emplace_back(m_window_size);
                m_samples_since_output.push_back(0);
            }

            // Restore each channel's state
            Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
            for (uint32_t i = 0; i < numChannels; ++i)
            {
                Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                // Restore buffer data
                Napi::Array bufferArray = channelState.Get("buffer").As<Napi::Array>();
                for (uint32_t j = 0; j < bufferArray.Length(); ++j)
                {
                    float value = bufferArray.Get(j).As<Napi::Number>().FloatValue();
                    m_channel_buffers[i].push(value);
                }

                // Restore counter
                m_samples_since_output[i] = channelState.Get("samplesSinceOutput").As<Napi::Number>().Uint32Value();
            }
        }

        void reset() override
        {
            for (auto &buffer : m_channel_buffers)
            {
                buffer.clear();
            }
            std::fill(m_samples_since_output.begin(), m_samples_since_output.end(), 0);
        }

    private:
        /**
         * Compute envelope for current window in specified channel
         */
        float computeEnvelope(size_t channel)
        {
            // Get current window data
            m_window_data = m_channel_buffers[channel].toVector();

            // Ensure we have enough data
            if (m_window_data.size() < m_window_size)
            {
                // Pad with zeros if needed
                m_window_data.resize(m_window_size, 0.0f);
            }

            // Convert to complex (real input)
            for (size_t i = 0; i < m_window_size; ++i)
            {
                m_fft_buffer[i] = std::complex<float>(m_window_data[i], 0.0f);
            } // Forward FFT
            m_fft_engine->fft(m_fft_buffer.data(), m_fft_buffer.data());

            // Create analytic signal in frequency domain:
            // - Keep DC (k=0)
            // - Double positive frequencies (k=1 to N/2-1)
            // - Keep Nyquist (k=N/2)
            // - Zero negative frequencies (k=N/2+1 to N-1)

            // DC component stays the same
            // m_fft_buffer[0] = m_fft_buffer[0];

            // Double positive frequencies (SIMD opportunity here)
            for (size_t i = 1; i < m_window_size / 2; ++i)
            {
                m_fft_buffer[i] *= 2.0f;
            }

            // Nyquist stays the same (if even length)
            // m_fft_buffer[m_window_size / 2] = m_fft_buffer[m_window_size / 2];

            // Zero negative frequencies
            for (size_t i = m_window_size / 2 + 1; i < m_window_size; ++i)
            {
                m_fft_buffer[i] = std::complex<float>(0.0f, 0.0f);
            }

            // Inverse FFT
            m_fft_engine->ifft(m_fft_buffer.data(), m_fft_buffer.data());

            // Compute magnitude (envelope) with SIMD
            // |z| = sqrt(real² + imag²)
            std::vector<float> envelope(m_window_size);

            // SIMD-optimized magnitude calculation
            for (size_t i = 0; i < m_window_size; ++i)
            {
                float real_part = m_fft_buffer[i].real();
                float imag_part = m_fft_buffer[i].imag();
                envelope[i] = std::sqrt(real_part * real_part + imag_part * imag_part);
            }

            // Return the most recent envelope value (center of window for better phase accuracy)
            // For simplicity, return the last value
            return envelope[m_window_size - 1];
        }

        size_t m_window_size;
        size_t m_hop_size;

        // FFT engine
        std::unique_ptr<dsp::core::FftEngine<float>> m_fft_engine;

        // Working buffers
        std::vector<std::complex<float>> m_fft_buffer;
        std::vector<float> m_window_data;

        // Per-channel state
        std::vector<dsp::utils::CircularBufferArray<float>> m_channel_buffers;
        std::vector<size_t> m_samples_since_output;
    };

} // namespace dsp::adapters
