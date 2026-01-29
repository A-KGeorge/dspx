#pragma once
#include <vector>
#include <cstring>
#include <string>
#include <string_view>
#include <span>
#include <bit>
#include <stdexcept>

// SIMD intrinsics for byte swapping
#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64)))
#include <emmintrin.h>
#if defined(__SSSE3__) || (defined(_MSC_VER) && defined(__AVX__))
#include <tmmintrin.h>
#define TOON_HAS_SSSE3
#endif
#define TOON_HAS_SSE2
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define TOON_HAS_NEON
#endif

namespace dsp
{
    namespace toon
    {

        // TOON Protocol Tokens
        enum Token : uint8_t
        {
            T_NULL = 0x00,
            T_INT32 = 0x01,
            T_FLOAT = 0x02,
            T_STRING = 0x03,
            T_FLOAT_ARRAY = 0x04, // Optimized for SIMD/DSP (32-byte aligned)
            T_OBJECT_START = 0x10,
            T_OBJECT_END = 0x11,
            T_ARRAY_START = 0x12,
            T_ARRAY_END = 0x13,
            T_BOOL = 0x14,
            T_DOUBLE = 0x15
        };

        // Endianness handling for cross-platform compatibility
        template <typename T>
        inline T swap_if_needed(T value)
        {
            if constexpr (std::endian::native == std::endian::little)
            {
                return value; // Little-endian: no swap needed (x86, ARM in LE mode)
            }
            else
            {
                // Big-endian: swap to little-endian (C++23)
                if constexpr (std::is_same_v<T, float>)
                {
                    auto bits = std::bit_cast<uint32_t>(value);
                    return std::bit_cast<float>(std::byteswap(bits));
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    auto bits = std::bit_cast<uint64_t>(value);
                    return std::bit_cast<double>(std::byteswap(bits));
                }
                else
                {
                    return std::byteswap(value); // For int32_t and other integral types
                }
            }
        }

        // SIMD-optimized byte swapping for float arrays (big-endian only)
        inline void swap_float_array_simd(const float *src, float *dst, size_t count)
        {
            size_t i = 0;

#if defined(TOON_HAS_SSSE3)
            // SSE2/SSSE3: Process 4 floats at a time
            const __m128i shuffle_mask = _mm_set_epi8(
                12, 13, 14, 15, // Swap bytes of float[3]
                8, 9, 10, 11,   // Swap bytes of float[2]
                4, 5, 6, 7,     // Swap bytes of float[1]
                0, 1, 2, 3      // Swap bytes of float[0]
            );

            for (; i + 4 <= count; i += 4)
            {
                __m128 data = _mm_loadu_ps(src + i);
                __m128i data_i = _mm_castps_si128(data);
                __m128i swapped = _mm_shuffle_epi8(data_i, shuffle_mask);
                _mm_storeu_ps(dst + i, _mm_castsi128_ps(swapped));
            }
#elif defined(TOON_HAS_SSE2)
            // SSE2 fallback: Use shuffles instead of _mm_shuffle_epi8
            for (; i + 4 <= count; i += 4)
            {
                __m128 data = _mm_loadu_ps(src + i);
                __m128i data_i = _mm_castps_si128(data);

                // Swap bytes using SSE2 shuffles
                __m128i swapped = _mm_or_si128(
                    _mm_or_si128(
                        _mm_slli_epi32(data_i, 24),
                        _mm_slli_epi32(_mm_and_si128(data_i, _mm_set1_epi32(0x0000FF00)), 8)),
                    _mm_or_si128(
                        _mm_srli_epi32(_mm_and_si128(data_i, _mm_set1_epi32(0x00FF0000)), 8),
                        _mm_srli_epi32(data_i, 24)));

                _mm_storeu_ps(dst + i, _mm_castsi128_ps(swapped));
            }
#elif defined(TOON_HAS_NEON)
            // ARM NEON: Process 4 floats at a time
            for (; i + 4 <= count; i += 4)
            {
                float32x4_t data = vld1q_f32(src + i);
                uint8x16_t data_u8 = vreinterpretq_u8_f32(data);
                uint8x16_t swapped = vrev32q_u8(data_u8);
                vst1q_f32(dst + i, vreinterpretq_f32_u8(swapped));
            }
#endif

            // Scalar fallback for remaining elements
            for (; i < count; ++i)
            {
                auto bits = std::bit_cast<uint32_t>(src[i]);
                dst[i] = std::bit_cast<float>(std::byteswap(bits));
            }
        }

