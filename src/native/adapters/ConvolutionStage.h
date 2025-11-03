#pragma once

#include "../IDspStage.h"
#include "../utils/SlidingWindowFilter.h"
#include "../utils/ConvolutionPolicy.h"
#include "../utils/SimdOps.h"
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
         *                      Use FFT for kernels > threshold. For moving mode,
         *                      FFT becomes beneficial at larger sizes (~128+) due to
         *                      overlap-add overhead. For batch mode, FFT can help at smaller sizes.
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
              m_is_initialized(false),
              m_batch_fft_size(0)
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

        // For direct method (stateful) - per-channel history
        std::vector<utils::SlidingWindowFilter<float, utils::ConvolutionPolicy<float>>> m_filters;

        // High-performance linear buffers for moving mode direct convolution
        // These avoid the circular buffer overhead by using a linear sliding approach
        std::vector<std::vector<float>> m_linear_history_buffers; // Per-channel linear buffers
        std::vector<size_t> m_history_fill_count;                 // Track how many samples in each buffer

        // For FFT overlap-add method (stateful)
        std::vector<std::vector<float>> m_overlap_buffers;    // One per channel
        std::vector<std::complex<float>> m_kernel_fft;        // Precomputed kernel FFT
        size_t m_fft_size;                                    // FFT size for overlap-add
        std::unique_ptr<core::FftEngine<float>> m_fft_engine; // Reusable FFT engine

        // Pre-allocated buffers for FFT operations (avoid per-call allocations)
        std::vector<float> m_fft_input_buffer;
        std::vector<std::complex<float>> m_fft_freq_buffer;
        std::vector<float> m_fft_output_buffer;

        // Pre-allocated buffers for batch operations
        std::vector<float> m_temp_batch_buffer;
        std::vector<float> m_batch_kernel_padded;
        std::vector<std::complex<float>> m_batch_kernel_fft;
        std::vector<float> m_batch_channel_data;
        std::vector<std::complex<float>> m_batch_signal_fft;
        std::vector<float> m_batch_result;
        size_t m_batch_fft_size;

        // De-interleaved buffers for cache-friendly processing
        std::vector<std::vector<float>> m_deinterleaved_buffers;
        std::vector<float> m_temp_output_channel;
        std::vector<float> m_reversed_window; // Reversed window for SIMD convolution

        // Reusable buffer for moving mode direct convolution (eliminates per-sample allocation)
        mutable std::vector<float> m_moving_temp_buffer;

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
         * @brief Direct convolution in moving mode - Ultra high-performance version.
         *
         * This implementation eliminates ALL circular buffer overhead by using
         * a linear sliding buffer approach. This is the "naive JS" algorithm
         * implemented in C++ with SIMD.
         *
         * Key optimization: Instead of circular buffers with wrapping and copying,
         * we use a simple linear buffer and shift elements when needed. This allows
         * the SIMD dot product to operate directly on contiguous memory.
         */
        void processMovingDirect(float *buffer, size_t numSamples, int numChannels)
        {
            size_t samplesPerChannel = numSamples / numChannels;
            size_t kernelSize = m_kernel.size();

            // Initialize linear history buffers if needed
            if (m_linear_history_buffers.size() != static_cast<size_t>(numChannels))
            {
                m_linear_history_buffers.resize(numChannels);
                m_history_fill_count.resize(numChannels, 0);
                for (auto &buf : m_linear_history_buffers)
                {
                    buf.resize(kernelSize, 0.0f);
                }
            }

            // Pre-reverse the kernel ONCE (not per channel, not per sample!)
            std::vector<float> kernelReversed(m_kernel.rbegin(), m_kernel.rend());
            const float *kernelPtr = kernelReversed.data();

            // Process each channel
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto &history = m_linear_history_buffers[ch];
                size_t &fillCount = m_history_fill_count[ch];

                // Process all samples for this channel
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    float sample = buffer[idx];

                    // Shift the history buffer left by one (oldest sample drops off)
                    // For very small kernels, manual shifting can be faster than memmove
                    if (kernelSize <= 16)
                    {
                        // Manual unrolled shift for small kernels (avoids memmove overhead)
                        for (size_t k = 0; k < kernelSize - 1; ++k)
                        {
                            history[k] = history[k + 1];
                        }
                    }
                    else
                    {
                        // Use optimized memmove for larger kernels
                        std::memmove(history.data(), history.data() + 1, (kernelSize - 1) * sizeof(float));
                    }

                    // Add new sample at the end
                    history[kernelSize - 1] = sample;

                    // Track how many samples we've seen
                    if (fillCount < kernelSize)
                    {
                        fillCount++;
                    }

                    // Only compute when we have enough history
                    if (fillCount >= kernelSize)
                    {
                        // SIMD dot product directly on the linear buffer (NO COPY!)
                        float result = simd::dot_product(kernelPtr, history.data(), kernelSize);
                        buffer[idx] = result;
                    }
                    else
                    {
                        buffer[idx] = 0.0f;
                    }
                }
            }
        }

        /**
         * @brief Direct convolution in batch mode - TRUE "naive" C++ algorithm.
         *
         * This is the apples-to-apples comparison with the "naive JS" benchmark.
         * It's a simple, stateless loop with NO circular buffers, NO window reversing,
         * and NO per-sample memory copies. Just like the naive JS code, but with
         * de-interleaving for cache-friendly processing and SIMD for the inner loop.
         *
         * For very small kernels (K=8), the simplicity of this algorithm allows the
         * C++ compiler to optimize it aggressively (loop unrolling, SIMD vectorization).
         */
        void processBatchDirect(float *buffer, size_t numSamples, int numChannels)
        {
            size_t samplesPerChannel = numSamples / numChannels;
            size_t kernelSize = m_kernel.size();

            // Resize de-interleaved buffers if channel count changed
            if (m_deinterleaved_buffers.size() != static_cast<size_t>(numChannels))
            {
                m_deinterleaved_buffers.resize(numChannels);
                for (auto &buf : m_deinterleaved_buffers)
                {
                    buf.resize(samplesPerChannel);
                }
            }
            else if (!m_deinterleaved_buffers.empty() &&
                     m_deinterleaved_buffers[0].size() != samplesPerChannel)
            {
                for (auto &buf : m_deinterleaved_buffers)
                {
                    buf.resize(samplesPerChannel);
                }
            }

            // Resize output buffer
            if (m_temp_output_channel.size() != samplesPerChannel)
            {
                m_temp_output_channel.resize(samplesPerChannel);
            }

            // Resize reversed window buffer for SIMD (only allocate once)
            if (m_reversed_window.size() != kernelSize)
            {
                m_reversed_window.resize(kernelSize);
            }

            // Step 1: De-interleave input data for cache-friendly processing
            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    m_deinterleaved_buffers[ch][i] = buffer[i * numChannels + ch];
                }
            }

            // Step 2: Process each channel - optimized batch convolution
            const float *kernelPtr = m_kernel.data();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float *channelInput = m_deinterleaved_buffers[ch].data();
                float *output = m_temp_output_channel.data();

                // Standard convolution: y[n] = sum(h[k] * x[n-k])
                // For small K: simple loop (compiler auto-vectorizes)
                // For large K: use explicit SIMD with window collection
                for (size_t n = 0; n < samplesPerChannel; ++n)
                {
                    float sum = 0.0f;

                    if (kernelSize <= 16)
                    {
                        // Small kernels: tight loop, compiler auto-vectorizes
                        for (size_t k = 0; k < kernelSize; ++k)
                        {
                            if (n >= k)
                            {
                                sum += kernelPtr[k] * channelInput[n - k];
                            }
                        }
                    }
                    else
                    {
                        // Large kernels: collect window and use SIMD dot product
                        // This avoids the performance cliff from backward indexing
                        if (n >= kernelSize - 1)
                        {
                            // Full window available - collect in forward order for SIMD
                            for (size_t k = 0; k < kernelSize; ++k)
                            {
                                m_reversed_window[k] = channelInput[n - k];
                            }
                            sum = simd::dot_product(kernelPtr, m_reversed_window.data(), kernelSize);
                        }
                        else
                        {
                            // Partial window at the start
                            for (size_t k = 0; k <= n; ++k)
                            {
                                sum += kernelPtr[k] * channelInput[n - k];
                            }
                        }
                    }

                    output[n] = sum;
                }

                // Step 3: Re-interleave output back to original buffer
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    buffer[i * numChannels + ch] = output[i];
                }
            }
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

                // Pre-allocate FFT working buffers (eliminates per-chunk allocations)
                m_fft_input_buffer.resize(m_fft_size);
                m_fft_freq_buffer.resize(halfSize);
                m_fft_output_buffer.resize(m_fft_size);

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

            // Chunk size: process kernelSize samples at a time (FFT size = 2*kernelSize)
            size_t chunkSize = kernelSize;

            // Use pre-allocated buffers (no allocations in the loop!)

            // Process each channel independently
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Process signal in chunks
                for (size_t pos = 0; pos < samplesPerChannel; pos += chunkSize)
                {
                    size_t currentChunkSize = std::min(chunkSize, samplesPerChannel - pos);

                    // Zero-pad to FFT size using pre-allocated buffer
                    std::fill(m_fft_input_buffer.begin(), m_fft_input_buffer.end(), 0.0f);
                    for (size_t i = 0; i < currentChunkSize; ++i)
                    {
                        m_fft_input_buffer[i] = buffer[(pos + i) * numChannels + ch];
                    }

                    // FFT of input chunk
                    m_fft_engine->rfft(m_fft_input_buffer.data(), m_fft_freq_buffer.data());

                    // Complex multiplication in frequency domain
                    for (size_t i = 0; i < halfSize; ++i)
                    {
                        m_fft_freq_buffer[i] *= m_kernel_fft[i];
                    }

                    // IFFT to get convolution result
                    m_fft_engine->irfft(m_fft_freq_buffer.data(), m_fft_output_buffer.data());

                    // Overlap-add: add the tail from the previous chunk
                    for (size_t i = 0; i < kernelSize - 1 && i < currentChunkSize; ++i)
                    {
                        m_fft_output_buffer[i] += m_overlap_buffers[ch][i];
                    }

                    // Save the tail for the next chunk
                    std::fill(m_overlap_buffers[ch].begin(), m_overlap_buffers[ch].end(), 0.0f);
                    for (size_t i = 0; i < kernelSize - 1 && (currentChunkSize + i) < m_fft_size; ++i)
                    {
                        m_overlap_buffers[ch][i] = m_fft_output_buffer[currentChunkSize + i];
                    }

                    // Copy result back to interleaved buffer (only valid samples)
                    for (size_t i = 0; i < currentChunkSize; ++i)
                    {
                        buffer[(pos + i) * numChannels + ch] = m_fft_output_buffer[i];
                    }
                }
            }
        } /**
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

            // Resize buffers only if FFT size changed (avoid reallocation on every call)
            if (m_batch_fft_size != fftSizePow2)
            {
                m_batch_fft_size = fftSizePow2;
                size_t halfSize = fftSizePow2 / 2 + 1;

                // Create/resize FFT engine
                m_fft_engine = std::make_unique<core::FftEngine<float>>(fftSizePow2);

                // Pre-allocate buffers
                m_batch_kernel_padded.resize(fftSizePow2);
                m_batch_kernel_fft.resize(halfSize);
                m_batch_channel_data.resize(fftSizePow2);
                m_batch_signal_fft.resize(halfSize);
                m_batch_result.resize(fftSizePow2);

                // Zero-pad kernel and compute FFT (only once per size)
                std::fill(m_batch_kernel_padded.begin(), m_batch_kernel_padded.end(), 0.0f);
                std::copy(m_kernel.begin(), m_kernel.end(), m_batch_kernel_padded.begin());
                m_fft_engine->rfft(m_batch_kernel_padded.data(), m_batch_kernel_fft.data());
            }

            size_t halfSize = m_batch_fft_size / 2 + 1;

            // Resize de-interleaved buffers if needed
            if (m_deinterleaved_buffers.size() != static_cast<size_t>(numChannels))
            {
                m_deinterleaved_buffers.resize(numChannels);
                for (auto &buf : m_deinterleaved_buffers)
                {
                    buf.resize(samplesPerChannel);
                }
            }
            else if (!m_deinterleaved_buffers.empty() &&
                     m_deinterleaved_buffers[0].size() != samplesPerChannel)
            {
                for (auto &buf : m_deinterleaved_buffers)
                {
                    buf.resize(samplesPerChannel);
                }
            }

            // Resize output buffer
            if (m_temp_output_channel.size() != samplesPerChannel)
            {
                m_temp_output_channel.resize(samplesPerChannel);
            }

            // Step 1: De-interleave all input data (improves cache locality)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    m_deinterleaved_buffers[ch][i] = buffer[i * numChannels + ch];
                }
            }

            // Step 2: Process each channel on contiguous memory
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Zero-pad channel data using pre-allocated buffer
                std::fill(m_batch_channel_data.begin(), m_batch_channel_data.end(), 0.0f);
                std::copy(m_deinterleaved_buffers[ch].begin(),
                          m_deinterleaved_buffers[ch].end(),
                          m_batch_channel_data.begin());

                // FFT of signal
                m_fft_engine->rfft(m_batch_channel_data.data(), m_batch_signal_fft.data());

                // Complex multiplication in frequency domain
                for (size_t i = 0; i < halfSize; ++i)
                {
                    m_batch_signal_fft[i] *= m_batch_kernel_fft[i];
                }

                // IFFT to get convolution result
                m_fft_engine->irfft(m_batch_signal_fft.data(), m_batch_result.data());

                // Store in output buffer
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    m_temp_output_channel[i] = m_batch_result[i];
                }

                // Step 3: Re-interleave output back to original buffer
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    buffer[i * numChannels + ch] = m_temp_output_channel[i];
                }
            }
        }
    };

} // namespace dsp::adapters
