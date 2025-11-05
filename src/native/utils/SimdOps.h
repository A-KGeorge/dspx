#pragma once

/**
 * @file SimdOps.h
 * @brief Cross-platform SIMD operations for DSP processing
 *
 * This header provides SIMD-optimized operations with automatic fallback
 * to scalar implementations when SIMD is not available.
 *
 * Supports:
 * - x86/x64: SSE2 (baseline), AVX2 (when available)
 * - ARM: NEON (when available)
 * - Fallback: Scalar operations with compiler auto-vectorization
 */

#include <cstddef>
#include <cmath>
#include <algorithm>

// Platform detection
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
// --- x86/x64 Platform ---
#define SIMD_X86 // General x86/x64 flag (can be used for non-instruction specific things if needed)

// Check for specific instruction sets in descending order of capability
#if defined(__AVX2__)
#define SIMD_AVX2      // AVX2 (Haswell+)
#define SIMD_AVX       // AVX2 implies AVX
#define SIMD_SSE3      // AVX implies SSE3
#include <immintrin.h> // Includes AVX2, AVX, SSE3 etc.
#elif defined(__AVX__)
#define SIMD_AVX       // AVX (Sandy Bridge+)
#define SIMD_SSE3      // AVX implies SSE3
#include <immintrin.h> // Includes AVX, SSE3 etc.
#elif defined(__SSE3__)
#define SIMD_SSE3      // SSE3 (Prescott+)
#include <pmmintrin.h> // For SSE3 specific
#include <emmintrin.h> // SSE2 might still be needed
#include <xmmintrin.h> // SSE might still be needed
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
// If only SSE2 is available (older CPUs)
#define SIMD_SSE2
#include <emmintrin.h>
#include <xmmintrin.h> // SSE
#elif defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
// If only SSE is available (very old)
#define SIMD_SSE
#include <xmmintrin.h> // SSE
#else
// No specific SIMD detected for x86 beyond baseline
#endif

#elif defined(__ARM_NEON) || defined(__aarch64__)
// --- ARM Platform ---
#define SIMD_NEON
#include <arm_neon.h>

#else
// --- No SIMD Detected (Scalar Fallback) ---
// No SIMD macros defined

#endif // Platform checks

namespace dsp::simd
{
    /**
     * @brief Apply absolute value to array of floats (full-wave rectification)
     * @param buffer Input/output buffer (modified in-place)
     * @param size Number of elements
     */
    inline void abs_inplace(float *buffer, size_t size)
    {
#if defined(SIMD_AVX2)
        // AVX2: Process 8 floats at a time
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        // Sign bit mask (0x7FFFFFFF for each float)
        const __m256 sign_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 values = _mm256_loadu_ps(&buffer[i]);
            values = _mm256_and_ps(values, sign_mask); // Clear sign bit
            _mm256_storeu_ps(&buffer[i], values);
        }

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            buffer[i] = std::fabs(buffer[i]);
        }

#elif defined(SIMD_SSE2)
        // SSE2: Process 4 floats at a time
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        const __m128 sign_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 values = _mm_loadu_ps(&buffer[i]);
            values = _mm_and_ps(values, sign_mask);
            _mm_storeu_ps(&buffer[i], values);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            buffer[i] = std::fabs(buffer[i]);
        }

#elif defined(SIMD_NEON)
        // ARM NEON: Process 4 floats at a time
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t values = vld1q_f32(&buffer[i]);
            values = vabsq_f32(values);
            vst1q_f32(&buffer[i], values);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            buffer[i] = std::fabs(buffer[i]);
        }

#else
        // Scalar fallback (compiler may auto-vectorize)
        for (size_t i = 0; i < size; ++i)
        {
            buffer[i] = std::fabs(buffer[i]);
        }
#endif
    }

    /**
     * @brief Apply half-wave rectification (max(0, x))
     * @param buffer Input/output buffer (modified in-place)
     * @param size Number of elements
     */
    inline void max_zero_inplace(float *buffer, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        const __m256 zero = _mm256_setzero_ps();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 values = _mm256_loadu_ps(&buffer[i]);
            values = _mm256_max_ps(values, zero);
            _mm256_storeu_ps(&buffer[i], values);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            buffer[i] = std::max(0.0f, buffer[i]);
        }

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        const __m128 zero = _mm_setzero_ps();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 values = _mm_loadu_ps(&buffer[i]);
            values = _mm_max_ps(values, zero);
            _mm_storeu_ps(&buffer[i], values);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            buffer[i] = std::max(0.0f, buffer[i]);
        }

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        const float32x4_t zero = vdupq_n_f32(0.0f);

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t values = vld1q_f32(&buffer[i]);
            values = vmaxq_f32(values, zero);
            vst1q_f32(&buffer[i], values);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            buffer[i] = std::max(0.0f, buffer[i]);
        }

#else
        for (size_t i = 0; i < size; ++i)
        {
            buffer[i] = std::max(0.0f, buffer[i]);
        }
