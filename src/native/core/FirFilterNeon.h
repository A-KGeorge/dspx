#pragma once

/**
 * @file FirFilterNeon.h
 * @brief ARM NEON-optimized FIR filter with guard-zone circular buffer
 *
 * ⚠️ EXPERIMENTAL ARM OPTIMIZATION ⚠️
 *
 * This implementation uses ARM NEON intrinsics for vectorized FIR filtering.
 * Performance on mobile/embedded ARM devices may vary due to:
 * - Thermal throttling under sustained load
 * - Power management and frequency scaling
 * - Different memory hierarchy vs. desktop x86_64
 *
 * Tested on: Google Pixel 9 Pro XL (Tensor G4)
 * Status: Works correctly but may not show speedup vs. scalar on all ARM chips.
 *
 * Community contributions welcome! If you have ARM optimization expertise or
 * access to ARM development boards, please help improve mobile performance.
 *
 * This implementation keeps O(1) state updates while enabling fully contiguous
 * NEON vectorization using a "guard zone" (mirrored buffer) technique.
 *
 * Key insight: Allocate buffer of size N + GUARD (where GUARD >= max SIMD width).
 * When writing sample at index i, also write it at i+N. This ensures that any
 * NEON load starting from 'head' can read contiguously without wrap-around logic.
 *
 * Performance: O(1) state update + fully vectorized O(N) convolution.
 * Expected gain vs naive circular buffer: 3-6x for 16-128 tap filters on ARM desktop.
 * Mobile results may vary - see note above.
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
              m_head(0),
              m_samplesProcessed(0)
#if defined(__ARM_NEON) || defined(__aarch64__)
              ,
              m_stateAligned(nullptr)
#endif
        {
            if (coefficients.empty())
            {
                throw std::invalid_argument("FIR coefficients cannot be empty");
            }

#if defined(__ARM_NEON) || defined(__aarch64__)
            // One-time warning about experimental ARM support
            static bool warned = false;
            if (!warned)
            {
                fprintf(stderr,
                        "\n⚠️  ARM NEON FIR optimization is experimental.\n"
                        "   Mobile devices may not show speedup vs. scalar due to thermal/power constraints.\n"
                        "   Contributions welcome: https://github.com/A-KGeorge/dspx/issues\n\n");
                warned = true;
            }
#endif

            // Round up to next power of 2 for bitmask wrapping
            m_bufferSize = 1;
            while (m_bufferSize < m_numTaps)
            {
                m_bufferSize <<= 1;
            }
            m_headMask = m_bufferSize - 1;

            // Reverse coefficients to match memory access pattern:
            // readStart points to oldest sample, we read forward (oldest→newest)
            // So h[0] should multiply oldest sample = original h[numTaps-1]
            // This way: h_rev[0]*x[oldest] + ... + h_rev[N-1]*x[newest]
            //         = h[N-1]*x[oldest] + ... + h[0]*x[newest] ✓
            m_coefficients.resize(coefficients.size());
            std::reverse_copy(coefficients.begin(), coefficients.end(), m_coefficients.begin());

            // Allocate state buffer + guard zone
            // Guard zone mirrors the entire circular buffer for contiguous wraparound reads
#if defined(__ARM_NEON) || defined(__aarch64__)
            // ARM NEON: Use 32-byte aligned allocation for optimal SIMD performance
            // Guaranteed alignment eliminates potential unaligned load penalties
            size_t stateSize = (m_bufferSize * 2) * sizeof(float);

            // Round up size to multiple of alignment (required by some aligned_alloc implementations)
            constexpr size_t alignment = 32;
            size_t alignedSize = ((stateSize + alignment - 1) / alignment) * alignment;

            // Use C11 aligned_alloc (available on POSIX systems and modern compilers)
            m_stateAligned = static_cast<float *>(std::aligned_alloc(alignment, alignedSize));
            if (!m_stateAligned)
            {
                throw std::runtime_error("Failed to allocate aligned memory for FIR state buffer");
            }
            std::fill(m_stateAligned, m_stateAligned + (m_bufferSize * 2), 0.0f);
#else
            // Non-ARM platforms: Use std::vector (simpler, no alignment guarantee needed)
            m_state.resize(m_bufferSize * 2, 0.0f);
#endif
        }

        ~FirFilterNeon()
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            if (m_stateAligned)
            {
                std::free(m_stateAligned);
                m_stateAligned = nullptr;
            }
#endif
        }

        // Disable copy (aligned memory ownership)
        FirFilterNeon(const FirFilterNeon &) = delete;
        FirFilterNeon &operator=(const FirFilterNeon &) = delete;

        // Enable move
        FirFilterNeon(FirFilterNeon &&other) noexcept
            : m_numTaps(other.m_numTaps),
              m_bufferSize(other.m_bufferSize),
              m_head(other.m_head),
              m_headMask(other.m_headMask),
              m_samplesProcessed(other.m_samplesProcessed),
              m_coefficients(std::move(other.m_coefficients))
#if defined(__ARM_NEON) || defined(__aarch64__)
              ,
              m_stateAligned(other.m_stateAligned)
#else
              ,
              m_state(std::move(other.m_state))
#endif
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            other.m_stateAligned = nullptr;
#endif
        }

        FirFilterNeon &operator=(FirFilterNeon &&other) noexcept
        {
            if (this != &other)
            {
#if defined(__ARM_NEON) || defined(__aarch64__)
                if (m_stateAligned)
                {
                    std::free(m_stateAligned);
                }
#endif
                m_numTaps = other.m_numTaps;
                m_bufferSize = other.m_bufferSize;
                m_head = other.m_head;
                m_headMask = other.m_headMask;
                m_samplesProcessed = other.m_samplesProcessed;
                m_coefficients = std::move(other.m_coefficients);
#if defined(__ARM_NEON) || defined(__aarch64__)
                m_stateAligned = other.m_stateAligned;
                other.m_stateAligned = nullptr;
#else
                m_state = std::move(other.m_state);
#endif
            }
            return *this;
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
#if defined(__ARM_NEON) || defined(__aarch64__)
            processBatchNeon(buffer, numSamples);
#else
            processBatchScalar(buffer, numSamples);
#endif
        }

        /**
         * @brief Reset filter state (clear circular buffer and guard zone)
         */
        void reset()
        {
            float *state = getState();
            std::fill(state, state + (m_bufferSize * 2), 0.0f);
            m_head = 0;
            m_samplesProcessed = 0;
        }

        size_t getNumTaps() const { return m_numTaps; }
        size_t getBufferSize() const { return m_bufferSize; }

    private:
        // Helper to get state pointer (platform-specific)
        inline float *getState()
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            return m_stateAligned;
#else
            return m_state.data();
