#pragma once
#include <cmath>
#include <algorithm>
#include <numeric>

namespace dsp::core
{
    /**
     * @brief Policy for calculating a running mean (for Moving Average).
     *
     * Maintains a running sum and computes the mean on demand.
     */
    template <typename T>
    struct MeanPolicy
    {
        T m_sum = 0;

        void onAdd(T val) { m_sum += val; }
        void onRemove(T val) { m_sum -= val; }
        void clear() { m_sum = 0; }

        T getResult(const std::vector<T> &buffer) const
        {
            size_t count = buffer.size();
            if (count == 0)
                return 0;
            return m_sum / static_cast<T>(count);
        }

        // For state serialization
        T getState() const { return m_sum; }
        void setState(T sum) { m_sum = sum; }
    };

    /**
     * @brief Policy for calculating RMS (Root Mean Square).
     *
     * Maintains a running sum of squares and computes RMS on demand.
     */
    template <typename T>
    struct RmsPolicy
    {
        T m_sum_sq = 0;

        void onAdd(T val) { m_sum_sq += (val * val); }
        void onRemove(T val) { m_sum_sq -= (val * val); }
        void clear() { m_sum_sq = 0; }

        T getResult(const std::vector<T> &buffer) const
        {
            size_t count = buffer.size();
            if (count == 0)
                return 0;
            // Clamp to avoid negative values due to floating-point errors
            T mean_sq = std::max(static_cast<T>(0), m_sum_sq / static_cast<T>(count));
            return std::sqrt(mean_sq);
        }

        // For state serialization
        T getState() const { return m_sum_sq; }
        void setState(T sumSq) { m_sum_sq = sumSq; }
    };

    /**
     * @brief Policy for SlidingWindowFilter that maintains a running sum.
     * @tparam T The numeric type (e.g., float, double).
     */
    template <typename T>
    struct SumPolicy
    {
        double m_sum = 0.0;

        void onAdd(T value)
        {
            m_sum += static_cast<double>(value);
        }

        void onRemove(T value)
        {
            m_sum -= static_cast<double>(value);
        }

        void clear()
        {
            m_sum = 0.0;
        }

        T getResult(const std::vector<T> &buffer) const
        {
            // The result is just the total sum
            return static_cast<T>(m_sum);
        }

        // --- State Serialization ---
        double getState() const
        {
            return m_sum;
        }

        void setState(double state)
        {
            m_sum = state;
        }

        // Optional: State validation (used in adapter)
        static bool validateState(double state, const std::vector<T> &buffer)
        {
            double actualSum = std::accumulate(buffer.begin(), buffer.end(), 0.0);
            const double tolerance = 0.0001 * std::max(1.0, std::abs(actualSum));
            return (std::abs(state - actualSum) <= tolerance);
        }
    };

    /**
     * @brief Policy for SlidingWindowFilter that counts 'true' entries.
     */
    struct CounterPolicy
    {
        size_t m_count = 0;

        void onAdd(bool value)
        {
            if (value)
            {
                m_count++;
            }
        }

        void onRemove(bool value)
        {
            if (value)
            {
                m_count--;
            }
        }

        void clear()
        {
            m_count = 0;
        }

        // Note: The process buffer is float, so we cast the count
        float getResult(const std::vector<bool> &buffer) const
        {
            return static_cast<float>(m_count);
        }

        // --- State Serialization ---
        size_t getState() const
        {
            return m_count;
        }

        void setState(size_t state)
        {
            m_count = state;
        }

        // Optional: State validation (used in adapter)
        static bool validateState(size_t state, const std::vector<bool> &buffer)
        {
            size_t actualCount = 0;
            for (bool val : buffer)
            {
                if (val)
                    actualCount++;
            }
            return state == actualCount;
        }
    };

    /**
     * @brief Policy for calculating Mean Absolute Value (MAV).
     *
     * Maintains a running sum of absolute values.
     */
    template <typename T>
    struct MeanAbsoluteValuePolicy
    {
        T m_sum_abs = 0;