#endif
    }

    /**
     * @brief Compute sum of array (optimized for batch mode operations)
     * @param buffer Input buffer
     * @param size Number of elements
     * @return Sum of all elements
     */
    inline double sum(const float *buffer, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        __m256d acc1 = _mm256_setzero_pd();
        __m256d acc2 = _mm256_setzero_pd();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            // Load 8 floats
            __m256 values = _mm256_loadu_ps(&buffer[i]);

            // Convert to two groups of 4 doubles for precision
            __m128 lo = _mm256_castps256_ps128(values);
            __m128 hi = _mm256_extractf128_ps(values, 1);

            __m256d dbl_lo = _mm256_cvtps_pd(lo);
            __m256d dbl_hi = _mm256_cvtps_pd(hi);

            acc1 = _mm256_add_pd(acc1, dbl_lo);
            acc2 = _mm256_add_pd(acc2, dbl_hi);
        }

        // Horizontal sum
        acc1 = _mm256_add_pd(acc1, acc2);
        __m128d sum_high = _mm256_extractf128_pd(acc1, 1);
        __m128d sum_low = _mm256_castpd256_pd128(acc1);
        __m128d sum128 = _mm_add_pd(sum_low, sum_high);

        double result[2];
        _mm_storeu_pd(result, sum128);
        double total = result[0] + result[1];

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            total += static_cast<double>(buffer[i]);
        }

        return total;

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        __m128d acc1 = _mm_setzero_pd();
        __m128d acc2 = _mm_setzero_pd();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 values = _mm_loadu_ps(&buffer[i]);

            // Convert to doubles for precision
            __m128d dbl_lo = _mm_cvtps_pd(values);
            __m128d dbl_hi = _mm_cvtps_pd(_mm_movehl_ps(values, values));

            acc1 = _mm_add_pd(acc1, dbl_lo);
            acc2 = _mm_add_pd(acc2, dbl_hi);
        }

        acc1 = _mm_add_pd(acc1, acc2);
        double result[2];
        _mm_storeu_pd(result, acc1);
        double total = result[0] + result[1];

        for (size_t i = simd_end; i < size; ++i)
        {
            total += static_cast<double>(buffer[i]);
        }

        return total;

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        // ARM NEON: Accumulate in float, then convert to double for precision
        float32x4_t acc = vdupq_n_f32(0.0f);

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t values = vld1q_f32(&buffer[i]);
            acc = vaddq_f32(acc, values);
        }

        // Pairwise add to get horizontal sum, then convert to double
        float32x2_t sum_lo = vget_low_f32(acc);
        float32x2_t sum_hi = vget_high_f32(acc);
        float32x2_t sum_pair = vadd_f32(sum_lo, sum_hi);
        float32x2_t sum_final = vpadd_f32(sum_pair, sum_pair);

        double total = static_cast<double>(vget_lane_f32(sum_final, 0));

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            total += static_cast<double>(buffer[i]);
        }

        return total;

#else
        // Scalar with Kahan summation for precision
        double sum = 0.0;
        double c = 0.0; // Compensation for lost low-order bits

        for (size_t i = 0; i < size; ++i)
        {
            double y = static_cast<double>(buffer[i]) - c;
            double t = sum + y;
            c = (t - sum) - y;
            sum = t;
        }

        return sum;
#endif
    }

    /**
     * @brief Compute sum of squares (optimized for RMS calculations)
     * @param buffer Input buffer
     * @param size Number of elements
     * @return Sum of squared elements
     */
    inline double sum_of_squares(const float *buffer, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        __m256d acc1 = _mm256_setzero_pd();
        __m256d acc2 = _mm256_setzero_pd();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 values = _mm256_loadu_ps(&buffer[i]);

            // Square the values
            __m256 squares = _mm256_mul_ps(values, values);

            // Convert to doubles for precision accumulation
            __m128 lo = _mm256_castps256_ps128(squares);
            __m128 hi = _mm256_extractf128_ps(squares, 1);

            __m256d dbl_lo = _mm256_cvtps_pd(lo);
            __m256d dbl_hi = _mm256_cvtps_pd(hi);

            acc1 = _mm256_add_pd(acc1, dbl_lo);
            acc2 = _mm256_add_pd(acc2, dbl_hi);
        }

        // Horizontal sum
        acc1 = _mm256_add_pd(acc1, acc2);
        __m128d sum_high = _mm256_extractf128_pd(acc1, 1);
        __m128d sum_low = _mm256_castpd256_pd128(acc1);
        __m128d sum128 = _mm_add_pd(sum_low, sum_high);

        double result[2];
        _mm_storeu_pd(result, sum128);
        double total = result[0] + result[1];

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            double val = static_cast<double>(buffer[i]);
            total += val * val;
        }

        return total;

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        __m128d acc1 = _mm_setzero_pd();
        __m128d acc2 = _mm_setzero_pd();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 values = _mm_loadu_ps(&buffer[i]);
            __m128 squares = _mm_mul_ps(values, values);

            __m128d dbl_lo = _mm_cvtps_pd(squares);
            __m128d dbl_hi = _mm_cvtps_pd(_mm_movehl_ps(squares, squares));

            acc1 = _mm_add_pd(acc1, dbl_lo);
            acc2 = _mm_add_pd(acc2, dbl_hi);
        }

        acc1 = _mm_add_pd(acc1, acc2);
        double result[2];
        _mm_storeu_pd(result, acc1);
        double total = result[0] + result[1];

        for (size_t i = simd_end; i < size; ++i)
        {
            double val = static_cast<double>(buffer[i]);
            total += val * val;
        }

        return total;

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        // ARM NEON: Accumulate squares in float, then convert to double
        float32x4_t acc = vdupq_n_f32(0.0f);

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t values = vld1q_f32(&buffer[i]);
            // Fused multiply-add: acc += values * values
            acc = vmlaq_f32(acc, values, values);
        }

        // Convert to double for precision
        float temp[4];
        vst1q_f32(temp, acc);
        double total = static_cast<double>(temp[0]) + static_cast<double>(temp[1]) +
                       static_cast<double>(temp[2]) + static_cast<double>(temp[3]);

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            double val = static_cast<double>(buffer[i]);
            total += val * val;
        }

        return total;