#endif
        }

        inline const float *getState() const
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            return m_stateAligned;
#else
            return m_state.data();
#endif
        }

    private:
        size_t m_numTaps;                  // Number of filter taps
        size_t m_bufferSize;               // Power-of-2 buffer size (>= m_numTaps)
        size_t m_head;                     // Current write position
        size_t m_headMask;                 // Bitmask for wrapping (bufferSize - 1)
        size_t m_samplesProcessed;         // Track samples to detect initial transient
        std::vector<float> m_coefficients; // Filter coefficients (reversed order)

#if defined(__ARM_NEON) || defined(__aarch64__)
        float *m_stateAligned; // 32-byte aligned state buffer (ARM only)
#else
        std::vector<float> m_state; // Circular buffer + guard zone (non-ARM)
#endif

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
            m_samplesProcessed++;

            // Get state pointer
            float *state = getState();

            // Write input to current position AND guard zone (O(1) mirroring)
            state[m_head] = input;
            // Always mirror to guard zone - this is critical for wraparound reads!
            state[m_head + m_bufferSize] = input;

            // Return zero until buffer is filled (initial transient period)
            if (m_samplesProcessed < m_numTaps)
            {
                return 0.0f;
            }

            // NEON convolution: read samples from oldest to newest
            // Coefficients are stored in REVERSE order, so:
            // h_rev[0]*x[oldest] + h_rev[1]*x[older] + ... + h_rev[N-1]*x[newest]
            // = h[N-1]*x[oldest] + ... + h[0]*x[newest] = correct FIR formula ✓
            // The guard zone ensures contiguous reads even across the wrap boundary
            // Calculate start position: if m_head >= (numTaps-1), read from [m_head - numTaps + 1]
            // Otherwise, read from guard zone: [m_head + bufferSize - numTaps + 1]
            size_t readStart;
            if (m_head >= m_numTaps - 1)
            {
                readStart = m_head - m_numTaps + 1;
            }
            else
            {
                // Wrap using guard zone (no modulo needed!)
                readStart = m_head + m_bufferSize - m_numTaps + 1;
            }
            const float *x = &state[readStart];
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

        /**
         * @brief NEON-optimized batch processing with 4x tiling
         * @param buffer Input/output buffer
         * @param numSamples Number of samples to process
         *
         * Processes 4 samples per iteration to maximize coefficient reuse.
         * Loads coefficients ONCE per inner loop iteration and reuses them
         * across 4 different sample windows, achieving 75% reduction in
         * coefficient memory bandwidth compared to per-sample processing.
         */
        void processBatchNeon(float *buffer, size_t numSamples)
        {
            const float *h = m_coefficients.data();
            float *state = getState();
            constexpr size_t simd_width = 8;
            const size_t simd_end = (m_numTaps / simd_width) * simd_width;

            // Process 4 samples at a time (4x tiling for maximum coefficient reuse)
            size_t sampleIdx = 0;
            for (; sampleIdx + 3 < numSamples; sampleIdx += 4)
            {
                // State updates for 4 samples
                m_head = (m_head + 1) & m_headMask;
                state[m_head] = buffer[sampleIdx];
                state[m_head + m_bufferSize] = buffer[sampleIdx];
                size_t readStart0 = (m_head - m_numTaps + 1) & m_headMask;
                const float *x0 = &state[readStart0];

                m_head = (m_head + 1) & m_headMask;
                state[m_head] = buffer[sampleIdx + 1];
                state[m_head + m_bufferSize] = buffer[sampleIdx + 1];
                size_t readStart1 = (m_head - m_numTaps + 1) & m_headMask;
                const float *x1 = &state[readStart1];

                m_head = (m_head + 1) & m_headMask;
                state[m_head] = buffer[sampleIdx + 2];
                state[m_head + m_bufferSize] = buffer[sampleIdx + 2];
                size_t readStart2 = (m_head - m_numTaps + 1) & m_headMask;
                const float *x2 = &state[readStart2];

                m_head = (m_head + 1) & m_headMask;
                state[m_head] = buffer[sampleIdx + 3];
                state[m_head + m_bufferSize] = buffer[sampleIdx + 3];
                size_t readStart3 = (m_head - m_numTaps + 1) & m_headMask;
                const float *x3 = &state[readStart3];

                // 8 accumulators (2 per sample for dual unrolling)
                float32x4_t acc0_0 = vdupq_n_f32(0.0f);
                float32x4_t acc0_1 = vdupq_n_f32(0.0f);
                float32x4_t acc1_0 = vdupq_n_f32(0.0f);
                float32x4_t acc1_1 = vdupq_n_f32(0.0f);
                float32x4_t acc2_0 = vdupq_n_f32(0.0f);
                float32x4_t acc2_1 = vdupq_n_f32(0.0f);
                float32x4_t acc3_0 = vdupq_n_f32(0.0f);
                float32x4_t acc3_1 = vdupq_n_f32(0.0f);

                // TRUE BATCH LOOP - coefficient reuse across 4 samples
                for (size_t i = 0; i < simd_end; i += simd_width)
                {
                    // Load coefficients ONCE (critical optimization)
                    float32x4_t c0 = vld1q_f32(h + i);
                    float32x4_t c1 = vld1q_f32(h + i + 4);

                    // Reuse c0, c1 for all 4 samples
                    float32x4_t d0_0 = vld1q_f32(x0 + i);
                    acc0_0 = vmlaq_f32(acc0_0, c0, d0_0);
                    float32x4_t d0_1 = vld1q_f32(x0 + i + 4);
                    acc0_1 = vmlaq_f32(acc0_1, c1, d0_1);

                    float32x4_t d1_0 = vld1q_f32(x1 + i);
                    acc1_0 = vmlaq_f32(acc1_0, c0, d1_0);
                    float32x4_t d1_1 = vld1q_f32(x1 + i + 4);
                    acc1_1 = vmlaq_f32(acc1_1, c1, d1_1);

                    float32x4_t d2_0 = vld1q_f32(x2 + i);
                    acc2_0 = vmlaq_f32(acc2_0, c0, d2_0);
                    float32x4_t d2_1 = vld1q_f32(x2 + i + 4);
                    acc2_1 = vmlaq_f32(acc2_1, c1, d2_1);

                    float32x4_t d3_0 = vld1q_f32(x3 + i);
                    acc3_0 = vmlaq_f32(acc3_0, c0, d3_0);
                    float32x4_t d3_1 = vld1q_f32(x3 + i + 4);
                    acc3_1 = vmlaq_f32(acc3_1, c1, d3_1);
                }

                // Horizontal reduction for all 4 samples (ARMv8.0/v8.1 compatible)

                // --- Reduction for sample 0 ---
                float32x4_t acc0 = vaddq_f32(acc0_0, acc0_1);
#if defined(__aarch64__) && defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
                float output0 = vaddvq_f32(acc0);
#else
                float32x2_t sum0_lo = vget_low_f32(acc0);
                float32x2_t sum0_hi = vget_high_f32(acc0);
                float32x2_t sum0_pair = vadd_f32(sum0_lo, sum0_hi);
                float32x2_t sum0_final = vpadd_f32(sum0_pair, sum0_pair);
                float output0 = vget_lane_f32(sum0_final, 0);
#endif
                for (size_t i = simd_end; i < m_numTaps; ++i)
                {
                    output0 += h[i] * x0[i];
                }
                buffer[sampleIdx] = output0;

                // --- Reduction for sample 1 ---
                float32x4_t acc1 = vaddq_f32(acc1_0, acc1_1);
#if defined(__aarch64__) && defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
                float output1 = vaddvq_f32(acc1);
#else
                float32x2_t sum1_lo = vget_low_f32(acc1);
                float32x2_t sum1_hi = vget_high_f32(acc1);
                float32x2_t sum1_pair = vadd_f32(sum1_lo, sum1_hi);
                float32x2_t sum1_final = vpadd_f32(sum1_pair, sum1_pair);
                float output1 = vget_lane_f32(sum1_final, 0);
#endif
                for (size_t i = simd_end; i < m_numTaps; ++i)
                {
                    output1 += h[i] * x1[i];
                }
                buffer[sampleIdx + 1] = output1;

                // --- Reduction for sample 2 ---
                float32x4_t acc2 = vaddq_f32(acc2_0, acc2_1);
#if defined(__aarch64__) && defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
                float output2 = vaddvq_f32(acc2);
#else
                float32x2_t sum2_lo = vget_low_f32(acc2);
                float32x2_t sum2_hi = vget_high_f32(acc2);
                float32x2_t sum2_pair = vadd_f32(sum2_lo, sum2_hi);
                float32x2_t sum2_final = vpadd_f32(sum2_pair, sum2_pair);
                float output2 = vget_lane_f32(sum2_final, 0);
#endif
                for (size_t i = simd_end; i < m_numTaps; ++i)
                {
                    output2 += h[i] * x2[i];
                }
                buffer[sampleIdx + 2] = output2;

                // --- Reduction for sample 3 ---
                float32x4_t acc3 = vaddq_f32(acc3_0, acc3_1);
#if defined(__aarch64__) && defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
                float output3 = vaddvq_f32(acc3);
#else
                float32x2_t sum3_lo = vget_low_f32(acc3);
                float32x2_t sum3_hi = vget_high_f32(acc3);
                float32x2_t sum3_pair = vadd_f32(sum3_lo, sum3_hi);
                float32x2_t sum3_final = vpadd_f32(sum3_pair, sum3_pair);
                float output3 = vget_lane_f32(sum3_final, 0);
#endif
                for (size_t i = simd_end; i < m_numTaps; ++i)
                {
                    output3 += h[i] * x3[i];
                }
                buffer[sampleIdx + 3] = output3;
            }

            // Process remaining samples (0-3 samples left)
            for (; sampleIdx < numSamples; ++sampleIdx)
            {
                buffer[sampleIdx] = processSampleNeon(buffer[sampleIdx]);
            }
        }
