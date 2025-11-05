/**
 * FIR (Finite Impulse Response) Filter
 *
 * A non-recursive filter defined by:
 *   y[n] = b[0]*x[n] + b[1]*x[n-1] + ... + b[M]*x[n-M]
 *
 * Features:
 * - Always stable (no feedback)
 * - Linear phase possible
 * - Stateful (maintains sample history) and stateless modes
 * - SIMD-optimized convolution
 * - Efficient circular buffer for state management
 */

#ifndef DSP_CORE_FIR_FILTER_H
#define DSP_CORE_FIR_FILTER_H

#include <vector>
#include <cstddef>
#include <memory>
#include "../utils/CircularBufferArray.h"

// Include NEON-optimized filter for ARM platforms
#if defined(__ARM_NEON) || defined(__aarch64__)
#include "FirFilterNeon.h"
#endif

namespace dsp
{
    namespace core
    {

        template <typename T = float>
        class FirFilter
        {
        public:
            /**
             * Constructor
             * @param coefficients Filter coefficients (b[0], b[1], ..., b[M])
             * @param stateful If true, maintains state between process calls
             */
            explicit FirFilter(const std::vector<T> &coefficients, bool stateful = true);

            /**
             * Process single sample (stateful mode only)
             * @param input Input sample
             * @return Filtered output sample
             */
            T processSample(T input);

            /**
             * Process batch of samples
             * @param input Input samples
             * @param output Output buffer (must be same size as input)
             * @param length Number of samples
             * @param stateless If true, ignores internal state (batch processing)
             */
            void process(const T *input, T *output, size_t length, bool stateless = false);

            /**
             * Reset filter state (clear history)
             */
            void reset();

            /**
             * Get filter order (M = number of coefficients - 1)
             */
            size_t getOrder() const { return m_coefficients.size() - 1; }

            /**
             * Get number of coefficients
             */
            size_t getNumCoefficients() const { return m_coefficients.size(); }

            /**
             * Get coefficients
             */
            const std::vector<T> &getCoefficients() const { return m_coefficients; }

            /**
             * Update coefficients (resets state)
             */
            void setCoefficients(const std::vector<T> &coefficients);

            /**
             * Check if filter is stateful
             */
            bool isStateful() const { return m_stateful; }

            /**
             * Get internal state (for serialization)
             * @return Pair of (state buffer, state index)
             */
            std::pair<std::vector<T>, size_t> getState() const;

            /**
             * Set internal state (for deserialization)
             * @param state State buffer
             * @param stateIndex Current position in circular buffer
             */
            void setState(const std::vector<T> &state, size_t stateIndex);

            // ========== Common FIR Filter Designs ==========

            /**
             * Create low-pass filter using windowed sinc method
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param numTaps Number of filter taps (higher = sharper transition)
             * @param windowType Window function ("hamming", "hann", "blackman")
             */
            static FirFilter<T> createLowPass(T cutoffFreq, size_t numTaps, const std::string &windowType = "hamming");

            /**
             * Create high-pass filter
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param numTaps Number of filter taps
             * @param windowType Window function
             */
            static FirFilter<T> createHighPass(T cutoffFreq, size_t numTaps, const std::string &windowType = "hamming");

            /**
             * Create band-pass filter
             * @param lowCutoff Low cutoff frequency (normalized)
             * @param highCutoff High cutoff frequency (normalized)
             * @param numTaps Number of filter taps
             * @param windowType Window function
             */
            static FirFilter<T> createBandPass(T lowCutoff, T highCutoff, size_t numTaps, const std::string &windowType = "hamming");

            /**
             * Create band-stop (notch) filter
             * @param lowCutoff Low cutoff frequency (normalized)
             * @param highCutoff High cutoff frequency (normalized)
             * @param numTaps Number of filter taps
             * @param windowType Window function
             */
            static FirFilter<T> createBandStop(T lowCutoff, T highCutoff, size_t numTaps, const std::string &windowType = "hamming");

        private:
            std::vector<T> m_coefficients; // Filter coefficients (b[0], b[1], ..., b[M])
            std::vector<T> m_state;        // Sample history (x[n-1], x[n-2], ..., x[n-M])
            size_t m_stateIndex;           // Current position in circular state buffer
            size_t m_stateMask;            // Bitmask for power-of-2 circular buffer (replaces modulo)
            bool m_stateful;               // Whether to maintain state between calls
            bool m_useDoubleBuffer;        // Use double-buffered state (article optimization)

#if defined(__ARM_NEON) || defined(__aarch64__)
            // NEON-optimized filter for ARM (auto-selected for small-medium taps + float32)
            std::unique_ptr<FirFilterNeon> m_neonFilter;
            bool m_useNeon;
#endif

            /**
             * Compute single output sample via convolution
             * @param input Current input sample
             * @param history Previous samples
             * @param historySize Number of valid history samples
             */
            T convolve(T input, const T *history, size_t historySize);

            /**
             * Apply window function for filter design
             */
            static void applyWindow(std::vector<T> &impulse, const std::string &windowType);

            /**
             * Generate ideal sinc low-pass impulse response
             */
            static std::vector<T> generateSincLowPass(T cutoffFreq, size_t numTaps);
        };

    } // namespace core
} // namespace dsp

#endif // DSP_CORE_FIR_FILTER_H