        // --- SERIALIZER ---
        class Serializer
        {
        public:
            std::vector<uint8_t> buffer;

            Serializer(size_t initial_capacity = 1024)
            {
                buffer.reserve(initial_capacity);
            }

            void writeInt32(int32_t val)
            {
                writeTag(T_INT32);
                val = swap_if_needed(val);
                appendRaw(&val, sizeof(val));
            }

            void writeFloat(float val)
            {
                writeTag(T_FLOAT);
                val = swap_if_needed(val);
                appendRaw(&val, sizeof(val));
            }

            void writeDouble(double val)
            {
                writeTag(T_DOUBLE);
                val = swap_if_needed(val);
                appendRaw(&val, sizeof(val));
            }

            void writeBool(bool val)
            {
                writeTag(T_BOOL);
                buffer.push_back(val ? 1 : 0);
            }

            void writeString(std::string_view val)
            {
                writeTag(T_STRING);
                int32_t len = static_cast<int32_t>(val.length());
                len = swap_if_needed(len);
                appendRaw(&len, sizeof(len));
                appendRaw(val.data(), val.length());
            }

            // Writes float array with 32-byte alignment for SIMD
            void writeFloatArray(std::span<const float> data)
            {
                writeTag(T_FLOAT_ARRAY);
                int32_t count = static_cast<int32_t>(data.size());
                count = swap_if_needed(count);
                appendRaw(&count, sizeof(count));

                // Align the data payload to 32 bytes for AVX2/NEON
                size_t remainder = buffer.size() % 32;
                if (remainder != 0)
                {
                    buffer.resize(buffer.size() + (32 - remainder), 0x00);
                }

                if (data.size() > 0)
                {
                    // On big-endian systems, we need to swap each float
                    if constexpr (std::endian::native != std::endian::little)
                    {
                        // SIMD-optimized byte swapping
                        std::vector<float> swapped(data.size());
                        swap_float_array_simd(data.data(), swapped.data(), data.size());
                        appendRaw(swapped.data(), swapped.size() * sizeof(float));
                    }
                    else
                    {
                        // On little-endian, write directly (zero-copy)
                        appendRaw(data.data(), data.size() * sizeof(float));
                    }
                }
            }

            void startObject() { writeTag(T_OBJECT_START); }
            void endObject() { writeTag(T_OBJECT_END); }
            void startArray() { writeTag(T_ARRAY_START); }
            void endArray() { writeTag(T_ARRAY_END); }

        private:
            inline void writeTag(Token tag)
            {
                buffer.push_back(static_cast<uint8_t>(tag));
            }

            void appendRaw(const void *ptr, size_t size)
            {
                const uint8_t *bytes = static_cast<const uint8_t *>(ptr);
                buffer.insert(buffer.end(), bytes, bytes + size);
            }
        };

        // --- DESERIALIZER ---
        class Deserializer
        {
        public:
            const uint8_t *data;
            size_t size;
            size_t pos;
            bool error_state;

            Deserializer(const uint8_t *d, size_t s) : data(d), size(s), pos(0), error_state(false) {}

            bool hasError() const { return error_state; }

            Token peekToken()
            {
                if (pos >= size)
                    return T_NULL;
                return static_cast<Token>(data[pos]);
            }

            Token readToken()
            {
                if (pos >= size)
                {
                    error_state = true;
                    return T_NULL;
                }
                return static_cast<Token>(data[pos++]);
            }

            int32_t readInt32()
            {
                if (!consumeToken(T_INT32))
                    return 0;
                return swap_if_needed(readRaw<int32_t>());
            }

