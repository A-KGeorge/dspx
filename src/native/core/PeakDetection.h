#pragma once

#include <cmath>
#include <cstddef>
#include <vector>
#include <algorithm>
#include "../utils/SimdOps.h"

namespace dsp::core
{
    /**
     * @brief Post-processing pass to enforce minimum peak distance.
     * Modifies the output buffer in-place.
     */
    inline void apply_min_peak_distance(float *output, size_t size, int minPeakDistance)
    {
        if (minPeakDistance <= 1)
        {
            return;
        }

        int cooldown = 0;
        for (size_t i = 0; i < size; ++i)
        {
            if (output[i] == 1.0f)
            {
                if (cooldown == 0)
                {
                    // This is a valid peak, start cooldown
                    cooldown = minPeakDistance - 1;
                }
                else
                {
                    // This peak is suppressed
                    output[i] = 0.0f;
                    cooldown--; // Still decrement cooldown
                }
            }
            else if (cooldown > 0)
            {
                cooldown--;
            }
        }
    }

    /**
     * @brief Scalar (flexible) delayed peak detection.
     * This is the fallback for windowSize != 3.
     */
    inline void find_peaks_batch_scalar_delayed(const float *input, float *output, size_t size, float threshold, int windowSize)
    {
        // We check a peak at [i-k] when we see sample [i].
        // This maintains the "delayed" logic.
        const int k = (windowSize - 1) / 2;
        const int end_idx = windowSize - 1;

        if (size < windowSize)
        {
            return; // Not enough samples
        }

        for (size_t i = end_idx; i < size; ++i)
        {
            const size_t peak_idx = i - k; // The candidate peak index
            const float candidate = input[peak_idx];

            if (candidate < threshold)
            {
                output[peak_idx] = 0.0f;
                continue;
            }

            bool is_max = true;
            // Check all neighbors from [i-k-k] to [i-k+k] (which is [i-end_idx] to [i])
            for (int j = -k; j <= k; ++j)
            {
                if (j == 0)
                    continue; // Don't compare to self

                if (candidate <= input[peak_idx + j])
                {
                    is_max = false;
                    break;
                }
            }

            output[peak_idx] = is_max ? 1.0f : 0.0f;
        }
    }

