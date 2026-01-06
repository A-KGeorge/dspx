/**
 * FFT Stage Implementation
 */

#include "FftStage.h"
#include <cstring>
#include <stdexcept>

namespace dsp
{
    namespace adapters
    {

        FftStage::FftStage(size_t size, TransformType type, bool forward, OutputFormat format)
            : m_fftSize(size), m_type(type), m_forward(forward), m_format(format)
        {
            if (m_fftSize == 0)
            {
                throw std::invalid_argument("FFT size must be greater than 0");
            }

            // Create FFT engine
            m_engine = std::make_unique<core::FftEngine<float>>(m_fftSize);

            // Check power-of-2 requirement for FFT/RFFT
            if ((m_type == TransformType::FFT || m_type == TransformType::RFFT) && !m_engine->isPowerOfTwo())
            {
                throw std::invalid_argument("FFT/RFFT requires power-of-2 size. Use DFT/RDFT for arbitrary sizes.");
            }

            // Allocate working buffers
            m_complexBuffer.resize(m_fftSize);
            m_tempComplexBuffer.resize(m_fftSize);
            m_realBuffer.resize(m_fftSize);
        }

        size_t FftStage::calculateOutputSize(size_t inputSize) const
        {
            // Determine transform direction
            bool isInverseComplex = (m_type == TransformType::IFFT || m_type == TransformType::IDFT) ||
                                    ((m_type == TransformType::FFT || m_type == TransformType::DFT) && !m_forward);
            bool isInverseReal = (m_type == TransformType::IRFFT || m_type == TransformType::IRDFT) ||
                                 ((m_type == TransformType::RFFT || m_type == TransformType::RDFT) && !m_forward);
            bool isForwardReal = (m_type == TransformType::RFFT || m_type == TransformType::RDFT) && m_forward;

            // Calculate input size per frame
            size_t inputSizePerFrame;
            if (isInverseComplex)
            {
                inputSizePerFrame = m_fftSize * 2; // Complex input
            }
            else if (isInverseReal)
            {
                inputSizePerFrame = m_engine->getHalfSize() * 2; // Half-spectrum complex input
            }
            else
            {
                inputSizePerFrame = m_fftSize; // Real input
            }

            size_t numFrames = inputSize / inputSizePerFrame;

            // Calculate output size per frame
            size_t outputSizePerFrame;
            if (isForwardReal)
            {
                outputSizePerFrame = m_engine->getHalfSize();
                if (m_format == OutputFormat::COMPLEX)
                {
                    outputSizePerFrame *= 2;
                }
            }
            else if (isInverseReal || isInverseComplex)
            {
                // All inverse transforms output real time-domain values
                outputSizePerFrame = m_fftSize;
            }
            else if (m_format == OutputFormat::COMPLEX)
            {
                // Forward complex transforms
                outputSizePerFrame = m_fftSize * 2;
            }
            else
            {
                outputSizePerFrame = m_fftSize;
            }

            return numFrames * outputSizePerFrame;
        }

        void FftStage::process(
            float *buffer,
            size_t numSamples,
            int numChannels,
            const float *timestamps)
        {
            // FFT is a resizing stage - this shouldn't be called
            // Pipeline will call processResizing() instead
            throw std::runtime_error("FFT stage requires processResizing(), not process()");
        }

