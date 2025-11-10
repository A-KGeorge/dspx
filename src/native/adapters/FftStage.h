/**
 * FFT Stage Adapter
 *
 * Wraps FftEngine for use in the DSP pipeline.
 * Supports forward/inverse transforms with multiple output formats.
 */

#ifndef DSP_ADAPTERS_FFT_STAGE_H
#define DSP_ADAPTERS_FFT_STAGE_H

#include "../IDspStage.h"
#include "../core/FftEngine.h"
#include <memory>
#include <string>

namespace dsp
{
    namespace adapters
    {

        class FftStage : public IDspStage
        {
        public:
            enum class TransformType
            {
                FFT,   // Complex FFT (power-of-2 only)
                IFFT,  // Inverse FFT
                DFT,   // Direct FT (any size)
                IDFT,  // Inverse DFT
                RFFT,  // Real FFT (power-of-2 only)
                IRFFT, // Inverse RFFT
                RDFT,  // Real DFT (any size)
                IRDFT  // Inverse RDFT
            };

            enum class OutputFormat
            {
                COMPLEX,   // Full complex output (interleaved real/imag)
                MAGNITUDE, // |X[k]|
                POWER,     // |X[k]|²
                PHASE      // ∠X[k]
            };

            /**
             * Constructor
             * @param size FFT size
             * @param type Transform type (fft, dft, rfft, rdft, etc.)
             * @param forward True for forward, false for inverse
             * @param format Output format (complex, magnitude, power, phase)
             */
            FftStage(size_t size, TransformType type, bool forward, OutputFormat format);

            ~FftStage() override = default;

            const char *getType() const override { return "fft"; }

            bool isResizing() const override { return true; }

            size_t calculateOutputSize(size_t inputSize) const override;

            void process(
                float *buffer,
                size_t numSamples,
                int numChannels,
                const float *timestamps = nullptr) override;

            void processResizing(
                const float *inputBuffer,
                size_t inputSize,
                float *outputBuffer,
                size_t &outputSize,
                int numChannels,
                const float *timestamps = nullptr) override;

            void reset() override;

            // State serialization
            Napi::Object serializeState(Napi::Env env) const override;
            void deserializeState(const Napi::Object &state) override;

            // Helper to parse transform type from string
            static TransformType parseTransformType(const std::string &typeStr);
            static OutputFormat parseOutputFormat(const std::string &formatStr);

        private:
            size_t m_fftSize;
            TransformType m_type;
            bool m_forward;
            OutputFormat m_format;

            // FFT engine instance
            std::unique_ptr<core::FftEngine<float>> m_engine;

            // Working buffers
            std::vector<std::complex<float>> m_complexBuffer;
            std::vector<std::complex<float>> m_tempComplexBuffer; // For DFT/IDFT in-place issue
            std::vector<float> m_realBuffer;
        };

    } // namespace adapters
} // namespace dsp

#endif // DSP_ADAPTERS_FFT_STAGE_H
