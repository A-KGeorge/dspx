#pragma once
#include "Policies.h"
#include <vector>
#include <stdexcept>

namespace dsp::core
{
    /**
     * @brief Implements an Exponential Moving Average (EMA) filter.
     *
     * EMA gives more weight to recent samples and exponentially decaying weight to older samples.
     * Formula: EMA(t) = α * value(t) + (1 - α) * EMA(t-1)
     *
     * where α (alpha) is the smoothing factor:
     * - α close to 1: Fast response to changes (less smoothing)
     * - α close to 0: Slow response to changes (more smoothing)
     *
     * Common conversions:
     * - From N-period SMA: α = 2 / (N + 1)
     * - From time constant: α = 1 - exp(-Δt / τ)
     *
     * Unlike simple moving average, EMA does not require a fixed window size,
     * making it memory-efficient for very long averaging periods.
     *
     * @tparam T The numeric type of the samples (e.g., float, double).
     */
    template <typename T>
    class ExponentialMovingAverageFilter
    {
    public:
        /**
         * @brief Constructs a new EMA Filter with specified alpha.
         * @param alpha The smoothing factor (0 < α ≤ 1).
         * @throws std::invalid_argument if alpha is outside valid range.
         */
        explicit ExponentialMovingAverageFilter(T alpha)
            : m_policy(alpha)
        {
            if (alpha <= 0 || alpha > 1)
            {
                throw std::invalid_argument("EMA alpha must be in range (0, 1]");
            }
        }

        // Delete copy constructor and copy assignment
        ExponentialMovingAverageFilter(const ExponentialMovingAverageFilter &) = delete;
        ExponentialMovingAverageFilter &operator=(const ExponentialMovingAverageFilter &) = delete;

        // Enable move semantics
        ExponentialMovingAverageFilter(ExponentialMovingAverageFilter &&) noexcept = default;
        ExponentialMovingAverageFilter &operator=(ExponentialMovingAverageFilter &&) noexcept = default;

        /**
         * @brief Adds a new sample and updates the EMA.
         * @param newValue The new sample value.
         * @return T The updated EMA value.
         */
        T addSample(T newValue)
        {
            m_policy.onAdd(newValue);
            return m_policy.getResult(0); // Count parameter unused for EMA
        }

        /**
         * @brief Process array of samples in batch (optimized for throughput).
         *
         * @param input Input array of samples
         * @param output Output array (same size as input)
         * @param length Number of samples to process
         */
        void processArray(const T *input, T *output, size_t length)
        {
            for (size_t i = 0; i < length; ++i)
            {
                output[i] = addSample(input[i]);
            }
        }

        /**
         * @brief Gets the current EMA value.
         * @return T The current exponential moving average.
         */
        T getEma() const { return m_policy.getResult(0); }

        /**
         * @brief Gets the alpha (smoothing factor).
         * @return T The alpha value.
         */
        T getAlpha() const { return m_policy.getAlpha(); }

        /**
         * @brief Clears the EMA state.
         */
        void clear() { m_policy.clear(); }

        /**
         * @brief Checks if the filter has been initialized with at least one sample.
         * @return true if initialized, false otherwise.
         */
        bool isInitialized() const
        {
            auto [ema, initialized] = m_policy.getState();
            return initialized;
        }

        /**
         * @brief Exports the filter's internal state.
         * @return A pair containing the current EMA value and initialization flag.
         */
        std::pair<T, bool> getState() const
        {
            return m_policy.getState();
        }

        /**
         * @brief Restores the filter's internal state.
         * @param ema The EMA value to restore.
         * @param initialized The initialization flag to restore.
         */
        void setState(T ema, bool initialized)
        {
            m_policy.setState(ema, initialized);
        }

    private:
        EmaPolicy<T> m_policy;
    };

} // namespace dsp::core
