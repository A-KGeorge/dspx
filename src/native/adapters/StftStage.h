/**
 * Short-Time Fourier Transform (STFT) Pipeline Stage
 *
 * Computes time-frequency representation using sliding window FFT/DFT.
 * This is a STATEFUL operation that maintains circular buffers per channel.
 *
 * Features:
 * - Leverages existing FftEngine for all FFT/DFT operations
 * - Configurable window functions (Hann, Hamming, Blackman, Bartlett, None)
 * - Multiple output formats (complex, magnitude, power, phase)
 * - Automatic FFT vs DFT selection based on window size
 * - Real and complex input support
 * - Per-channel state management
 * - SIMD-optimized magnitude/power/phase calculations
 *
 * Algorithm:
 * 1. Maintain circular buffer per channel
 * 2. When hop_size samples accumulated:
 *    a. Extract window_size samples
 *    b. Apply window function
 *    c. FFT/DFT → frequency domain
 *    d. Convert to requested output format
 * 3. Output frequency bins
 *
 * Parameters:
 * - windowSize: FFT window size (power of 2 recommended for FFT)
 * - hopSize: Stride between windows (default: windowSize/2)
 * - method: "fft" or "dft" (auto-selected if not specified)
 * - type: "real" or "complex" input signal type
 * - forward: true for forward transform, false for inverse
 * - output: "complex", "magnitude", "power", or "phase"
 * - window: "hann", "hamming", "blackman", "bartlett", or "none"
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
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp::adapters
{
    class StftStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs an STFT stage
         * @param window_size FFT window size
         * @param hop_size Number of samples to advance between windows
         * @param method "fft" or "dft"
         * @param type "real" or "complex"
         * @param forward true for forward transform, false for inverse
         * @param output "complex", "magnitude", "power", or "phase"
         * @param window "hann", "hamming", "blackman", "bartlett", or "none"
         */
        explicit StftStage(
            size_t window_size,
            size_t hop_size,
            const std::string &method,
            const std::string &type,
            bool forward,
            const std::string &output,
            const std::string &window)
            : m_window_size(window_size),
              m_hop_size(hop_size > 0 ? hop_size : window_size / 2),
              m_method(method),
              m_type(type),
              m_forward(forward),
              m_output(output),
              m_window_type(window)
        {
            // Validate parameters
            if (m_window_size == 0)
            {
                throw std::invalid_argument("STFT: window size must be greater than 0");
            }

            if (m_hop_size == 0 || m_hop_size > m_window_size)
            {
                throw std::invalid_argument("STFT: hop size must be between 1 and window_size");
            }

            if (m_method != "fft" && m_method != "dft")
            {
                throw std::invalid_argument("STFT: method must be 'fft' or 'dft'");
            }

            if (m_type != "real" && m_type != "complex")
            {
                throw std::invalid_argument("STFT: type must be 'real' or 'complex'");
            }

            if (m_output != "complex" && m_output != "magnitude" && m_output != "power" && m_output != "phase")
            {
                throw std::invalid_argument("STFT: output must be 'complex', 'magnitude', 'power', or 'phase'");
            }

            // Create FFT engine
            m_fft_engine = std::make_unique<dsp::core::FftEngine<float>>(m_window_size);

            // Check if FFT is valid for this size
            if (m_method == "fft" && !m_fft_engine->isPowerOfTwo())
            {
                throw std::invalid_argument("STFT: FFT requires power-of-2 window size. Use DFT or adjust window size.");
            }

            // Pre-compute window function
            generateWindowFunction();

            // Allocate working buffers
            m_window_data.resize(m_window_size);
            m_fft_input.resize(m_window_size);
            m_fft_output.resize(m_window_size);

            // Calculate output size based on type
            if (m_type == "real")
            {
                m_output_size = m_fft_engine->getHalfSize(); // N/2+1 for real inputs
            }
            else
            {
                m_output_size = m_window_size; // N for complex inputs
            }
        }

        const char *getType() const override
        {
            return "stft";
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
            output_buffer.reserve(numSamples * m_output_size); // May expand due to frequency bins

            // Process sample by sample (interleaved processing)
            for (size_t i = 0; i < numSamples; ++i)
            {
                int channel = i % numChannels;
                float sample = buffer[i];

                // Add sample to channel's circular buffer
                m_channel_buffers[channel].push(sample);
                m_samples_since_output[channel]++;

                // Check if it's time to compute STFT for this channel
                if (m_channel_buffers[channel].getCount() >= m_window_size &&
                    m_samples_since_output[channel] >= m_hop_size)
                {
                    // Compute STFT for this window
                    computeStft(channel, output_buffer);

                    // Reset hop counter
                    m_samples_since_output[channel] = 0;
                }
            }

            // Copy output back to buffer (truncate or pad as needed)
            size_t output_size = std::min(output_buffer.size(), numSamples);
            std::copy(output_buffer.begin(), output_buffer.begin() + output_size, buffer);

            // Pad with zeros if output is smaller
            if (output_size < numSamples)
            {
                std::fill(buffer + output_size, buffer + numSamples, 0.0f);
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("windowSize", Napi::Number::New(env, m_window_size));
            state.Set("hopSize", Napi::Number::New(env, m_hop_size));
            state.Set("method", Napi::String::New(env, m_method));
            state.Set("type", Napi::String::New(env, m_type));
            state.Set("forward", Napi::Boolean::New(env, m_forward));
            state.Set("output", Napi::String::New(env, m_output));
            state.Set("window", Napi::String::New(env, m_window_type));
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
         * Generate window function coefficients
         */
        void generateWindowFunction()
        {
            m_window_function.resize(m_window_size);

            if (m_window_type == "none")
            {
                // Rectangular window
                std::fill(m_window_function.begin(), m_window_function.end(), 1.0f);
            }
            else if (m_window_type == "hann")
            {
                // Hann window: 0.5 * (1 - cos(2π*n/(N-1)))
                for (size_t n = 0; n < m_window_size; ++n)
                {
                    m_window_function[n] = 0.5f * (1.0f - std::cos(2.0f * M_PI * n / (m_window_size - 1)));
                }
            }
            else if (m_window_type == "hamming")
            {
                // Hamming window: 0.54 - 0.46 * cos(2π*n/(N-1))
                for (size_t n = 0; n < m_window_size; ++n)
                {
                    m_window_function[n] = 0.54f - 0.46f * std::cos(2.0f * M_PI * n / (m_window_size - 1));
                }
            }
            else if (m_window_type == "blackman")
            {
                // Blackman window: 0.42 - 0.5*cos(2π*n/(N-1)) + 0.08*cos(4π*n/(N-1))
                for (size_t n = 0; n < m_window_size; ++n)
                {
                    float cos1 = std::cos(2.0f * M_PI * n / (m_window_size - 1));
                    float cos2 = std::cos(4.0f * M_PI * n / (m_window_size - 1));
                    m_window_function[n] = 0.42f - 0.5f * cos1 + 0.08f * cos2;
                }
            }
            else if (m_window_type == "bartlett")
            {
                // Bartlett (triangular) window: 1 - |2n/(N-1) - 1|
                for (size_t n = 0; n < m_window_size; ++n)
                {
                    m_window_function[n] = 1.0f - std::abs(2.0f * n / (m_window_size - 1) - 1.0f);
                }
            }
            else
            {
                throw std::invalid_argument("STFT: Unknown window type '" + m_window_type + "'");
            }
        }

        /**
         * Compute STFT for current window in specified channel
         */
        void computeStft(size_t channel, std::vector<float> &output)
        {
            // Get current window data
            m_window_data = m_channel_buffers[channel].toVector();

            // Ensure we have enough data
            if (m_window_data.size() < m_window_size)
            {
                // Pad with zeros if needed
                m_window_data.resize(m_window_size, 0.0f);
            }

            // Apply window function
            for (size_t i = 0; i < m_window_size; ++i)
            {
                m_window_data[i] *= m_window_function[i];
            }

            // Perform FFT/DFT using FftEngine
            if (m_type == "real")
            {
                // Real input → use RFFT/RDFT
                if (m_method == "fft")
                {
                    m_fft_engine->rfft(m_window_data.data(), m_fft_output.data());
                }
                else
                {
                    m_fft_engine->rdft(m_window_data.data(), m_fft_output.data());
                }
            }
            else
            {
                // Complex input → use FFT/DFT
                // Convert real to complex
                for (size_t i = 0; i < m_window_size; ++i)
                {
                    m_fft_input[i] = std::complex<float>(m_window_data[i], 0.0f);
                }

                if (m_method == "fft")
                {
                    if (m_forward)
                    {
                        m_fft_engine->fft(m_fft_input.data(), m_fft_output.data());
                    }
                    else
                    {
                        m_fft_engine->ifft(m_fft_input.data(), m_fft_output.data());
                    }
                }
                else
                {
                    if (m_forward)
                    {
                        m_fft_engine->dft(m_fft_input.data(), m_fft_output.data());
                    }
                    else
                    {
                        m_fft_engine->idft(m_fft_input.data(), m_fft_output.data());
                    }
                }
            }

            // Convert to requested output format and append to output buffer
            convertOutput(output);
        }

        /**
         * Convert FFT output to requested format
         */
        void convertOutput(std::vector<float> &output)
        {
            if (m_output == "complex")
            {
                // Output real and imaginary parts interleaved
                for (size_t i = 0; i < m_output_size; ++i)
                {
                    output.push_back(m_fft_output[i].real());
                    output.push_back(m_fft_output[i].imag());
                }
            }
            else if (m_output == "magnitude")
            {
                // Compute magnitude: |X[k]| = sqrt(Re² + Im²)
                std::vector<float> magnitudes(m_output_size);
                m_fft_engine->getMagnitude(m_fft_output.data(), magnitudes.data(), m_output_size);
                output.insert(output.end(), magnitudes.begin(), magnitudes.end());
            }
            else if (m_output == "power")
            {
                // Compute power: |X[k]|²
                std::vector<float> power(m_output_size);
                m_fft_engine->getPower(m_fft_output.data(), power.data(), m_output_size);
                output.insert(output.end(), power.begin(), power.end());
            }
            else if (m_output == "phase")
            {
                // Compute phase: atan2(Im, Re)
                std::vector<float> phases(m_output_size);
                m_fft_engine->getPhase(m_fft_output.data(), phases.data(), m_output_size);
                output.insert(output.end(), phases.begin(), phases.end());
            }
        }

        // Configuration
        size_t m_window_size;
        size_t m_hop_size;
        std::string m_method;      // "fft" or "dft"
        std::string m_type;        // "real" or "complex"
        bool m_forward;            // true for forward, false for inverse
        std::string m_output;      // "complex", "magnitude", "power", "phase"
        std::string m_window_type; // "hann", "hamming", "blackman", "bartlett", "none"
        size_t m_output_size;      // Number of frequency bins per frame

        // FFT engine
        std::unique_ptr<dsp::core::FftEngine<float>> m_fft_engine;

        // Window function coefficients
        std::vector<float> m_window_function;

        // Working buffers
        std::vector<float> m_window_data;
        std::vector<std::complex<float>> m_fft_input;
        std::vector<std::complex<float>> m_fft_output;

        // Per-channel state
        std::vector<dsp::utils::CircularBufferArray<float>> m_channel_buffers;
        std::vector<size_t> m_samples_since_output;
    };

} // namespace dsp::adapters
