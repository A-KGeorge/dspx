#pragma once
#include "Policies.h"
#include <vector>
#include <stdexcept>

namespace dsp::core
{
    /**
     * @brief Implements a Cumulative Moving Average (CMA) filter.
     *
     * CMA is the average of all samples seen since initialization.
     * Formula: CMA(n) = (CMA(n-1) * (n-1) + value(n)) / n
     *          or: CMA(n) = sum(values[1..n]) / n
     *
     * Unlike simple moving average (SMA), CMA considers ALL historical data:
     * - SMA: Uses fixed window of recent N samples
     * - CMA: Uses all samples from start to current
     *
     * Properties:
     * - Memory-efficient: Only stores running sum and count
     * - Stable convergence: Influence of new samples decreases over time
     * - No window size needed: Adapts to any data length
     *
     * Use cases:
     * - Long-term averages where all history matters
     * - Baseline estimation from calibration data
     * - Online mean estimation for statistics
     *
     * @tparam T The numeric type of the samples (e.g., float, double).
     */
    template <typename T>
    class CumulativeMovingAverageFilter
    {
    public:
        /**
         * @brief Constructs a new CMA Filter.
         */
        CumulativeMovingAverageFilter()
            : m_policy()
        {
        }

        // Delete copy constructor and copy assignment
        CumulativeMovingAverageFilter(const CumulativeMovingAverageFilter &) = delete;
        CumulativeMovingAverageFilter &operator=(const CumulativeMovingAverageFilter &) = delete;

        // Enable move semantics
        CumulativeMovingAverageFilter(CumulativeMovingAverageFilter &&) noexcept = default;
        CumulativeMovingAverageFilter &operator=(CumulativeMovingAverageFilter &&) noexcept = default;

        /**
         * @brief Adds a new sample and updates the CMA.
         * @param newValue The new sample value.
         * @return T The updated CMA value.
         */
        T addSample(T newValue)
        {
            m_policy.onAdd(newValue);
            return m_policy.getResult(0); // Window count parameter unused for CMA
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
         * @brief Gets the current CMA value.
         * @return T The cumulative moving average.
         */
        T getCma() const { return m_policy.getResult(0); }

        /**
         * @brief Gets the number of samples processed.
         * @return size_t The total count of samples.
         */
        size_t getCount() const { return m_policy.getCount(); }

        /**
         * @brief Clears the CMA state (resets to zero).
         */
        void clear() { m_policy.clear(); }

        /**
         * @brief Checks if any samples have been processed.
         * @return true if count > 0, false otherwise.
         */
        bool hasData() const { return m_policy.getCount() > 0; }

        /**
         * @brief Exports the filter's internal state.
         * @return A pair containing the running sum and sample count.
         */
        std::pair<T, size_t> getState() const
        {
            return m_policy.getState();
        }

        /**
         * @brief Restores the filter's internal state.
         * @param sum The running sum to restore.
         * @param count The sample count to restore.
         */
        void setState(T sum, size_t count)
        {
            m_policy.setState(sum, count);
        }

    private:
        CmaPolicy<T> m_policy;
    };

} // namespace dsp::core
