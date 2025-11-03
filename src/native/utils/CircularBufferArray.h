#pragma once

#include <vector>
#include <stdexcept>
#include <memory>

namespace dsp::utils
{

    template <typename T>

    class CircularBufferArray
    {
    public:
        // constructors
        explicit CircularBufferArray(size_t size, double windowDuration_ms = 0.0);
        CircularBufferArray(const CircularBufferArray &other) = delete;            // disable copy to avoid shallow copy
        CircularBufferArray &operator=(const CircularBufferArray &other) = delete; // disable copy assignment to avoid shallow copy

        // move semantics (defaulted - compiler generated is now safe with unique_ptr)
        CircularBufferArray(CircularBufferArray &&other) noexcept = default;
        CircularBufferArray &operator=(CircularBufferArray &&other) noexcept = default;

        // methods
        bool push(const T &item);
        bool pop(T &item) noexcept;
        void clear() noexcept;
        void pushOverwrite(const T &item);

        // Time-aware methods (require timestamps to be enabled)
        void pushWithTimestamp(const T &item, double timestamp);
        void pushOverwriteWithTimestamp(const T &item, double timestamp);
        size_t expireOld(double currentTimestamp);

        // getters (inline for performance)
        size_t getCapacity() const noexcept { return capacity; }
        size_t getCount() const noexcept { return count; }
        bool isEmpty() const noexcept { return count == 0; }
        bool isFull() const noexcept { return count == capacity; }
        T peek() const;
        bool isTimeAware() const noexcept { return windowDuration_ms > 0.0; }
        double getWindowDuration() const noexcept { return windowDuration_ms; }

        // state management
        std::vector<T> toVector() const;
        void fromVector(const std::vector<T> &data);
        std::vector<std::pair<double, T>> toVectorWithTimestamps() const;
        void fromVectorWithTimestamps(const std::vector<std::pair<double, T>> &data);

        /**
         * @brief Copy buffer contents to a destination array (zero-allocation).
         *
         * Copies the buffer contents in chronological order (oldest to newest)
         * directly to the provided destination pointer. This is more efficient
         * than toVector() when you already have allocated storage.
         *
         * @param dest Destination array (must have space for at least getCount() elements)
         */
        void copyTo(T *dest) const;

        // destructor (defaulted - unique_ptr handles cleanup automatically)
        ~CircularBufferArray() = default;

    private:
        std::unique_ptr<T[]> buffer;
        std::unique_ptr<double[]> timestamps; // Optional timestamp array (nullptr if not time-aware)
        size_t head;
        size_t tail;
        size_t capacity;
        size_t count;
        double windowDuration_ms; // Maximum age of samples (0 = disabled)
    };
} // namespace dsp::utils