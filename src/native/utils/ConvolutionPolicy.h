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
     * Optimization: The kernel is pre-reversed during construction, so the
     * calculate() method can use a simple forward SIMD dot product without
     * needing to reverse the window data on every call.
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
         *               Will be reversed internally for efficient calculation.
         */
        explicit ConvolutionPolicy(const std::vector<T> &kernel)
            : m_kernel_reversed(kernel.rbegin(), kernel.rend())
        {
            if (kernel.empty())
            {
                throw std::invalid_argument("Convolution kernel cannot be empty");
            }
        }

        /**
         * @brief Compute the convolution for the current window.
         *
         * This performs a dot product between the (pre-reversed) kernel and the window data.
         * The window data is in order [oldest, ..., newest], and the kernel has been
         * pre-reversed, so we can do a simple forward dot product.
         *
         * Standard convolution: y[n] = h[0]*x[n] + h[1]*x[n-1] + ... + h[M-1]*x[n-M+1]
         * With pre-reversed kernel: y[n] = h_rev[0]*x[n-M+1] + ... + h_rev[M-1]*x[n]
         *
         * @param windowData Pointer to the current window data (oldest to newest).
         * @param windowSize Size of the window (should match kernel size).
         * @return The convolution result for this position.
         */
        T calculate(const T *windowData, size_t windowSize) const
        {
            if (windowSize != m_kernel_reversed.size())
            {
                throw std::runtime_error("Window size must match kernel size");
            }

            // SIMD-optimized forward dot product (no reversal needed!)
            // Window: [x[n-M+1], ..., x[n-1], x[n]] (oldest to newest)
            // Reversed kernel: [h[M-1], ..., h[1], h[0]]
            // Dot product: h[M-1]*x[n-M+1] + ... + h[1]*x[n-1] + h[0]*x[n]
            // Which equals: h[0]*x[n] + h[1]*x[n-1] + ... + h[M-1]*x[n-M+1] ✓

            if constexpr (std::is_same_v<T, float>)
            {
                // Use SIMD dot product - no reversing needed!
                return simd::dot_product(m_kernel_reversed.data(), windowData, windowSize);
            }
            else
            {
                // Scalar fallback
                T result = T(0);
                for (size_t i = 0; i < windowSize; ++i)
                {
                    result += m_kernel_reversed[i] * windowData[i];
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
         * @param buffer The window buffer contents.
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
            return m_kernel_reversed.size();
        }

        /**
         * @brief Get the original kernel (not reversed).
         * @return Vector containing the original kernel coefficients.
         */
        std::vector<T> getKernel() const
        {
            // Return original kernel (reverse the reversed kernel)
            return std::vector<T>(m_kernel_reversed.rbegin(), m_kernel_reversed.rend());
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
        std::vector<T> m_kernel_reversed; // Pre-reversed kernel for efficient calculation
    };

} // namespace dsp::utils