        void onAdd(T val) { m_sum_abs += std::abs(val); }
        void onRemove(T val) { m_sum_abs -= std::abs(val); }
        void clear() { m_sum_abs = 0; }

        T getResult(const std::vector<T> &buffer) const
        {
            size_t count = buffer.size();
            if (count == 0)
                return 0;
            return m_sum_abs / static_cast<T>(count);
        }

        // For state serialization
        T getState() const { return m_sum_abs; }
        void setState(T sumAbs) { m_sum_abs = sumAbs; }
    };

    /**
     * @brief Policy for calculating Variance.
     *
     * Maintains both sum and sum of squares for variance calculation.
     * Uses the computational formula: Var(X) = E[X²] - (E[X])²
     */
    template <typename T>
    struct VariancePolicy
    {
        T m_sum = 0;
        T m_sum_sq = 0;

        void onAdd(T val)
        {
            m_sum += val;
            m_sum_sq += (val * val);
        }

        void onRemove(T val)
        {
            m_sum -= val;
            m_sum_sq -= (val * val);
        }

        void clear()
        {
            m_sum = 0;
            m_sum_sq = 0;
        }

        T getResult(const std::vector<T> &buffer) const
        {
            size_t count = buffer.size();
            if (count == 0)
                return 0;

            T mean = m_sum / static_cast<T>(count);
            T mean_sq = m_sum_sq / static_cast<T>(count);

            // Clamp to avoid negative variance due to floating-point errors
            return std::max(static_cast<T>(0), mean_sq - (mean * mean));
        }

        // For state serialization - returns both values as a pair
        std::pair<T, T> getState() const { return {m_sum, m_sum_sq}; }
        void setState(T sum, T sumSq)
        {
            m_sum = sum;
            m_sum_sq = sumSq;
        }
    };

    /**
     * @brief Policy for calculating Z-Score normalization.
     *
     * Maintains sum and sum of squares to compute mean and stddev,
     * then normalizes values to Z-scores.
     */
    template <typename T>
    struct ZScorePolicy
    {
        T m_sum = 0;
        T m_sum_sq = 0;
        T m_epsilon;

        explicit ZScorePolicy(T epsilon = static_cast<T>(1e-8))
            : m_epsilon(epsilon) {}

        void onAdd(T val)
        {
            m_sum += val;
            m_sum_sq += (val * val);
        }

        void onRemove(T val)
        {
            m_sum -= val;
            m_sum_sq -= (val * val);
        }

        void clear()
        {
            m_sum = 0;
            m_sum_sq = 0;
        }

        // Z-Score needs the current value to normalize
        T getResult(T currentValue, size_t count) const
        {
            if (count == 0)
                return 0;

            T mean = m_sum / static_cast<T>(count);
            T mean_sq = m_sum_sq / static_cast<T>(count);
            T variance = std::max(static_cast<T>(0), mean_sq - (mean * mean));
            T stddev = std::sqrt(variance);

            // Avoid division by zero
            if (stddev < m_epsilon)
            {
                return 0;
            }

            return (currentValue - mean) / stddev;
        }

        // For state serialization
        std::pair<T, T> getState() const { return {m_sum, m_sum_sq}; }
        void setState(T sum, T sumSq)
        {
            m_sum = sum;
            m_sum_sq = sumSq;
        }

        T getEpsilon() const { return m_epsilon; }
    };

    /**
     * @brief Policy for peak detection in Time domain.
     *
     * Detects local maxima using three-point comparison with the sliding window.
     * Window size is typically 3 (current + 2 previous samples).
     *
     */
    template <typename T>
    struct PeakDetectionPolicy
    {
        T m_threshold;

        explicit PeakDetectionPolicy(T threshold = T(0))
            : m_threshold(threshold) {}

        void onAdd(T val) { /* No incremental update needed */ }
        void onRemove(T val) { /* No incremental update needed */ }
        void clear() { /* No state to clear */ }

