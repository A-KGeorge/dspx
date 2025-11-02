#pragma once

#include "../IDspStage.h"
#include "../utils/SlidingWindowFilter.h"
#include "../utils/ConvolutionPolicy.h"
#include "../core/FftEngine.h"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

namespace dsp::adapters
{
    enum class ConvolutionMode
    {
        Moving, // Stateful, streaming convolution
        Batch   // Stateless, batch convolution
    };

    enum class ConvolutionMethod
    {
        Auto,   // Smart selection between direct and FFT
        Direct, // Time-domain convolution (O(N*M))
        FFT     // Frequency-domain convolution (O(N*logN))
    };

    class ConvolutionStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a convolution stage.
         * @param kernel The convolution kernel.
         * @param mode Moving (stateful) or Batch (stateless).
         * @param method Auto, Direct, or FFT.
         * @param autoThreshold Kernel size threshold for auto mode (default: 64).
         */
        explicit ConvolutionStage(
            const std::vector<float> &kernel,
            ConvolutionMode mode = ConvolutionMode::Moving,
            ConvolutionMethod method = ConvolutionMethod::Auto,
            size_t autoThreshold = 64)
            : m_kernel(kernel),
              m_mode(mode),
              m_method(method),
              m_autoThreshold(autoThreshold),
              m_is_initialized(false)
        {
            if (kernel.empty())
            {
                throw std::invalid_argument("Convolution kernel cannot be empty");
            }

            // Determine actual method to use
            m_actualMethod = determineMethod();
        }

        const char *getType() const override
        {
            return "convolution";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == ConvolutionMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // ConvolutionMode::Moving
            {
                processMoving(buffer, numSamples, numChannels);
            }
        }