#else
        // Scalar with Kahan summation
        double sum = 0.0;
        double c = 0.0;

        for (size_t i = 0; i < size; ++i)
        {
            double val = static_cast<double>(buffer[i]);
            double y = (val * val) - c;
            double t = sum + y;
            c = (t - sum) - y;
            sum = t;
        }

        return sum;
#endif
    }

    /**
     * @brief QUICK WIN: Apply window function to signal (element-wise multiply)
     * @param input Input signal buffer
     * @param window Window coefficients
     * @param output Output windowed signal
     * @param size Number of elements
     */
    inline void apply_window(const float *input, const float *window, float *output, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 in = _mm256_loadu_ps(&input[i]);
            __m256 win = _mm256_loadu_ps(&window[i]);
            __m256 result = _mm256_mul_ps(in, win);
            _mm256_storeu_ps(&output[i], result);
        }

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            output[i] = input[i] * window[i];
        }

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 in = _mm_loadu_ps(&input[i]);
            __m128 win = _mm_loadu_ps(&window[i]);
            __m128 result = _mm_mul_ps(in, win);
            _mm_storeu_ps(&output[i], result);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            output[i] = input[i] * window[i];
        }

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t in = vld1q_f32(&input[i]);
            float32x4_t win = vld1q_f32(&window[i]);
            float32x4_t result = vmulq_f32(in, win);
            vst1q_f32(&output[i], result);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            output[i] = input[i] * window[i];
        }

#else
        for (size_t i = 0; i < size; ++i)
        {
            output[i] = input[i] * window[i];
        }
#endif
    }

    /**
     * @brief MEDIUM WIN: Compute magnitude spectrum from complex values
     * magnitude[i] = sqrt(real[i]² + imag[i]²)
     * @param real Real components
     * @param imag Imaginary components
     * @param magnitude Output magnitudes
     * @param size Number of complex values
     */
    inline void complex_magnitude(const float *real, const float *imag, float *magnitude, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 re = _mm256_loadu_ps(&real[i]);
            __m256 im = _mm256_loadu_ps(&imag[i]);

            // mag² = re² + im²
            __m256 re_sq = _mm256_mul_ps(re, re);
            __m256 im_sq = _mm256_mul_ps(im, im);
            __m256 mag_sq = _mm256_add_ps(re_sq, im_sq);

            // mag = sqrt(mag²)
            __m256 mag = _mm256_sqrt_ps(mag_sq);

            _mm256_storeu_ps(&magnitude[i], mag);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            magnitude[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        }

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 re = _mm_loadu_ps(&real[i]);
            __m128 im = _mm_loadu_ps(&imag[i]);

            __m128 re_sq = _mm_mul_ps(re, re);
            __m128 im_sq = _mm_mul_ps(im, im);
            __m128 mag_sq = _mm_add_ps(re_sq, im_sq);
            __m128 mag = _mm_sqrt_ps(mag_sq);

            _mm_storeu_ps(&magnitude[i], mag);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            magnitude[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        }

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t re = vld1q_f32(&real[i]);
            float32x4_t im = vld1q_f32(&imag[i]);

            float32x4_t re_sq = vmulq_f32(re, re);
            float32x4_t im_sq = vmulq_f32(im, im);
            float32x4_t mag_sq = vaddq_f32(re_sq, im_sq);

            // ARM NEON sqrt (reciprocal square root + Newton-Raphson)
            float32x4_t mag = vsqrtq_f32(mag_sq);

            vst1q_f32(&magnitude[i], mag);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            magnitude[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        }

#else
        for (size_t i = 0; i < size; ++i)
        {
            magnitude[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        }
#endif
    }

    /**
     * @brief MEDIUM WIN: Compute power spectrum from complex values
     * power[i] = real[i]² + imag[i]²
     * @param real Real components
     * @param imag Imaginary components
     * @param power Output power values
     * @param size Number of complex values
     */
    inline void complex_power(const float *real, const float *imag, float *power, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 re = _mm256_loadu_ps(&real[i]);
            __m256 im = _mm256_loadu_ps(&imag[i]);

            __m256 re_sq = _mm256_mul_ps(re, re);
            __m256 im_sq = _mm256_mul_ps(im, im);
            __m256 pwr = _mm256_add_ps(re_sq, im_sq);

            _mm256_storeu_ps(&power[i], pwr);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            power[i] = real[i] * real[i] + imag[i] * imag[i];
        }

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 re = _mm_loadu_ps(&real[i]);
            __m128 im = _mm_loadu_ps(&imag[i]);

            __m128 re_sq = _mm_mul_ps(re, re);
            __m128 im_sq = _mm_mul_ps(im, im);
            __m128 pwr = _mm_add_ps(re_sq, im_sq);

            _mm_storeu_ps(&power[i], pwr);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            power[i] = real[i] * real[i] + imag[i] * imag[i];
        }

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t re = vld1q_f32(&real[i]);
            float32x4_t im = vld1q_f32(&imag[i]);

            float32x4_t re_sq = vmulq_f32(re, re);
            float32x4_t im_sq = vmulq_f32(im, im);
            float32x4_t pwr = vaddq_f32(re_sq, im_sq);

            vst1q_f32(&power[i], pwr);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            power[i] = real[i] * real[i] + imag[i] * imag[i];
        }

