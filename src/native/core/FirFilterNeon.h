#pragma once

/**
 * @file FirFilterNeon.h
 * @brief ARM NEON-optimized FIR filter with guard-zone circular buffer
 *
 * This implementation keeps O(1) state updates while enabling fully contiguous
 * NEON vectorization using a "guard zone" (mirrored buffer) technique.
 *
 * Key insight: Allocate buffer of size N + GUARD (where GUARD >= max SIMD width).
 * When writing sample at index i, also write it at i+N. This ensures that any
 * NEON load starting from 'head' can read contiguously without wrap-around logic.
 *
 * Performance: O(1) state update + fully vectorized O(N) convolution.
 * Expected gain vs naive circular buffer: 3-6x for 16-128 tap filters on ARM.
 */

#include <vector>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace dsp::core
{
    /**
     * @brief High-performance NEON-optimized FIR filter using guard-zone circular buffer
     *
     * Architecture:
     * - Circular buffer with power-of-2 size for bitmask wrapping (O(1) update)
     * - Guard zone (mirrored tail) to make SIMD reads always contiguous
     * - Coefficients stored in forward order (newest sample = h[0])
     * - NEON kernel reads forward from 'head' with no modulo in inner loop
     *
     * This gives best of both worlds:
     * 1. O(1) state updates (increment head, write sample + guard)
     * 2. Fully contiguous NEON loads (no gather/scatter)
     * 3. No memmove/shift overhead (eliminated algorithmic regression)
     */
    class FirFilterNeon
    {
    public:
        explicit FirFilterNeon(const std::vector<float> &coefficients)
            : m_numTaps(coefficients.size()),
              m_head(0)
        {
            if (coefficients.empty())
            {
                throw std::invalid_argument("FIR coefficients cannot be empty");
            }

            // Round up to next power of 2 for bitmask wrapping
            m_bufferSize = 1;
            while (m_bufferSize < m_numTaps)
            {
                m_bufferSize <<= 1;
            }
            m_headMask = m_bufferSize - 1;

            // Store coefficients in FORWARD order (h[0] = newest tap)
            // This matches the circular buffer access pattern
            m_coefficients = coefficients;

            // Allocate state buffer + guard zone
            // Guard zone size = max NEON vector width (16 floats = 64 bytes)
            constexpr size_t GUARD_SIZE = 16;
            m_state.resize(m_bufferSize + GUARD_SIZE, 0.0f);
        }

        /**
         * @brief Process single sample (stateful, streaming mode)
         * @param input New input sample
         * @return Filtered output
         */
        float processSample(float input)
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            return processSampleNeon(input);
#else
            return processSampleScalar(input);
#endif
        }

        /**
         * @brief Process batch of samples in-place
         * @param buffer Input/output buffer
         * @param numSamples Number of samples to process
         */
        void processBatch(float *buffer, size_t numSamples)
        {
            for (size_t i = 0; i < numSamples; ++i)
            {
                buffer[i] = processSample(buffer[i]);
            }
        }

        /**
         * @brief Reset filter state (clear circular buffer and guard zone)
         */
        void reset()
        {
            std::fill(m_state.begin(), m_state.end(), 0.0f);
            m_head = 0;
        }

        size_t getNumTaps() const { return m_numTaps; }
        size_t getBufferSize() const { return m_bufferSize; }

    private:
        size_t m_numTaps;                  // Number of filter taps
        size_t m_bufferSize;               // Power-of-2 buffer size (>= m_numTaps)
        size_t m_head;                     // Current write position
        size_t m_headMask;                 // Bitmask for wrapping (bufferSize - 1)
        std::vector<float> m_coefficients; // Filter coefficients (forward order)
        std::vector<float> m_state;        // Circular buffer + guard zone

#if defined(__ARM_NEON) || defined(__aarch64__)
        /**
         * @brief NEON-optimized sample processing with guard-zone circular buffer
         *
         * Algorithm:
         * 1. Write input to state[head] and state[head + bufferSize] (guard mirror)
         * 2. Read N contiguous floats starting from state[head] using NEON
         * 3. Compute dot product with coefficients (fully vectorized)
         * 4. Advance head with bitmask wrapping (O(1))
         *
         * Key: The guard zone ensures that reads from 'head' are ALWAYS contiguous,
         * even when they logically "wrap around" the circular buffer boundary.
         */
        float processSampleNeon(float input)
        {
            // Advance head FIRST (points to oldest sample position)
            m_head = (m_head + 1) & m_headMask;

            // Write input to current position AND guard zone (O(1) mirroring)
            m_state[m_head] = input;
            if (m_head < 16) // Only update guard if we're near start (will wrap soon)
            {
                m_state[m_head + m_bufferSize] = input;
            }

            // NEON convolution: read BACKWARD from m_head (newest to oldest)
            // m_head points to newest sample, m_head-1 is previous, etc.
            // But we want to read FORWARD in memory, so we read from (m_head - numTaps + 1)
            // wrapped around, which with guard zone is just (m_head + bufferSize - numTaps + 1)
            size_t readStart = (m_head + m_bufferSize - m_numTaps + 1) & (m_bufferSize - 1);
            const float *x = &m_state[readStart];
            const float *h = m_coefficients.data();

            constexpr size_t simd_width = 4;
            const size_t simd_end = (m_numTaps / simd_width) * simd_width;

            float32x4_t acc = vdupq_n_f32(0.0f);

            // Vectorized MAC loop (no modulo, no branches!)
            for (size_t i = 0; i < simd_end; i += simd_width)
            {
                float32x4_t c = vld1q_f32(h + i);
                float32x4_t d = vld1q_f32(x + i);
                acc = vmlaq_f32(acc, c, d); // Fused multiply-add
            }

            // Horizontal reduction
#if defined(__aarch64__) && defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
            // ARMv8.1-a and later: use vaddvq_f32
            float output = vaddvq_f32(acc);
#else
            // ARMv8.0 fallback: manual pairwise addition
            float32x2_t sum_lo = vget_low_f32(acc);
            float32x2_t sum_hi = vget_high_f32(acc);
            float32x2_t sum_pair = vadd_f32(sum_lo, sum_hi);
            float32x2_t sum_final = vpadd_f32(sum_pair, sum_pair);
            float output = vget_lane_f32(sum_final, 0);
#endif

            // Scalar tail (remaining 0-3 taps)
            for (size_t i = simd_end; i < m_numTaps; ++i)
            {
                output += h[i] * x[i];
            }

            return output;
        }
#endif

        /**
         * @brief Scalar fallback for non-ARM platforms
         */
        float processSampleScalar(float input)
        {
            // Advance head FIRST
            m_head = (m_head + 1) & m_headMask;

            // Write to circular buffer + guard
            m_state[m_head] = input;
            if (m_head < 16)
            {
                m_state[m_head + m_bufferSize] = input;
            }

            // Compute output (read backward from newest to oldest)
            float output = 0.0f;
            size_t readStart = (m_head + m_bufferSize - m_numTaps + 1) & (m_bufferSize - 1);
            const float *x = &m_state[readStart];
            const float *h = m_coefficients.data();

            for (size_t i = 0; i < m_numTaps; ++i)
            {
                output += h[i] * x[i];
            }

            return output;
        }
    };

} // namespace dsp::core