            float readFloat()
            {
                if (!consumeToken(T_FLOAT))
                    return 0.0f;
                return swap_if_needed(readRaw<float>());
            }

            double readDouble()
            {
                if (!consumeToken(T_DOUBLE))
                    return 0.0;
                return swap_if_needed(readRaw<double>());
            }

            bool readBool()
            {
                if (!consumeToken(T_BOOL))
                    return false;
                if (pos >= size)
                {
                    error_state = true;
                    return false;
                }
                return data[pos++] != 0;
            }

            // Zero-copy string view (modern approach)
            std::string_view readStringView()
            {
                if (!consumeToken(T_STRING))
                    return {};
                int32_t len = swap_if_needed(readRaw<int32_t>());
                if (error_state || len < 0 || pos + static_cast<size_t>(len) > size)
                {
                    error_state = true;
                    return {};
                }
                std::string_view sv(reinterpret_cast<const char *>(data + pos), len);
                pos += len;
                return sv;
            }

            // Backward compatibility: copies to std::string
            std::string readString()
            {
                auto sv = readStringView();
                return std::string(sv);
            }

            // Zero-copy span for float arrays (SIMD-friendly!)
            std::span<const float> readFloatSpan()
            {
                if (!consumeToken(T_FLOAT_ARRAY))
                    return {};
                int32_t count = swap_if_needed(readRaw<int32_t>());

                if (error_state || count < 0)
                {
                    error_state = true;
                    return {};
                }

                // Align pos to 32-byte boundary (matching serializer)
                size_t remainder = pos % 32;
                if (remainder != 0)
                    pos += (32 - remainder);

                size_t bytes = static_cast<size_t>(count) * sizeof(float);
                if (pos + bytes > size)
                {
                    error_state = true;
                    return {};
                }

                // Zero-copy span only works on little-endian systems
                // On big-endian, the floats need to be swapped, so we can't return a direct span
                if constexpr (std::endian::native == std::endian::little)
                {
                    std::span<const float> s(reinterpret_cast<const float *>(data + pos), count);
                    pos += bytes;
                    return s;
                }
                else
                {
                    // Big-endian: must use readFloatArray() which will swap each float
                    error_state = true;
                    return {};
                }
            }

            // Backward compatibility: copies to std::vector
            std::vector<float> readFloatArray()
            {
                if constexpr (std::endian::native == std::endian::little)
                {
                    // Little-endian: use zero-copy span
                    auto span = readFloatSpan();
                    if (error_state)
                        return {};
                    return std::vector<float>(span.begin(), span.end());
                }
                else
                {
                    // Big-endian: manually read and swap each float
                    if (!consumeToken(T_FLOAT_ARRAY))
                        return {};
                    int32_t count = swap_if_needed(readRaw<int32_t>());

                    if (error_state || count < 0)
                    {
                        error_state = true;
                        return {};
                    }

                    // Align pos to 32-byte boundary
                    size_t remainder = pos % 32;
                    if (remainder != 0)
                        pos += (32 - remainder);

                    size_t bytes = static_cast<size_t>(count) * sizeof(float);
                    if (pos + bytes > size)
                    {
                        error_state = true;
                        return {};
                    }

                    std::vector<float> vec;
                    vec.reserve(count);
                    vec.resize(count);

                    // SIMD-optimized byte swapping
                    const float *src = reinterpret_cast<const float *>(data + pos);
                    swap_float_array_simd(src, vec.data(), count);
                    pos += bytes;

                    return vec;
                }
            }

            bool consumeToken(Token expected)
            {
                if (pos >= size || data[pos] != static_cast<uint8_t>(expected))
                {
                    error_state = true;
                    return false;
                }
                pos++;
                return true;
            }

        private:
            template <typename T>
            inline T readRaw()
            {
                if (pos + sizeof(T) > size)
                {
                    error_state = true;
                    return T{};
                }
                T val;
                std::memcpy(&val, data + pos, sizeof(T));
                pos += sizeof(T);
                return val;
            }
        };

    } // namespace toon
} // namespace dsp
