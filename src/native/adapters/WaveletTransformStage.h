/**
 * Discrete Wavelet Transform (DWT) Pipeline Stage
 *
 * Performs single-level discrete wavelet decomposition using Daubechies wavelets.
 * This is a STATELESS/BATCH operation - processes entire buffer at once.
 *
 * Features:
 * - Haar, db2-db10 wavelets
 * - SIMD-optimized convolution via FirFilter
 * - Symmetric padding for boundary handling
 * - Outputs [approximation coefficients | detail coefficients]
 *
 * Algorithm:
 * 1. Apply symmetric padding
 * 2. Convolve with low-pass (scaling) filter → approximation
 * 3. Convolve with high-pass (wavelet) filter → detail
 * 4. Downsample both by factor of 2
 * 5. Concatenate [approx | detail]
 */

#pragma once

#include "../IDspStage.h"
#include "../core/WaveletCoeffs.h"
#include "../core/FirFilter.h"
#include "../utils/SimdOps.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace dsp::adapters
{
    class WaveletTransformStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a Wavelet Transform stage
         * @param wavelet_name Name of wavelet ("haar", "db2", "db3", ..., "db10")
         */
        explicit WaveletTransformStage(const std::string &wavelet_name)
            : m_wavelet_name(wavelet_name)
        {
            // Get wavelet filters
            auto filters = dsp::core::getWaveletFilters(wavelet_name);

            // Convert to float for FirFilter
            std::vector<float> lo_float(filters.dec_lo.begin(), filters.dec_lo.end());
            std::vector<float> hi_float(filters.dec_hi.begin(), filters.dec_hi.end());

            // Store filter length
            m_filter_length = lo_float.size();

            // Create FIR filters (stateless mode)
            m_lowpass_filter = std::make_unique<dsp::core::FirFilter<float>>(lo_float, false);
            m_highpass_filter = std::make_unique<dsp::core::FirFilter<float>>(hi_float, false);
        }

        const char *getType() const override
        {
            return "waveletTransform";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Process each channel independently
            for (int c = 0; c < numChannels; ++c)
            {
                processChannel(buffer, numSamples, numChannels, c);
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("waveletName", Napi::String::New(env, m_wavelet_name));
            state.Set("filterLength", Napi::Number::New(env, m_filter_length));
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            // Wavelet transform is stateless, but verify wavelet name matches
            std::string loaded_name = state.Get("waveletName").As<Napi::String>().Utf8Value();
            if (loaded_name != m_wavelet_name)
            {
                throw std::runtime_error("Wavelet name mismatch during deserialization");
            }
        }

        void reset() override
        {
            // Stateless - nothing to reset
        }

    private:
        /**
         * Apply symmetric padding (reflect about edge)
         * Example: [a b c d] with pad=2 → [b a | a b c d | d c]
         */
        std::vector<float> applyPadding(const float *data, size_t length, size_t pad_width)
        {
            std::vector<float> padded(length + 2 * pad_width);

            // Front padding (reflect)
            for (size_t i = 0; i < pad_width; ++i)
            {
                size_t src_idx = std::min(pad_width - 1 - i, length - 1);
                padded[i] = data[src_idx];
            }

            // Original data
            std::copy(data, data + length, padded.begin() + pad_width);

            // Back padding (reflect)
            for (size_t i = 0; i < pad_width; ++i)
            {
                size_t src_idx = std::max(int(length) - 2 - int(i), 0);
                padded[pad_width + length + i] = data[src_idx];
            }

            return padded;
        }

    private:
        void ensureScratchBuffer(size_t samples_per_channel)
        {
            if (m_scratch_channel.size() < samples_per_channel)
            {
                m_scratch_channel.resize(samples_per_channel * 2);
            }
        }

        /**
         * Process a single channel
         */
        void processChannel(float *buffer, size_t numSamples, int numChannels, int channel)
        {
            size_t samples_per_channel = numSamples / numChannels;

            // Ensure scratch buffer is large enough
            ensureScratchBuffer(samples_per_channel);

            // Extract channel data (de-interleave) into scratch buffer
            for (size_t i = 0; i < samples_per_channel; ++i)
            {
                m_scratch_channel[i] = buffer[i * numChannels + channel];
            }

            // Determine padding
            size_t pad_width = m_filter_length - 1;

            // Apply symmetric padding
            std::vector<float> padded = applyPadding(m_scratch_channel.data(), samples_per_channel, pad_width);

            // Convolve with filters (stateless mode)
            std::vector<float> approx_filtered(padded.size());
            std::vector<float> detail_filtered(padded.size());

            m_lowpass_filter->process(padded.data(), approx_filtered.data(), padded.size(), true);
            m_highpass_filter->process(padded.data(), detail_filtered.data(), padded.size(), true);

            // Downsample by 2 and handle group delay
            // For symmetric wavelets, the group delay is (filter_length - 1) / 2
            size_t delay = (m_filter_length - 1) / 2;
            size_t approx_length = (padded.size() - delay) / 2;
            size_t detail_length = approx_length;

            std::vector<float> approx_coeffs;
            std::vector<float> detail_coeffs;
            approx_coeffs.reserve(approx_length);
            detail_coeffs.reserve(detail_length);

            // Downsample: take every 2nd sample starting from delay
            for (size_t i = delay; i < approx_filtered.size(); i += 2)
            {
                if (approx_coeffs.size() < approx_length)
                {
                    approx_coeffs.push_back(approx_filtered[i]);
                    detail_coeffs.push_back(detail_filtered[i]);
                }
            }

            // Output format: [approx_coeffs | detail_coeffs]
            size_t output_length = approx_coeffs.size() + detail_coeffs.size();

            // Write back to buffer (interleaved)
            // Note: Output may be shorter than input due to downsampling
            size_t write_idx = 0;
            for (size_t i = 0; i < approx_coeffs.size() && write_idx < samples_per_channel; ++i)
            {
                buffer[write_idx * numChannels + channel] = approx_coeffs[i];
                write_idx++;
            }
            for (size_t i = 0; i < detail_coeffs.size() && write_idx < samples_per_channel; ++i)
            {
                buffer[write_idx * numChannels + channel] = detail_coeffs[i];
                write_idx++;
            }

            // Zero-pad any remaining samples in this channel
            for (size_t i = write_idx; i < samples_per_channel; ++i)
            {
                buffer[i * numChannels + channel] = 0.0f;
            }
        }

        std::string m_wavelet_name;
        size_t m_filter_length;
        std::unique_ptr<dsp::core::FirFilter<float>> m_lowpass_filter;
        std::unique_ptr<dsp::core::FirFilter<float>> m_highpass_filter;

        // Pre-allocated scratch buffer for channel extraction
        std::vector<float> m_scratch_channel;
    };

} // namespace dsp::adapters
