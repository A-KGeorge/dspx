/**
 * FFT/DFT Engine - High-performance Fourier transforms
 *
 * Implements all 8 standard transforms:
 * - DFT/IDFT: Direct Fourier Transform (O(N²))
 * - FFT/IFFT: Fast Fourier Transform (O(N log N))
 * - RDFT/IRDFT: Real-input DFT (outputs N/2+1 bins)
 * - RFFT/IRFFT: Real-input FFT (fast version)
 *
 * Features:
 * - Cooley-Tukey radix-2 FFT algorithm
 * - In-place computation for memory efficiency
 * - Bit-reversal permutation
 * - Twiddle factor caching
 * - SIMD optimization where possible
 * - Hermitian symmetry exploitation for real inputs
 */

#ifndef DSP_CORE_FFT_ENGINE_H
#define DSP_CORE_FFT_ENGINE_H

#include <complex>
#include <vector>
#include <cmath>
#include <memory>
#include "Fftpack.h"

namespace dsp
{
    namespace core
    {

        template <typename T = float>
        class FftEngine
        {
        public:
            using Complex = std::complex<T>;

            /**
             * Constructor
             * @param size FFT size (must be power of 2 for FFT, any size for DFT)
             */
            explicit FftEngine(size_t size);

            ~FftEngine() = default;

            // ========== Complex Transforms ==========

            /**
             * Forward FFT (complex -> complex)
             * X[k] = Σ x[n] * e^(-j2πkn/N)
             *
             * @param input Complex input signal (size N)
             * @param output Complex frequency spectrum (size N)
             */
            void fft(const Complex *input, Complex *output);

            /**
             * Inverse FFT (complex -> complex)
             * x[n] = (1/N) * Σ X[k] * e^(j2πkn/N)
             *
             * @param input Complex frequency spectrum (size N)
             * @param output Complex time-domain signal (size N)
             */
            void ifft(const Complex *input, Complex *output);

            /**
             * Forward DFT (complex -> complex)
             * Direct computation, slower but works for any size
             *
             * @param input Complex input signal
             * @param output Complex frequency spectrum
             */
            void dft(const Complex *input, Complex *output);

            /**
             * Inverse DFT (complex -> complex)
             *
             * @param input Complex frequency spectrum
             * @param output Complex time-domain signal
             */
            void idft(const Complex *input, Complex *output);

            // ========== Real-Input Transforms ==========

            /**
             * Forward RFFT (real -> complex, half spectrum)
             * Exploits Hermitian symmetry: X[k] = X*[N-k]
             *
             * @param input Real input signal (size N)
             * @param output Complex half-spectrum (size N/2+1)
             */
            void rfft(const T *input, Complex *output);

            /**
             * Inverse RFFT (complex half-spectrum -> real)
             * Reconstructs real signal from half spectrum
             *
             * @param input Complex half-spectrum (size N/2+1)
             * @param output Real time-domain signal (size N)
             */
            void irfft(const Complex *input, T *output);

            /**
             * Forward RDFT (real -> complex, half spectrum)
             * Direct computation version of RFFT
             *
             * @param input Real input signal (size N)
             * @param output Complex half-spectrum (size N/2+1)
             */
            void rdft(const T *input, Complex *output);

            /**
             * Inverse RDFT (complex half-spectrum -> real)
             * Direct computation version of IRFFT
             *
             * @param input Complex half-spectrum (size N/2+1)
             * @param output Real time-domain signal (size N)
             */
            void irdft(const Complex *input, T *output);

            // ========== Utility Methods ==========

            /**
             * Get FFT size
             */
            size_t getSize() const { return m_size; }

            /**
             * Get half-spectrum size (for real transforms)
             */
            size_t getHalfSize() const { return m_size / 2 + 1; }

            /**
             * Check if size is power of 2
             */
            bool isPowerOfTwo() const { return m_isPowerOfTwo; }

            /**
             * Get magnitude spectrum from complex spectrum
             * |X[k]| = sqrt(Re²(X[k]) + Im²(X[k]))
             *
             * @param spectrum Complex spectrum
             * @param magnitudes Output magnitude array (same size)
             */
            void getMagnitude(const Complex *spectrum, T *magnitudes, size_t length);

            /**
             * Get phase spectrum from complex spectrum
             * ∠X[k] = atan2(Im(X[k]), Re(X[k]))
             *
             * @param spectrum Complex spectrum
             * @param phases Output phase array (same size)
             */
            void getPhase(const Complex *spectrum, T *phases, size_t length);

            /**
             * Get power spectrum (magnitude squared)
             * P[k] = |X[k]|²
             *
             * @param spectrum Complex spectrum
             * @param power Output power array (same size)
             */
            void getPower(const Complex *spectrum, T *power, size_t length);

        private:
            size_t m_size;       // FFT size
            bool m_isPowerOfTwo; // Whether size is power of 2

            // Cached twiddle factors for FFT
            std::vector<Complex> m_twiddleFactors;

            // Bit-reversal permutation indices
            std::vector<size_t> m_bitReversalIndices;

            // Working buffer for in-place operations
            std::vector<Complex> m_workBuffer;

            // FFTPACK context for all sizes
            std::unique_ptr<fftpack::FftpackContext<T>> m_fftpack;

            /**
             * Initialize twiddle factors: W_N^k = e^(-j2πk/N)
             */
            void initTwiddleFactors();

            /**
             * Initialize bit-reversal permutation table
             */
            void initBitReversal();

            /**
             * Perform bit-reversal permutation
             */
            void bitReverse(Complex *data);

            /**
             * Core Cooley-Tukey FFT algorithm (in-place)
             * @param data Complex array (size must be power of 2)
             * @param inverse If true, performs inverse transform
             */
            void cooleyTukeyFFT(Complex *data, bool inverse);

            /**
             * Butterfly operation for FFT
             */
            inline void butterfly(Complex &a, Complex &b, const Complex &twiddle);

            /**
             * Check if number is power of 2
             */
            static bool checkPowerOfTwo(size_t n);

            /**
             * Compute next power of 2
             */
            static size_t nextPowerOfTwo(size_t n);

            /**
             * Reverse bits of integer
             */
            static size_t reverseBits(size_t x, size_t bits);
        };

    } // namespace core
} // namespace dsp

#endif // DSP_CORE_FFT_ENGINE_H
