#include "SlidingWindowFilter.h"
#include "ConvolutionPolicy.h"
#include "../core/Policies.h"

using namespace dsp::utils;

// -----------------------------------------------------------------------------
// Constructor
// Constructs a new sliding window filter with the specified window size
// @ param window_size - The number of samples in the sliding window
// @ param policy - An instance of the policy (default-constructed if not provided)
// @ return void
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
SlidingWindowFilter<T, Policy>::SlidingWindowFilter(size_t window_size, Policy policy)
    : m_buffer(window_size), m_policy(std::move(policy))
{
}

// -----------------------------------------------------------------------------
// Constructor (Time-Aware)
// Constructs a new time-aware sliding window filter
// @ param window_size - The number of samples in the sliding window
// @ param window_duration_ms - The maximum age of samples in milliseconds
// @ param policy - An instance of the policy (default-constructed if not provided)
// @ return void
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
SlidingWindowFilter<T, Policy>::SlidingWindowFilter(size_t window_size, double window_duration_ms, Policy policy)
    : m_buffer(window_size, window_duration_ms), m_policy(std::move(policy))
{
}

// -----------------------------------------------------------------------------
// addSample
// Adds a new sample to the sliding window
// If buffer is full, removes oldest sample (delegates to policy.onRemove)
// Then adds new sample (delegates to policy.onAdd)
// @ param newValue - The new sample to add
// @ return T - The computed result from the policy
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
T SlidingWindowFilter<T, Policy>::addSample(T newValue)
{
    if (m_buffer.isFull())
    {
        T oldestValue = m_buffer.peek();
        m_policy.onRemove(oldestValue);
    }

    m_buffer.pushOverwrite(newValue);
    m_policy.onAdd(newValue);

    return m_policy.getResult(m_buffer.toVector());
}

// -----------------------------------------------------------------------------
// addSampleWithTimestamp
// Adds a new sample with timestamp (time-aware mode)
// Expires old samples first, then adds the new sample
// @ param newValue - The new sample to add
// @ param timestamp - The timestamp in milliseconds
// @ return T - The computed result from the policy
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
T SlidingWindowFilter<T, Policy>::addSampleWithTimestamp(T newValue, double timestamp)
{
    if (!m_buffer.isTimeAware())
    {
        throw std::runtime_error("addSampleWithTimestamp requires time-aware mode");
    }

    // First, expire old samples and update policy
    while (m_buffer.getCount() > 0)
    {
        size_t initial_count = m_buffer.getCount();
        m_buffer.expireOld(timestamp);
        size_t expired_count = initial_count - m_buffer.getCount();

        // We need to reconstruct the policy state from remaining samples
        // This is inefficient but correct. Better approach: track what was removed
        if (expired_count > 0)
        {
            // Rebuild policy from scratch using remaining samples
            m_policy.clear();
            auto remaining = m_buffer.toVector();
            for (const auto &value : remaining)
            {
                m_policy.onAdd(value);
            }
        }
        break; // expireOld already removed all expired samples
    }

    // Check if buffer is full (by sample count)
    if (m_buffer.isFull())
    {
        T oldestValue = m_buffer.peek();
        m_policy.onRemove(oldestValue);
    }

    // Add the new sample
    m_buffer.pushOverwriteWithTimestamp(newValue, timestamp);
    m_policy.onAdd(newValue);

    return m_policy.getResult(m_buffer.toVector());
}

// -----------------------------------------------------------------------------
// clear
// Clears all samples from the filter
// Resets both the circular buffer and the policy state
// @ return void
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
void SlidingWindowFilter<T, Policy>::clear()
{
    m_buffer.clear();
    m_policy.clear();
}

// -----------------------------------------------------------------------------
// isFull
// Checks if the buffer is full
// @ return bool - true if buffer contains window_size samples
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
bool SlidingWindowFilter<T, Policy>::isFull() const noexcept
{
    return m_buffer.isFull();
}

