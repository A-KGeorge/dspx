/**
 * Mel Spectrogram Pipeline Stage
 *
 * Converts power spectrum to Mel-scale representation using filterbank matrix multiplication.
 * This is a STATELESS operation that applies the Mel filterbank to incoming power spectra.
 *
 * Features:
 * - High-performance matrix multiplication using Eigen
 * - Pre-computed Mel filterbank (passed from TypeScript)
 * - Processes power spectrum bins → Mel frequency bins
 * - Multi-channel support (each channel processed independently)
 *
 * Mathematical Operation:
 *   mel_energies = filterbank × power_spectrum
 *   where:
 *     - power_spectrum is (numBins × 1) vector
 *     - filterbank is (numMelBands × numBins) matrix
 *     - mel_energies is (numMelBands × 1) vector
 *
 * Typical Pipeline:
 *   STFT → Power → MelSpectrogram → Log → MFCC
 *
 * Parameters:
 * - filterbankMatrix: Pre-computed Mel filterbank (TypeScript provides this)
 * - numBins: Number of input frequency bins (from STFT/FFT)
 * - numMelBands: Number of Mel frequency bands (output size)
 */

#pragma once

#include "../IDspStage.h"
#include <Eigen/Dense>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>
#include <cmath>

namespace dsp::adapters
{
    class MelSpectrogramStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a Mel Spectrogram stage
         * @param filterbank_matrix Pre-computed Mel filterbank (numMelBands × numBins), row-major
         * @param num_bins Number of input frequency bins
         * @param num_mel_bands Number of output Mel frequency bands
         */
        explicit MelSpectrogramStage(
            const std::vector<float> &filterbank_matrix,
            size_t num_bins,
            size_t num_mel_bands)
            : m_numBins(num_bins),
              m_numMelBands(num_mel_bands),
              m_filterbank(num_mel_bands, num_bins)
        {
            // Validate parameters
            if (m_numBins == 0)
            {
                throw std::invalid_argument("MelSpectrogram: num_bins must be greater than 0");
            }

            if (m_numMelBands == 0)
            {
                throw std::invalid_argument("MelSpectrogram: num_mel_bands must be greater than 0");
            }

            if (filterbank_matrix.size() != num_mel_bands * num_bins)
            {
                throw std::invalid_argument(
                    "MelSpectrogram: filterbank matrix size (" +
                    std::to_string(filterbank_matrix.size()) +
                    ") must equal numMelBands × numBins (" +
                    std::to_string(num_mel_bands * num_bins) + ")");
            }

            // Copy filterbank matrix (input is row-major from TypeScript)
            // Eigen uses column-major by default, so we need to specify row-major
            m_filterbank = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
                filterbank_matrix.data(), num_mel_bands, num_bins);
        }

        const char *getType() const override
        {
            return "melSpectrogram";
        }

        bool isResizing() const override
        {
            return true; // This stage changes output size
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // This stage changes output size - processResizing() should be called instead
            throw std::runtime_error("MelSpectrogram stage requires processResizing() to be called");
        }

        size_t calculateOutputSize(size_t inputSize) const override
        {
            // Input has numBins per frame, output has numMelBands per frame
            // Example: 10 samples, 2 channels, numBins=5 → samplesPerChannel=5, numFrames=1
            // Output: 1 frame × numMelBands × numChannels
            // Since we don't know numChannels here, we need to handle it in processResizing
            // For now, return based on the ratio: (numMelBands / numBins) * inputSize
            return (inputSize / m_numBins) * m_numMelBands;
        }

        void processResizing(const float *inputBuffer, size_t inputSize,
                             float *outputBuffer, size_t &outputSize,
                             int numChannels, const float *timestamps = nullptr) override
        {
            // Calculate how many complete spectrum frames we have
            // Each frame should be numBins samples per channel
            size_t samplesPerChannel = inputSize / numChannels;
            size_t numFrames = samplesPerChannel / m_numBins;

            if (numFrames == 0)
            {
                // Not enough data for even one frame - output nothing
                outputSize = 0;
                return;
            }

            // Calculate output size
            outputSize = numFrames * m_numMelBands * numChannels;

            // Temporary buffers for Eigen operations
            Eigen::VectorXf input(m_numBins);
            Eigen::VectorXf output(m_numMelBands);

            // Process each channel independently
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Process each frame for this channel
                for (size_t frame = 0; frame < numFrames; ++frame)
                {
                    // Extract input spectrum (de-interleaved)
                    for (size_t i = 0; i < m_numBins; ++i)
                    {
                        size_t index = (frame * m_numBins + i) * numChannels + ch;
                        input(i) = inputBuffer[index];
                    }

                    // Apply Mel filterbank: mel_energies = filterbank × power_spectrum
                    output = m_filterbank * input;

                    // Write output (re-interleaved)
                    for (size_t i = 0; i < m_numMelBands; ++i)
                    {
                        size_t outIndex = (frame * m_numMelBands + i) * numChannels + ch;
                        outputBuffer[outIndex] = output(i);
                    }
                }
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("numBins", Napi::Number::New(env, m_numBins));
            state.Set("numMelBands", Napi::Number::New(env, m_numMelBands));

            // Serialize filterbank matrix (row-major)
            Napi::Array filterbankArray = Napi::Array::New(env, m_numMelBands * m_numBins);
            for (size_t i = 0; i < m_numMelBands; ++i)
            {
                for (size_t j = 0; j < m_numBins; ++j)
                {
                    filterbankArray.Set(i * m_numBins + j, Napi::Number::New(env, m_filterbank(i, j)));
                }
            }
            state.Set("filterbank", filterbankArray);

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            size_t numBins = state.Get("numBins").As<Napi::Number>().Uint32Value();
            size_t numMelBands = state.Get("numMelBands").As<Napi::Number>().Uint32Value();

            if (numBins != m_numBins || numMelBands != m_numMelBands)
            {
                throw std::runtime_error("MelSpectrogram: Dimension mismatch during deserialization");
            }

            // Restore filterbank matrix
            Napi::Array filterbankArray = state.Get("filterbank").As<Napi::Array>();
            for (size_t i = 0; i < m_numMelBands; ++i)
            {
                for (size_t j = 0; j < m_numBins; ++j)
                {
                    m_filterbank(i, j) = filterbankArray.Get(i * m_numBins + j).As<Napi::Number>().FloatValue();
                }
            }
        }

        void reset() override
        {
            // Stateless - no reset needed
        }

        void serializeToon(toon::Serializer &serializer) const override
        {
            serializer.writeInt32(static_cast<int32_t>(m_numBins));
            serializer.writeInt32(static_cast<int32_t>(m_numMelBands));

            // Serialize filterbank matrix (row-major)
            for (size_t i = 0; i < m_numMelBands; ++i)
            {
                for (size_t j = 0; j < m_numBins; ++j)
                {
                    serializer.writeFloat(m_filterbank(i, j));
                }
            }
        }

        void deserializeToon(toon::Deserializer &deserializer) override
        {
            size_t numBins = static_cast<size_t>(deserializer.readInt32());
            size_t numMelBands = static_cast<size_t>(deserializer.readInt32());
            {
                throw std::runtime_error("MelSpectrogram: Dimension mismatch during TOON deserialization");
            }

            // Restore filterbank matrix
            for (size_t i = 0; i < m_numMelBands; ++i)
            {
                for (size_t j = 0; j < m_numBins; ++j)
                {
                    m_filterbank(i, j) = deserializer.readFloat();
                }
            }
        }

    private:
        size_t m_numBins;             // Number of input frequency bins
        size_t m_numMelBands;         // Number of output Mel bands
        Eigen::MatrixXf m_filterbank; // Mel filterbank matrix (numMelBands × numBins)
    };

} // namespace dsp::adapters
