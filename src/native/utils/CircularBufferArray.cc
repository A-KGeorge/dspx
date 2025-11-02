#include "CircularBufferArray.h"
#include <algorithm>
#include <stdexcept>
#include <memory>

using namespace dsp::utils;

// -----------------------------------------------------------------------------
// Constructor
// Initializes the circular buffer with a specified size using std::make_unique
// @ param size - The size of the circular buffer
// @ param windowDuration_ms - Optional window duration for time-based expiration (0 = disabled)
// @ return void
// -----------------------------------------------------------------------------
template <typename T>
CircularBufferArray<T>::CircularBufferArray(size_t size, double windowDuration_ms)
    : buffer(std::make_unique<T[]>(std::max(size, static_cast<size_t>(1)))),
      timestamps(windowDuration_ms > 0.0 ? std::make_unique<double[]>(std::max(size, static_cast<size_t>(1))) : nullptr),
      head(0),
      tail(0),
      capacity(std::max(size, static_cast<size_t>(1))),
      count(0),
      windowDuration_ms(windowDuration_ms)
{
    // Buffers are automatically initialized by make_unique
}

// Note: Move constructor and move assignment operator are now defaulted in the header
// std::unique_ptr handles move semantics correctly by default

// -----------------------------------------------------------------------------
// Method: push
// Adds an item to the circular buffer
// @ param item - The item to add
// @ return bool - True if the item was added, false if the buffer is full
// -----------------------------------------------------------------------------
template <typename T>
bool CircularBufferArray<T>::push(const T &item)
{
    if (isFull())
    {
        return false; // Buffer is full
    }

    this->buffer[this->head] = item;
    this->head = (this->head + 1) % this->capacity;
    this->count++;
    return true;
}

// -----------------------------------------------------------------------------
// Method: pop
// Removes an item from the circular buffer
// @ param item - The item to remove
// @ return bool - True if the item was removed, false if the buffer is empty
// -----------------------------------------------------------------------------
template <typename T>
bool CircularBufferArray<T>::pop(T &item) noexcept
{
    if (isEmpty())
    {
        return false; // Buffer is empty
    }

    item = this->buffer[this->tail];
    this->tail = (this->tail + 1) % this->capacity;
    this->count--;
    return true;
}

// -----------------------------------------------------------------------------

// Method: clear
// Clears the circular buffer
// @ param void
// @ return void
// -----------------------------------------------------------------------------
template <typename T>
void CircularBufferArray<T>::clear() noexcept
{
    this->head = 0;
    this->tail = 0;
    this->count = 0;
}

// -----------------------------------------------------------------------------
// Method: pushOverwrite
// Adds an item to the circular buffer, overwriting the oldest item if full
// @ param item - The item to add
// @ return bool - Always returns true
template <typename T>
void CircularBufferArray<T>::pushOverwrite(const T &item)
{
    if (isFull())
        this->tail = (this->tail + 1) % this->capacity;
    this->buffer[this->head] = item;
    this->head = (this->head + 1) % this->capacity;
    if (this->count < this->capacity)
        ++this->count;
}

// -----------------------------------------------------------------------------
// Method: peek
// Returns the item at the head of the circular buffer without removing it
// @ param void
// @ return T - The item at the head of the buffer
template <typename T>
T CircularBufferArray<T>::peek() const
{
    if (isEmpty())
    {
        throw std::runtime_error("Buffer is empty");
    }

    return this->buffer[this->tail];
}

// -----------------------------------------------------------------------------
// Method: toVector
// Exports the buffer contents in order (oldest to newest) as a vector
// @ return std::vector<T> - The buffer contents in order
// -----------------------------------------------------------------------------
template <typename T>
std::vector<T> CircularBufferArray<T>::toVector() const
{
    std::vector<T> result;
    result.reserve(this->count);

    for (size_t i = 0; i < this->count; ++i)
    {
        size_t index = (this->tail + i) % this->capacity;
        result.push_back(this->buffer[index]);
    }

    return result;
}