        void FftStage::processResizing(
            const float *inputBuffer,
            size_t inputSize,
            float *outputBuffer,
            size_t &outputSize,
            int numChannels,
            const float *timestamps)
        {
            // Determine transform direction
            bool isInverseComplex = (m_type == TransformType::IFFT || m_type == TransformType::IDFT) ||
                                    ((m_type == TransformType::FFT || m_type == TransformType::DFT) && !m_forward);
            bool isInverseReal = (m_type == TransformType::IRFFT || m_type == TransformType::IRDFT) ||
                                 ((m_type == TransformType::RFFT || m_type == TransformType::RDFT) && !m_forward);
            bool isForwardReal = (m_type == TransformType::RFFT || m_type == TransformType::RDFT) && m_forward;

            // Calculate input/output sizes per frame
            size_t inputSizePerFrame;
            if (isInverseComplex)
            {
                inputSizePerFrame = m_fftSize * 2; // Complex input
            }
            else if (isInverseReal)
            {
                inputSizePerFrame = m_engine->getHalfSize() * 2; // Half-spectrum complex input
            }
            else
            {
                inputSizePerFrame = m_fftSize; // Real input
            }

            size_t numFrames = inputSize / (inputSizePerFrame * numChannels);

            // Calculate output size per frame
            size_t outputSizePerFrame;
            if (isForwardReal)
            {
                outputSizePerFrame = m_engine->getHalfSize();
                if (m_format == OutputFormat::COMPLEX)
                {
                    outputSizePerFrame *= 2;
                }
            }
            else if (isInverseReal || isInverseComplex)
            {
                // All inverse transforms output real values only
                outputSizePerFrame = m_fftSize;
            }
            else if (m_format == OutputFormat::COMPLEX)
            {
                // Forward complex transforms only
                outputSizePerFrame = m_fftSize * 2;
            }
            else
            {
                outputSizePerFrame = m_fftSize;
            }

            outputSize = numFrames * outputSizePerFrame * numChannels;

            // OPTIMIZATION: Process all frames and channels with minimal memory operations
            // Use pre-allocated member buffers to avoid repeated allocations

            for (size_t frame = 0; frame < numFrames; ++frame)
            {
                for (size_t ch = 0; ch < static_cast<size_t>(numChannels); ++ch)
                {
                    const float *frameInput = inputBuffer + (frame * inputSizePerFrame * numChannels) + ch;
                    float *frameOutput = outputBuffer + (frame * outputSizePerFrame * numChannels) + ch;

                    // OPTIMIZATION 1: Minimize conditional branches - process by transform type
                    // OPTIMIZATION 2: Use member buffers (already allocated in constructor)

                    // Load input data (OPTIMIZED: Loop unrolling + better ILP)
                    if (isInverseComplex)
                    {
                        // Complex input: deinterleave directly into complex buffer
                        // OPTIMIZATION: Unroll by 4 for better instruction-level parallelism
                        size_t i = 0;
                        const size_t stride = numChannels;
                        const size_t stride2 = 2 * stride;

                        // Process 4 complex numbers at a time
                        for (; i + 3 < m_fftSize; i += 4)
                        {
                            size_t idx0 = (i * 2) * stride;
                            size_t idx1 = ((i + 1) * 2) * stride;
                            size_t idx2 = ((i + 2) * 2) * stride;
                            size_t idx3 = ((i + 3) * 2) * stride;

                            m_complexBuffer[i] = std::complex<float>(frameInput[idx0], frameInput[idx0 + stride]);
                            m_complexBuffer[i + 1] = std::complex<float>(frameInput[idx1], frameInput[idx1 + stride]);
                            m_complexBuffer[i + 2] = std::complex<float>(frameInput[idx2], frameInput[idx2 + stride]);
                            m_complexBuffer[i + 3] = std::complex<float>(frameInput[idx3], frameInput[idx3 + stride]);
                        }

                        // Handle remainder
                        for (; i < m_fftSize; ++i)
                        {
                            size_t idx = (i * 2) * stride;
                            m_complexBuffer[i] = std::complex<float>(frameInput[idx], frameInput[idx + stride]);
                        }
                    }
                    else if (isInverseReal)
                    {
                        // Half-spectrum complex input: deinterleave with loop unrolling
                        size_t halfSize = m_engine->getHalfSize();
                        size_t i = 0;
                        const size_t stride = numChannels;

                        // Unroll by 4
                        for (; i + 3 < halfSize; i += 4)
                        {
                            size_t idx0 = (i * 2) * stride;
                            size_t idx1 = ((i + 1) * 2) * stride;
                            size_t idx2 = ((i + 2) * 2) * stride;
                            size_t idx3 = ((i + 3) * 2) * stride;

                            m_complexBuffer[i] = std::complex<float>(frameInput[idx0], frameInput[idx0 + stride]);
                            m_complexBuffer[i + 1] = std::complex<float>(frameInput[idx1], frameInput[idx1 + stride]);
                            m_complexBuffer[i + 2] = std::complex<float>(frameInput[idx2], frameInput[idx2 + stride]);
                            m_complexBuffer[i + 3] = std::complex<float>(frameInput[idx3], frameInput[idx3 + stride]);
                        }

                        // Handle remainder
                        for (; i < halfSize; ++i)
                        {
                            size_t idx = (i * 2) * stride;
                            m_complexBuffer[i] = std::complex<float>(frameInput[idx], frameInput[idx + stride]);
                        }
                    }
                    else
                    {
                        // Real input: deinterleave with loop unrolling
                        // OPTIMIZATION: Special case for numChannels == 1 (no deinterleaving needed)
                        if (numChannels == 1)
                        {
                            // Direct memcpy when no deinterleaving needed
                            std::memcpy(m_realBuffer.data(), frameInput, m_fftSize * sizeof(float));
                        }
                        else
                        {
                            // Deinterleave with unrolling by 8 for better ILP
                            size_t i = 0;
                            const size_t stride = numChannels;

                            for (; i + 7 < m_fftSize; i += 8)
                            {
                                m_realBuffer[i] = frameInput[i * stride];
                                m_realBuffer[i + 1] = frameInput[(i + 1) * stride];
                                m_realBuffer[i + 2] = frameInput[(i + 2) * stride];
                                m_realBuffer[i + 3] = frameInput[(i + 3) * stride];
                                m_realBuffer[i + 4] = frameInput[(i + 4) * stride];
                                m_realBuffer[i + 5] = frameInput[(i + 5) * stride];
                                m_realBuffer[i + 6] = frameInput[(i + 6) * stride];
                                m_realBuffer[i + 7] = frameInput[(i + 7) * stride];
                            }

                            // Handle remainder
                            for (; i < m_fftSize; ++i)
                            {
                                m_realBuffer[i] = frameInput[i * stride];
                            }
                        }
                    }

                    // Perform transform (OPTIMIZED: Reduced switch cases, grouped by category)
                    if (isInverseComplex)
                    {
                        // All complex inverse transforms
                        if (m_type == TransformType::IFFT || (m_type == TransformType::FFT && !m_forward))
                        {
                            m_engine->ifft(m_complexBuffer.data(), m_complexBuffer.data());
                        }
                        else // IDFT or (DFT && !forward)
                        {
                            m_engine->idft(m_complexBuffer.data(), m_tempComplexBuffer.data());
                            // OPTIMIZATION: Use memcpy instead of std::copy for POD types
                            std::memcpy(m_complexBuffer.data(), m_tempComplexBuffer.data(),
                                        m_fftSize * sizeof(std::complex<float>));
                        }
                    }
                    else if (isInverseReal)
                    {
                        // All real inverse transforms
                        if (m_type == TransformType::IRFFT || (m_type == TransformType::RFFT && !m_forward))
                        {
                            m_engine->irfft(m_complexBuffer.data(), m_realBuffer.data());
                        }
                        else // IRDFT or (RDFT && !forward)
                        {
                            m_engine->irdft(m_complexBuffer.data(), m_realBuffer.data());
                        }
                    }
                    else
                    {
                        // Forward transforms
                        if (m_type == TransformType::FFT)
                        {
                            // Forward FFT: real to complex
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                m_complexBuffer[i] = std::complex<float>(m_realBuffer[i], 0.0f);
                            }
                            m_engine->fft(m_complexBuffer.data(), m_complexBuffer.data());
                        }
                        else if (m_type == TransformType::DFT)
                        {
                            // Forward DFT: real to complex
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                m_complexBuffer[i] = std::complex<float>(m_realBuffer[i], 0.0f);
                            }
                            m_engine->dft(m_complexBuffer.data(), m_tempComplexBuffer.data());
                            // OPTIMIZATION: Use memcpy instead of std::copy
                            std::memcpy(m_complexBuffer.data(), m_tempComplexBuffer.data(),
                                        m_fftSize * sizeof(std::complex<float>));
                        }
                        else if (m_type == TransformType::RFFT)
                        {
                            m_engine->rfft(m_realBuffer.data(), m_complexBuffer.data());
                        }
                        else // RDFT
                        {
                            m_engine->rdft(m_realBuffer.data(), m_complexBuffer.data());
                        }
                    }

                    // Write output (OPTIMIZED: Loop unrolling for interleaving)
                    if (isInverseReal || isInverseComplex)
                    {
                        // ALL inverse transforms output real time-domain values
                        const float *sourceData = isInverseReal ? m_realBuffer.data() : nullptr;

                        // OPTIMIZATION: Special case for single channel (no interleaving)
                        if (numChannels == 1)
                        {
                            if (isInverseReal)
                            {
                                std::memcpy(frameOutput, m_realBuffer.data(), m_fftSize * sizeof(float));
                            }
                            else
                            {
                                // Extract real parts - unroll by 8
                                size_t i = 0;
                                for (; i + 7 < m_fftSize; i += 8)
                                {
                                    frameOutput[i] = m_complexBuffer[i].real();
                                    frameOutput[i + 1] = m_complexBuffer[i + 1].real();
                                    frameOutput[i + 2] = m_complexBuffer[i + 2].real();
                                    frameOutput[i + 3] = m_complexBuffer[i + 3].real();
                                    frameOutput[i + 4] = m_complexBuffer[i + 4].real();
                                    frameOutput[i + 5] = m_complexBuffer[i + 5].real();
                                    frameOutput[i + 6] = m_complexBuffer[i + 6].real();
                                    frameOutput[i + 7] = m_complexBuffer[i + 7].real();
                                }
                                for (; i < m_fftSize; ++i)
                                {
                                    frameOutput[i] = m_complexBuffer[i].real();
                                }
                            }
                        }
                        else
                        {
                            // Interleave with loop unrolling by 4
                            size_t i = 0;
                            const size_t stride = numChannels;

                            for (; i + 3 < m_fftSize; i += 4)
                            {
                                if (isInverseReal)
                                {
                                    frameOutput[i * stride] = m_realBuffer[i];
                                    frameOutput[(i + 1) * stride] = m_realBuffer[i + 1];
                                    frameOutput[(i + 2) * stride] = m_realBuffer[i + 2];
                                    frameOutput[(i + 3) * stride] = m_realBuffer[i + 3];
                                }
                                else
                                {
                                    frameOutput[i * stride] = m_complexBuffer[i].real();
                                    frameOutput[(i + 1) * stride] = m_complexBuffer[i + 1].real();
                                    frameOutput[(i + 2) * stride] = m_complexBuffer[i + 2].real();
                                    frameOutput[(i + 3) * stride] = m_complexBuffer[i + 3].real();
                                }
                            }

                            // Handle remainder
                            for (; i < m_fftSize; ++i)
                            {
                                frameOutput[i * stride] = isInverseReal
                                                              ? m_realBuffer[i]
                                                              : m_complexBuffer[i].real();
                            }
                        }
                    }
                    else
                    {
                        // Forward transforms - write output based on format
                        switch (m_format)
                        {
                        case OutputFormat::COMPLEX:
                        {
                            // OPTIMIZATION: Calculate numBins once, interleave with loop unrolling
                            size_t numBins = (m_type == TransformType::RFFT || m_type == TransformType::RDFT)
                                                 ? m_engine->getHalfSize()
                                                 : m_fftSize;

                            // OPTIMIZATION: Unroll complex interleaving by 4
                            size_t i = 0;
                            const size_t stride = numChannels;

                            for (; i + 3 < numBins; i += 4)
                            {
                                size_t outIdx0 = (i * 2) * stride;
                                size_t outIdx1 = ((i + 1) * 2) * stride;
                                size_t outIdx2 = ((i + 2) * 2) * stride;
                                size_t outIdx3 = ((i + 3) * 2) * stride;

                                frameOutput[outIdx0] = m_complexBuffer[i].real();
                                frameOutput[outIdx0 + stride] = m_complexBuffer[i].imag();
                                frameOutput[outIdx1] = m_complexBuffer[i + 1].real();
                                frameOutput[outIdx1 + stride] = m_complexBuffer[i + 1].imag();
                                frameOutput[outIdx2] = m_complexBuffer[i + 2].real();
                                frameOutput[outIdx2 + stride] = m_complexBuffer[i + 2].imag();
                                frameOutput[outIdx3] = m_complexBuffer[i + 3].real();
                                frameOutput[outIdx3 + stride] = m_complexBuffer[i + 3].imag();
                            }

                            // Handle remainder
                            for (; i < numBins; ++i)
                            {
                                size_t outIdx = (i * 2) * stride;
                                frameOutput[outIdx] = m_complexBuffer[i].real();
                                frameOutput[outIdx + stride] = m_complexBuffer[i].imag();
                            }
                            break;
                        }
                        case OutputFormat::MAGNITUDE:
                        case OutputFormat::POWER:
                        case OutputFormat::PHASE:
                        {
                            // OPTIMIZATION: Use stack buffer for small sizes, avoid heap allocation
                            size_t numBins = (m_type == TransformType::RFFT || m_type == TransformType::RDFT)
                                                 ? m_engine->getHalfSize()
                                                 : m_fftSize;

                            // Use member realBuffer for temporary storage (already allocated)
                            float *tempOutput = m_realBuffer.data(); // Reuse buffer

                            if (m_format == OutputFormat::MAGNITUDE)
                            {
                                m_engine->getMagnitude(m_complexBuffer.data(), tempOutput, numBins);
                            }
                            else if (m_format == OutputFormat::POWER)
                            {
                                m_engine->getPower(m_complexBuffer.data(), tempOutput, numBins);
                            }
                            else // PHASE
                            {
                                m_engine->getPhase(m_complexBuffer.data(), tempOutput, numBins);
                            }

                            // Write to output with stride (OPTIMIZED: Loop unrolling + special case)
                            if (numChannels == 1)
                            {
                                // Direct memcpy for single channel
                                std::memcpy(frameOutput, tempOutput, numBins * sizeof(float));
                            }
                            else
                            {
                                // Interleave with loop unrolling by 8
                                size_t i = 0;
                                const size_t stride = numChannels;

                                for (; i + 7 < numBins; i += 8)
                                {
                                    frameOutput[i * stride] = tempOutput[i];
                                    frameOutput[(i + 1) * stride] = tempOutput[i + 1];
                                    frameOutput[(i + 2) * stride] = tempOutput[i + 2];
                                    frameOutput[(i + 3) * stride] = tempOutput[i + 3];
                                    frameOutput[(i + 4) * stride] = tempOutput[i + 4];
                                    frameOutput[(i + 5) * stride] = tempOutput[i + 5];
                                    frameOutput[(i + 6) * stride] = tempOutput[i + 6];
                                    frameOutput[(i + 7) * stride] = tempOutput[i + 7];
                                }

                                // Handle remainder
                                for (; i < numBins; ++i)
                                {
                                    frameOutput[i * stride] = tempOutput[i];
                                }
                            }
                            break;
                        }
                        }
                    }
                }
            }
        }

        void FftStage::reset()
        {
            // FFT is stateless, nothing to reset
        }

        Napi::Object FftStage::serializeState(Napi::Env env) const
        {
            // FFT is stateless, save configuration only
            Napi::Object state = Napi::Object::New(env);
            state.Set("fftSize", Napi::Number::New(env, m_fftSize));
            state.Set("type", Napi::Number::New(env, static_cast<int>(m_type)));
            state.Set("forward", Napi::Boolean::New(env, m_forward));
            state.Set("format", Napi::Number::New(env, static_cast<int>(m_format)));
            return state;
        }

        void FftStage::deserializeState(const Napi::Object &state)
        {
            size_t fftSize = state.Get("fftSize").As<Napi::Number>().Uint32Value();

            if (fftSize != m_fftSize)
            {
                throw std::runtime_error("FFT size mismatch during state deserialization");
            }

            m_type = static_cast<TransformType>(state.Get("type").As<Napi::Number>().Int32Value());
            m_forward = state.Get("forward").As<Napi::Boolean>().Value();
            m_format = static_cast<OutputFormat>(state.Get("format").As<Napi::Number>().Int32Value());
        }

        void FftStage::serializeToon(toon::Serializer &serializer) const
        {
            serializer.writeInt32(static_cast<int32_t>(m_fftSize));
            serializer.writeInt32(static_cast<int>(m_type));
            serializer.writeBool(m_forward);
            serializer.writeInt32(static_cast<int>(m_format));
        }

        void FftStage::deserializeToon(toon::Deserializer &deserializer)
        {
            size_t fftSize = static_cast<size_t>(deserializer.readInt32());
            {
                throw std::runtime_error("FFT size mismatch during TOON deserialization");
            }

            m_type = static_cast<TransformType>(deserializer.readInt32());
            m_forward = deserializer.readBool();
            m_format = static_cast<OutputFormat>(deserializer.readInt32());
        }

        FftStage::TransformType FftStage::parseTransformType(const std::string &typeStr)
        {
            if (typeStr == "fft")
                return TransformType::FFT;
            if (typeStr == "ifft")
                return TransformType::IFFT;
            if (typeStr == "dft")
                return TransformType::DFT;
            if (typeStr == "idft")
                return TransformType::IDFT;
            if (typeStr == "rfft")
                return TransformType::RFFT;
            if (typeStr == "irfft")
                return TransformType::IRFFT;
            if (typeStr == "rdft")
                return TransformType::RDFT;
            if (typeStr == "irdft")
                return TransformType::IRDFT;

            throw std::invalid_argument("Unknown transform type: " + typeStr);
        }

        FftStage::OutputFormat FftStage::parseOutputFormat(const std::string &formatStr)
        {
            if (formatStr == "complex")
                return OutputFormat::COMPLEX;
            if (formatStr == "magnitude")
                return OutputFormat::MAGNITUDE;
            if (formatStr == "power")
                return OutputFormat::POWER;
            if (formatStr == "phase")
                return OutputFormat::PHASE;

            throw std::invalid_argument("Unknown output format: " + formatStr);
        }

    } // namespace adapters
} // namespace dsp
