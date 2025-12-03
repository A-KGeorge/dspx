#pragma once

#include "../IDspStage.h"
#include "../utils/SlidingWindowFilter.h"
#include "../utils/ConvolutionPolicy.h"
#include "../utils/SimdOps.h"
#include "../core/FftEngine.h"
#include "../core/FirFilterNeon.h"
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

        /**
         * @brief Batch mode convolution resizes the buffer (N -> N - M + 1).
         *        Moving mode does not resize (N -> N).
         */
        bool isResizing() const override
        {
            return (m_mode == ConvolutionMode::Batch);
        }

        /**
         * @brief Calculate output size for batch mode valid convolution.
         *        Output length = (N - M + 1) * channels, where N is samplesPerChannel and M is kernel size.
         */
        size_t calculateOutputSize(size_t inputSize) const override
        {
            if (m_mode == ConvolutionMode::Moving)
            {
                return inputSize; // Same size output
            }

            // Batch mode: valid convolution reduces size
            // inputSize is total samples (numSamples * numChannels)
            // We'll handle the actual calculation in processResizing
            return inputSize; // Will be recalculated with proper channel info in processResizing
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == ConvolutionMode::Batch)
            {
                // Batch mode is handled by processResizing() since it resizes the buffer
                // This should not be called for batch mode if isResizing() returns true
                processBatch(buffer, numSamples, numChannels);
            }
            else // ConvolutionMode::Moving
            {
                processMoving(buffer, numSamples, numChannels);
            }
        }

        /**
         * @brief Process with buffer resizing (for batch mode convolution).
         *        Computes valid convolution where output length = N - M + 1.
         */
        void processResizing(const float *inputBuffer, size_t inputSize,
                             float *outputBuffer, size_t &outputSize,
                             int numChannels, const float *timestamps = nullptr) override
        {
            size_t samplesPerChannel = inputSize / numChannels;
            size_t kernelSize = m_kernel.size();

            // Valid convolution output length: N - M + 1
            if (samplesPerChannel < kernelSize)
            {
                // Not enough samples for valid convolution
                outputSize = 0;
                return;
            }

            size_t outputSamplesPerChannel = samplesPerChannel - kernelSize + 1;
            outputSize = outputSamplesPerChannel * numChannels;

            // Copy input to temporary buffer (we'll process from inputBuffer to outputBuffer)
            // For batch mode, we use the batch convolution logic
            if (m_actualMethod == ConvolutionMethod::Direct)
            {
                processResizingDirect(inputBuffer, samplesPerChannel, outputBuffer, outputSamplesPerChannel, numChannels);
            }
            else // FFT
            {
                processResizingFFT(inputBuffer, samplesPerChannel, outputBuffer, outputSamplesPerChannel, numChannels);
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

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.startObject();

            s.writeString("mode");
            s.writeString((m_mode == ConvolutionMode::Moving) ? "moving" : "batch");

            s.writeString("method");
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
            s.writeString(methodStr);

            s.writeString("kernel");
            s.writeFloatArray(m_kernel);

            if (m_mode == ConvolutionMode::Moving && m_actualMethod == ConvolutionMethod::Direct)
            {
                s.writeString("channels");
                s.startArray();

                for (const auto &filter : m_filters)
                {
                    s.startObject();
                    auto filterState = filter.getState();
                    const auto &bufferData = filterState.first;

                    s.writeString("buffer");
                    s.writeFloatArray(bufferData);
                    s.endObject();
                }
                s.endArray();
            }

            s.endObject();
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            d.consumeToken(dsp::toon::T_OBJECT_START);

            std::string key = d.readString(); // "mode"
            std::string modeStr = d.readString();
            ConvolutionMode newMode = (modeStr == "moving") ? ConvolutionMode::Moving : ConvolutionMode::Batch;
            if (newMode != m_mode)
            {
                throw std::runtime_error("Convolution mode mismatch during TOON deserialization");
            }

            key = d.readString(); // "method"
            d.readString();       // Skip method validation

            key = d.readString(); // "kernel"
            std::vector<float> kernel = d.readFloatArray();
            if (kernel.size() != m_kernel.size())
            {
                throw std::runtime_error("Convolution kernel size mismatch during TOON deserialization");
            }

            if (modeStr == "moving" && m_actualMethod == ConvolutionMethod::Direct)
            {
                key = d.readString(); // "channels"
                d.consumeToken(dsp::toon::T_ARRAY_START);

                m_filters.clear();

                while (d.peekToken() != dsp::toon::T_ARRAY_END)
                {
                    d.consumeToken(dsp::toon::T_OBJECT_START);

                    d.readString(); // "buffer"
                    std::vector<float> bufferData = d.readFloatArray();

                    // Create policy with kernel
                    utils::ConvolutionPolicy<float> policy(m_kernel);
                    m_filters.emplace_back(m_kernel.size(), std::move(policy));

                    // Restore state
                    typename utils::ConvolutionPolicy<float>::EmptyState policyState;
                    m_filters.back().setState(bufferData, policyState);

                    d.consumeToken(dsp::toon::T_OBJECT_END);
                }
                d.consumeToken(dsp::toon::T_ARRAY_END);

                m_is_initialized = true;
            }

            d.consumeToken(dsp::toon::T_OBJECT_END);
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

        // High-performance FIR filters for batch convolution (one per channel)
        std::vector<std::unique_ptr<core::FirFilterNeon>> m_batch_fir_filters;

        // High-performance FIR filters for moving/streaming convolution (one per channel)
        std::vector<std::unique_ptr<core::FirFilterNeon>> m_moving_fir_filters;

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
         * @brief Process in batch (stateless) mode - IN-PLACE VERSION.
         *        Only used if isResizing() is false (which it's not for batch mode).
         *        This is kept for backward compatibility but should use processResizing().
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            // For batch mode, we should be using processResizing() since isResizing() returns true
            // But if called directly (legacy), we still handle it
            size_t samplesPerChannel = numSamples / numChannels;
            size_t kernelSize = m_kernel.size();

            if (samplesPerChannel < kernelSize)
            {
                std::fill_n(buffer, numSamples, 0.0f);
                return;
            }

            size_t outputSamplesPerChannel = samplesPerChannel - kernelSize + 1;
            size_t outputSize = outputSamplesPerChannel * numChannels;

            // Process using resizing logic but write back to same buffer (truncated)
            if (m_actualMethod == ConvolutionMethod::Direct)
            {
                processResizingDirect(buffer, samplesPerChannel, buffer, outputSamplesPerChannel, numChannels);
            }
            else // FFT
            {
                processResizingFFT(buffer, samplesPerChannel, buffer, outputSamplesPerChannel, numChannels);
            }

            // Note: buffer now contains outputSize valid samples at the beginning
        }

        /**
         * @brief Direct convolution for batch mode - processes from input to output buffers.
         *
         * Computes VALID convolution where the kernel fully overlaps the signal.
         * Output length is outputSamplesPerChannel = inputSamplesPerChannel - M + 1.
         *
         * For input [1,2,3,4,5] and kernel [1,0,-1]:
         *   output[0] = 1*1 + 0*2 + (-1)*3 = -2
         *   output[1] = 1*2 + 0*3 + (-1)*4 = -2
         *   output[2] = 1*3 + 0*4 + (-1)*5 = -2
         */
        void processResizingDirect(const float *inputBuffer, size_t inputSamplesPerChannel,
                                   float *outputBuffer, size_t outputSamplesPerChannel,
                                   int numChannels)
        {
            size_t kernelSize = m_kernel.size();

            // Resize de-interleaved buffers if needed
            if (m_deinterleaved_buffers.size() != static_cast<size_t>(numChannels))
            {
                m_deinterleaved_buffers.resize(numChannels);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (m_deinterleaved_buffers[ch].size() != inputSamplesPerChannel)
                {
                    m_deinterleaved_buffers[ch].resize(inputSamplesPerChannel);
                }
            }

            // Resize output buffer
            if (m_temp_output_channel.size() != outputSamplesPerChannel)
            {
                m_temp_output_channel.resize(outputSamplesPerChannel);
            }

            // Step 1: De-interleave input data
            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (size_t i = 0; i < inputSamplesPerChannel; ++i)
                {
                    m_deinterleaved_buffers[ch][i] = inputBuffer[i * numChannels + ch];
                }
            }

            // Step 2: Process each channel - valid convolution
            const float *kernelPtr = m_kernel.data();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float *channelInput = m_deinterleaved_buffers[ch].data();
                float *output = m_temp_output_channel.data();

                // Valid convolution: y[n] = Σ h[k] * x[n+k] for k=0 to M-1
                for (size_t n = 0; n < outputSamplesPerChannel; ++n)
                {
                    float sum = 0.0f;

                    // Convolve kernel with signal at position n
                    for (size_t k = 0; k < kernelSize; ++k)
                    {
                        sum += kernelPtr[k] * channelInput[n + k];
                    }

                    output[n] = sum;
                }

                // Step 3: Re-interleave output
                for (size_t i = 0; i < outputSamplesPerChannel; ++i)
                {
                    outputBuffer[i * numChannels + ch] = output[i];
                }
            }
        }

        /**
         * @brief FFT convolution for batch mode - processes from input to output buffers.
         */
        void processResizingFFT(const float *inputBuffer, size_t inputSamplesPerChannel,
                                float *outputBuffer, size_t outputSamplesPerChannel,
                                int numChannels)
        {
            size_t kernelSize = m_kernel.size();

            // FFT convolution size must be at least inputSamplesPerChannel + kernelSize - 1
            size_t fftSize = inputSamplesPerChannel + kernelSize - 1;

            // Round up to next power of 2
            size_t fftSizePow2 = 1;
            while (fftSizePow2 < fftSize)
            {
                fftSizePow2 *= 2;
            }

            // Resize buffers if needed
            if (m_batch_fft_size != fftSizePow2)
            {
                m_batch_fft_size = fftSizePow2;
                size_t halfSize = fftSizePow2 / 2 + 1;

                m_fft_engine = std::make_unique<core::FftEngine<float>>(fftSizePow2);

                m_batch_kernel_padded.resize(fftSizePow2);
                m_batch_kernel_fft.resize(halfSize);
                m_batch_channel_data.resize(fftSizePow2);
                m_batch_signal_fft.resize(halfSize);
                m_batch_result.resize(fftSizePow2);

                // Precompute kernel FFT
                std::fill(m_batch_kernel_padded.begin(), m_batch_kernel_padded.end(), 0.0f);
                std::copy(m_kernel.begin(), m_kernel.end(), m_batch_kernel_padded.begin());
                m_fft_engine->rfft(m_batch_kernel_padded.data(), m_batch_kernel_fft.data());
            }

            size_t halfSize = m_batch_fft_size / 2 + 1;

            // Resize de-interleaved buffers
            if (m_deinterleaved_buffers.size() != static_cast<size_t>(numChannels))
            {
                m_deinterleaved_buffers.resize(numChannels);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (m_deinterleaved_buffers[ch].size() != inputSamplesPerChannel)
                {
                    m_deinterleaved_buffers[ch].resize(inputSamplesPerChannel);
                }
            }

            if (m_temp_output_channel.size() != outputSamplesPerChannel)
            {
                m_temp_output_channel.resize(outputSamplesPerChannel);
            }

            // Step 1: De-interleave input data
            for (int ch = 0; ch < numChannels; ++ch)
            {
                for (size_t i = 0; i < inputSamplesPerChannel; ++i)
                {
                    m_deinterleaved_buffers[ch][i] = inputBuffer[i * numChannels + ch];
                }
            }

            // Step 2: Process each channel
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Zero-pad channel data
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

                // Copy valid portion to output buffer
                for (size_t i = 0; i < outputSamplesPerChannel; ++i)
                {
                    m_temp_output_channel[i] = m_batch_result[i];
                }

                // Step 3: Re-interleave output
                for (size_t i = 0; i < outputSamplesPerChannel; ++i)
                {
                    outputBuffer[i * numChannels + ch] = m_temp_output_channel[i];
                }
            }
        }

        /**
         * @brief Direct convolution in moving mode - Ultra high-performance version.
         *
         * Uses FirFilterNeon with O(1) guard-zone circular buffer instead of
         * the old O(N) memmove approach. Each channel maintains its own stateful
         * FIR filter for true streaming processing.
         *
         * Key optimization: The FirFilterNeon class handles all state management
         * with O(1) updates and fully vectorized NEON convolution.
         */
        void processMovingDirect(float *buffer, size_t numSamples, int numChannels)
        {
            size_t samplesPerChannel = numSamples / numChannels;

            // Initialize FIR filters for each channel (only once)
            if (m_moving_fir_filters.size() != static_cast<size_t>(numChannels))
            {
                m_moving_fir_filters.clear();
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    m_moving_fir_filters.push_back(
                        std::make_unique<core::FirFilterNeon>(m_kernel));
                }
            }

            // Process each channel with stateful FIR filter
            // For interleaved data, we process per-sample but the FirFilterNeon
            // uses O(1) circular buffer updates (no memmove!)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto &fir = m_moving_fir_filters[ch];

                // Process all samples for this channel (stateful streaming)
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    size_t idx = i * numChannels + ch;
                    buffer[idx] = fir->processSample(buffer[idx]);
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
        }
    };

} // namespace dsp::adapters