// -----------------------------------------------------------------------------
// Method: fromVector
// Imports buffer contents from a vector, maintaining order
// @ param data - The vector containing the data to import
// -----------------------------------------------------------------------------
template <typename T>
void CircularBufferArray<T>::fromVector(const std::vector<T> &data)
{
    clear();

    for (const auto &item : data)
    {
        pushOverwrite(item);
    }
}

// -----------------------------------------------------------------------------
// Method: pushWithTimestamp
// Adds an item with a timestamp to the buffer (requires time-aware mode)
// @ param item - The item to add
// @ param timestamp - The timestamp in milliseconds
// -----------------------------------------------------------------------------
template <typename T>
void CircularBufferArray<T>::pushWithTimestamp(const T &item, double timestamp)
{
    if (!isTimeAware())
    {
        throw std::runtime_error("pushWithTimestamp requires time-aware mode (windowDuration > 0)");
    }

    if (isFull())
    {
        return; // Buffer is full
    }

    buffer[head] = item;
    timestamps[head] = timestamp;
    head = (head + 1) % capacity;
    count++;
}

// -----------------------------------------------------------------------------
// Method: pushOverwriteWithTimestamp
// Adds an item with timestamp, overwriting oldest if full (requires time-aware mode)
// @ param item - The item to add
// @ param timestamp - The timestamp in milliseconds
// -----------------------------------------------------------------------------
template <typename T>
void CircularBufferArray<T>::pushOverwriteWithTimestamp(const T &item, double timestamp)
{
    if (!isTimeAware())
    {
        throw std::runtime_error("pushOverwriteWithTimestamp requires time-aware mode (windowDuration > 0)");
    }

    if (isFull())
    {
        tail = (tail + 1) % capacity;
    }

    buffer[head] = item;
    timestamps[head] = timestamp;
    head = (head + 1) % capacity;

    if (count < capacity)
    {
        ++count;
    }
}

// -----------------------------------------------------------------------------
// Method: expireOld
// Removes samples older than windowDuration from the current timestamp
// @ param currentTimestamp - The current timestamp in milliseconds
// @ return size_t - Number of samples expired
// -----------------------------------------------------------------------------
template <typename T>
size_t CircularBufferArray<T>::expireOld(double currentTimestamp)
{
    if (!isTimeAware() || isEmpty())
    {
        return 0;
    }

    size_t expired_count = 0;
    double cutoff_time = currentTimestamp - windowDuration_ms;

    // Remove samples from tail while they're older than cutoff
    while (count > 0 && timestamps[tail] < cutoff_time)
    {
        tail = (tail + 1) % capacity;
        --count;
        ++expired_count;
    }

    return expired_count;
}

// -----------------------------------------------------------------------------
// Method: toVectorWithTimestamps
// Exports the buffer contents with timestamps (requires time-aware mode)
// @ return std::vector<std::pair<double, T>> - The buffer contents with timestamps
// -----------------------------------------------------------------------------
template <typename T>
std::vector<std::pair<double, T>> CircularBufferArray<T>::toVectorWithTimestamps() const
{
    if (!isTimeAware())
    {
        throw std::runtime_error("toVectorWithTimestamps requires time-aware mode");
    }

    std::vector<std::pair<double, T>> result;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        size_t index = (tail + i) % capacity;
        result.push_back({timestamps[index], buffer[index]});
    }

    return result;
}

// -----------------------------------------------------------------------------
// Method: fromVectorWithTimestamps
// Imports buffer contents with timestamps (requires time-aware mode)
// @ param data - The vector containing (timestamp, value) pairs
// -----------------------------------------------------------------------------
template <typename T>
void CircularBufferArray<T>::fromVectorWithTimestamps(const std::vector<std::pair<double, T>> &data)
{
    if (!isTimeAware())
    {
        throw std::runtime_error("fromVectorWithTimestamps requires time-aware mode");
    }

    clear();

    for (const auto &[timestamp, value] : data)
    {
        pushOverwriteWithTimestamp(value, timestamp);
    }
}

// Note: Destructor is now defaulted in the header
// std::unique_ptr automatically cleans up the buffers

// Explicit template instantiation for common types
namespace dsp::utils
{
    template class CircularBufferArray<int>;
    template class CircularBufferArray<float>;
    template class CircularBufferArray<double>;
    template class CircularBufferArray<bool>;
}
