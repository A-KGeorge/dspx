#pragma once

#include "SimdOps.h"
#include <vector>
#include <stdexcept>

namespace dsp::utils
{
    /**
     * @brief Policy for computing convolution using a sliding window.
     *
     * This policy is designed to work with SlidingWindowFilter.
     * It performs a dot product between the kernel and the current window.
     *
     * This is equivalent to FIR filtering with a user-specified kernel.
     *
     * @tparam T The numeric type (float or double).
     */
    template <typename T>
    class ConvolutionPolicy
    {
    public:
        /**
         * @brief Constructs a convolution policy with a given kernel.
         * @param kernel The convolution kernel (filter coefficients).
         */
        explicit ConvolutionPolicy(const std::vector<T> &kernel)
            : m_kernel(kernel)
        {
            if (kernel.empty())
            {
                throw std::invalid_argument("Convolution kernel cannot be empty");
            }
        }

        /**
         * @brief Compute the convolution for the current window.
         *
         * This performs a dot product between the kernel and the window data.
         * The window data is in order [oldest, ..., newest], so we need to reverse it
         * to match standard convolution: y[n] = h[0]*x[n] + h[1]*x[n-1] + ...
         *
         * @param windowData Pointer to the current window data (oldest to newest).
         * @param windowSize Size of the window (should match kernel size).
         * @return The convolution result for this position.
         */
        T calculate(const T *windowData, size_t windowSize) const
        {
            if (windowSize != m_kernel.size())
            {
                throw std::runtime_error("Window size must match kernel size");
            }

            // Use SIMD-optimized dot product with reversed window
            // Window is [x[n-M+1], ..., x[n-1], x[n]] (oldest to newest)
            // Kernel is [h[0], h[1], ..., h[M-1]]
            // We want: h[0]*x[n] + h[1]*x[n-1] + ... + h[M-1]*x[n-M+1]
            // So we need to reverse the window or kernel

            if constexpr (std::is_same_v<T, float>)
            {
                // Reverse the window for standard convolution
                // Could also reverse the kernel, but reversing the window is more cache-friendly
                T result = T(0);
                for (size_t i = 0; i < windowSize; ++i)
                {
                    result += m_kernel[i] * windowData[windowSize - 1 - i];
                }
                return result;
            }
            else
            {
                // Scalar fallback for double with reversed window
                T result = T(0);
                for (size_t i = 0; i < windowSize; ++i)
                {
                    result += m_kernel[i] * windowData[windowSize - 1 - i];
                }
                return result;
            }
        }

        // Required policy interface methods for SlidingWindowFilter compatibility
        // Note: Convolution doesn't track running statistics, so these are no-ops

        void onAdd(T) { /* No-op: convolution computed directly from window */ }
        void onRemove(T) { /* No-op: convolution computed directly from window */ }
        void clear() { /* No-op: kernel is immutable */ }

        /**
         * @brief Compute result from the entire window.
         * For convolution, this delegates to calculate() with the full window.
         * @param count The number of samples in the window (not used).
         * @return The convolution result (computed in ConvolutionStage).
         */
        T getResult(size_t count) const
        {
            // This is called by SlidingWindowFilter::addSample()
            // But for convolution, we compute the result using calculate()
            // with the full window contents. The actual computation happens
            // in ConvolutionStage which calls calculate() directly.
            // Return 0 here as placeholder - the real result comes from calculate().
            (void)count; // Suppress unused parameter warning
            return T(0);
        }

        /**
         * @brief Get the kernel size.
         * @return The number of coefficients in the kernel.
         */
        size_t getKernelSize() const
        {
            return m_kernel.size();
        }

        /**
         * @brief Get the kernel.
         * @return Reference to the kernel vector.
         */
        const std::vector<T> &getKernel() const
        {
            return m_kernel;
        }

        /**
         * @brief Get state for serialization.
         * ConvolutionPolicy has no dynamic state (kernel is immutable).
         * @return Empty struct (no state to serialize).
         */
        struct EmptyState
        {
        };
        EmptyState getState() const { return {}; }

        /**
         * @brief Set state for deserialization.
         * ConvolutionPolicy has no dynamic state (kernel is immutable).
         */
        void setState(const EmptyState &) {}

    private:
        std::vector<T> m_kernel; // The convolution kernel
    };

} // namespace dsp::utils
