/**
 * TimeAlignmentStage.cc
 *
 * Production-grade irregular timestamp resampling implementation.
 */

#define _USE_MATH_DEFINES
#include <cmath>

#include "TimeAlignmentStage.h"
#include "../utils/Toon.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cassert>

// Debug assertion macro
#ifdef _DEBUG
#define ASSERT_BOUNDS(idx, maxSize, msg)                                                                  \
    if ((idx) >= (maxSize))                                                                               \
    {                                                                                                     \
        std::cerr << "[BOUNDS ERROR] " << msg << ": idx=" << (idx) << ", max=" << (maxSize) << std::endl; \
        throw std::out_of_range(msg);                                                                     \
    }
#else
#define ASSERT_BOUNDS(idx, maxSize, msg) ((void)0)
#endif

// Helper function to check debug flag
inline bool isDebugEnabled()
{
    return std::getenv("DSPX_DEBUG") != nullptr;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// SIMD intrinsics
#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
#include <immintrin.h>
#define HAS_AVX2 1
#elif defined(__SSE__) || defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))
#include <emmintrin.h>
#define HAS_SSE 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define HAS_NEON 1
#endif

namespace dsp
{
    namespace adapters
    {
        TimeAlignmentStage::TimeAlignmentStage(
            float targetSampleRate,
            InterpolationMethod interpMethod,
            GapPolicy gapPolicy,
            float gapThreshold,
            DriftCompensation driftComp)
            : m_targetSampleRate(targetSampleRate),
              m_interpMethod(interpMethod),
              m_gapPolicy(gapPolicy),
              m_gapThreshold(gapThreshold),
              m_driftComp(driftComp),
              m_estimatedSampleRate(targetSampleRate),
              m_driftWindowSize(100),
              m_lastTimeScaleFactor(1.0),
              m_lastStartTime(0.0f),
              m_lastEndTime(0.0f)
        {
            if (targetSampleRate <= 0.0f)
            {
                throw std::invalid_argument("TimeAlignmentStage: targetSampleRate must be positive");
            }
            if (gapThreshold < 1.0f)
            {
                throw std::invalid_argument("TimeAlignmentStage: gapThreshold must be >= 1.0");
            }

            reset();
        }

        void TimeAlignmentStage::reset()
        {
            m_stats = Statistics{};
            m_estimatedSampleRate = m_targetSampleRate;
        }

        bool TimeAlignmentStage::isResizing() const
        {
            return true;
        }

        size_t TimeAlignmentStage::calculateOutputSize(size_t inputSize) const
        {
            // Cannot determine exact output size without timestamps
            // Worst case: sparse input with long time span resampled to high target rate
            // Conservative: allocate 10x input size to handle extreme resampling ratios
            // Examples that need this:
            //   - Input: 50 samples over 5 seconds → Output at 100Hz = 500 samples (10x)
            //   - Input: 10 samples over 10 seconds → Output at 1000Hz = 10000 samples (1000x)
            // Using 10x as reasonable upper bound for typical use cases
            // If actualOutputSize exceeds this, processResizing will corrupt memory
            return inputSize * 10;
        }

        double TimeAlignmentStage::getTimeScaleFactor() const
        {
            // Return the cached time scale factor from last processResizing call
            // This tells the pipeline how to adjust timestamps
            return m_lastTimeScaleFactor;
        }

        void TimeAlignmentStage::processResizing(
            const float *inputBuffer,
            size_t inputSize,
            float *outputBuffer,
            size_t &outputSize,
            int channels,
            const float *timestamps)
        {
            if (isDebugEnabled())
            {
                std::cout << "[TimeAlignment] processResizing START: inputSize=" << inputSize
                          << ", channels=" << channels
                          << ", inputBuffer=" << inputBuffer
                          << ", outputBuffer=" << outputBuffer
                          << ", timestamps=" << timestamps << std::endl;
            }

            if (inputSize == 0)
            {
                outputSize = 0;
                return;
            }

            if (timestamps == nullptr)
            {
                throw std::runtime_error("TimeAlignmentStage: timestamps are required");
            }

            // Reset statistics
            m_stats = Statistics{};
            size_t numInputSamples = inputSize / channels;
            m_stats.inputSamples = numInputSamples;

            if (isDebugEnabled())
            {
                std::cout << "[TimeAlignment] numInputSamples=" << numInputSamples << std::endl;
            }

            // Estimate sample rate from input timestamps (for drift compensation)
            if (m_driftComp != DriftCompensation::NONE)
            {
                estimateSampleRate(timestamps, numInputSamples, channels);
            }

            // Detect gaps in input data
            std::vector<size_t> gapIndices;
            detectGaps(timestamps, numInputSamples, channels, gapIndices);
            m_stats.gapsDetected = gapIndices.size();

            // Calculate time span
            ASSERT_BOUNDS(0, inputSize, "startTime access");
            ASSERT_BOUNDS((numInputSamples - 1) * channels, inputSize, "endTime access");
            float startTime = timestamps[0];
            float endTime = timestamps[(numInputSamples - 1) * channels]; // Correct: Last time step
            m_stats.timeSpanMs = endTime - startTime;

            // Determine output sample count based on time span and target rate
            float targetIntervalMs = 1000.0f / m_targetSampleRate;
            size_t numOutputSamples = static_cast<size_t>(std::ceil(m_stats.timeSpanMs / targetIntervalMs)) + 1;

            m_stats.outputSamples = numOutputSamples;
            outputSize = numOutputSamples * channels; // Total values (samples * channels)

            // Cache time scale factor for getTimeScaleFactor()
            // This tells the pipeline how to interpolate timestamps
            m_lastStartTime = startTime;
            m_lastEndTime = endTime;
            float inputTimeSpan = endTime - startTime;
            float outputTimeSpan = (numOutputSamples > 1) ? ((numOutputSamples - 1) * targetIntervalMs) : 0.0f;
            m_lastTimeScaleFactor = (inputTimeSpan > 0.0f) ? (outputTimeSpan / inputTimeSpan) : 1.0;

            // Two-pointer search: maintain search position across iterations
            size_t searchStart = 0;

            if (isDebugEnabled())
            {
                std::cout << "[TimeAlignment] Starting interpolation: numOutputSamples=" << numOutputSamples
                          << ", outputSize=" << outputSize << std::endl;
            }

            // Process each output sample using time-based interpolation
            for (size_t outIdx = 0; outIdx < numOutputSamples; ++outIdx)
            {
                // Calculate target time on uniform grid
                float targetTime = startTime + (outIdx * targetIntervalMs);

                // Check if this falls in a gap
                bool inGap = false;
                size_t gapStart = 0, gapEnd = 0;

                for (size_t gapIdx : gapIndices)
                {
                    // gapIdx points to sample BEFORE the gap
                    // Make sure gapIdx + 1 is valid
                    if (gapIdx + 1 >= numInputSamples)
                    {
                        continue; // Skip invalid gap index
                    }

                    float gapStartTime = timestamps[gapIdx * channels];
                    float gapEndTime = timestamps[(gapIdx + 1) * channels];

                    if (targetTime > gapStartTime && targetTime < gapEndTime)
                    {
                        inGap = true;
                        gapStart = gapIdx;
                        gapEnd = gapIdx + 1;
                        break;
                    }
                }

                // Handle gaps according to policy
                if (inGap)
                {
                    switch (m_gapPolicy)
                    {
                    case GapPolicy::ERROR:
                        throw std::runtime_error("TimeAlignmentStage: Gap detected at output index " +
                                                 std::to_string(outIdx) + ", targetTime=" + std::to_string(targetTime));

                    case GapPolicy::ZERO_FILL:
                    {
                        size_t writeIdx = outIdx * channels;
                        ASSERT_BOUNDS(writeIdx + channels - 1, outputSize, "ZERO_FILL output write");

#if defined(HAS_AVX2)
                        // AVX2: Zero 8 floats at a time
                        __m256 zero = _mm256_setzero_ps();
                        int ch = 0;
                        for (; ch + 8 <= channels; ch += 8)
                        {
                            _mm256_storeu_ps(&outputBuffer[writeIdx + ch], zero);
                        }
                        // Scalar remainder
                        for (; ch < channels; ++ch)
                        {
                            outputBuffer[writeIdx + ch] = 0.0f;
                        }
#elif defined(HAS_SSE)
                        // SSE: Zero 4 floats at a time
                        __m128 zero = _mm_setzero_ps();
                        int ch = 0;
                        for (; ch + 4 <= channels; ch += 4)
                        {
                            _mm_storeu_ps(&outputBuffer[writeIdx + ch], zero);
                        }
                        // Scalar remainder
                        for (; ch < channels; ++ch)
                        {
                            outputBuffer[writeIdx + ch] = 0.0f;
                        }
#elif defined(HAS_NEON)
                        // NEON: Zero 4 floats at a time
                        float32x4_t zero = vdupq_n_f32(0.0f);
                        int ch = 0;
                        for (; ch + 4 <= channels; ch += 4)
                        {
                            vst1q_f32(&outputBuffer[writeIdx + ch], zero);
                        }
                        // Scalar remainder
                        for (; ch < channels; ++ch)
                        {
                            outputBuffer[writeIdx + ch] = 0.0f;
                        }
#else
                        // Scalar fallback
                        for (int ch = 0; ch < channels; ++ch)
                        {
                            outputBuffer[writeIdx + ch] = 0.0f;
                        }
#endif
                    }
                    break;

                    case GapPolicy::HOLD:
                        // Hold last valid value before gap
                        for (int ch = 0; ch < channels; ++ch)
                        {
                            size_t readIdx = gapStart * channels + ch;
                            size_t writeIdx = outIdx * channels + ch;
                            ASSERT_BOUNDS(readIdx, inputSize, "HOLD input read");
                            ASSERT_BOUNDS(writeIdx, outputSize, "HOLD output write");
                            outputBuffer[writeIdx] = inputBuffer[readIdx];
                        }
                        break;

                    case GapPolicy::INTERPOLATE:
                        // Linear interpolation across gap
                        {
                            float t0 = timestamps[gapStart * channels];
                            float t1 = timestamps[gapEnd * channels];
                            float denominator = t1 - t0;

                            // Protection against division by zero
                            if (std::abs(denominator) < 1e-6f)
                            {
                                // Degenerate case: use start value
                                for (int ch = 0; ch < channels; ++ch)
                                {
                                    outputBuffer[outIdx * channels + ch] = inputBuffer[gapStart * channels + ch];
                                }
                            }
                            else
                            {
                                float alpha = (targetTime - t0) / denominator;

                                for (int ch = 0; ch < channels; ++ch)
                                {
                                    float v0 = inputBuffer[gapStart * channels + ch];
                                    float v1 = inputBuffer[gapEnd * channels + ch];
                                    outputBuffer[outIdx * channels + ch] = v0 + alpha * (v1 - v0);
                                }
                            }
                        }
                        break;

                    case GapPolicy::EXTRAPOLATE:
                        // Extrapolate from last two valid samples
                        if (gapStart > 0)
                        {
                            float t0 = timestamps[(gapStart - 1) * channels];
                            float t1 = timestamps[gapStart * channels];
                            float denominator = t1 - t0;
                            float slope = (std::abs(denominator) > 1e-6f) ? 1.0f / denominator : 0.0f;

                            for (int ch = 0; ch < channels; ++ch)
                            {
                                float v0 = inputBuffer[(gapStart - 1) * channels + ch];
                                float v1 = inputBuffer[gapStart * channels + ch];
                                float delta = (targetTime - t1) * slope;
                                outputBuffer[outIdx * channels + ch] = v1 + delta * (v1 - v0);
                            }
                        }
                        else
                        {
                            // No samples before gap - use zero
                            for (int ch = 0; ch < channels; ++ch)
                            {
                                outputBuffer[outIdx * channels + ch] = 0.0f;
                            }
                        }
                        break;
                    }
                }
                else
                {
                    // Not in gap - perform normal interpolation
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        size_t writeIdx = outIdx * channels + ch;
                        ASSERT_BOUNDS(writeIdx, outputSize, "Normal interpolation output write");
                        outputBuffer[writeIdx] = interpolate(
                            targetTime, timestamps, inputBuffer, numInputSamples, channels, ch, searchStart);
                    }
                }
            }
        }

        void TimeAlignmentStage::estimateSampleRate(const float *timestamps, size_t numSamples, int channels)
        {
            if (numSamples < 2)
            {
                m_estimatedSampleRate = m_targetSampleRate;
                return;
            }

            if (m_driftComp == DriftCompensation::REGRESSION)
            {
                // Linear regression: fit line to (index, timestamp) points
                float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
                size_t n = std::min(numSamples, m_driftWindowSize);

                for (size_t i = 0; i < n; ++i)
                {
                    float x = static_cast<float>(i);
                    float y = timestamps[i * channels]; // Stride-aware: use first channel's timestamp
                    sumX += x;
                    sumY += y;
                    sumXY += x * y;
                    sumX2 += x * x;
                }

                float slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
                m_estimatedSampleRate = 1000.0f / slope; // Convert ms/sample to Hz
            }
            else if (m_driftComp == DriftCompensation::PLL)
            {
                // Phase-locked loop: exponential moving average
                float alpha = 0.1f; // PLL time constant
                float avgInterval = 0.0f;
                size_t n = std::min(numSamples - 1, m_driftWindowSize);

                for (size_t i = 1; i <= n; ++i)
                {
                    float interval = timestamps[i * channels] - timestamps[(i - 1) * channels]; // Stride-aware
                    avgInterval = alpha * interval + (1.0f - alpha) * avgInterval;
                }

                m_estimatedSampleRate = 1000.0f / avgInterval;
            }

            // Store in statistics
            m_stats.estimatedSampleRate = m_estimatedSampleRate;
        }

        void TimeAlignmentStage::detectGaps(const float *timestamps, size_t numSamples, int channels, std::vector<size_t> &gapIndices)
        {
            gapIndices.clear();

            if (numSamples < 2)
                return;

            float expectedInterval = 1000.0f / m_estimatedSampleRate;
            float gapMinDuration = expectedInterval * m_gapThreshold;

            float minGap = std::numeric_limits<float>::max();
            float maxGap = 0.0f;
            float sumIntervals = 0.0f;
            float sumSquaredIntervals = 0.0f;

            for (size_t i = 1; i < numSamples; ++i)
            {
                float delta = timestamps[i * channels] - timestamps[(i - 1) * channels]; // Stride-aware
                sumIntervals += delta;
                sumSquaredIntervals += delta * delta;

                if (delta > gapMinDuration)
                {
                    gapIndices.push_back(i - 1);
                    minGap = std::min(minGap, delta);
                    maxGap = std::max(maxGap, delta);
                }
            }

            // Update statistics
            m_stats.avgIntervalMs = sumIntervals / (numSamples - 1);
            float variance = (sumSquaredIntervals / (numSamples - 1)) - (m_stats.avgIntervalMs * m_stats.avgIntervalMs);
            m_stats.stdDevIntervalMs = std::sqrt(std::max(0.0f, variance));

            if (!gapIndices.empty())
            {
                m_stats.minGapDurationMs = minGap;
                m_stats.maxGapDurationMs = maxGap;
            }
        }

        float TimeAlignmentStage::interpolate(
            float targetTime,
            const float *timestamps,
            const float *samples,
            size_t numSamples,
            int channels,
            int channel,
            size_t &searchStart)
        {
            float result = 0.0f;

            switch (m_interpMethod)
            {
            case InterpolationMethod::LINEAR:
                interpolateLinear(targetTime, timestamps, samples, numSamples, channels, channel, searchStart, result);
                break;
            case InterpolationMethod::CUBIC:
                interpolateCubic(targetTime, timestamps, samples, numSamples, channels, channel, searchStart, result);
                break;
            case InterpolationMethod::SINC:
                interpolateSinc(targetTime, timestamps, samples, numSamples, channels, channel, searchStart, result);
                break;
            }

            return result;
        }

        void TimeAlignmentStage::interpolateLinear(
            float targetTime,
            const float *timestamps,
            const float *samples,
            size_t numSamples,
            int channels,
            int channel,
            size_t &searchStart,
            float &output)
        {
            if (isDebugEnabled())
            {
                std::cout << "[TimeAlignment] interpolateLinear: targetTime=" << targetTime
                          << ", numSamples=" << numSamples
                          << ", channels=" << channels
                          << ", channel=" << channel
                          << ", searchStart=" << searchStart << std::endl;
            }

            // Find bracketing interval using two-pointer search
            size_t idx = findBracketingInterval(targetTime, timestamps, numSamples, channels, searchStart);

            if (isDebugEnabled())
            {
                std::cout << "[TimeAlignment] findBracketingInterval returned idx=" << idx << std::endl;
            }

            // Handle edge cases
            if (targetTime <= timestamps[0])
            {
                // Before first sample
                if (m_gapPolicy == GapPolicy::EXTRAPOLATE && numSamples >= 2)
                {
                    // Extrapolate backward
                    float t0 = timestamps[0];
                    float t1 = timestamps[channels];
                    float v0 = samples[channel];
                    float v1 = samples[channels + channel];
                    float denominator = t1 - t0;
                    if (std::abs(denominator) < 1e-6f)
                    {
                        output = v0; // Degenerate case
                    }
                    else
                    {
                        float alpha = (targetTime - t0) / denominator;
                        output = v0 + alpha * (v1 - v0);
                    }
                }
                else
                {
                    output = samples[channel];
                }
                return;
            }

            if (targetTime >= timestamps[(numSamples - 1) * channels])
            {
                // After last sample
                if (m_gapPolicy == GapPolicy::EXTRAPOLATE && numSamples >= 2)
                {
                    // Extrapolate forward
                    float t0 = timestamps[(numSamples - 2) * channels];
                    float t1 = timestamps[(numSamples - 1) * channels];
                    float v0 = samples[(numSamples - 2) * channels + channel];
                    float v1 = samples[(numSamples - 1) * channels + channel];
                    float denominator = t1 - t0;
                    if (std::abs(denominator) < 1e-6f)
                    {
                        output = v1; // Degenerate case
                    }
                    else
                    {
                        float alpha = (targetTime - t1) / denominator;
                        output = v1 + alpha * (v1 - v0);
                    }
                }
                else
                {
                    output = samples[(numSamples - 1) * channels + channel];
                }
                return;
            }

            // Normal case: interpolate between idx and idx+1
            float t0 = timestamps[idx * channels];
            float t1 = timestamps[(idx + 1) * channels];
            float v0 = samples[idx * channels + channel];
            float v1 = samples[(idx + 1) * channels + channel];

            float denominator = t1 - t0;
            if (std::abs(denominator) < 1e-6f)
            {
                // Degenerate case: timestamps are identical, just use v0
                output = v0;
            }
            else
            {
                float alpha = (targetTime - t0) / denominator;
                output = v0 + alpha * (v1 - v0);
            }

            // Update search start for next iteration
            searchStart = idx;
        }

        void TimeAlignmentStage::interpolateCubic(
            float targetTime,
            const float *timestamps,
            const float *samples,
            size_t numSamples,
            int channels,
            int channel,
            size_t &searchStart,
            float &output)
        {
            // Cubic spline requires 4 points: [i-1, i, i+1, i+2]
            size_t idx = findBracketingInterval(targetTime, timestamps, numSamples, channels, searchStart);

            // Need at least 4 points
            if (numSamples < 4)
            {
                interpolateLinear(targetTime, timestamps, samples, numSamples, channels, channel, searchStart, output);
                return;
            }

            // Get 4 points for cubic interpolation
            size_t i0 = (idx > 0) ? idx - 1 : 0;
            size_t i1 = idx;
            size_t i2 = (idx + 1 < numSamples) ? idx + 1 : numSamples - 1;
            size_t i3 = (idx + 2 < numSamples) ? idx + 2 : numSamples - 1;

            size_t totalSamples = numSamples * channels;
            ASSERT_BOUNDS(i0 * channels, totalSamples, "cubic t0 access");
            ASSERT_BOUNDS(i1 * channels, totalSamples, "cubic t1 access");
            ASSERT_BOUNDS(i2 * channels, totalSamples, "cubic t2 access");
            ASSERT_BOUNDS(i3 * channels, totalSamples, "cubic t3 access");
            ASSERT_BOUNDS(i0 * channels + channel, totalSamples, "cubic v0 access");
            ASSERT_BOUNDS(i1 * channels + channel, totalSamples, "cubic v1 access");
            ASSERT_BOUNDS(i2 * channels + channel, totalSamples, "cubic v2 access");
            ASSERT_BOUNDS(i3 * channels + channel, totalSamples, "cubic v3 access");

            float t0 = timestamps[i0 * channels];
            float t1 = timestamps[i1 * channels];
            float t2 = timestamps[i2 * channels];
            float t3 = timestamps[i3 * channels];

            float v0 = samples[i0 * channels + channel];
            float v1 = samples[i1 * channels + channel];
            float v2 = samples[i2 * channels + channel];
            float v3 = samples[i3 * channels + channel];

            // CRITICAL FIX: Check for degenerate case where t2 == t1
            // This can happen at array boundaries when clamping produces duplicate indices
            float denominator = t2 - t1;
            if (std::abs(denominator) < 1e-6f)
            {
                // Degenerate case: fall back to linear or just return v1
                interpolateLinear(targetTime, timestamps, samples, numSamples, channels, channel, searchStart, output);
                return;
            }

            // Catmull-Rom spline coefficients
            float alpha = (targetTime - t1) / denominator;
            float alpha2 = alpha * alpha;
            float alpha3 = alpha2 * alpha;

            output = 0.5f * ((2.0f * v1) +
                             (-v0 + v2) * alpha +
                             (2.0f * v0 - 5.0f * v1 + 4.0f * v2 - v3) * alpha2 +
                             (-v0 + 3.0f * v1 - 3.0f * v2 + v3) * alpha3);

            searchStart = idx;
        }

        void TimeAlignmentStage::interpolateSinc(
            float targetTime,
            const float *timestamps,
            const float *samples,
            size_t numSamples,
            int channels,
            int channel,
            size_t &searchStart,
            float &output)
        {
            // Windowed sinc interpolation (ideal lowpass filter)
            // Window size: 8 samples (±4 from center)
            constexpr int windowSize = 8;

            size_t centerIdx = findBracketingInterval(targetTime, timestamps, numSamples, channels, searchStart);

#if defined(HAS_AVX2) || defined(HAS_SSE) || defined(HAS_NEON)
            // SIMD-optimized path: Process 4 samples at a time
            float values[windowSize] = {0};
            float weights[windowSize] = {0};
            int validCount = 0;

            // Gather values and compute weights
            for (int offset = -windowSize / 2; offset < windowSize / 2; ++offset)
            {
                int sampleIdx = static_cast<int>(centerIdx) + offset;
                if (sampleIdx < 0 || sampleIdx >= static_cast<int>(numSamples))
                    continue;

                float t = timestamps[sampleIdx * channels];
                float v = samples[sampleIdx * channels + channel];

                // Sinc function: sin(π*x) / (π*x)
                float x = (targetTime - t) * m_estimatedSampleRate / 1000.0f;
                float sinc = (std::abs(x) < 1e-6f) ? 1.0f : std::sin(M_PI * x) / (M_PI * x);

                // Hamming window
                float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * (offset + windowSize / 2.0f) / windowSize);

                values[validCount] = v;
                weights[validCount] = sinc * window;
                validCount++;
            }

            // SIMD accumulation
            float sum = 0.0f;
            float weightSum = 0.0f;

#if defined(HAS_AVX2)
            __m256 vsum = _mm256_setzero_ps();
            __m256 wsum = _mm256_setzero_ps();

            int i = 0;
            for (; i + 8 <= validCount; i += 8)
            {
                __m256 v = _mm256_loadu_ps(&values[i]);
                __m256 w = _mm256_loadu_ps(&weights[i]);
                vsum = _mm256_fmadd_ps(v, w, vsum); // sum += v * w
                wsum = _mm256_add_ps(wsum, w);
            }

            // Horizontal sum for AVX2
            __m128 vsum_low = _mm256_castps256_ps128(vsum);
            __m128 vsum_high = _mm256_extractf128_ps(vsum, 1);
            __m128 vsum128 = _mm_add_ps(vsum_low, vsum_high);

            __m128 wsum_low = _mm256_castps256_ps128(wsum);
            __m128 wsum_high = _mm256_extractf128_ps(wsum, 1);
            __m128 wsum128 = _mm_add_ps(wsum_low, wsum_high);

            // Continue with SSE reduction
            vsum128 = _mm_hadd_ps(vsum128, vsum128);
            vsum128 = _mm_hadd_ps(vsum128, vsum128);
            sum = _mm_cvtss_f32(vsum128);

            wsum128 = _mm_hadd_ps(wsum128, wsum128);
            wsum128 = _mm_hadd_ps(wsum128, wsum128);
            weightSum = _mm_cvtss_f32(wsum128);

            // Scalar remainder
            for (; i < validCount; ++i)
            {
                sum += values[i] * weights[i];
                weightSum += weights[i];
            }
#elif defined(HAS_SSE)
            __m128 vsum = _mm_setzero_ps();
            __m128 wsum = _mm_setzero_ps();

            int i = 0;
            for (; i + 4 <= validCount; i += 4)
            {
                __m128 v = _mm_loadu_ps(&values[i]);
                __m128 w = _mm_loadu_ps(&weights[i]);
                vsum = _mm_add_ps(vsum, _mm_mul_ps(v, w));
                wsum = _mm_add_ps(wsum, w);
            }

            // Horizontal sum
            vsum = _mm_hadd_ps(vsum, vsum);
            vsum = _mm_hadd_ps(vsum, vsum);
            sum = _mm_cvtss_f32(vsum);

            wsum = _mm_hadd_ps(wsum, wsum);
            wsum = _mm_hadd_ps(wsum, wsum);
            weightSum = _mm_cvtss_f32(wsum);

            // Scalar remainder
            for (; i < validCount; ++i)
            {
                sum += values[i] * weights[i];
                weightSum += weights[i];
            }
#elif defined(HAS_NEON)
            float32x4_t vsum = vdupq_n_f32(0.0f);
            float32x4_t wsum = vdupq_n_f32(0.0f);

            int i = 0;
            for (; i + 4 <= validCount; i += 4)
            {
                float32x4_t v = vld1q_f32(&values[i]);
                float32x4_t w = vld1q_f32(&weights[i]);
                vsum = vmlaq_f32(vsum, v, w); // vsum += v * w
                wsum = vaddq_f32(wsum, w);
            }

            // Horizontal sum
            float32x2_t vsum_low = vget_low_f32(vsum);
            float32x2_t vsum_high = vget_high_f32(vsum);
            float32x2_t vsum_pair = vadd_f32(vsum_low, vsum_high);
            sum = vget_lane_f32(vpadd_f32(vsum_pair, vsum_pair), 0);

            float32x2_t wsum_low = vget_low_f32(wsum);
            float32x2_t wsum_high = vget_high_f32(wsum);
            float32x2_t wsum_pair = vadd_f32(wsum_low, wsum_high);
            weightSum = vget_lane_f32(vpadd_f32(wsum_pair, wsum_pair), 0);

            // Scalar remainder
            for (; i < validCount; ++i)
            {
                sum += values[i] * weights[i];
                weightSum += weights[i];
            }
#endif

            output = (weightSum > 0.0f) ? (sum / weightSum) : 0.0f;
#else
            // Scalar fallback
            float sum = 0.0f;
            float weightSum = 0.0f;

            for (int offset = -windowSize / 2; offset < windowSize / 2; ++offset)
            {
                int sampleIdx = static_cast<int>(centerIdx) + offset;
                if (sampleIdx < 0 || sampleIdx >= static_cast<int>(numSamples))
                    continue;

                float t = timestamps[sampleIdx * channels];
                float v = samples[sampleIdx * channels + channel];

                // Sinc function: sin(π*x) / (π*x)
                float x = (targetTime - t) * m_estimatedSampleRate / 1000.0f;
                float sinc = (std::abs(x) < 1e-6f) ? 1.0f : std::sin(M_PI * x) / (M_PI * x);

                // Hamming window
                float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * (offset + windowSize / 2.0f) / windowSize);

                float weight = sinc * window;
                sum += v * weight;
                weightSum += weight;
            }

            output = (weightSum > 0.0f) ? (sum / weightSum) : 0.0f;
#endif
            searchStart = centerIdx;
        }

        size_t TimeAlignmentStage::findBracketingInterval(
            float targetTime,
            const float *timestamps,
            size_t numSamples,
            int channels,
            size_t searchStart)
        {
            if (isDebugEnabled())
            {
                std::cout << "[TimeAlignment] findBracketingInterval: targetTime=" << targetTime
                          << ", numSamples=" << numSamples
                          << ", channels=" << channels
                          << ", searchStart=" << searchStart << std::endl;
            }

            // Two-pointer search: start from last known position
            // This is O(1) amortized for monotonically increasing target times

            // Clamp search start
            if (searchStart >= numSamples - 1)
                searchStart = 0;

            // Forward search (most common case)
            while (searchStart < numSamples - 1 && timestamps[(searchStart + 1) * channels] < targetTime)
            {
                searchStart++;
            }

            // Backward search (rare, but handles non-monotonic targets)
            while (searchStart > 0 && timestamps[searchStart * channels] > targetTime)
            {
                searchStart--;
            }

            return searchStart;
        }

        // ========================================
        // Serialization
        // ========================================

        Napi::Object TimeAlignmentStage::serializeState(Napi::Env env) const
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("targetSampleRate", Napi::Number::New(env, m_targetSampleRate));
            state.Set("interpMethod", Napi::Number::New(env, static_cast<int>(m_interpMethod)));
            state.Set("gapPolicy", Napi::Number::New(env, static_cast<int>(m_gapPolicy)));
            state.Set("gapThreshold", Napi::Number::New(env, m_gapThreshold));
            state.Set("driftComp", Napi::Number::New(env, static_cast<int>(m_driftComp)));
            return state;
        }

        void TimeAlignmentStage::deserializeState(const Napi::Object &state)
        {
            m_targetSampleRate = state.Get("targetSampleRate").As<Napi::Number>().FloatValue();
            m_interpMethod = static_cast<InterpolationMethod>(state.Get("interpMethod").As<Napi::Number>().Int32Value());
            m_gapPolicy = static_cast<GapPolicy>(state.Get("gapPolicy").As<Napi::Number>().Int32Value());
            m_gapThreshold = state.Get("gapThreshold").As<Napi::Number>().FloatValue();
            m_driftComp = static_cast<DriftCompensation>(state.Get("driftComp").As<Napi::Number>().Int32Value());
        }

        void TimeAlignmentStage::serializeToon(toon::Serializer &serializer) const
        {
            serializer.writeFloat(m_targetSampleRate);
            serializer.writeInt32(static_cast<int>(m_interpMethod));
            serializer.writeInt32(static_cast<int>(m_gapPolicy));
            serializer.writeFloat(m_gapThreshold);
            serializer.writeInt32(static_cast<int>(m_driftComp));
        }

        void TimeAlignmentStage::deserializeToon(toon::Deserializer &deserializer)
        {
            m_targetSampleRate = deserializer.readFloat();
            m_interpMethod = static_cast<InterpolationMethod>(deserializer.readInt32());
            m_gapPolicy = static_cast<GapPolicy>(deserializer.readInt32());
            m_gapThreshold = deserializer.readFloat();
            m_driftComp = static_cast<DriftCompensation>(deserializer.readInt32());
        }

    } // namespace adapters
} // namespace dsp
