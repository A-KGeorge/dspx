#ifndef DSPX_FFTCACHE_H
#define DSPX_FFTCACHE_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

namespace dsp
{
    namespace core
    {

        /**
         * Fast hash function for float arrays (inspired by xxHash)
         * Processes data in 8-byte chunks with prime number mixing
         */
        inline uint64_t hashFloatArray(const float *data, size_t length)
        {
            const uint64_t PRIME1 = 11400714785074694791ULL;
            const uint64_t PRIME2 = 14029467366897019727ULL;
            const uint64_t PRIME3 = 1609587929392839161ULL;
            const uint64_t PRIME4 = 9650029242287828579ULL;
            const uint64_t PRIME5 = 2870177450012600261ULL;

            uint64_t hash = PRIME5;
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
            size_t byteLength = length * sizeof(float);

            // Process 8-byte chunks
            while (byteLength >= 8)
            {
                uint64_t chunk;
                std::memcpy(&chunk, bytes, 8);

                hash ^= chunk * PRIME2;
                hash = ((hash << 31) | (hash >> 33)) * PRIME1;

                bytes += 8;
                byteLength -= 8;
            }

            // Process remaining bytes
            while (byteLength > 0)
            {
                hash ^= static_cast<uint64_t>(*bytes) * PRIME5;
                hash = ((hash << 11) | (hash >> 53)) * PRIME1;
                bytes++;
                byteLength--;
            }

            // Avalanche mixing
            hash ^= hash >> 33;
            hash *= PRIME2;
            hash ^= hash >> 29;
            hash *= PRIME3;
            hash ^= hash >> 32;

            return hash;
        }

        /**
         * LRU Cache for FFT results
         * Stores complex-valued FFT outputs keyed by input signal hash
         *
         * Template parameter T is the input type (float or double)
         */
        template <typename T>
        class FftCache
        {
        public:
            using Complex = std::complex<T>;

            struct CacheEntry
            {
                std::vector<T> input;        // Original input (for collision detection)
                std::vector<Complex> result; // FFT result
                size_t inputSize;            // Length of input
                bool isRealInput;            // true for RFFT, false for complex FFT
            };

            /**
             * Constructor
             * @param maxEntries Maximum number of cache entries (default: 128)
             * @param maxInputSize Don't cache inputs larger than this (default: 65536)
             */
            explicit FftCache(size_t maxEntries = 128, size_t maxInputSize = 65536)
                : m_maxEntries(maxEntries), m_maxInputSize(maxInputSize),
                  m_hits(0), m_misses(0) {}

            /**
             * Look up FFT result in cache
             * @param input Input signal
             * @param length Length of input
             * @param isRealInput true for RFFT, false for complex FFT
             * @param output Output buffer (caller allocates)
             * @return true if cache hit, false if miss
             */
            bool lookup(const T *input, size_t length, bool isRealInput, Complex *output)
            {
                // Don't cache very large inputs (hashing cost > computation savings)
                if (length > m_maxInputSize)
                {
                    m_misses++;
                    return false;
                }

                // Compute hash
                uint64_t hash = hashFloatArray(reinterpret_cast<const float *>(input), length);

                // Check cache
                auto it = m_cache.find(hash);
                if (it == m_cache.end())
                {
                    m_misses++;
                    return false;
                }

                // Verify collision (hash match but different data)
                const CacheEntry &entry = it->second;
                if (entry.inputSize != length || entry.isRealInput != isRealInput)
                {
                    m_misses++;
                    return false;
                }

                // Verify input matches (detect hash collisions)
                for (size_t i = 0; i < length; ++i)
                {
                    if (entry.input[i] != input[i])
                    {
                        m_misses++;
                        return false;
                    }
                }

                // Cache hit! Copy result
                std::memcpy(output, entry.result.data(),
                            entry.result.size() * sizeof(Complex));

                // Move to front of LRU list
                m_lruList.erase(m_lruMap[hash]);
                m_lruList.push_front(hash);
                m_lruMap[hash] = m_lruList.begin();

                m_hits++;
                return true;
            }

            /**
             * Store FFT result in cache
             * @param input Input signal
             * @param length Length of input
             * @param isRealInput true for RFFT, false for complex FFT
             * @param result FFT result
             * @param resultSize Length of result
             */
            void store(const T *input, size_t length, bool isRealInput,
                       const Complex *result, size_t resultSize)
            {
                // Don't cache very large inputs
                if (length > m_maxInputSize)
                {
                    return;
                }

                // Compute hash
                uint64_t hash = hashFloatArray(reinterpret_cast<const float *>(input), length);

                // Check if already in cache
                if (m_cache.find(hash) != m_cache.end())
                {
                    // Already cached, just update LRU
                    m_lruList.erase(m_lruMap[hash]);
                    m_lruList.push_front(hash);
                    m_lruMap[hash] = m_lruList.begin();
                    return;
                }

                // Evict oldest if cache full
                if (m_cache.size() >= m_maxEntries)
                {
                    uint64_t oldestHash = m_lruList.back();
                    m_lruList.pop_back();
                    m_lruMap.erase(oldestHash);
                    m_cache.erase(oldestHash);
                }

                // Create cache entry
                CacheEntry entry;
                entry.input.assign(input, input + length);
                entry.result.assign(result, result + resultSize);
                entry.inputSize = length;
                entry.isRealInput = isRealInput;

                // Store in cache
                m_cache[hash] = std::move(entry);
                m_lruList.push_front(hash);
                m_lruMap[hash] = m_lruList.begin();
            }

            /**
             * Clear all cache entries
             */
            void clear()
            {
                m_cache.clear();
                m_lruList.clear();
                m_lruMap.clear();
                m_hits = 0;
                m_misses = 0;
            }

            /**
             * Get cache statistics
             */
            size_t hits() const { return m_hits; }
            size_t misses() const { return m_misses; }
            double hitRate() const
            {
                size_t total = m_hits + m_misses;
                return total > 0 ? static_cast<double>(m_hits) / total : 0.0;
            }
            size_t size() const { return m_cache.size(); }

        private:
            size_t m_maxEntries;
            size_t m_maxInputSize;
            size_t m_hits;
            size_t m_misses;

            // Cache storage: hash -> entry
            std::unordered_map<uint64_t, CacheEntry> m_cache;

            // LRU tracking
            std::list<uint64_t> m_lruList; // Front = most recent
            std::unordered_map<uint64_t, std::list<uint64_t>::iterator> m_lruMap;
        };

    } // namespace core
} // namespace dsp

#endif // DSPX_FFTCACHE_H