        /**
         * @brief Check if the middle sample in the window is a peak.
         * Requires exactly 3 samples in the buffer: [oldest, middle, newest]
         * Peak condition: oldest < middle > newest && middle >= threshold
         */
        T getResult(const std::vector<T> &buffer) const
        {
            if (buffer.size() < 3)
                return T(0);

            // Buffer is in chronological order: [oldest, middle, newest]
            T oldest = buffer[0];
            T middle = buffer[1];
            T newest = buffer[2];

            bool isPeak = (middle > oldest) && (middle > newest) && (middle >= m_threshold);
            return isPeak ? T(1) : T(0);
        }

        // For state serialization (threshold is constant)
        T getState() const { return m_threshold; }
        void setState(T threshold) { m_threshold = threshold; }
        T getThreshold() const { return m_threshold; }
    };

    /**
     * @brief Policy for peak detection in frequency domain.
     *
     * Used for batch operations on entire spectrum.
     * Not meant for use with SlidingWindowFilter.
     */
    template <typename T>
    struct FrequencyPeakPolicy
    {
        T m_threshold;
        size_t m_minPeakDistance;

        explicit FrequencyPeakPolicy(T threshold = T(0), size_t minPeakDistance = 1)
            : m_threshold(threshold), m_minPeakDistance(minPeakDistance) {}

        /**
         * @brief Detect peaks in frequency domain (batch operation).
         * Returns indices of spectral peaks above threshold.
         */
        std::vector<size_t> detectPeaks(const T *data, size_t length) const
        {
            std::vector<size_t> peakIndices;

            if (length < 3)
                return peakIndices;

            // Find local maxima in spectrum
            for (size_t i = 1; i < length - 1; ++i)
            {
                if (data[i] > data[i - 1] && data[i] > data[i + 1] && data[i] >= m_threshold)
                {
                    // Check minimum peak distance constraint
                    bool tooClose = false;
                    for (size_t peakIdx : peakIndices)
                    {
                        if (std::abs(static_cast<int>(i) - static_cast<int>(peakIdx)) < static_cast<int>(m_minPeakDistance))
                        {
                            // Keep the higher peak
                            if (data[i] > data[peakIdx])
                            {
                                peakIndices.erase(std::remove(peakIndices.begin(), peakIndices.end(), peakIdx), peakIndices.end());
                            }
                            else
                            {
                                tooClose = true;
                            }
                            break;
                        }
                    }

                    if (!tooClose)
                    {
                        peakIndices.push_back(i);
                    }
                }
            }

            return peakIndices;
        }

        // For state serialization
        std::pair<T, size_t> getState() const { return {m_threshold, m_minPeakDistance}; }
        void setState(T threshold, size_t minDist)
        {
            m_threshold = threshold;
            m_minPeakDistance = minDist;
        }
    };

    /**
     * @brief Policy for FIR filter convolution.
     *
     * Performs FIR filtering using dot product with filter coefficients.
     * The buffer stores recent input samples, and coefficients are the filter taps.
     * Note: SIMD optimization is handled in the FirFilter implementation.
     */
    template <typename T>
    struct FirConvolutionPolicy
    {
        std::vector<T> m_coefficients;

        explicit FirConvolutionPolicy(const std::vector<T> &coefficients)
            : m_coefficients(coefficients) {}

        void onAdd(T val) {}    // No incremental state to update
        void onRemove(T val) {} // No incremental state to update
        void clear() {}         // No state to clear

        // Compute convolution (dot product) with the buffer
        // Note: This is a simple scalar version; SIMD is applied in FirFilter
        T getResult(const std::vector<T> &buffer) const
        {
            if (buffer.empty() || m_coefficients.empty())
                return T(0);

            T result = T(0);
            size_t len = std::min(buffer.size(), m_coefficients.size());
            for (size_t i = 0; i < len; ++i)
            {
                result += buffer[i] * m_coefficients[i];
            }
            return result;
        }

        // For state serialization
        const std::vector<T> &getCoefficients() const { return m_coefficients; }
        void setCoefficients(const std::vector<T> &coeffs) { m_coefficients = coeffs; }
    };

} // namespace dsp::core