// -----------------------------------------------------------------------------
// getCount
// Gets the current number of samples in the buffer
// @ return size_t - The number of samples
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
size_t SlidingWindowFilter<T, Policy>::getCount() const noexcept
{
    return m_buffer.getCount();
}

// -----------------------------------------------------------------------------
// getWindowSize
// Gets the window size
// @ return size_t - The maximum number of samples in the window
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
size_t SlidingWindowFilter<T, Policy>::getWindowSize() const noexcept
{
    return m_buffer.getCapacity();
}

// -----------------------------------------------------------------------------
// getBufferContents
// Exports the buffer contents
// @ return std::vector<T> - A vector containing all samples in the buffer
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
std::vector<T> SlidingWindowFilter<T, Policy>::getBufferContents() const
{
    return m_buffer.toVector();
}

// -----------------------------------------------------------------------------
// setBufferContents
// Restores the buffer contents
// @ param bufferData - The samples to restore to the buffer
// @ return void
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
void SlidingWindowFilter<T, Policy>::setBufferContents(const std::vector<T> &bufferData)
{
    m_buffer.fromVector(bufferData);
}

// -----------------------------------------------------------------------------
// getPolicy
// Access to the policy for state serialization
// @ return Policy& - Reference to the internal policy object
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
Policy &SlidingWindowFilter<T, Policy>::getPolicy()
{
    return m_policy;
}

// -----------------------------------------------------------------------------
// getPolicy (const)
// Const access to the policy
// @ return const Policy& - Const reference to the internal policy object
// -----------------------------------------------------------------------------
template <typename T, typename Policy>
const Policy &SlidingWindowFilter<T, Policy>::getPolicy() const
{
    return m_policy;
}

// Note: getState() and setState() are template methods defined in the header
// They use decltype and are auto-deduced, so they must remain in the header

// -----------------------------------------------------------------------------
// Explicit template instantiations
// Instantiate all Policy combinations we use
// -----------------------------------------------------------------------------
namespace dsp::utils
{
    using namespace dsp::core;

    // MeanPolicy instantiations
    template class SlidingWindowFilter<int, MeanPolicy<int>>;
    template class SlidingWindowFilter<float, MeanPolicy<float>>;
    template class SlidingWindowFilter<double, MeanPolicy<double>>;

    // RmsPolicy instantiations
    template class SlidingWindowFilter<int, RmsPolicy<int>>;
    template class SlidingWindowFilter<float, RmsPolicy<float>>;
    template class SlidingWindowFilter<double, RmsPolicy<double>>;

    // MeanAbsoluteValuePolicy instantiations
    template class SlidingWindowFilter<float, MeanAbsoluteValuePolicy<float>>;
    template class SlidingWindowFilter<double, MeanAbsoluteValuePolicy<double>>;

    // VariancePolicy instantiations
    template class SlidingWindowFilter<int, VariancePolicy<int>>;
    template class SlidingWindowFilter<float, VariancePolicy<float>>;
    template class SlidingWindowFilter<double, VariancePolicy<double>>;

    // Note: ZScorePolicy is NOT instantiated here because it has a different interface
    // (getResult takes 2 parameters: currentValue and count, not just count)
    // MovingZScoreFilter doesn't use SlidingWindowFilter template
    // Same goes for FrequencyPeakPolicy, since it isn't meant to be used with SlidingWindowFilter

    template class SlidingWindowFilter<float, dsp::core::PeakDetectionPolicy<float>>;
    template class SlidingWindowFilter<double, dsp::core::PeakDetectionPolicy<double>>;

    // WaveformLengthFilter instantiations
    template class dsp::utils::SlidingWindowFilter<float, SumPolicy<float>>;
    template class dsp::utils::SlidingWindowFilter<double, SumPolicy<double>>;

    // WAMP,SSC instantiations
    template class dsp::utils::SlidingWindowFilter<bool, CounterPolicy>;

    // ConvolutionPolicy instantiations
    template class SlidingWindowFilter<float, ConvolutionPolicy<float>>;
    template class SlidingWindowFilter<double, ConvolutionPolicy<double>>;
}
