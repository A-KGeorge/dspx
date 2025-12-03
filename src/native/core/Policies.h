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

        T getResult(size_t count) const
        {
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

        T getResult(size_t count) const
        {
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

        T getResult(size_t count) const
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
        float getResult(size_t count) const
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

        T getResult(size_t count) const
        {
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

        T getResult(size_t count) const
        {
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

    /**
     * @brief Policy for Exponential Moving Average (EMA).
     *
     * Implements EMA: EMA(t) = α * value(t) + (1 - α) * EMA(t-1)
     * where α (alpha) is the smoothing factor (0 < α ≤ 1).
     *
     * This policy is optimized for scalar operations and can be SIMD-accelerated
     * in batch processing contexts.
     */
    template <typename T>
    struct EmaPolicy
    {
        T m_ema = 0; // Current EMA value
        T m_alpha;   // Smoothing factor
        bool m_initialized = false;

        explicit EmaPolicy(T alpha)
            : m_alpha(alpha)
        {
            if (alpha <= 0 || alpha > 1)
            {
                throw std::invalid_argument("EMA alpha must be in range (0, 1]");
            }
        }

        void onAdd(T val)
        {
            if (!m_initialized)
            {
                // Initialize with first value
                m_ema = val;
                m_initialized = true;
            }
            else
            {
                // EMA formula: EMA(t) = α * value(t) + (1 - α) * EMA(t-1)
                m_ema = m_alpha * val + (static_cast<T>(1) - m_alpha) * m_ema;
            }
        }

        void onRemove(T val)
        {
            // EMA doesn't support removal in sliding window context
            // This should not be called in typical EMA usage
        }

        void clear()
        {
            m_ema = 0;
            m_initialized = false;
        }

        T getResult(size_t count) const
        {
            return m_ema;
        }

        // For state serialization
        std::pair<T, bool> getState() const { return {m_ema, m_initialized}; }
        void setState(T ema, bool initialized)
        {
            m_ema = ema;
            m_initialized = initialized;
        }

        T getAlpha() const { return m_alpha; }
    };

    /**
     * @brief Policy for Cumulative Moving Average (CMA).
     *
     * Implements CMA: CMA(n) = (CMA(n-1) * (n-1) + value(n)) / n
     *
     * Maintains the cumulative average over all samples seen since initialization.
     * More efficient than recalculating from scratch each time.
     */
    template <typename T>
    struct CmaPolicy
    {
        T m_sum = 0;        // Running sum of all values
        size_t m_count = 0; // Total number of samples seen

        void onAdd(T val)
        {
            m_sum += val;
            m_count++;
        }

        void onRemove(T val)
        {
            // CMA doesn't support removal in typical usage
            // If called, decrement count and sum
            if (m_count > 0)
            {
                m_sum -= val;
                m_count--;
            }
        }

        void clear()
        {
            m_sum = 0;
            m_count = 0;
        }

        T getResult(size_t windowCount) const
        {
            // Use the policy's internal count, not the window count
            if (m_count == 0)
                return 0;
            return m_sum / static_cast<T>(m_count);
        }

        // For state serialization
        std::pair<T, size_t> getState() const { return {m_sum, m_count}; }
        void setState(T sum, size_t count)
        {
            m_sum = sum;
            m_count = count;
        }

        size_t getCount() const { return m_count; }
    };

} // namespace dsp::core