#else
        for (size_t i = 0; i < size; ++i)
        {
            power[i] = real[i] * real[i] + imag[i] * imag[i];
        }
#endif
    }

    /**
     * @brief SIMD-optimized dot product for FIR convolution
     * Uses double-precision accumulation to minimize rounding errors.
     * result = sum(a[i] * b[i]) for i in [0, size)
     * @param a First array
     * @param b Second array
     * @param size Number of elements
     * @return Dot product (accumulated in double precision, returned as double)
     */
    inline double dot_product(const float *a, const float *b, size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        // Use double precision accumulators
        __m256d acc1 = _mm256_setzero_pd();
        __m256d acc2 = _mm256_setzero_pd();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            // Load and multiply 8 floats
            __m256 va = _mm256_loadu_ps(&a[i]);
            __m256 vb = _mm256_loadu_ps(&b[i]);
            __m256 prod = _mm256_mul_ps(va, vb);

            // Convert to two groups of 4 doubles for precision
            __m128 lo = _mm256_castps256_ps128(prod);
            __m128 hi = _mm256_extractf128_ps(prod, 1);

            __m256d dbl_lo = _mm256_cvtps_pd(lo);
            __m256d dbl_hi = _mm256_cvtps_pd(hi);

            acc1 = _mm256_add_pd(acc1, dbl_lo);
            acc2 = _mm256_add_pd(acc2, dbl_hi);
        }

        // Horizontal sum
        acc1 = _mm256_add_pd(acc1, acc2);
        __m128d sum_high = _mm256_extractf128_pd(acc1, 1);
        __m128d sum_low = _mm256_castpd256_pd128(acc1);
        __m128d sum128 = _mm_add_pd(sum_low, sum_high);

        double result[2];
        _mm_storeu_pd(result, sum128);
        double total = result[0] + result[1];

        // Handle remainder
        for (size_t i = simd_end; i < size; ++i)
        {
            total += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        }

        return total;

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        // Use double precision accumulators
        __m128d acc1 = _mm_setzero_pd();
        __m128d acc2 = _mm_setzero_pd();

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            // Load and multiply 4 floats
            __m128 va = _mm_loadu_ps(&a[i]);
            __m128 vb = _mm_loadu_ps(&b[i]);
            __m128 prod = _mm_mul_ps(va, vb);

            // Convert to doubles for precision
            __m128d dbl_lo = _mm_cvtps_pd(prod);
            __m128d dbl_hi = _mm_cvtps_pd(_mm_movehl_ps(prod, prod));

            acc1 = _mm_add_pd(acc1, dbl_lo);
            acc2 = _mm_add_pd(acc2, dbl_hi);
        }

        acc1 = _mm_add_pd(acc1, acc2);
        double result[2];
        _mm_storeu_pd(result, acc1);
        double total = result[0] + result[1];

        for (size_t i = simd_end; i < size; ++i)
        {
            total += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        }

        return total;

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        // ARM NEON: Use fused multiply-add with float, then accumulate in double
        float32x4_t acc = vdupq_n_f32(0.0f);

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t va = vld1q_f32(&a[i]);
            float32x4_t vb = vld1q_f32(&b[i]);
            acc = vmlaq_f32(acc, va, vb); // Fused multiply-add
        }

        // Convert accumulated floats to double for final sum
        float temp[4];
        vst1q_f32(temp, acc);
        double total = static_cast<double>(temp[0]) + static_cast<double>(temp[1]) +
                       static_cast<double>(temp[2]) + static_cast<double>(temp[3]);

        for (size_t i = simd_end; i < size; ++i)
        {
            total += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        }

        return total;

#else
        // Scalar with Kahan summation for precision
        double sum = 0.0;
        double c = 0.0; // Compensation for lost low-order bits

        for (size_t i = 0; i < size; ++i)
        {
            double prod = static_cast<double>(a[i]) * static_cast<double>(b[i]);
            double y = prod - c;
            double t = sum + y;
            c = (t - sum) - y;
            sum = t;
        }

        return sum;