#endif

        /**
         * @brief Scalar fallback for non-ARM platforms
         */
        float processSampleScalar(float input)
        {
            // Advance head FIRST
            m_head = (m_head + 1) & m_headMask;
            m_samplesProcessed++;

            // Get state pointer
            float *state = getState();

            // Write to circular buffer + guard
            state[m_head] = input;
            // Always mirror to guard zone
            state[m_head + m_bufferSize] = input;

            // Return zero until buffer is filled (initial transient period)
            if (m_samplesProcessed < m_numTaps)
            {
                return 0.0f;
            }

            // Compute output (read backward from newest to oldest)
            float output = 0.0f;
            size_t readStart;
            if (m_head >= m_numTaps - 1)
            {
                readStart = m_head - m_numTaps + 1;
            }
            else
            {
                readStart = m_head + m_bufferSize - m_numTaps + 1;
            }
            const float *x = &state[readStart];
            const float *h = m_coefficients.data();

            for (size_t i = 0; i < m_numTaps; ++i)
            {
                output += h[i] * x[i];
            }

            return output;
        }

        /**
         * @brief Optimized scalar batch processing for non-ARM platforms
         * @param buffer Input/output buffer
         * @param numSamples Number of samples to process
         */
        void processBatchScalar(float *buffer, size_t numSamples)
        {
            const float *h = m_coefficients.data();
            float *state = getState();

            for (size_t sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx)
            {
                // Update circular buffer state
                m_head = (m_head + 1) & m_headMask;
                state[m_head] = buffer[sampleIdx];
                state[m_head + m_bufferSize] = buffer[sampleIdx];

                // Compute FIR output
                size_t readStart;
                if (m_head >= m_numTaps - 1)
                {
                    readStart = m_head - m_numTaps + 1;
                }
                else
                {
                    readStart = m_head + m_bufferSize - m_numTaps + 1;
                }
                const float *x = &state[readStart];

                float output = 0.0f;
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    output += h[i] * x[i];
                }

                buffer[sampleIdx] = output;
            }
        }
    };

} // namespace dsp::core
