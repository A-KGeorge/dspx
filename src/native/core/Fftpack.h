/**
 * FFTPACK - Real FFT Implementation
 *
 * Ported from FFTPACK (public domain) by Paul N. Swarztrauber
 * at the National Center for Atmospheric Research, Boulder, CO USA.
 *
 * This implementation handles arbitrary-length real FFTs using
 * mixed-radix decomposition (factors of 2, 3, 4, 5).
 *
 * Key advantages over naive approach:
 * - Specialized real-optimized butterflies (no wasted complex ops)
 * - Mixed-radix factorization (works on any length efficiently)
 * - Cache-friendly access patterns
 * - Direct N reals → N/2+1 complex (no packing overhead)
 */

#pragma once

#include <vector>
#include <cmath>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    namespace core
    {
        namespace fftpack
        {
            /**
             * FFTPACK Context - holds twiddle factors and factorization info
             */
            template <typename T>
            class FftpackContext
            {
            public:
                explicit FftpackContext(size_t n);

                // Forward/inverse real FFT
                void rfft(const T *input, std::complex<T> *output);
                void irfft(const std::complex<T> *input, T *output);

                size_t size() const { return m_n; }
                size_t halfSize() const { return (m_n / 2) + 1; }

            private:
                size_t m_n;                  // Transform size
                std::vector<T> m_wsave;      // Twiddle factors
                std::vector<int> m_ifac;     // Prime factorization
                std::vector<T> m_workBuffer; // Scratch space

                // Initialization
                void drfti1(size_t n, T *wa, int *ifac);

                // Forward transform helpers
                void drftf1(size_t n, T *c, T *ch, const T *wa, const int *ifac);
                void dradf2(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1);
                void dradf4(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1, const T *wa2, const T *wa3);
                void dradfg(size_t ido, size_t ip, size_t l1, size_t idl1, T *cc, T *c1, T *c2, T *ch, T *ch2, const T *wa);

                // Backward transform helpers
                void drftb1(size_t n, T *c, T *ch, const T *wa, const int *ifac);
                void dradb2(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1);
                void dradb3(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1, const T *wa2);
                void dradb4(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1, const T *wa2, const T *wa3);
                void dradbg(size_t ido, size_t ip, size_t l1, size_t idl1, T *cc, T *c1, T *c2, T *ch, T *ch2, const T *wa);
            };

        } // namespace fftpack
    } // namespace core
} // namespace dsp