#endif
    }

    /**
     * @brief MAJOR WIN: Complex multiplication for FFT butterflies
     * result = a * b (complex multiplication)
     * @param a_real Real part of a
     * @param a_imag Imaginary part of a
     * @param b_real Real part of b
     * @param b_imag Imaginary part of b
     * @param out_real Output real part
     * @param out_imag Output imaginary part
     * @param size Number of complex multiplications
     */
    inline void complex_multiply(
        const float *a_real, const float *a_imag,
        const float *b_real, const float *b_imag,
        float *out_real, float *out_imag,
        size_t size)
    {
#if defined(SIMD_AVX2)
        const size_t simd_width = 8;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m256 ar = _mm256_loadu_ps(&a_real[i]);
            __m256 ai = _mm256_loadu_ps(&a_imag[i]);
            __m256 br = _mm256_loadu_ps(&b_real[i]);
            __m256 bi = _mm256_loadu_ps(&b_imag[i]);

            // (a + bi) * (c + di) = (ac - bd) + (ad + bc)i
            __m256 ac = _mm256_mul_ps(ar, br);
            __m256 bd = _mm256_mul_ps(ai, bi);
            __m256 ad = _mm256_mul_ps(ar, bi);
            __m256 bc = _mm256_mul_ps(ai, br);

            __m256 real = _mm256_sub_ps(ac, bd);
            __m256 imag = _mm256_add_ps(ad, bc);

            _mm256_storeu_ps(&out_real[i], real);
            _mm256_storeu_ps(&out_imag[i], imag);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            float ar = a_real[i], ai = a_imag[i];
            float br = b_real[i], bi = b_imag[i];
            out_real[i] = ar * br - ai * bi;
            out_imag[i] = ar * bi + ai * br;
        }

#elif defined(SIMD_SSE2)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            __m128 ar = _mm_loadu_ps(&a_real[i]);
            __m128 ai = _mm_loadu_ps(&a_imag[i]);
            __m128 br = _mm_loadu_ps(&b_real[i]);
            __m128 bi = _mm_loadu_ps(&b_imag[i]);

            __m128 ac = _mm_mul_ps(ar, br);
            __m128 bd = _mm_mul_ps(ai, bi);
            __m128 ad = _mm_mul_ps(ar, bi);
            __m128 bc = _mm_mul_ps(ai, br);

            __m128 real = _mm_sub_ps(ac, bd);
            __m128 imag = _mm_add_ps(ad, bc);

            _mm_storeu_ps(&out_real[i], real);
            _mm_storeu_ps(&out_imag[i], imag);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            float ar = a_real[i], ai = a_imag[i];
            float br = b_real[i], bi = b_imag[i];
            out_real[i] = ar * br - ai * bi;
            out_imag[i] = ar * bi + ai * br;
        }

#elif defined(SIMD_NEON)
        const size_t simd_width = 4;
        const size_t simd_count = size / simd_width;
        const size_t simd_end = simd_count * simd_width;

        for (size_t i = 0; i < simd_end; i += simd_width)
        {
            float32x4_t ar = vld1q_f32(&a_real[i]);
            float32x4_t ai = vld1q_f32(&a_imag[i]);
            float32x4_t br = vld1q_f32(&b_real[i]);
            float32x4_t bi = vld1q_f32(&b_imag[i]);

            float32x4_t ac = vmulq_f32(ar, br);
            float32x4_t bd = vmulq_f32(ai, bi);
            float32x4_t ad = vmulq_f32(ar, bi);
            float32x4_t bc = vmulq_f32(ai, br);

            float32x4_t real = vsubq_f32(ac, bd);
            float32x4_t imag = vaddq_f32(ad, bc);

            vst1q_f32(&out_real[i], real);
            vst1q_f32(&out_imag[i], imag);
        }

        for (size_t i = simd_end; i < size; ++i)
        {
            float ar = a_real[i], ai = a_imag[i];
            float br = b_real[i], bi = b_imag[i];
            out_real[i] = ar * br - ai * bi;
            out_imag[i] = ar * bi + ai * br;
        }

#else
        for (size_t i = 0; i < size; ++i)
        {
            float ar = a_real[i], ai = a_imag[i];
            float br = b_real[i], bi = b_imag[i];
            out_real[i] = ar * br - ai * bi;
            out_imag[i] = ar * bi + ai * br;
        }
#endif
    }

#if defined(SIMD_SSE3)
    // --- sse_complex_mul (Unchanged) ---
    inline __m128 sse_complex_mul(const __m128 &a, const __m128 &b)
    {
        __m128 a_real = _mm_moveldup_ps(a);             // [ar1, ar1, ar2, ar2]
        __m128 a_imag = _mm_movehdup_ps(a);             // [ai1, ai1, ai2, ai2]
        __m128 b_shuffled = _mm_shuffle_ps(b, b, 0xB1); // [bi1, br1, bi2, br2]
        __m128 mult1 = _mm_mul_ps(a_real, b);
        __m128 mult2 = _mm_mul_ps(a_imag, b_shuffled);
        return _mm_addsub_ps(mult1, mult2); // [r1, i1, r2, i2]
    }

    /**
     * @brief SIMD SSE butterfly operation (Processes 2 complex floats)
     * Handles inverse conjugation internally.
     */
    inline void sse_butterfly(
        __m128 &a,
        __m128 &b,
        const __m128 &tw_orig, // Original (forward) twiddle
        bool inverse)
    {
        // Conjugate twiddle factor if doing inverse FFT
        const __m128 sse_conj_mask = _mm_setr_ps(1.0f, -1.0f, 1.0f, -1.0f);
        __m128 tw = inverse ? _mm_mul_ps(tw_orig, sse_conj_mask) : tw_orig;

        // Perform butterfly
        __m128 temp = sse_complex_mul(b, tw);
        b = _mm_sub_ps(a, temp);
        a = _mm_add_ps(a, temp);
    }
#endif // SIMD_SSE3

    // ========== AVX2 (float) ==========