    /**
     * @brief Stateless (batch) peak detection using the 3-point delayed method.
     *
     * Finds local maxima in a buffer.
     *
     * @param input The input signal buffer.
     * @param output The output buffer (will be filled with 1.0 at peaks, 0.0 otherwise).
     * @param size The number of samples in the input/output buffers.
     * @param threshold The minimum value for a peak to be considered.
     * @param windowSize The local neighborhood size (must be odd, >= 3).
     * @param minPeakDistance The minimum samples between peaks.
     *
     * @note Output edges (0..k and size-k..size) will be 0.
     */
    inline void find_peaks_batch_delayed(const float *input, float *output, size_t size, float threshold, int windowSize, int minPeakDistance)
    {
        // Initialize output to 0.0
        std::fill(output, output + size, 0.0f);

        if (windowSize < 3 || windowSize % 2 == 0)
        {
            return; // Invalid window size
        }

        if (size < windowSize)
        {
            return; // Not enough samples
        }

        if (windowSize == 3)
        {
            // --- Optimized SIMD Path for windowSize = 3 ---
            size_t i = 2;

#if defined(SIMD_AVX2)
            const size_t simd_width = 8;
            const size_t simd_limit = (size >= simd_width + 2) ? (size - simd_width) : 0;
            const __m256 v_thresh = _mm256_set1_ps(threshold);
            const __m256 v_ones = _mm256_set1_ps(1.0f);

            for (i = 2; i <= simd_limit; i += simd_width)
            {
                __m256 v_prev_prev = _mm256_loadu_ps(&input[i - 2]);
                __m256 v_prev = _mm256_loadu_ps(&input[i - 1]);
                __m256 v_current = _mm256_loadu_ps(&input[i]);

                __m256 mask1 = _mm256_cmp_ps(v_prev, v_prev_prev, _CMP_GT_OQ);
                __m256 mask2 = _mm256_cmp_ps(v_prev, v_current, _CMP_GT_OQ);
                __m256 mask3 = _mm256_cmp_ps(v_prev, v_thresh, _CMP_GE_OQ);

                __m256 mask = _mm256_and_ps(_mm256_and_ps(mask1, mask2), mask3);
                __m256 v_result = _mm256_and_ps(mask, v_ones);
                _mm256_storeu_ps(&output[i - 1], v_result);
            }
#elif defined(SIMD_SSE2)
            // ... (Same SSE2 logic as before) ...
            const size_t simd_width = 4;
            const size_t simd_limit = (size >= simd_width + 2) ? (size - simd_width) : 0;
            const __m128 v_thresh = _mm_set1_ps(threshold);
            const __m128 v_ones = _mm_set1_ps(1.0f);

            for (i = 2; i <= simd_limit; i += simd_width)
            {
                __m128 v_prev_prev = _mm_loadu_ps(&input[i - 2]);
                __m128 v_prev = _mm_loadu_ps(&input[i - 1]);
                __m128 v_current = _mm_loadu_ps(&input[i]);

                __m128 mask1 = _mm_cmpgt_ps(v_prev, v_prev_prev);
                __m128 mask2 = _mm_cmpgt_ps(v_prev, v_current);
                __m128 mask3 = _mm_cmpge_ps(v_prev, v_thresh);

                __m128 mask = _mm_and_ps(_mm_and_ps(mask1, mask2), mask3);
                __m128 v_result = _mm_and_ps(mask, v_ones);
                _mm_storeu_ps(&output[i - 1], v_result);
            }
#elif defined(SIMD_NEON)
            // ... (Same NEON logic as before) ...
            const size_t simd_width = 4;
            const size_t simd_limit = (size >= simd_width + 2) ? (size - simd_width) : 0;
            const float32x4_t v_thresh = vdupq_n_f32(threshold);
            const float32x4_t v_ones = vdupq_n_f32(1.0f);
            const float32x4_t v_zeros = vdupq_n_f32(0.0f);

            for (i = 2; i <= simd_limit; i += simd_width)
            {
                float32x4_t v_prev_prev = vld1q_f32(&input[i - 2]);
                float32x4_t v_prev = vld1q_f32(&input[i - 1]);
                float32x4_t v_current = vld1q_f32(&input[i]);

                uint32x4_t mask1 = vcgtq_f32(v_prev, v_prev_prev);
                uint32x4_t mask2 = vcgtq_f32(v_prev, v_current);
                uint32x4_t mask3 = vcgeq_f32(v_prev, v_thresh);

                uint32x4_t mask = vandq_u32(vandq_u32(mask1, mask2), mask3);
                float32x4_t v_result = vbslq_f32(mask, v_ones, v_zeros);
                vst1q_f32(&output[i - 1], v_result);
            }
#endif
            // Handle remainder for windowSize = 3
            for (; i < size; ++i)
            {
                const float prev_prev = input[i - 2];
                const float prev = input[i - 1];
                const float current = input[i];

                bool prev_is_peak = (prev > prev_prev) && (prev > current) && (prev >= threshold);
                if (prev_is_peak)
                {
                    output[i - 1] = 1.0f;
                }
            }
        }
        else
        {
            // --- Scalar Fallback Path for windowSize != 3 ---
            find_peaks_batch_scalar_delayed(input, output, size, threshold, windowSize);
        }

        // --- Post-processing for Minimum Peak Distance ---
        if (minPeakDistance > 1)
        {
            apply_min_peak_distance(output, size, minPeakDistance);
        }
    }

    /**
     * @brief Stateless (batch) peak detection for frequency domain.
     */
    inline void find_freq_peaks_batch(const float *input, float *output, size_t size, float threshold, int windowSize, int minPeakDistance)
    {
        // The algorithm is identical to the time-domain batch version.
        find_peaks_batch_delayed(input, output, size, threshold, windowSize, minPeakDistance);
    }

} // namespace dsp::core