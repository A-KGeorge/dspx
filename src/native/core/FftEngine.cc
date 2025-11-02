/**
 * FFT/DFT Engine Implementation with SIMD Optimizations
 */

#define _USE_MATH_DEFINES
#include <cmath>
#include "FftEngine.h"
#include "../utils/SimdOps.h"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    namespace core
    {

        template <typename T>
        FftEngine<T>::FftEngine(size_t size)
            : m_size(size), m_isPowerOfTwo(checkPowerOfTwo(size))
        {
            if (size == 0)
            {
                throw std::invalid_argument("FFT size must be > 0");
            }

            // Initialize FFTPACK for all sizes
            m_fftpack = std::make_unique<fftpack::FftpackContext<T>>(m_size);

            // Initialize twiddle factors and bit-reversal for FFT (legacy/unused now)
            if (m_isPowerOfTwo)
            {
                initTwiddleFactors();
                initBitReversal();
            }

            // Allocate working buffer
            m_workBuffer.resize(m_size);
        }

        // ========== Complex Transforms ==========

        template <typename T>
        void FftEngine<T>::fft(const Complex *input, Complex *output)
        {
            if (!m_isPowerOfTwo)
            {
                throw std::runtime_error("FFT requires power-of-2 size. Use DFT for arbitrary sizes.");
            }

            // Copy input to output for in-place operation
            std::copy(input, input + m_size, output);

            // Perform Cooley-Tukey FFT
            cooleyTukeyFFT(output, false);
        }

        template <typename T>
        void FftEngine<T>::ifft(const Complex *input, Complex *output)
        {
            if (!m_isPowerOfTwo)
            {
                throw std::runtime_error("IFFT not supported for non-power-of-2 size");
            }
            std::copy(input, input + m_size, output);
            cooleyTukeyFFT(output, true); // Call the one true function

            // Apply scaling
            T scale = 1.0f / static_cast<T>(m_size);
            for (size_t i = 0; i < m_size; ++i)
            {
                output[i] *= scale;
            }
        }

        template <typename T>
        void FftEngine<T>::dft(const Complex *input, Complex *output)
        {
            // Direct DFT computation: X[k] = Σ x[n] * e^(-j2πkn/N)
            const T two_pi = static_cast<T>(2.0 * M_PI);

            for (size_t k = 0; k < m_size; ++k)
            {
                Complex sum(0, 0);

                for (size_t n = 0; n < m_size; ++n)
                {
                    T angle = -two_pi * static_cast<T>(k * n) / static_cast<T>(m_size);
                    Complex twiddle(std::cos(angle), std::sin(angle));
                    sum += input[n] * twiddle;
                }

                output[k] = sum;
            }
        }

        template <typename T>
        void FftEngine<T>::idft(const Complex *input, Complex *output)
        {
            // Inverse DFT: x[n] = (1/N) * Σ X[k] * e^(j2πkn/N)
            const T two_pi = static_cast<T>(2.0 * M_PI);
            const T scale = T(1) / static_cast<T>(m_size);

            for (size_t n = 0; n < m_size; ++n)
            {
                Complex sum(0, 0);

                for (size_t k = 0; k < m_size; ++k)
                {
                    T angle = two_pi * static_cast<T>(k * n) / static_cast<T>(m_size);
                    Complex twiddle(std::cos(angle), std::sin(angle));
                    sum += input[k] * twiddle;
                }

                output[n] = sum * scale;
            }
        }

        // ========== Real-Input Transforms ==========

        template <typename T>
        void FftEngine<T>::rfft(const T *input, Complex *output)
        {
            // Use FFTPACK for all sizes
            if (m_fftpack)
            {
                m_fftpack->rfft(input, output);
            }
            else
            {
                // Fallback to RDFT for edge cases
                rdft(input, output);
            }
        }

        template <typename T>
        void FftEngine<T>::irfft(const Complex *input, T *output)
        {
            // Use FFTPACK for all sizes
            if (m_fftpack)
            {
                m_fftpack->irfft(input, output);

                // FFTPACK doesn't normalize, so apply 1/N scaling
                T scale = T(1) / static_cast<T>(m_size);
                for (size_t i = 0; i < m_size; ++i)
                {
                    output[i] *= scale;
                }
            }
            else
            {
                // Fallback to IRDFT for edge cases
                irdft(input, output);
            }
        }

        template <typename T>
        void FftEngine<T>::rdft(const T *input, Complex *output)
        {
            // Direct RDFT: compute only half spectrum
            const T two_pi = static_cast<T>(2.0 * M_PI);
            size_t halfSize = getHalfSize();

            for (size_t k = 0; k < halfSize; ++k)
            {
                Complex sum(0, 0);

                for (size_t n = 0; n < m_size; ++n)
                {
                    T angle = -two_pi * static_cast<T>(k * n) / static_cast<T>(m_size);
                    Complex twiddle(std::cos(angle), std::sin(angle));
                    sum += Complex(input[n], 0) * twiddle;
                }

                output[k] = sum;
            }
        }

        template <typename T>
        void FftEngine<T>::irdft(const Complex *input, T *output)
        {
            // Inverse RDFT: reconstruct real signal from half spectrum
            const T two_pi = static_cast<T>(2.0 * M_PI);
            const T scale = T(1) / static_cast<T>(m_size);
            size_t halfSize = getHalfSize();

            for (size_t n = 0; n < m_size; ++n)
            {
                Complex sum(0, 0);

                // DC component
                sum += input[0];

                // Positive frequencies
                for (size_t k = 1; k < halfSize - 1; ++k)
                {
                    T angle = two_pi * static_cast<T>(k * n) / static_cast<T>(m_size);
                    Complex twiddle(std::cos(angle), std::sin(angle));

                    // Add X[k] * e^(j2πkn/N) + X*[k] * e^(-j2πkn/N)
                    sum += input[k] * twiddle;
                    sum += std::conj(input[k]) * std::conj(twiddle);
                }

                // Nyquist frequency (if N is even)
                if (m_size % 2 == 0 && halfSize > 1)
                {
                    T angle = two_pi * static_cast<T>((halfSize - 1) * n) / static_cast<T>(m_size);
                    Complex twiddle(std::cos(angle), std::sin(angle));
                    sum += input[halfSize - 1] * twiddle;
                }

                output[n] = sum.real() * scale;
            }
        }

        // ========== Utility Methods ==========

        template <typename T>
        void FftEngine<T>::getMagnitude(const Complex *spectrum, T *magnitudes, size_t length)
        {
            // MEDIUM WIN: SIMD-optimized magnitude calculation
            if constexpr (std::is_same_v<T, float>)
            {
                // Extract real and imaginary parts into separate arrays for SIMD
                std::vector<float> real(length);
                std::vector<float> imag(length);

                for (size_t i = 0; i < length; ++i)
                {
                    real[i] = spectrum[i].real();
                    imag[i] = spectrum[i].imag();
                }

                dsp::simd::complex_magnitude(real.data(), imag.data(), magnitudes, length);
            }
            else
            {
                // Fallback for double precision
                for (size_t i = 0; i < length; ++i)
                {
                    magnitudes[i] = std::abs(spectrum[i]);
                }
            }
        }

        template <typename T>
        void FftEngine<T>::getPhase(const Complex *spectrum, T *phases, size_t length)
        {
            // Phase calculation (atan2 doesn't benefit much from SIMD)
            for (size_t i = 0; i < length; ++i)
            {
                phases[i] = std::arg(spectrum[i]);
            }
        }

        template <typename T>
        void FftEngine<T>::getPower(const Complex *spectrum, T *power, size_t length)
        {
            // MEDIUM WIN: SIMD-optimized power calculation
            if constexpr (std::is_same_v<T, float>)
            {
                // Extract real and imaginary parts into separate arrays for SIMD
                std::vector<float> real(length);
                std::vector<float> imag(length);

                for (size_t i = 0; i < length; ++i)
                {
                    real[i] = spectrum[i].real();
                    imag[i] = spectrum[i].imag();
                }

                dsp::simd::complex_power(real.data(), imag.data(), power, length);
            }
            else
            {
                // Fallback for double precision
                for (size_t i = 0; i < length; ++i)
                {
                    T mag = std::abs(spectrum[i]);
                    power[i] = mag * mag;
                }
            }
        }

        // ========== Private Methods ==========

        template <typename T>
        void FftEngine<T>::initTwiddleFactors()
        {
            m_twiddleFactors.resize(m_size / 2);

            const T two_pi = static_cast<T>(2.0 * M_PI);

            for (size_t k = 0; k < m_size / 2; ++k)
            {
                T angle = -two_pi * static_cast<T>(k) / static_cast<T>(m_size);
                m_twiddleFactors[k] = Complex(std::cos(angle), std::sin(angle));
            }
        }

        template <typename T>
        void FftEngine<T>::initBitReversal()
        {
            m_bitReversalIndices.resize(m_size);

            size_t bits = 0;
            size_t temp = m_size;
            while (temp > 1)
            {
                temp >>= 1;
                ++bits;
            }

            for (size_t i = 0; i < m_size; ++i)
            {
                m_bitReversalIndices[i] = reverseBits(i, bits);
            }
        }

        template <typename T>
        void FftEngine<T>::bitReverse(Complex *data)
        {
            for (size_t i = 0; i < m_size; ++i)
            {
                size_t j = m_bitReversalIndices[i];
                if (i < j)
                {
                    std::swap(data[i], data[j]);
                }
            }
        }

        /**
         * Core Cooley-Tukey FFT algorithm (in-place)
         * SIMPLIFIED: Passes 'inverse' flag to SIMD helpers.
         */
        template <typename T>
        void FftEngine<T>::cooleyTukeyFFT(Complex *data, bool inverse)
        {
            // 1. Bit-reversal permutation (unchanged)
            bitReverse(data);

            // 2. Cooley-Tukey decimation-in-time
            const size_t logN = static_cast<size_t>(std::log2(static_cast<double>(m_size)));

            for (size_t s = 1; s <= logN; ++s)
            {
                size_t halfLen = 1ULL << (s - 1);
                size_t len = 2 * halfLen;
                const auto &twiddles = m_twiddleFactors; // Always use forward twiddles
                size_t twiddle_step = m_size / len;

                for (size_t i = 0; i < m_size; i += len)
                {
                    size_t k = 0; // Twiddle index
                    size_t j = 0; // Butterfly index

                    // --- COMPILE-TIME DISPATCH ---
                    if constexpr (std::is_same_v<T, float>)
                    {
                        // --- FLOAT PATH ---
#if defined(SIMD_AVX2)
                        for (; j + 3 < halfLen; j += 4)
                        {
                            // Load forward twiddles
                            const float tw_data[8] = {/* ... load k, k+step, k+2s, k+3s ... */
                                                      twiddles[k].real(), twiddles[k].imag(),
                                                      twiddles[k + twiddle_step].real(), twiddles[k + twiddle_step].imag(),
                                                      twiddles[k + 2 * twiddle_step].real(), twiddles[k + 2 * twiddle_step].imag(),
                                                      twiddles[k + 3 * twiddle_step].real(), twiddles[k + 3 * twiddle_step].imag()};
                            __m256 tw = _mm256_loadu_ps(tw_data);
                            __m256 a = _mm256_loadu_ps(reinterpret_cast<float *>(&data[i + j]));
                            __m256 b = _mm256_loadu_ps(reinterpret_cast<float *>(&data[i + j + halfLen]));

                            // Pass 'inverse' flag to helper
                            dsp::simd::avx_butterfly(a, b, tw, inverse);

                            _mm256_storeu_ps(reinterpret_cast<float *>(&data[i + j]), a);
                            _mm256_storeu_ps(reinterpret_cast<float *>(&data[i + j + halfLen]), b);
                            k += (4 * twiddle_step);
                        }
#endif
#if defined(SIMD_SSE3)
                        for (; j + 1 < halfLen; j += 2)
                        {
                            // Load forward twiddles
                            const float tw_data[4] = {/* ... load k, k+step ... */
                                                      twiddles[k].real(), twiddles[k].imag(),
                                                      twiddles[k + twiddle_step].real(), twiddles[k + twiddle_step].imag()};
                            __m128 tw = _mm_loadu_ps(tw_data);
                            __m128 a = _mm_loadu_ps(reinterpret_cast<float *>(&data[i + j]));
                            __m128 b = _mm_loadu_ps(reinterpret_cast<float *>(&data[i + j + halfLen]));

                            // Pass 'inverse' flag to helper
                            dsp::simd::sse_butterfly(a, b, tw, inverse);

                            _mm_storeu_ps(reinterpret_cast<float *>(&data[i + j]), a);
                            _mm_storeu_ps(reinterpret_cast<float *>(&data[i + j + halfLen]), b);
                            k += (2 * twiddle_step);
                        }
#endif
                    }
                    else if constexpr (std::is_same_v<T, double>)
                    {
                        // --- DOUBLE PATH ---
#if defined(SIMD_AVX)
                        for (; j + 1 < halfLen; j += 2)
                        {
                            // Load forward twiddles
                            const double tw_data[4] = {/* ... load k, k+step ... */
                                                       twiddles[k].real(), twiddles[k].imag(),
                                                       twiddles[k + twiddle_step].real(), twiddles[k + twiddle_step].imag()};
                            __m256d tw = _mm256_loadu_pd(tw_data);
                            __m256d a = _mm256_loadu_pd(reinterpret_cast<const double *>(&data[i + j]));
                            __m256d b = _mm256_loadu_pd(reinterpret_cast<const double *>(&data[i + j + halfLen]));

                            // Pass 'inverse' flag to helper
                            dsp::simd::avx_butterfly_double(a, b, tw, inverse);

                            _mm256_storeu_pd(reinterpret_cast<double *>(&data[i + j]), a);
                            _mm256_storeu_pd(reinterpret_cast<double *>(&data[i + j + halfLen]), b);
                            k += (2 * twiddle_step);
                        }
#endif
                    }

                    // --- SCALAR CLEANUP (Unchanged) ---
                    for (; j < halfLen; ++j)
                    {
                        const Complex &twiddle = twiddles[k]; // Use forward twiddle
                        if (inverse)
                        {
                            butterfly(data[i + j], data[i + j + halfLen], std::conj(twiddle));
                        }
                        else
                        {
                            butterfly(data[i + j], data[i + j + halfLen], twiddle);
                        }
                        k += twiddle_step;
                    }
                }
            }
        }

        template <typename T>
        inline void FftEngine<T>::butterfly(Complex &a, Complex &b, const Complex &twiddle)
        {
            Complex temp = b * twiddle;
            b = a - temp;
            a = a + temp;
        }

        template <typename T>
        bool FftEngine<T>::checkPowerOfTwo(size_t n)
        {
            return n > 0 && (n & (n - 1)) == 0;
        }

        template <typename T>
        size_t FftEngine<T>::nextPowerOfTwo(size_t n)
        {
            if (n == 0)
                return 1;

            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            n |= n >> 32;

            return n + 1;
        }

        template <typename T>
        size_t FftEngine<T>::reverseBits(size_t x, size_t bits)
        {
            size_t result = 0;
            for (size_t i = 0; i < bits; ++i)
            {
                result = (result << 1) | (x & 1);
                x >>= 1;
            }
            return result;
        }

        // Explicit template instantiations
        template class FftEngine<float>;
        template class FftEngine<double>;

    } // namespace core
} // namespace dsp