#if defined(SIMD_AVX2)
    // --- avx_complex_mul (Unchanged) ---
    inline __m256 avx_complex_mul(const __m256 &a, const __m256 &b)
    {
        __m256 a_real = _mm256_moveldup_ps(a);
        __m256 a_imag = _mm256_movehdup_ps(a);
        __m256 b_shuffled = _mm256_shuffle_ps(b, b, 0xB1);
        __m256 mult1 = _mm256_mul_ps(a_real, b);
        __m256 mult2 = _mm256_mul_ps(a_imag, b_shuffled);
        return _mm256_addsub_ps(mult1, mult2);
    }

    /**
     * @brief SIMD AVX butterfly operation (Processes 4 complex floats)
     * Handles inverse conjugation internally.
     */
    inline void avx_butterfly(
        __m256 &a,
        __m256 &b,
        const __m256 &tw_orig, // Original (forward) twiddle
        bool inverse)
    {
        // Conjugate twiddle factor if doing inverse FFT
        const __m256 conj_mask = _mm256_setr_ps(1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f);
        __m256 tw = inverse ? _mm256_mul_ps(tw_orig, conj_mask) : tw_orig;

        // Perform butterfly
        __m256 temp = avx_complex_mul(b, tw);
        b = _mm256_sub_ps(a, temp);
        a = _mm256_add_ps(a, temp);
    }
#endif // SIMD_AVX2

    // ========== AVX (double) ==========
#if defined(SIMD_AVX)
    // --- avx_complex_mul_double (Unchanged) ---
    inline __m256d avx_complex_mul_double(const __m256d &a, const __m256d &b)
    {
        __m256d a_real = _mm256_permute_pd(a, 0x0);
        __m256d a_imag = _mm256_permute_pd(a, 0xF);
        __m256d b_shuffled = _mm256_permute_pd(b, 0x5);
        __m256d mult1 = _mm256_mul_pd(a_real, b);
        __m256d mult2 = _mm256_mul_pd(a_imag, b_shuffled);
        return _mm256_addsub_pd(mult1, mult2);
    }

    /**
     * @brief SIMD AVX butterfly operation (Processes 2 complex doubles)
     * Handles inverse conjugation internally.
     */
    inline void avx_butterfly_double(
        __m256d &a,
        __m256d &b,
        const __m256d &tw_orig, // Original (forward) twiddle
        bool inverse)
    {
        // Conjugate twiddle factor if doing inverse FFT
        const __m256d avx_conj_mask = _mm256_setr_pd(1.0, -1.0, 1.0, -1.0);
        __m256d tw = inverse ? _mm256_mul_pd(tw_orig, avx_conj_mask) : tw_orig;

        // Perform butterfly
        __m256d temp = avx_complex_mul_double(b, tw);
        b = _mm256_sub_pd(a, temp);
        a = _mm256_add_pd(a, temp);
    }
