#include "MovingVarianceFilter.h"

using namespace dsp::core;

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
template <typename T>
MovingVarianceFilter<T>::MovingVarianceFilter(size_t window_size)
    : buffer(window_size), // Initialize the circular buffer
      running_sum(0),
      running_sum_of_squares(0),
      window_size(window_size)
{
    if (window_size == 0)
    {
        throw std::invalid_argument("Window size must be greater than 0");
    }
}

// -----------------------------------------------------------------------------
// Time-aware Constructor
// -----------------------------------------------------------------------------
template <typename T>
MovingVarianceFilter<T>::MovingVarianceFilter(size_t window_size, double window_duration_ms)
    : buffer(window_size, window_duration_ms), // Initialize time-aware circular buffer
      running_sum(0),
      running_sum_of_squares(0),
      window_size(window_size)
{
    if (window_size == 0)
    {
        throw std::invalid_argument("Window size must be greater than 0");
    }
    if (window_duration_ms <= 0.0)
    {
        throw std::invalid_argument("Window duration must be greater than 0");
    }
}

// -----------------------------------------------------------------------------
// Method: addSample
// -----------------------------------------------------------------------------
template <typename T>
T MovingVarianceFilter<T>::addSample(T newValue)
{
    T oldestValue = 0;

    // If the buffer is full, get the oldest value
    if (buffer.isFull())
    {
        oldestValue = buffer.peek();
    }

    // Add the new value to the buffer
    buffer.pushOverwrite(newValue);

    // Update running sums
    T oldestValueSquared = oldestValue * oldestValue;
    T newValueSquared = newValue * newValue;

    running_sum = running_sum - oldestValue + newValue;
    running_sum_of_squares = running_sum_of_squares - oldestValueSquared + newValueSquared;

    // Return the new variance
    return getVariance();
} // -----------------------------------------------------------------------------
// Method: addSampleWithTimestamp
// -----------------------------------------------------------------------------
template <typename T>
T MovingVarianceFilter<T>::addSampleWithTimestamp(T newValue, double timestamp)
{
    // Expire old samples
    size_t initial_count = buffer.getCount();
    size_t expired_count = buffer.expireOld(timestamp);

    // Rebuild running statistics if samples were expired
    if (expired_count > 0)
    {
        running_sum = 0;
        running_sum_of_squares = 0;

        auto remaining = buffer.toVector();
        for (const auto &value : remaining)
        {
            running_sum += value;
            running_sum_of_squares += value * value;
        }
    }

    // Add new sample
    T oldestValue = 0;
    if (buffer.isFull())
    {
        oldestValue = buffer.peek();
    }

    buffer.pushOverwriteWithTimestamp(newValue, timestamp);

    // Update running sums
    T oldestValueSquared = oldestValue * oldestValue;
    T newValueSquared = newValue * newValue;

    running_sum = running_sum - oldestValue + newValue;
    running_sum_of_squares = running_sum_of_squares - oldestValueSquared + newValueSquared;

    return getVariance();
}

// -----------------------------------------------------------------------------
// Method: getVariance
// -----------------------------------------------------------------------------
template <typename T>
T MovingVarianceFilter<T>::getVariance() const
{
    size_t count = buffer.getCount();
    if (count == 0)
    {
        return 0; // Avoid division by zero
    }

    T countT = static_cast<T>(count);

    // Calculate mean
    T mean = running_sum / countT;

    // Calculate mean of squares
    T mean_of_squares = running_sum_of_squares / countT;

    // Variance = E[X^2] - (E[X])^2
    // Use std::max to prevent negative values from floating point inaccuracies
    T variance = std::max(static_cast<T>(0), mean_of_squares - (mean * mean));

    return variance;
}

// -----------------------------------------------------------------------------
// Method: clear
// -----------------------------------------------------------------------------
template <typename T>
void MovingVarianceFilter<T>::clear()
{
    buffer.clear();
    running_sum = 0;
    running_sum_of_squares = 0;
}

// -----------------------------------------------------------------------------
// Method: isFull
// -----------------------------------------------------------------------------
template <typename T>
bool MovingVarianceFilter<T>::isFull() const noexcept
{
    return buffer.isFull();
}

// -----------------------------------------------------------------------------
// Method: isTimeAware
// -----------------------------------------------------------------------------
template <typename T>
bool MovingVarianceFilter<T>::isTimeAware() const noexcept
{
    return buffer.isTimeAware();
}

// -----------------------------------------------------------------------------
// Method: getState
// -----------------------------------------------------------------------------
template <typename T>
std::pair<std::vector<T>, std::pair<T, T>> MovingVarianceFilter<T>::getState() const
{
    return {buffer.toVector(), {running_sum, running_sum_of_squares}};
}

// -----------------------------------------------------------------------------
// Method: setState
// -----------------------------------------------------------------------------
template <typename T>
void MovingVarianceFilter<T>::setState(const std::vector<T> &bufferData, T sum, T sumOfSquares)
{
    buffer.fromVector(bufferData);
    running_sum = sum;
    running_sum_of_squares = sumOfSquares;
}

// Explicit template instantiation for common types
namespace dsp::core
{
    template class MovingVarianceFilter<int>;
    template class MovingVarianceFilter<float>;
    template class MovingVarianceFilter<double>;
}