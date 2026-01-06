/**
 * MFCC (Mel-Frequency Cepstral Coefficients) Pipeline Stage
 *
 * Applies Discrete Cosine Transform (DCT) to log Mel-scale energies to produce MFCCs.
 * This is a STATELESS operation that leverages the DCT engine.
 *
 * Features:
 * - High-performance DCT using pre-computed cosine tables
 * - Optional log-energy normalization
 * - Coefficient selection (keep first N coefficients)
 * - Multi-channel support
 *
 * Mathematical Operation:
 *   1. Input: log(mel_energies) from Mel spectrogram
 *   2. Apply DCT-II: mfcc[k] = DCT(log_mel_energies)
 *   3. Keep first numCoefficients (typically 13-20)
 *
 * Typical Pipeline:
 *   STFT → Power → MelSpectrogram → Log → MFCC
 *
 * Parameters:
 * - numMelBands: Number of input Mel bands (from MelSpectrogram)
 * - numCoefficients: Number of MFCC coefficients to output (default: 13)
 * - useLogEnergy: Apply log to input before DCT (default: true)
 * - lifterCoefficient: Optional cepstral liftering (default: 0 = disabled)
 */

#pragma once

#include "../IDspStage.h"
#include "../core/DctEngine.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>
#include <cmath>
#include <algorithm>

namespace dsp::adapters
{
    class MfccStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs an MFCC stage
         * @param num_mel_bands Number of input Mel frequency bands
         * @param num_coefficients Number of MFCC coefficients to output (default: 13)
         * @param use_log_energy Apply log to input energies before DCT (default: true)
         * @param lifter_coefficient Cepstral liftering parameter (0 = disabled)
         */
        explicit MfccStage(
            size_t num_mel_bands,
            size_t num_coefficients = 13,
            bool use_log_energy = true,
            float lifter_coefficient = 0.0f)
            : m_numMelBands(num_mel_bands),
              m_numCoefficients(num_coefficients),
              m_useLogEnergy(use_log_energy),
              m_lifterCoefficient(lifter_coefficient)
        {
            // Validate parameters
            if (m_numMelBands == 0)
            {
                throw std::invalid_argument("MFCC: num_mel_bands must be greater than 0");
            }

            if (m_numCoefficients == 0 || m_numCoefficients > m_numMelBands)
            {
                throw std::invalid_argument(
                    "MFCC: num_coefficients must be in range [1, num_mel_bands]");
            }

            // Create DCT engine (size = numMelBands)
            m_dctEngine = std::make_unique<dsp::core::DctEngine<float>>(m_numMelBands);

            // Allocate working buffers
            m_logEnergies.resize(m_numMelBands);
            m_dctOutput.resize(m_numMelBands);

            // Pre-compute lifter weights if liftering is enabled
            if (m_lifterCoefficient > 0)
            {
                m_lifterWeights.resize(m_numCoefficients);
                for (size_t i = 0; i < m_numCoefficients; ++i)
                {
                    m_lifterWeights[i] = 1.0f + (m_lifterCoefficient / 2.0f) *
                                                    std::sin(M_PI * static_cast<float>(i) / m_lifterCoefficient);
                }
            }
        }

        const char *getType() const override
        {
            return "mfcc";
        }

        bool isResizing() const override
        {
            return true; // This stage changes output size
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // This stage changes output size - processResizing() should be called instead
            throw std::runtime_error("MFCC stage requires processResizing() to be called");
        }

        size_t calculateOutputSize(size_t inputSize) const override
        {
            // Input has numMelBands per frame, output has numCoefficients per frame
            // Calculate output size based on the ratio: (numCoefficients / numMelBands) * inputSize
            return (inputSize / m_numMelBands) * m_numCoefficients;
        }