#endif // SIMD_AVX

    // ========== Interleave/Deinterleave Operations ==========

    /**
     * @brief Deinterleave 2-channel interleaved buffer to two separate planar buffers
     *
     * Converts: [L0, R0, L1, R1, L2, R2, ...]
     * To: ch0=[L0, L1, L2, ...], ch1=[R0, R1, R2, ...]
     *
     * @param interleaved Source buffer with interleaved samples
     * @param ch0 Destination buffer for channel 0 (must be pre-allocated)
     * @param ch1 Destination buffer for channel 1 (must be pre-allocated)
     * @param samples Number of samples per channel
     *
     * @note AVX2 implementation currently disabled due to incorrect shuffle logic.
     *       SSE2 implementation verified correct and provides 2-3x speedup vs scalar.
     */
    inline void deinterleave2Ch(const float *interleaved, float *ch0, float *ch1, size_t samples)
    {
// TODO: Fix AVX2 implementation - current shuffle+blend approach produces incorrect output
// SSE2 provides good performance (2-3x vs scalar), so AVX2 optimization is lower priority
#if 0 && defined(SIMD_AVX2)
        // AVX2: Process 8 samples at a time (16 interleaved floats)
        // Strategy: Extract 128-bit lanes and process each with SSE2 logic
        const size_t simd_width = 8;
        const size_t simd_count = samples / simd_width;

        for (size_t i = 0; i < simd_count; ++i)
        {
            // Load 16 interleaved floats: [L0,R0,L1,R1,L2,R2,L3,R3, L4,R4,L5,R5,L6,R6,L7,R7]
            __m256 v = _mm256_loadu_ps(&interleaved[i * 16]);

            // Extract lower and upper 128-bit lanes
            __m128 v_low = _mm256_castps256_ps128(v);        // [L0,R0,L1,R1,L2,R2,L3,R3]
            __m128 v_high = _mm256_extractf128_ps(v, 1);     // [L4,R4,L5,R5,L6,R6,L7,R7]

            // Process each lane with SSE shuffle (same as SSE2 version below)
            __m128 ch0_low = _mm_shuffle_ps(v_low, v_low, _MM_SHUFFLE(3,1,2,0));   // Indices 0,2,1,3 -> [L0,L1,R0,R1]
            __m128 ch0_high = _mm_shuffle_ps(v_high, v_high, _MM_SHUFFLE(3,1,2,0)); // [L4,L5,R4,R5]

            // Wait, _mm_shuffle_ps doesn't work like that. Let me use the correct SSE2 approach:
            // Split v_low [L0,R0,L1,R1,L2,R2,L3,R3] into two parts:
            __m128 v_low_0 = v_low;                                    // [L0,R0,L1,R1]
            __m128 v_low_1 = _mm_movehl_ps(v_low, v_low);            // [L2,R2,L3,R3]

            __m128 ch0_low_vec = _mm_shuffle_ps(v_low_0, v_low_1, 0x88); // [L0,L1,L2,L3]
            __m128 ch1_low_vec = _mm_shuffle_ps(v_low_0, v_low_1, 0xDD); // [R0,R1,R2,R3]

            __m128 v_high_0 = v_high;
            __m128 v_high_1 = _mm_movehl_ps(v_high, v_high);

            __m128 ch0_high_vec = _mm_shuffle_ps(v_high_0, v_high_1, 0x88); // [L4,L5,L6,L7]
            __m128 ch1_high_vec = _mm_shuffle_ps(v_high_0, v_high_1, 0xDD); // [R4,R5,R6,R7]

            // Combine low and high into 256-bit vectors
            __m256 ch0_vec = _mm256_set_m128(ch0_high_vec, ch0_low_vec);
            __m256 ch1_vec = _mm256_set_m128(ch1_high_vec, ch1_low_vec);

            _mm256_storeu_ps(&ch0[i * 8], ch0_vec);
            _mm256_storeu_ps(&ch1[i * 8], ch1_vec);
        }        // Handle remainder with scalar code
        for (size_t i = simd_count * simd_width; i < samples; ++i)
        {
            ch0[i] = interleaved[i * 2 + 0];
            ch1[i] = interleaved[i * 2 + 1];
        }

#elif defined(SIMD_SSE2)
        // SSE2: Process 4 samples at a time (8 interleaved floats)
        const size_t simd_width = 4;
        const size_t simd_count = samples / simd_width;

        for (size_t i = 0; i < simd_count; ++i)
        {
            // Load 8 interleaved floats: [L0,R0,L1,R1,L2,R2,L3,R3]
            __m128 v0 = _mm_loadu_ps(&interleaved[i * 8 + 0]); // [L0,R0,L1,R1]
            __m128 v1 = _mm_loadu_ps(&interleaved[i * 8 + 4]); // [L2,R2,L3,R3]

            // Shuffle to separate channels
            // _mm_shuffle_ps with mask 0x88 (10001000) = select index 0,2,0,2
            // _mm_shuffle_ps with mask 0xDD (11011101) = select index 1,3,1,3
            __m128 ch0_vec = _mm_shuffle_ps(v0, v1, 0x88); // [L0,L1,L2,L3]
            __m128 ch1_vec = _mm_shuffle_ps(v0, v1, 0xDD); // [R0,R1,R2,R3]

            _mm_storeu_ps(&ch0[i * 4], ch0_vec);
            _mm_storeu_ps(&ch1[i * 4], ch1_vec);
        }

        // Handle remainder
        for (size_t i = simd_count * simd_width; i < samples; ++i)
        {
            ch0[i] = interleaved[i * 2 + 0];
            ch1[i] = interleaved[i * 2 + 1];
        }

#elif defined(SIMD_NEON)
        // NEON: Process 4 samples at a time (8 interleaved floats)
        const size_t simd_width = 4;
        const size_t simd_count = samples / simd_width;

        for (size_t i = 0; i < simd_count; ++i)
        {
            // Load 8 interleaved floats
            float32x4x2_t interleaved_data = vld2q_f32(&interleaved[i * 8]);

            // vld2q_f32 automatically deinterleaves into .val[0] and .val[1]
            vst1q_f32(&ch0[i * 4], interleaved_data.val[0]); // Channel 0
            vst1q_f32(&ch1[i * 4], interleaved_data.val[1]); // Channel 1
        }

        // Handle remainder
        for (size_t i = simd_count * simd_width; i < samples; ++i)
        {
            ch0[i] = interleaved[i * 2 + 0];
            ch1[i] = interleaved[i * 2 + 1];
        }

#else
        // Scalar fallback
        for (size_t i = 0; i < samples; ++i)
        {
            ch0[i] = interleaved[i * 2 + 0];
            ch1[i] = interleaved[i * 2 + 1];
        }
#endif
    }

    /**
     * @brief Interleave two planar buffers into a 2-channel interleaved buffer
     *
     * Converts: ch0=[L0, L1, L2, ...], ch1=[R0, R1, R2, ...]
     * To: [L0, R0, L1, R1, L2, R2, ...]
     *
     * @param ch0 Source buffer for channel 0
     * @param ch1 Source buffer for channel 1
     * @param interleaved Destination buffer (must be pre-allocated with size 2*samples)
     * @param samples Number of samples per channel
     *
     * @note AVX2 implementation currently disabled to match deinterleave2Ch.
     *       SSE2 implementation verified correct and provides 2-3x speedup vs scalar.
     */
    inline void interleave2Ch(const float *ch0, const float *ch1, float *interleaved, size_t samples)
    {
// TODO: Fix AVX2 implementation to match corrected deinterleave2Ch
#if 0 && defined(SIMD_AVX2)
        // AVX2: Process 8 samples at a time (produce 16 interleaved floats)
        const size_t simd_width = 8;
        const size_t simd_count = samples / simd_width;

        for (size_t i = 0; i < simd_count; ++i)
        {
            // Load 8 samples from each channel
            __m256 ch0_vec = _mm256_loadu_ps(&ch0[i * 8]); // [L0,L1,L2,L3,L4,L5,L6,L7]
            __m256 ch1_vec = _mm256_loadu_ps(&ch1[i * 8]); // [R0,R1,R2,R3,R4,R5,R6,R7]

            // Interleave using unpack operations
            __m256 low = _mm256_unpacklo_ps(ch0_vec, ch1_vec);  // [L0,R0,L1,R1, L4,R4,L5,R5]
            __m256 high = _mm256_unpackhi_ps(ch0_vec, ch1_vec); // [L2,R2,L3,R3, L6,R6,L7,R7]

            // Permute to correct lane order
            __m256 out0 = _mm256_permute2f128_ps(low, high, 0x20); // [L0,R0,L1,R1, L2,R2,L3,R3]
            __m256 out1 = _mm256_permute2f128_ps(low, high, 0x31); // [L4,R4,L5,R5, L6,R6,L7,R7]

            _mm256_storeu_ps(&interleaved[i * 16 + 0], out0);
            _mm256_storeu_ps(&interleaved[i * 16 + 8], out1);
        }

        // Handle remainder
        for (size_t i = simd_count * simd_width; i < samples; ++i)
        {
            interleaved[i * 2 + 0] = ch0[i];
            interleaved[i * 2 + 1] = ch1[i];
        }

#elif defined(SIMD_SSE2)
        // SSE2: Process 4 samples at a time (produce 8 interleaved floats)
        const size_t simd_width = 4;
        const size_t simd_count = samples / simd_width;

        for (size_t i = 0; i < simd_count; ++i)
        {
            __m128 ch0_vec = _mm_loadu_ps(&ch0[i * 4]); // [L0,L1,L2,L3]
            __m128 ch1_vec = _mm_loadu_ps(&ch1[i * 4]); // [R0,R1,R2,R3]

            // Interleave using unpack operations
            __m128 low = _mm_unpacklo_ps(ch0_vec, ch1_vec);  // [L0,R0,L1,R1]
            __m128 high = _mm_unpackhi_ps(ch0_vec, ch1_vec); // [L2,R2,L3,R3]

            _mm_storeu_ps(&interleaved[i * 8 + 0], low);
            _mm_storeu_ps(&interleaved[i * 8 + 4], high);
        }

        // Handle remainder
        for (size_t i = simd_count * simd_width; i < samples; ++i)
        {
            interleaved[i * 2 + 0] = ch0[i];
            interleaved[i * 2 + 1] = ch1[i];
        }

#elif defined(SIMD_NEON)
        // NEON: Process 4 samples at a time (produce 8 interleaved floats)
        const size_t simd_width = 4;
        const size_t simd_count = samples / simd_width;

        for (size_t i = 0; i < simd_count; ++i)
        {
            float32x4_t ch0_vec = vld1q_f32(&ch0[i * 4]);
            float32x4_t ch1_vec = vld1q_f32(&ch1[i * 4]);

            // Create interleaved structure
            float32x4x2_t interleaved_data;
            interleaved_data.val[0] = ch0_vec;
            interleaved_data.val[1] = ch1_vec;

            // vst2q_f32 automatically interleaves
            vst2q_f32(&interleaved[i * 8], interleaved_data);
        }

        // Handle remainder
        for (size_t i = simd_count * simd_width; i < samples; ++i)
        {
            interleaved[i * 2 + 0] = ch0[i];
            interleaved[i * 2 + 1] = ch1[i];
        }

#else
        // Scalar fallback
        for (size_t i = 0; i < samples; ++i)
        {
            interleaved[i * 2 + 0] = ch0[i];
            interleaved[i * 2 + 1] = ch1[i];
        }
#endif
    }

    /**
     * @brief Deinterleave N-channel interleaved buffer to N separate planar buffers
     *
     * @param interleaved Source buffer with interleaved samples
     * @param planar Array of N destination buffers (must be pre-allocated)
     * @param numChannels Number of channels
     * @param samples Number of samples per channel
     */
    inline void deinterleaveNCh(const float *interleaved, float **planar, int numChannels, size_t samples)
    {
        // For N-channel, use scalar loop (SIMD benefit diminishes for N>2)
        // Could be optimized for specific cases (4, 8 channels) if needed
        for (size_t i = 0; i < samples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                planar[ch][i] = interleaved[i * numChannels + ch];
            }
        }
    }

    /**
     * @brief Interleave N planar buffers into an N-channel interleaved buffer
     *
     * @param planar Array of N source buffers
     * @param interleaved Destination buffer (must be pre-allocated with size numChannels*samples)
     * @param numChannels Number of channels
     * @param samples Number of samples per channel
     */
    inline void interleaveNCh(const float **planar, float *interleaved, int numChannels, size_t samples)
    {
        // Scalar loop for N-channel
        for (size_t i = 0; i < samples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                interleaved[i * numChannels + ch] = planar[ch][i];
            }
        }
    }

    // SIMD_X86 removed since SIMD_SSE3 covers it for most of the modern devices
} // namespace dsp::simd