        void reset() override
        {
            if (m_mode == ConvolutionMode::Moving)
            {
                m_filters.clear();
                m_overlap_buffers.clear();
                m_is_initialized = false;
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == ConvolutionMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);

            std::string methodStr;
            switch (m_method)
            {
            case ConvolutionMethod::Auto:
                methodStr = "auto";
                break;
            case ConvolutionMethod::Direct:
                methodStr = "direct";
                break;
            case ConvolutionMethod::FFT:
                methodStr = "fft";
                break;
            }
            state.Set("method", methodStr);

            // Serialize kernel
            Napi::Array kernelArray = Napi::Array::New(env, m_kernel.size());
            for (size_t i = 0; i < m_kernel.size(); ++i)
            {
                kernelArray.Set(i, Napi::Number::New(env, m_kernel[i]));
            }
            state.Set("kernel", kernelArray);

            if (m_mode == ConvolutionMode::Moving && m_actualMethod == ConvolutionMethod::Direct)
            {
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the sliding window filter's state (returns std::pair)
                    auto filterState = m_filters[i].getState();
                    const auto &bufferData = filterState.first;

                    // Serialize buffer
                    Napi::Array bufferArray = Napi::Array::New(env, bufferData.size());
                    for (size_t j = 0; j < bufferData.size(); ++j)
                    {
                        bufferArray.Set(j, bufferData[j]);
                    }

                    channelState.Set("buffer", bufferArray);

                    channelsArray.Set(static_cast<uint32_t>(i), channelState);
                }
                state.Set("channels", channelsArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (m_mode == ConvolutionMode::Moving && m_actualMethod == ConvolutionMethod::Direct)
            {
                if (!state.Has("channels"))
                {
                    return;
                }

                Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
                uint32_t numChannels = channelsArray.Length();

                // Initialize filters if needed
                initializeFilters(static_cast<int>(numChannels));

                // Restore state for each channel
                for (uint32_t i = 0; i < numChannels && i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                    Napi::Array bufferArray = channelState.Get("buffer").As<Napi::Array>();
                    std::vector<float> bufferData(bufferArray.Length());
                    for (uint32_t j = 0; j < bufferArray.Length(); ++j)
                    {
                        bufferData[j] = bufferArray.Get(j).As<Napi::Number>().FloatValue();
                    }

                    // ConvolutionPolicy has empty state, so just pass the empty state
                    typename utils::ConvolutionPolicy<float>::EmptyState policyState;
                    m_filters[i].setState(bufferData, policyState);
                }
            }
        }

    private:
        std::vector<float> m_kernel;
        ConvolutionMode m_mode;
        ConvolutionMethod m_method;
        ConvolutionMethod m_actualMethod;
        size_t m_autoThreshold;
        bool m_is_initialized;

        // For direct method (stateful)
        std::vector<utils::SlidingWindowFilter<float, utils::ConvolutionPolicy<float>>> m_filters;

        // For FFT overlap-add method (stateful)
        std::vector<std::vector<float>> m_overlap_buffers;    // One per channel
        std::vector<std::complex<float>> m_kernel_fft;        // Precomputed kernel FFT
        size_t m_fft_size;                                    // FFT size for overlap-add
        std::unique_ptr<core::FftEngine<float>> m_fft_engine; // Reusable FFT engine

        /**
         * @brief Determine which method to actually use.
         */
        ConvolutionMethod determineMethod() const
        {
            if (m_method == ConvolutionMethod::Auto)
            {
                // Use direct convolution for small kernels, FFT for large kernels
                return (m_kernel.size() <= m_autoThreshold) ? ConvolutionMethod::Direct : ConvolutionMethod::FFT;
            }
            return m_method;
        }

        /**
         * @brief Initialize filters for each channel.
         */
        void initializeFilters(int numChannels)
        {
            if (!m_is_initialized || static_cast<int>(m_filters.size()) != numChannels)
            {
                m_filters.clear();
                m_filters.reserve(numChannels);

                for (int i = 0; i < numChannels; ++i)
                {
                    // Create policy with kernel
                    utils::ConvolutionPolicy<float> policy(m_kernel);

                    // Create sliding window filter with the policy
                    m_filters.emplace_back(m_kernel.size(), std::move(policy));
                }

                m_is_initialized = true;
            }
        }

        /**
         * @brief Process in moving (stateful) mode.
         */
        void processMoving(float *buffer, size_t numSamples, int numChannels)
        {
            if (m_actualMethod == ConvolutionMethod::Direct)
            {
                processMovingDirect(buffer, numSamples, numChannels);
            }
            else // FFT
            {
                processMovingFFT(buffer, numSamples, numChannels);
            }
        }

        /**
         * @brief Process in batch (stateless) mode.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            if (m_actualMethod == ConvolutionMethod::Direct)
            {
                processBatchDirect(buffer, numSamples, numChannels);
            }
            else // FFT
            {
                processBatchFFT(buffer, numSamples, numChannels);
            }
        }

        /**
         * @brief Direct convolution in moving mode.
         */
        void processMovingDirect(float *buffer, size_t numSamples, int numChannels)
        {
            initializeFilters(numChannels);

            size_t samplesPerChannel = numSamples / numChannels;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float sample = buffer[idx];

                    // Add sample to the sliding window
                    m_filters[ch].addSample(sample);

                    // Now manually compute convolution from the buffer
                    auto bufferContents = m_filters[ch].getBufferContents();

                    if (bufferContents.size() == m_kernel.size())
                    {
                        float result = m_filters[ch].getPolicy().calculate(bufferContents.data(), bufferContents.size());
                        buffer[idx] = result;
                    }
                    else
                    {
                        // Not enough samples yet - output zero or the input
                        buffer[idx] = 0.0f;
                    }
                }
            }
        } /**
           * @brief Direct convolution in batch mode.
           */
        void processBatchDirect(float *buffer, size_t numSamples, int numChannels)
        {
            size_t samplesPerChannel = numSamples / numChannels;
            size_t kernelSize = m_kernel.size();

            // Create temporary output buffer
            std::vector<float> tempOutput(numSamples, 0.0f);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Standard convolution: y[n] = sum(h[k] * x[n-k])
                for (size_t n = 0; n < samplesPerChannel; ++n)
                {
                    float sum = 0.0f;

                    for (size_t k = 0; k < kernelSize; ++k)
                    {
                        int inputIdx = static_cast<int>(n) - static_cast<int>(k);
                        if (inputIdx >= 0)
                        {
                            size_t bufferIdx = inputIdx * numChannels + ch;
                            sum += m_kernel[k] * buffer[bufferIdx];
                        }
                    }

                    tempOutput[n * numChannels + ch] = sum;
                }
            }

            // Copy result back
            std::copy(tempOutput.begin(), tempOutput.end(), buffer);
        }

        /**
         * @brief Initialize overlap-add FFT state for each channel.
         */
        void initializeOverlapAdd(int numChannels)
        {
            if (!m_is_initialized || static_cast<int>(m_overlap_buffers.size()) != numChannels)
            {
                size_t kernelSize = m_kernel.size();

                // Choose FFT size: next power of 2 >= 2 * kernelSize
                // This allows processing chunks of kernelSize samples
                m_fft_size = 1;
                while (m_fft_size < 2 * kernelSize)
                {
                    m_fft_size *= 2;
                }

                // Create FFT engine
                m_fft_engine = std::make_unique<core::FftEngine<float>>(m_fft_size);

                // Precompute kernel FFT
                size_t halfSize = m_fft_size / 2 + 1;
                m_kernel_fft.resize(halfSize);

                std::vector<float> kernelPadded(m_fft_size, 0.0f);
                std::copy(m_kernel.begin(), m_kernel.end(), kernelPadded.begin());
                m_fft_engine->rfft(kernelPadded.data(), m_kernel_fft.data());

                // Initialize overlap buffers (store tail from previous chunk)
                m_overlap_buffers.clear();
                m_overlap_buffers.resize(numChannels, std::vector<float>(kernelSize - 1, 0.0f));

                m_is_initialized = true;
            }
        }

        /**
         * @brief FFT convolution in moving mode using overlap-add method.
         *
         * Overlap-add divides the input into chunks and convolves each chunk,
         * then adds the overlapping tails from previous chunks.
         */
        void processMovingFFT(float *buffer, size_t numSamples, int numChannels)
        {
            initializeOverlapAdd(numChannels);

            size_t samplesPerChannel = numSamples / numChannels;
            size_t kernelSize = m_kernel.size();
            size_t halfSize = m_fft_size / 2 + 1;

            // Process each channel independently
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Extract channel data
                std::vector<float> channelData(samplesPerChannel);
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    channelData[i] = buffer[i * numChannels + ch];
                }

                // Zero-pad to FFT size
                std::vector<float> inputPadded(m_fft_size, 0.0f);
                std::copy(channelData.begin(), channelData.end(), inputPadded.begin());

                // FFT of input chunk
                std::vector<std::complex<float>> inputFFT(halfSize);
                m_fft_engine->rfft(inputPadded.data(), inputFFT.data());

                // Complex multiplication in frequency domain
                for (size_t i = 0; i < halfSize; ++i)
                {
                    inputFFT[i] *= m_kernel_fft[i];
                }

                // IFFT to get convolution result
                std::vector<float> convResult(m_fft_size);
                m_fft_engine->irfft(inputFFT.data(), convResult.data());

                // Overlap-add: add the tail from the previous chunk
                for (size_t i = 0; i < kernelSize - 1 && i < samplesPerChannel; ++i)
                {
                    convResult[i] += m_overlap_buffers[ch][i];
                }

                // Save the tail for the next chunk
                std::fill(m_overlap_buffers[ch].begin(), m_overlap_buffers[ch].end(), 0.0f);
                for (size_t i = 0; i < kernelSize - 1 && (samplesPerChannel + i) < m_fft_size; ++i)
                {
                    m_overlap_buffers[ch][i] = convResult[samplesPerChannel + i];
                }

                // Copy result back to interleaved buffer
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    buffer[i * numChannels + ch] = convResult[i];
                }
            }
        }

        /**
         * @brief FFT convolution in batch mode.
         *
         * Uses FFT-based fast convolution: FFT(x) * FFT(h) -> IFFT
         */
        void processBatchFFT(float *buffer, size_t numSamples, int numChannels)
        {
            size_t samplesPerChannel = numSamples / numChannels;
            size_t kernelSize = m_kernel.size();

            // FFT convolution size must be at least N + M - 1
            size_t fftSize = samplesPerChannel + kernelSize - 1;

            // Round up to next power of 2 for efficiency
            size_t fftSizePow2 = 1;
            while (fftSizePow2 < fftSize)
            {
                fftSizePow2 *= 2;
            }

            // Create FFT engine
            core::FftEngine<float> fftEngine(fftSizePow2);

            // Zero-pad kernel to FFT size
            std::vector<float> kernelPadded(fftSizePow2, 0.0f);
            std::copy(m_kernel.begin(), m_kernel.end(), kernelPadded.begin());

            // Compute FFT of kernel (only once)
            size_t halfSize = fftSizePow2 / 2 + 1;
            std::vector<std::complex<float>> kernelFFT(halfSize);
            fftEngine.rfft(kernelPadded.data(), kernelFFT.data());

            // Process each channel
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Extract channel data and zero-pad
                std::vector<float> channelData(fftSizePow2, 0.0f);
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    channelData[i] = buffer[i * numChannels + ch];
                }

                // FFT of signal
                std::vector<std::complex<float>> signalFFT(halfSize);
                fftEngine.rfft(channelData.data(), signalFFT.data());

                // Complex multiplication in frequency domain
                for (size_t i = 0; i < halfSize; ++i)
                {
                    signalFFT[i] *= kernelFFT[i];
                }

                // IFFT to get convolution result
                std::vector<float> result(fftSizePow2);
                fftEngine.irfft(signalFFT.data(), result.data());

                // Copy valid portion back (same size as input for 'same' mode)
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    buffer[i * numChannels + ch] = result[i];
                }
            }
        }
    };

} // namespace dsp::adapters