        void processResizing(const float *inputBuffer, size_t inputSize,
                             float *outputBuffer, size_t &outputSize,
                             int numChannels, const float *timestamps = nullptr) override
        {
            // Calculate how many complete Mel spectrum frames we have
            size_t samplesPerChannel = inputSize / numChannels;
            size_t numFrames = samplesPerChannel / m_numMelBands;

            if (numFrames == 0)
            {
                // Not enough data for even one frame - output nothing
                outputSize = 0;
                return;
            }

            // Calculate output size
            outputSize = numFrames * m_numCoefficients * numChannels;

            // Process each channel independently
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Process each frame for this channel
                for (size_t frame = 0; frame < numFrames; ++frame)
                {
                    // Extract Mel energies for this frame (de-interleaved)
                    for (size_t i = 0; i < m_numMelBands; ++i)
                    {
                        size_t index = (frame * m_numMelBands + i) * numChannels + ch;
                        float energy = inputBuffer[index];

                        // Apply log if requested (add small epsilon to avoid log(0))
                        if (m_useLogEnergy)
                        {
                            const float epsilon = 1e-10f;
                            m_logEnergies[i] = std::log(energy + epsilon);
                        }
                        else
                        {
                            m_logEnergies[i] = energy;
                        }
                    }

                    // Apply DCT to get MFCCs
                    m_dctEngine->dct(m_logEnergies.data(), m_dctOutput.data());

                    // Extract first numCoefficients and apply liftering if enabled
                    for (size_t i = 0; i < m_numCoefficients; ++i)
                    {
                        float coeff = m_dctOutput[i];

                        // Apply cepstral liftering
                        if (m_lifterCoefficient > 0)
                        {
                            coeff *= m_lifterWeights[i];
                        }

                        // Write output (re-interleaved)
                        size_t outIndex = (frame * m_numCoefficients + i) * numChannels + ch;
                        outputBuffer[outIndex] = coeff;
                    }
                }
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("numMelBands", Napi::Number::New(env, m_numMelBands));
            state.Set("numCoefficients", Napi::Number::New(env, m_numCoefficients));
            state.Set("useLogEnergy", Napi::Boolean::New(env, m_useLogEnergy));
            state.Set("lifterCoefficient", Napi::Number::New(env, m_lifterCoefficient));
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            size_t numMelBands = state.Get("numMelBands").As<Napi::Number>().Uint32Value();
            size_t numCoefficients = state.Get("numCoefficients").As<Napi::Number>().Uint32Value();

            if (numMelBands != m_numMelBands || numCoefficients != m_numCoefficients)
            {
                throw std::runtime_error("MFCC: Dimension mismatch during deserialization");
            }
        }

        void reset() override
        {
            // Stateless - no reset needed
        }

        void serializeToon(toon::Serializer &serializer) const override
        {
            serializer.writeInt32(static_cast<int32_t>(m_numMelBands));
            serializer.writeInt32(static_cast<int32_t>(m_numCoefficients));
            serializer.writeBool(m_useLogEnergy);
            serializer.writeFloat(m_lifterCoefficient);
        }

        void deserializeToon(toon::Deserializer &deserializer) override
        {
            size_t numMelBands = static_cast<size_t>(deserializer.readInt32());
            size_t numCoefficients = static_cast<size_t>(deserializer.readInt32());

            if (numMelBands != m_numMelBands || numCoefficients != m_numCoefficients)
            {
                throw std::runtime_error("MFCC: Dimension mismatch during TOON deserialization");
            }

            deserializer.readBool();  // useLogEnergy
            deserializer.readFloat(); // lifterCoefficient
        }

    private:
        size_t m_numMelBands;      // Number of input Mel bands
        size_t m_numCoefficients;  // Number of MFCC coefficients to output
        bool m_useLogEnergy;       // Apply log to input energies
        float m_lifterCoefficient; // Cepstral liftering parameter (0 = disabled)

        // DCT engine
        std::unique_ptr<dsp::core::DctEngine<float>> m_dctEngine;

        // Working buffers
        std::vector<float> m_logEnergies;   // Log Mel energies (input to DCT)
        std::vector<float> m_dctOutput;     // Full DCT output (before truncation)
        std::vector<float> m_lifterWeights; // Pre-computed lifter weights
    };

} // namespace dsp::adapters
