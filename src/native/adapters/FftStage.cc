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

            for (size_t frame = 0; frame < numFrames; ++frame)
            {
                for (size_t ch = 0; ch < static_cast<size_t>(numChannels); ++ch)
                {
                    const float *frameInput = inputBuffer + (frame * inputSizePerFrame * numChannels) + ch;
                    float *frameOutput = outputBuffer + (frame * outputSizePerFrame * numChannels) + ch;

                    // Load input based on transform direction
                    if (isInverseComplex)
                    {
                        // Input is complex interleaved: [real0, imag0, real1, imag1, ...]
                        for (size_t i = 0; i < m_fftSize; ++i)
                        {
                            m_complexBuffer[i] = std::complex<float>(
                                frameInput[(i * 2) * numChannels],      // real part
                                frameInput[(i * 2 + 1) * numChannels]); // imag part
                        }
                    }
                    else if (isInverseReal)
                    {
                        // Input is half-spectrum complex
                        size_t halfSize = m_engine->getHalfSize();
                        for (size_t i = 0; i < halfSize; ++i)
                        {
                            m_complexBuffer[i] = std::complex<float>(
                                frameInput[(i * 2) * numChannels],
                                frameInput[(i * 2 + 1) * numChannels]);
                        }
                    }
                    else
                    {
                        // Deinterleave real channel data
                        for (size_t i = 0; i < m_fftSize; ++i)
                        {
                            m_realBuffer[i] = frameInput[i * numChannels];
                        }
                    }

                    // Perform transform based on type and direction
                    switch (m_type)
                    {
                    case TransformType::FFT:
                    {
                        if (m_forward)
                        {
                            // Forward FFT: real to complex
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                m_complexBuffer[i] = std::complex<float>(m_realBuffer[i], 0.0f);
                            }
                            m_engine->fft(m_complexBuffer.data(), m_complexBuffer.data());
                        }
                        else
                        {
                            // Inverse FFT: complex input already loaded
                            m_engine->ifft(m_complexBuffer.data(), m_complexBuffer.data());
                        }
                        break;
                    }
                    case TransformType::IFFT:
                    {
                        // Complex input already loaded into m_complexBuffer
                        m_engine->ifft(m_complexBuffer.data(), m_complexBuffer.data());
                        break;
                    }
                    case TransformType::DFT:
                    {
                        if (m_forward)
                        {
                            // Forward DFT: real to complex
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                m_complexBuffer[i] = std::complex<float>(m_realBuffer[i], 0.0f);
                            }
                            m_engine->dft(m_complexBuffer.data(), m_tempComplexBuffer.data());
                            std::copy(m_tempComplexBuffer.begin(), m_tempComplexBuffer.end(), m_complexBuffer.begin());
                        }
                        else
                        {
                            // Inverse DFT: complex input already loaded
                            m_engine->idft(m_complexBuffer.data(), m_tempComplexBuffer.data());
                            std::copy(m_tempComplexBuffer.begin(), m_tempComplexBuffer.end(), m_complexBuffer.begin());
                        }
                        break;
                    }
                    case TransformType::IDFT:
                    {
                        // Complex input already loaded into m_complexBuffer
                        m_engine->idft(m_complexBuffer.data(), m_tempComplexBuffer.data());
                        std::copy(m_tempComplexBuffer.begin(), m_tempComplexBuffer.end(), m_complexBuffer.begin());
                        break;
                    }
                    case TransformType::RFFT:
                    {
                        if (m_forward)
                        {
                            m_engine->rfft(m_realBuffer.data(), m_complexBuffer.data());
                        }
                        else
                        {
                            // Inverse RFFT: half-spectrum complex input already loaded
                            m_engine->irfft(m_complexBuffer.data(), m_realBuffer.data());
                        }
                        break;
                    }
                    case TransformType::IRFFT:
                    {
                        // Half-spectrum complex input already loaded into m_complexBuffer
                        m_engine->irfft(m_complexBuffer.data(), m_realBuffer.data());
                        break;
                    }
                    case TransformType::RDFT:
                    {
                        if (m_forward)
                        {
                            m_engine->rdft(m_realBuffer.data(), m_complexBuffer.data());
                        }
                        else
                        {
                            // Inverse RDFT: half-spectrum complex input already loaded
                            m_engine->irdft(m_complexBuffer.data(), m_realBuffer.data());
                        }
                        break;
                    }
                    case TransformType::IRDFT:
                    {
                        // Half-spectrum complex input already loaded into m_complexBuffer
                        m_engine->irdft(m_complexBuffer.data(), m_realBuffer.data());
                        break;
                    }
                    }

                    // Convert to output format
                    if (isInverseReal || isInverseComplex)
                    {
                        // ALL inverse transforms output real time-domain values
                        if (isInverseReal)
                        {
                            // Real inverse transforms: use m_realBuffer directly
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                frameOutput[i * numChannels] = m_realBuffer[i];
                            }
                        }
                        else
                        {
                            // IFFT/IDFT: extract real parts from complex result
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                frameOutput[i * numChannels] = m_complexBuffer[i].real();
                            }
                        }
                    }
                    else
                    {
                        // Forward transforms only
                        switch (m_format)
                        {
                        case OutputFormat::COMPLEX:
                        {
                            // Interleave real/imag
                            for (size_t i = 0; i < m_fftSize; ++i)
                            {
                                frameOutput[(i * 2) * numChannels] = m_complexBuffer[i].real();
                                frameOutput[(i * 2 + 1) * numChannels] = m_complexBuffer[i].imag();
                            }
                            break;
                        }
                        case OutputFormat::MAGNITUDE:
                        {
                            size_t numBins = (m_type == TransformType::RFFT || m_type == TransformType::RDFT)
                                                 ? m_engine->getHalfSize()
                                                 : m_fftSize;
                            std::vector<float> magnitudes(numBins);
                            m_engine->getMagnitude(m_complexBuffer.data(), magnitudes.data(), numBins);
                            for (size_t i = 0; i < numBins; ++i)
                            {
                                frameOutput[i * numChannels] = magnitudes[i];
                            }
                            break;
                        }
                        case OutputFormat::POWER:
                        {
                            size_t numBins = (m_type == TransformType::RFFT || m_type == TransformType::RDFT)
                                                 ? m_engine->getHalfSize()
                                                 : m_fftSize;
                            std::vector<float> power(numBins);
                            m_engine->getPower(m_complexBuffer.data(), power.data(), numBins);
                            for (size_t i = 0; i < numBins; ++i)
                            {
                                frameOutput[i * numChannels] = power[i];
                            }
                            break;
                        }
                        case OutputFormat::PHASE:
                        {
                            size_t numBins = (m_type == TransformType::RFFT || m_type == TransformType::RDFT)
                                                 ? m_engine->getHalfSize()
                                                 : m_fftSize;
                            std::vector<float> phases(numBins);
                            m_engine->getPhase(m_complexBuffer.data(), phases.data(), numBins);
                            for (size_t i = 0; i < numBins; ++i)
                            {
                                frameOutput[i * numChannels] = phases[i];
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
