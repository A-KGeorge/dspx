#ifndef DSP_ADAPTERS_AMPLIFYSTAGE_H
#define DSP_ADAPTERS_AMPLIFYSTAGE_H

#include "IDspStage.h"
#include "../utils/SimdOps.h"
#include <cstring>

// Platform detection for SIMD
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace dsp
{
    namespace adapters
    {
        /**
         * Amplify (Gain) Stage
         * Multiplies all samples by a constant gain factor.
         * SIMD-optimized for maximum throughput (AVX2/SSE2/NEON).
         * Useful for scaling signals to appropriate amplitude ranges.
         */
        class AmplifyStage : public IDspStage
        {
        public:
            explicit AmplifyStage(float gain = 1.0f)
                : m_gain(gain)
            {
            }

            void process(float *data, size_t numSamples, int channels, const float *timestamps = nullptr) override
            {
                // SIMD-optimized gain multiplication
#if defined(__AVX2__)
                // AVX2: Process 8 floats at a time
                const size_t simd_width = 8;
                const size_t simd_count = numSamples / simd_width;
                const size_t simd_end = simd_count * simd_width;

                const __m256 gain_vec = _mm256_set1_ps(m_gain);

                for (size_t i = 0; i < simd_end; i += simd_width)
                {
                    __m256 samples = _mm256_loadu_ps(&data[i]);
                    samples = _mm256_mul_ps(samples, gain_vec);
                    _mm256_storeu_ps(&data[i], samples);
                }

                // Handle remainder
                for (size_t i = simd_end; i < numSamples; ++i)
                {
                    data[i] *= m_gain;
                }

#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
                // SSE2: Process 4 floats at a time
                const size_t simd_width = 4;
                const size_t simd_count = numSamples / simd_width;
                const size_t simd_end = simd_count * simd_width;

                const __m128 gain_vec = _mm_set1_ps(m_gain);

                for (size_t i = 0; i < simd_end; i += simd_width)
                {
                    __m128 samples = _mm_loadu_ps(&data[i]);
                    samples = _mm_mul_ps(samples, gain_vec);
                    _mm_storeu_ps(&data[i], samples);
                }

                // Handle remainder
                for (size_t i = simd_end; i < numSamples; ++i)
                {
                    data[i] *= m_gain;
                }

#elif defined(__ARM_NEON) || defined(__aarch64__)
                // ARM NEON: Process 4 floats at a time
                const size_t simd_width = 4;
                const size_t simd_count = numSamples / simd_width;
                const size_t simd_end = simd_count * simd_width;

                const float32x4_t gain_vec = vdupq_n_f32(m_gain);

                for (size_t i = 0; i < simd_end; i += simd_width)
                {
                    float32x4_t samples = vld1q_f32(&data[i]);
                    samples = vmulq_f32(samples, gain_vec);
                    vst1q_f32(&data[i], samples);
                }

                // Handle remainder
                for (size_t i = simd_end; i < numSamples; ++i)
                {
                    data[i] *= m_gain;
                }

#else
                // Scalar fallback (compiler may auto-vectorize)
                for (size_t i = 0; i < numSamples; ++i)
                {
                    data[i] *= m_gain;
                }
#endif
            }

            const char *getType() const override
            {
                return "amplify";
            }

            Napi::Object serializeState(Napi::Env env) const override
            {
                // Stateless - return empty object
                return Napi::Object::New(env);
            }

            void deserializeState(const Napi::Object &state) override
            {
                // Stateless - nothing to restore
            }

            // Serialization
            void serializeToon(dsp::toon::Serializer &s) const override
            {
                // Stateless - no data to serialize
            }

            void deserializeToon(dsp::toon::Deserializer &d) override
            {
                // Stateless - no data to deserialize
            }

            void reset() override
            {
                // Stateless - no internal state to reset
            }

        private:
            float m_gain;
        };
    } // namespace adapters
} // namespace dsp

#endif // DSP_ADAPTERS_AMPLIFYSTAGE_H
