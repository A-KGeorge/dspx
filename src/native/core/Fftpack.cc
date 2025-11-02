/**
 * FFTPACK - Real FFT Implementation
 *
 * This is a C++ port of the classic FFTPACK library.
 * Original Fortran code by Paul N. Swarztrauber (public domain).
 */

#include "Fftpack.h"
#include "../utils/SimdOps.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

#if defined(__AVX2__)
#define FFTPACK_USE_AVX2
#include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64)
#define FFTPACK_USE_SSE2
#include <emmintrin.h>
#include <xmmintrin.h>
#endif

namespace dsp
{
    namespace core
    {
        namespace fftpack
        {
            // ========== Constructor & Initialization ==========

            template <typename T>
            FftpackContext<T>::FftpackContext(size_t n) : m_n(n)
            {
                if (n == 0)
                {
                    throw std::invalid_argument("FFTPACK size must be > 0");
                }

                // Allocate twiddle factors and factorization
                m_wsave.resize(n + n);  // wa + workspace
                m_ifac.resize(15);      // Factorization info
                m_workBuffer.resize(n); // Working buffer

                // Initialize for real FFT
                if (n > 1)
                {
                    drfti1(n, m_wsave.data() + n, m_ifac.data());
                }
            }

            // ========== Public Transform Methods ==========

            template <typename T>
            void FftpackContext<T>::rfft(const T *input, std::complex<T> *output)
            {
                if (m_n == 1)
                {
                    output[0] = std::complex<T>(input[0], 0);
                    return;
                }

                // Copy input to work buffer
                std::copy(input, input + m_n, m_workBuffer.data());

                // Perform forward real FFT
                drftf1(m_n, m_workBuffer.data(), m_wsave.data(), m_wsave.data() + m_n, m_ifac.data());

                // Convert FFTPACK halfcomplex format to standard complex format
                // FFTPACK stores: [DC, re1, re2, ..., reN/2-1, Nyquist, im1, im2, ..., imN/2-1]
                // (for even N)

                size_t halfSize = (m_n / 2) + 1;

                // DC component (always real)
                output[0] = std::complex<T>(m_workBuffer[0], 0);

                if (m_n % 2 == 0)
                {
                    // Even N: Nyquist is at position m_n/2
                    for (size_t i = 1; i < m_n / 2; ++i)
                    {
                        output[i] = std::complex<T>(m_workBuffer[2 * i - 1], m_workBuffer[2 * i]);
                    }
                    output[m_n / 2] = std::complex<T>(m_workBuffer[m_n - 1], 0);
                }
                else
                {
                    // Odd N: no separate Nyquist
                    for (size_t i = 1; i < halfSize; ++i)
                    {
                        output[i] = std::complex<T>(m_workBuffer[2 * i - 1], m_workBuffer[2 * i]);
                    }
                }
            }

            template <typename T>
            void FftpackContext<T>::irfft(const std::complex<T> *input, T *output)
            {
                if (m_n == 1)
                {
                    output[0] = input[0].real();
                    return;
                }

                // Convert standard complex format to FFTPACK halfcomplex format
                m_workBuffer[0] = input[0].real(); // DC

                if (m_n % 2 == 0)
                {
                    // Even N
                    for (size_t i = 1; i < m_n / 2; ++i)
                    {
                        m_workBuffer[2 * i - 1] = input[i].real();
                        m_workBuffer[2 * i] = input[i].imag();
                    }
                    m_workBuffer[m_n - 1] = input[m_n / 2].real(); // Nyquist
                }
                else
                {
                    // Odd N
                    size_t halfSize = (m_n / 2) + 1;
                    for (size_t i = 1; i < halfSize; ++i)
                    {
                        m_workBuffer[2 * i - 1] = input[i].real();
                        m_workBuffer[2 * i] = input[i].imag();
                    }
                }

                // Perform inverse real FFT
                drftb1(m_n, m_workBuffer.data(), m_wsave.data(), m_wsave.data() + m_n, m_ifac.data());

                // Copy result (FFTPACK doesn't normalize)
                std::copy(m_workBuffer.begin(), m_workBuffer.end(), output);
            }

            // ========== FFTPACK Initialization ==========

            template <typename T>
            void FftpackContext<T>::drfti1(size_t n, T *wa, int *ifac)
            {
                static const int ntryh[4] = {4, 2, 3, 5};
                static const T tpi = static_cast<T>(2.0 * M_PI);

                int ntry = 0;
                int j = -1;
                int nf = 0;
                size_t nl = n;

                // Factor n into radices 2,3,4,5
                while (true)
                {
                    j++;
                    if (j < 4)
                        ntry = ntryh[j];
                    else
                        ntry += 2;

                    // Try to divide by ntry
                    while (true)
                    {
                        size_t nq = nl / ntry;
                        size_t nr = nl - ntry * nq;

                        if (nr != 0)
                            break; // Not divisible, try next factor

                        nf++;
                        ifac[nf + 1] = ntry;
                        nl = nq;

                        if (ntry == 2 && nf != 1)
                        {
                            // Move 2's to the end
                            for (int i = 1; i < nf; i++)
                            {
                                int ib = nf - i + 1;
                                ifac[ib + 1] = ifac[ib];
                            }
                            ifac[2] = 2;
                        }

                        if (nl == 1)
                            goto done_factoring;
                    }
                }

            done_factoring:
                ifac[0] = n;
                ifac[1] = nf;

                // Compute twiddle factors
                T argh = tpi / static_cast<T>(n);
                size_t is = 0;
                int nfm1 = nf - 1;
                size_t l1 = 1;

                if (nfm1 == 0)
                    return;

                for (int k1 = 0; k1 < nfm1; k1++)
                {
                    int ip = ifac[k1 + 2];
                    size_t ld = 0;
                    size_t l2 = l1 * ip;
                    size_t ido = n / l2;
                    int ipm = ip - 1;

                    for (int j = 0; j < ipm; j++)
                    {
                        ld += l1;
                        size_t i = is;
                        T argld = static_cast<T>(ld) * argh;
                        T fi = 0;

                        for (size_t ii = 2; ii < ido; ii += 2)
                        {
                            fi += 1;
                            T arg = fi * argld;
                            wa[i++] = std::cos(arg);
                            wa[i++] = std::sin(arg);
                        }
                        is += ido;
                    }
                    l1 = l2;
                }
            }

            // ========== Forward Transform (drftf1) ==========

            template <typename T>
            void FftpackContext<T>::drftf1(size_t n, T *c, T *ch, const T *wa, const int *ifac)
            {
                int nf = ifac[1];
                int na = 1;
                size_t l2 = n;
                size_t iw = n;

                for (int k1 = 0; k1 < nf; k1++)
                {
                    int kh = nf - k1;
                    int ip = ifac[kh + 1];
                    size_t l1 = l2 / ip;
                    size_t ido = n / l2;
                    size_t idl1 = ido * l1;
                    iw -= (ip - 1) * ido;
                    na = 1 - na;

                    if (ip == 4)
                    {
                        size_t ix2 = iw + ido;
                        size_t ix3 = ix2 + ido;

                        if (na != 0)
                            dradf4(ido, l1, ch, c, wa + iw - 1, wa + ix2 - 1, wa + ix3 - 1);
                        else
                            dradf4(ido, l1, c, ch, wa + iw - 1, wa + ix2 - 1, wa + ix3 - 1);
                    }
                    else if (ip == 2)
                    {
                        if (na != 0)
                            dradf2(ido, l1, ch, c, wa + iw - 1);
                        else
                            dradf2(ido, l1, c, ch, wa + iw - 1);
                    }
                    else
                    {
                        if (ido == 1)
                            na = 1 - na;

                        if (na != 0)
                        {
                            dradfg(ido, ip, l1, idl1, ch, ch, ch, c, c, wa + iw - 1);
                            na = 1;
                        }
                        else
                        {
                            dradfg(ido, ip, l1, idl1, c, c, c, ch, ch, wa + iw - 1);
                            na = 0;
                        }
                    }

                    l2 = l1;
                }

                if (na == 1)
                    return;

                for (size_t i = 0; i < n; i++)
                    c[i] = ch[i];
            }

            // ========== Backward Transform (drftb1) ==========

            template <typename T>
            void FftpackContext<T>::drftb1(size_t n, T *c, T *ch, const T *wa, const int *ifac)
            {
                int nf = ifac[1];
                int na = 0;
                size_t l1 = 1;
                size_t iw = 1;

                for (int k1 = 0; k1 < nf; k1++)
                {
                    int ip = ifac[k1 + 2];
                    size_t l2 = ip * l1;
                    size_t ido = n / l2;
                    size_t idl1 = ido * l1;

                    if (ip == 4)
                    {
                        size_t ix2 = iw + ido;
                        size_t ix3 = ix2 + ido;

                        if (na != 0)
                            dradb4(ido, l1, ch, c, wa + iw - 1, wa + ix2 - 1, wa + ix3 - 1);
                        else
                            dradb4(ido, l1, c, ch, wa + iw - 1, wa + ix2 - 1, wa + ix3 - 1);

                        na = 1 - na;
                    }
                    else if (ip == 2)
                    {
                        if (na != 0)
                            dradb2(ido, l1, ch, c, wa + iw - 1);
                        else
                            dradb2(ido, l1, c, ch, wa + iw - 1);

                        na = 1 - na;
                    }
                    else if (ip == 3)
                    {
                        size_t ix2 = iw + ido;

                        if (na != 0)
                            dradb3(ido, l1, ch, c, wa + iw - 1, wa + ix2 - 1);
                        else
                            dradb3(ido, l1, c, ch, wa + iw - 1, wa + ix2 - 1);

                        na = 1 - na;
                    }
                    else
                    {
                        if (na != 0)
                            dradbg(ido, ip, l1, idl1, ch, ch, ch, c, c, wa + iw - 1);
                        else
                            dradbg(ido, ip, l1, idl1, c, c, c, ch, ch, wa + iw - 1);

                        if (ido == 1)
                            na = 1 - na;
                    }

                    l1 = l2;
                    iw += (ip - 1) * ido;
                }

                if (na == 0)
                    return;

                for (size_t i = 0; i < n; i++)
                    c[i] = ch[i];
            }

            // ========== Radix-2 Butterflies ==========

            template <typename T>
            void FftpackContext<T>::dradf2(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1)
            {
                size_t t1 = 0;
                size_t t0 = l1 * ido;
                size_t t2 = t0;
                size_t t3 = ido << 1;

                for (size_t k = 0; k < l1; k++)
                {
                    ch[t1 << 1] = cc[t1] + cc[t2];
                    ch[(t1 << 1) + t3 - 1] = cc[t1] - cc[t2];
                    t1 += ido;
                    t2 += ido;
                }

                if (ido < 2)
                    return;
                if (ido == 2)
                    goto L105;

                t1 = 0;
                t2 = t0;
                for (size_t k = 0; k < l1; k++)
                {
                    size_t t3 = t2;
                    size_t t4 = (t1 << 1) + (ido << 1);
                    size_t t5 = t1;
                    size_t t6 = t1 + t1;

                    // Unroll by 4 for better performance (compiler can vectorize)
                    size_t i = 2;
                    size_t ido_minus_6 = (ido > 6) ? ido - 6 : 0;
                    for (; i < ido_minus_6; i += 8)
                    {
                        // Iteration 1
                        t3 += 2;
                        t4 -= 2;
                        t5 += 2;
                        t6 += 2;
                        T tr2_0 = wa1[i - 2] * cc[t3 - 1] + wa1[i - 1] * cc[t3];
                        T ti2_0 = wa1[i - 2] * cc[t3] - wa1[i - 1] * cc[t3 - 1];
                        ch[t6] = cc[t5] + ti2_0;
                        ch[t4] = ti2_0 - cc[t5];
                        ch[t6 - 1] = cc[t5 - 1] + tr2_0;
                        ch[t4 - 1] = cc[t5 - 1] - tr2_0;

                        // Iteration 2
                        t3 += 2;
                        t4 -= 2;
                        t5 += 2;
                        t6 += 2;
                        T tr2_1 = wa1[i] * cc[t3 - 1] + wa1[i + 1] * cc[t3];
                        T ti2_1 = wa1[i] * cc[t3] - wa1[i + 1] * cc[t3 - 1];
                        ch[t6] = cc[t5] + ti2_1;
                        ch[t4] = ti2_1 - cc[t5];
                        ch[t6 - 1] = cc[t5 - 1] + tr2_1;
                        ch[t4 - 1] = cc[t5 - 1] - tr2_1;

                        // Iteration 3
                        t3 += 2;
                        t4 -= 2;
                        t5 += 2;
                        t6 += 2;
                        T tr2_2 = wa1[i + 2] * cc[t3 - 1] + wa1[i + 3] * cc[t3];
                        T ti2_2 = wa1[i + 2] * cc[t3] - wa1[i + 3] * cc[t3 - 1];
                        ch[t6] = cc[t5] + ti2_2;
                        ch[t4] = ti2_2 - cc[t5];
                        ch[t6 - 1] = cc[t5 - 1] + tr2_2;
                        ch[t4 - 1] = cc[t5 - 1] - tr2_2;

                        // Iteration 4
                        t3 += 2;
                        t4 -= 2;
                        t5 += 2;
                        t6 += 2;
                        T tr2_3 = wa1[i + 4] * cc[t3 - 1] + wa1[i + 5] * cc[t3];
                        T ti2_3 = wa1[i + 4] * cc[t3] - wa1[i + 5] * cc[t3 - 1];
                        ch[t6] = cc[t5] + ti2_3;
                        ch[t4] = ti2_3 - cc[t5];
                        ch[t6 - 1] = cc[t5 - 1] + tr2_3;
                        ch[t4 - 1] = cc[t5 - 1] - tr2_3;
                    }

                    // Handle remainder
                    for (; i < ido; i += 2)
                    {
                        t3 += 2;
                        t4 -= 2;
                        t5 += 2;
                        t6 += 2;
                        T tr2 = wa1[i - 2] * cc[t3 - 1] + wa1[i - 1] * cc[t3];
                        T ti2 = wa1[i - 2] * cc[t3] - wa1[i - 1] * cc[t3 - 1];
                        ch[t6] = cc[t5] + ti2;
                        ch[t4] = ti2 - cc[t5];
                        ch[t6 - 1] = cc[t5 - 1] + tr2;
                        ch[t4 - 1] = cc[t5 - 1] - tr2;
                    }
                    t1 += ido;
                    t2 += ido;
                }

                if (ido % 2 == 1)
                    return;

            L105:
                t3 = (t2 = (t1 = ido) - 1);
                t2 += t0;
                for (size_t k = 0; k < l1; k++)
                {
                    ch[t1] = -cc[t2];
                    ch[t1 - 1] = cc[t3];
                    t1 += ido << 1;
                    t2 += ido;
                    t3 += ido;
                }
            }

            template <typename T>
            void FftpackContext<T>::dradb2(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1)
            {
                size_t t0 = l1 * ido;
                size_t t1 = 0;
                size_t t2 = 0;
                size_t t3 = (ido << 1) - 1;

                for (size_t k = 0; k < l1; k++)
                {
                    ch[t1] = cc[t2] + cc[t3 + t2];
                    ch[t1 + t0] = cc[t2] - cc[t3 + t2];
                    t2 = (t1 += ido) << 1;
                }

                if (ido < 2)
                    return;
                if (ido == 2)
                    goto L105;

                t1 = 0;
                t2 = 0;
                for (size_t k = 0; k < l1; k++)
                {
                    size_t t3 = t1;
                    size_t t4, t5 = (t4 = t2) + (ido << 1);
                    size_t t6 = t0 + t1;

                    for (size_t i = 2; i < ido; i += 2)
                    {
                        t3 += 2;
                        t4 += 2;
                        t5 -= 2;
                        t6 += 2;
                        ch[t3 - 1] = cc[t4 - 1] + cc[t5 - 1];
                        T tr2 = cc[t4 - 1] - cc[t5 - 1];
                        ch[t3] = cc[t4] - cc[t5];
                        T ti2 = cc[t4] + cc[t5];
                        ch[t6 - 1] = wa1[i - 2] * tr2 - wa1[i - 1] * ti2;
                        ch[t6] = wa1[i - 2] * ti2 + wa1[i - 1] * tr2;
                    }
                    t2 = (t1 += ido) << 1;
                }

                if (ido % 2 == 1)
                    return;

            L105:
                t1 = ido - 1;
                t2 = ido - 1;
                for (size_t k = 0; k < l1; k++)
                {
                    ch[t1] = cc[t2] + cc[t2];
                    ch[t1 + t0] = -(cc[t2 + 1] + cc[t2 + 1]);
                    t1 += ido;
                    t2 += ido << 1;
                }
            }

            // ========== Radix-4 Butterflies ==========

            template <typename T>
            void FftpackContext<T>::dradf4(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1, const T *wa2, const T *wa3)
            {
                static const T hsqt2 = static_cast<T>(0.70710678118654752440084436210485);
                size_t t0 = l1 * ido;

                size_t t1 = t0;
                size_t t4 = t1 << 1;
                size_t t2 = t1 + (t1 << 1);
                size_t t3 = 0;

                for (size_t k = 0; k < l1; k++)
                {
                    T tr1 = cc[t1] + cc[t2];
                    T tr2 = cc[t3] + cc[t4];
                    size_t t5 = t3 << 2;
                    ch[t5] = tr1 + tr2;
                    ch[(ido << 2) + t5 - 1] = tr2 - tr1;
                    ch[(t5 += (ido << 1)) - 1] = cc[t3] - cc[t4];
                    ch[t5] = cc[t2] - cc[t1];

                    t1 += ido;
                    t2 += ido;
                    t3 += ido;
                    t4 += ido;
                }

                if (ido < 2)
                    return;
                if (ido == 2)
                    goto L105;

                t1 = 0;
                for (size_t k = 0; k < l1; k++)
                {
                    size_t t2 = t1;
                    size_t t4 = t1 << 2;
                    size_t t6 = ido << 1;
                    size_t t5 = t6 + t4;

                    for (size_t i = 2; i < ido; i += 2)
                    {
                        size_t t3 = (t2 += 2);
                        t4 += 2;
                        t5 -= 2;

                        t3 += t0;
                        T cr2 = wa1[i - 2] * cc[t3 - 1] + wa1[i - 1] * cc[t3];
                        T ci2 = wa1[i - 2] * cc[t3] - wa1[i - 1] * cc[t3 - 1];
                        t3 += t0;
                        T cr3 = wa2[i - 2] * cc[t3 - 1] + wa2[i - 1] * cc[t3];
                        T ci3 = wa2[i - 2] * cc[t3] - wa2[i - 1] * cc[t3 - 1];
                        t3 += t0;
                        T cr4 = wa3[i - 2] * cc[t3 - 1] + wa3[i - 1] * cc[t3];
                        T ci4 = wa3[i - 2] * cc[t3] - wa3[i - 1] * cc[t3 - 1];

                        T tr1 = cr2 + cr4;
                        T tr4 = cr4 - cr2;
                        T ti1 = ci2 + ci4;
                        T ti4 = ci2 - ci4;
                        T ti2 = cc[t2] + ci3;
                        T ti3 = cc[t2] - ci3;
                        T tr2 = cc[t2 - 1] + cr3;
                        T tr3 = cc[t2 - 1] - cr3;

                        ch[t4 - 1] = tr1 + tr2;
                        ch[t4] = ti1 + ti2;
                        ch[t5 - 1] = tr3 - ti4;
                        ch[t5] = tr4 - ti3;
                        ch[t4 + t6 - 1] = ti4 + tr3;
                        ch[t4 + t6] = tr4 + ti3;
                        ch[t5 + t6 - 1] = tr2 - tr1;
                        ch[t5 + t6] = ti1 - ti2;
                    }
                    t1 += ido;
                }

                if (ido % 2 == 1)
                    return;

            L105:
            {
                size_t t2_temp = (t1 = t0 + ido - 1) + (t0 << 1);
                size_t t3_temp = ido << 2;
                size_t t4_temp = ido;
                size_t t5_temp = ido << 1;
                size_t t6_temp = ido;

                for (size_t k = 0; k < l1; k++)
                {
                    T ti1 = -hsqt2 * (cc[t1] + cc[t2_temp]);
                    T tr1 = hsqt2 * (cc[t1] - cc[t2_temp]);
                    ch[t4_temp - 1] = tr1 + cc[t6_temp - 1];
                    ch[t4_temp + t5_temp - 1] = cc[t6_temp - 1] - tr1;
                    ch[t4_temp] = ti1 - cc[t1 + t0];
                    ch[t4_temp + t5_temp] = ti1 + cc[t1 + t0];
                    t1 += ido;
                    t2_temp += ido;
                    t4_temp += t3_temp;
                    t6_temp += ido;
                }
            }
            }

            template <typename T>
            void FftpackContext<T>::dradb3(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1, const T *wa2)
            {
                static const T taur = static_cast<T>(-0.5);
                static const T taui = static_cast<T>(0.86602540378443864676372317075293618);
                size_t t0 = l1 * ido;

                size_t t1 = 0;
                size_t t2 = t0 << 1;
                size_t t3 = ido << 1;
                size_t t4 = ido + (ido << 1);
                size_t t5 = 0;

                for (size_t k = 0; k < l1; k++)
                {
                    T tr2 = cc[t3 - 1] + cc[t3 - 1];
                    T cr2 = cc[t5] + (taur * tr2);
                    ch[t1] = cc[t5] + tr2;
                    T ci3 = taui * (cc[t3] + cc[t3]);
                    ch[t1 + t0] = cr2 - ci3;
                    ch[t1 + t2] = cr2 + ci3;
                    t1 += ido;
                    t3 += t4;
                    t5 += t4;
                }

                if (ido == 1)
                    return;

                t1 = 0;
                t3 = ido << 1;
                for (size_t k = 0; k < l1; k++)
                {
                    size_t t7 = t1 + (t1 << 1);
                    size_t t5 = t7 + t3;
                    size_t t6 = t5;
                    size_t t8 = t1;
                    size_t t9 = t1 + t0;
                    size_t t10 = t9 + t0;

                    for (size_t i = 2; i < ido; i += 2)
                    {
                        t5 += 2;
                        t6 -= 2;
                        t7 += 2;
                        t8 += 2;
                        t9 += 2;
                        t10 += 2;
                        T tr2 = cc[t5 - 1] + cc[t6 - 1];
                        T cr2 = cc[t7 - 1] + (taur * tr2);
                        ch[t8 - 1] = cc[t7 - 1] + tr2;
                        T ti2 = cc[t5] - cc[t6];
                        T ci2 = cc[t7] + (taur * ti2);
                        ch[t8] = cc[t7] + ti2;
                        T cr3 = taui * (cc[t5 - 1] - cc[t6 - 1]);
                        T ci3 = taui * (cc[t5] + cc[t6]);
                        T dr2 = cr2 - ci3;
                        T dr3 = cr2 + ci3;
                        T di2 = ci2 + cr3;
                        T di3 = ci2 - cr3;
                        ch[t9 - 1] = wa1[i - 2] * dr2 - wa1[i - 1] * di2;
                        ch[t9] = wa1[i - 2] * di2 + wa1[i - 1] * dr2;
                        ch[t10 - 1] = wa2[i - 2] * dr3 - wa2[i - 1] * di3;
                        ch[t10] = wa2[i - 2] * di3 + wa2[i - 1] * dr3;
                    }
                    t1 += ido;
                }
            }

            template <typename T>
            void FftpackContext<T>::dradb4(size_t ido, size_t l1, const T *cc, T *ch, const T *wa1, const T *wa2, const T *wa3)
            {
                static const T sqrt2 = static_cast<T>(1.4142135623730950488016887242097);
                size_t t0 = l1 * ido;

                size_t t1 = 0;
                size_t t2 = ido << 2;
                size_t t3 = 0;
                size_t t6 = ido << 1;

                for (size_t k = 0; k < l1; k++)
                {
                    size_t t4 = t3 + t6;
                    size_t t5 = t1;
                    T tr3 = cc[t4 - 1] + cc[t4 - 1];
                    T tr4 = cc[t4] + cc[t4];
                    T tr1 = cc[t3] - cc[(t4 += t6) - 1];
                    T tr2 = cc[t3] + cc[t4 - 1];
                    ch[t5] = tr2 + tr3;
                    ch[t5 += t0] = tr1 - tr4;
                    ch[t5 += t0] = tr2 - tr3;
                    ch[t5 += t0] = tr1 + tr4;
                    t1 += ido;
                    t3 += t2;
                }

                if (ido < 2)
                    return;
                if (ido == 2)
                    goto L105;

                t1 = 0;
                for (size_t k = 0; k < l1; k++)
                {
                    size_t t2 = t1 << 2;
                    size_t t3 = t2 + t6;
                    size_t t4 = t3 + t6;
                    size_t t5 = t4;
                    size_t t7 = t1;

                    for (size_t i = 2; i < ido; i += 2)
                    {
                        t2 += 2;
                        t3 += 2;
                        t4 -= 2;
                        t5 -= 2;
                        t7 += 2;
                        T ti1 = cc[t2] + cc[t5];
                        T ti2 = cc[t2] - cc[t5];
                        T ti3 = cc[t3] - cc[t4];
                        T tr4 = cc[t3] + cc[t4];
                        T tr1 = cc[t2 - 1] - cc[t5 - 1];
                        T tr2 = cc[t2 - 1] + cc[t5 - 1];
                        T ti4 = cc[t3 - 1] - cc[t4 - 1];
                        T tr3 = cc[t3 - 1] + cc[t4 - 1];
                        ch[t7 - 1] = tr2 + tr3;
                        T cr3 = tr2 - tr3;
                        ch[t7] = ti2 + ti3;
                        T ci3 = ti2 - ti3;
                        T cr2 = tr1 - tr4;
                        T cr4 = tr1 + tr4;
                        T ci2 = ti1 + ti4;
                        T ci4 = ti1 - ti4;

                        size_t t8 = t7 + t0;
                        ch[t8 - 1] = wa1[i - 2] * cr2 - wa1[i - 1] * ci2;
                        ch[t8] = wa1[i - 2] * ci2 + wa1[i - 1] * cr2;
                        t8 += t0;
                        ch[t8 - 1] = wa2[i - 2] * cr3 - wa2[i - 1] * ci3;
                        ch[t8] = wa2[i - 2] * ci3 + wa2[i - 1] * cr3;
                        t8 += t0;
                        ch[t8 - 1] = wa3[i - 2] * cr4 - wa3[i - 1] * ci4;
                        ch[t8] = wa3[i - 2] * ci4 + wa3[i - 1] * cr4;
                    }
                    t1 += ido;
                }

                if (ido % 2 == 1)
                    return;

            L105:
            {
                size_t t1_temp = ido;
                size_t t2_temp = ido << 2;
                size_t t3_temp = ido - 1;
                size_t t4_temp = ido + (ido << 1);

                for (size_t k = 0; k < l1; k++)
                {
                    size_t t5_temp = t3_temp;
                    T ti1 = cc[t1_temp] + cc[t4_temp];
                    T ti2 = cc[t4_temp] - cc[t1_temp];
                    T tr1 = cc[t1_temp - 1] - cc[t4_temp - 1];
                    T tr2 = cc[t1_temp - 1] + cc[t4_temp - 1];
                    ch[t5_temp] = tr2 + tr2;
                    ch[t5_temp += t0] = sqrt2 * (tr1 - ti1);
                    ch[t5_temp += t0] = ti2 + ti2;
                    ch[t5_temp += t0] = -sqrt2 * (tr1 + ti1);

                    t3_temp += ido;
                    t1_temp += t2_temp;
                    t4_temp += t2_temp;
                }
            }
            }

            // ========== General Radix Butterflies ==========

            template <typename T>
            void FftpackContext<T>::dradfg(size_t ido, size_t ip, size_t l1, size_t idl1,
                                           T *cc, T *c1, T *c2, T *ch, T *ch2, const T *wa)
            {
                static const T tpi = static_cast<T>(2.0 * M_PI);
                size_t ipph, i, j, k, l, ic, ik;
                ptrdiff_t is, idij;                                    // Can be negative in index arithmetic
                ptrdiff_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10; // Can be negative
                T dc2, ai1, ai2, ar1, ar2, ds2;
                size_t nbd;
                T dcp, arg, dsp, ar1h, ar2h;
                size_t idp2, ipp2;

                arg = tpi / static_cast<T>(ip);
                dcp = std::cos(arg);
                dsp = std::sin(arg);
                ipph = (ip + 1) >> 1;
                ipp2 = ip;
                idp2 = ido;
                nbd = (ido - 1) >> 1;
                t0 = l1 * ido;
                t10 = ip * ido;

                if (ido == 1)
                    goto L119;

                for (ik = 0; ik < idl1; ik++)
                    ch2[ik] = c2[ik];

                t1 = 0;
                for (j = 1; j < ip; j++)
                {
                    t1 += t0;
                    t2 = t1;
                    for (k = 0; k < l1; k++)
                    {
                        ch[t2] = c1[t2];
                        t2 += ido;
                    }
                }

                is = -static_cast<ptrdiff_t>(ido);
                t1 = 0;
                if (nbd > l1)
                {
                    for (j = 1; j < ip; j++)
                    {
                        t1 += t0;
                        is += ido;
                        t2 = -static_cast<ptrdiff_t>(ido) + t1;
                        for (k = 0; k < l1; k++)
                        {
                            idij = is - 1;
                            t2 += ido;
                            t3 = t2;
                            for (i = 2; i < ido; i += 2)
                            {
                                idij += 2;
                                t3 += 2;
                                ch[t3 - 1] = wa[idij - 1] * c1[t3 - 1] + wa[idij] * c1[t3];
                                ch[t3] = wa[idij - 1] * c1[t3] - wa[idij] * c1[t3 - 1];
                            }
                        }
                    }
                }
                else
                {
                    for (j = 1; j < ip; j++)
                    {
                        is += ido;
                        idij = is - 1;
                        t1 += t0;
                        t2 = t1;
                        for (i = 2; i < ido; i += 2)
                        {
                            idij += 2;
                            t2 += 2;
                            t3 = t2;
                            for (k = 0; k < l1; k++)
                            {
                                ch[t3 - 1] = wa[idij - 1] * c1[t3 - 1] + wa[idij] * c1[t3];
                                ch[t3] = wa[idij - 1] * c1[t3] - wa[idij] * c1[t3 - 1];
                                t3 += ido;
                            }
                        }
                    }
                }

                t1 = 0;
                t2 = ipp2 * t0;
                if (nbd < l1)
                {
                    for (j = 1; j < ipph; j++)
                    {
                        t1 += t0;
                        t2 -= t0;
                        t3 = t1;
                        t4 = t2;
                        for (i = 2; i < ido; i += 2)
                        {
                            t3 += 2;
                            t4 += 2;
                            t5 = t3 - ido;
                            t6 = t4 - ido;
                            for (k = 0; k < l1; k++)
                            {
                                t5 += ido;
                                t6 += ido;
                                c1[t5 - 1] = ch[t5 - 1] + ch[t6 - 1];
                                c1[t6 - 1] = ch[t5] - ch[t6];
                                c1[t5] = ch[t5] + ch[t6];
                                c1[t6] = ch[t6 - 1] - ch[t5 - 1];
                            }
                        }
                    }
                }
                else
                {
                    for (j = 1; j < ipph; j++)
                    {
                        t1 += t0;
                        t2 -= t0;
                        t3 = t1;
                        t4 = t2;
                        for (k = 0; k < l1; k++)
                        {
                            t5 = t3;
                            t6 = t4;
                            for (i = 2; i < ido; i += 2)
                            {
                                t5 += 2;
                                t6 += 2;
                                c1[t5 - 1] = ch[t5 - 1] + ch[t6 - 1];
                                c1[t6 - 1] = ch[t5] - ch[t6];
                                c1[t5] = ch[t5] + ch[t6];
                                c1[t6] = ch[t6 - 1] - ch[t5 - 1];
                            }
                            t3 += ido;
                            t4 += ido;
                        }
                    }
                }

            L119:
                for (ik = 0; ik < idl1; ik++)
                    c2[ik] = ch2[ik];

                t1 = 0;
                t2 = ipp2 * idl1;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1 - ido;
                    t4 = t2 - ido;
                    for (k = 0; k < l1; k++)
                    {
                        t3 += ido;
                        t4 += ido;
                        c1[t3] = ch[t3] + ch[t4];
                        c1[t4] = ch[t4] - ch[t3];
                    }
                }

                ar1 = 1.;
                ai1 = 0.;
                t1 = 0;
                t2 = ipp2 * idl1;
                t3 = (ip - 1) * idl1;
                for (l = 1; l < ipph; l++)
                {
                    t1 += idl1;
                    t2 -= idl1;
                    ar1h = dcp * ar1 - dsp * ai1;
                    ai1 = dcp * ai1 + dsp * ar1;
                    ar1 = ar1h;
                    t4 = t1;
                    t5 = t2;
                    t6 = t3;
                    t7 = idl1;

                    for (ik = 0; ik < idl1; ik++)
                    {
                        ch2[t4++] = c2[ik] + ar1 * c2[t7++];
                        ch2[t5++] = ai1 * c2[t6++];
                    }

                    dc2 = ar1;
                    ds2 = ai1;
                    ar2 = ar1;
                    ai2 = ai1;

                    t4 = idl1;
                    t5 = (ipp2 - 1) * idl1;
                    for (j = 2; j < ipph; j++)
                    {
                        t4 += idl1;
                        t5 -= idl1;

                        ar2h = dc2 * ar2 - ds2 * ai2;
                        ai2 = dc2 * ai2 + ds2 * ar2;
                        ar2 = ar2h;

                        t6 = t1;
                        t7 = t2;
                        t8 = t4;
                        t9 = t5;
                        for (ik = 0; ik < idl1; ik++)
                        {
                            ch2[t6++] += ar2 * c2[t8++];
                            ch2[t7++] += ai2 * c2[t9++];
                        }
                    }
                }

                t1 = 0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += idl1;
                    t2 = t1;
                    for (ik = 0; ik < idl1; ik++)
                        ch2[ik] += c2[t2++];
                }

                if (ido < l1)
                    goto L132;

                t1 = 0;
                t2 = 0;
                for (k = 0; k < l1; k++)
                {
                    t3 = t1;
                    t4 = t2;
                    for (i = 0; i < ido; i++)
                        cc[t4++] = ch[t3++];
                    t1 += ido;
                    t2 += t10;
                }

                goto L135;

            L132:
                for (i = 0; i < ido; i++)
                {
                    t1 = i;
                    t2 = i;
                    for (k = 0; k < l1; k++)
                    {
                        cc[t2] = ch[t1];
                        t1 += ido;
                        t2 += t10;
                    }
                }

            L135:
                t1 = 0;
                t2 = ido << 1;
                t3 = 0;
                t4 = ipp2 * t0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t2;
                    t3 += t0;
                    t4 -= t0;

                    t5 = t1;
                    t6 = t3;
                    t7 = t4;

                    for (k = 0; k < l1; k++)
                    {
                        cc[t5 - 1] = ch[t6];
                        cc[t5] = ch[t7];
                        t5 += t10;
                        t6 += ido;
                        t7 += ido;
                    }
                }

                if (ido == 1)
                    return;
                if (nbd < l1)
                    goto L141;

                t1 = -static_cast<ptrdiff_t>(ido);
                t3 = 0;
                t4 = 0;
                t5 = ipp2 * t0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t2;
                    t3 += t2;
                    t4 += t0;
                    t5 -= t0;
                    t6 = t1;
                    t7 = t3;
                    t8 = t4;
                    t9 = t5;
                    for (k = 0; k < l1; k++)
                    {
                        for (i = 2; i < ido; i += 2)
                        {
                            ic = idp2 - i;
                            cc[i + t7 - 1] = ch[i + t8 - 1] + ch[i + t9 - 1];
                            cc[ic + t6 - 1] = ch[i + t8 - 1] - ch[i + t9 - 1];
                            cc[i + t7] = ch[i + t8] + ch[i + t9];
                            cc[ic + t6] = ch[i + t9] - ch[i + t8];
                        }
                        t6 += t10;
                        t7 += t10;
                        t8 += ido;
                        t9 += ido;
                    }
                }
                return;

            L141:
                t1 = -static_cast<ptrdiff_t>(ido);
                t3 = 0;
                t4 = 0;
                t5 = ipp2 * t0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t2;
                    t3 += t2;
                    t4 += t0;
                    t5 -= t0;
                    for (i = 2; i < ido; i += 2)
                    {
                        t6 = idp2 + t1 - i;
                        t7 = i + t3;
                        t8 = i + t4;
                        t9 = i + t5;
                        for (k = 0; k < l1; k++)
                        {
                            cc[t7 - 1] = ch[t8 - 1] + ch[t9 - 1];
                            cc[t6 - 1] = ch[t8 - 1] - ch[t9 - 1];
                            cc[t7] = ch[t8] + ch[t9];
                            cc[t6] = ch[t9] - ch[t8];
                            t6 += t10;
                            t7 += t10;
                            t8 += ido;
                            t9 += ido;
                        }
                    }
                }
            }

            template <typename T>
            void FftpackContext<T>::dradbg(size_t ido, size_t ip, size_t l1, size_t idl1,
                                           T *cc, T *c1, T *c2, T *ch, T *ch2, const T *wa)
            {
                static const T tpi = static_cast<T>(2.0 * M_PI);
                size_t ipph, i, j, k, l, ik;
                ptrdiff_t is, idij;                                              // Can be negative in index arithmetic
                ptrdiff_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12; // Can be negative
                T dc2, ai1, ai2, ar1, ar2, ds2;
                size_t nbd;
                T dcp, arg, dsp, ar1h, ar2h;
                size_t ipp2;

                t10 = ip * ido;
                t0 = l1 * ido;
                arg = tpi / static_cast<T>(ip);
                dcp = std::cos(arg);
                dsp = std::sin(arg);
                nbd = (ido - 1) >> 1;
                ipp2 = ip;
                ipph = (ip + 1) >> 1;

                if (ido < l1)
                    goto L103;

                t1 = 0;
                t2 = 0;
                for (k = 0; k < l1; k++)
                {
                    t3 = t1;
                    t4 = t2;
                    for (i = 0; i < ido; i++)
                    {
                        ch[t3] = cc[t4];
                        t3++;
                        t4++;
                    }
                    t1 += ido;
                    t2 += t10;
                }
                goto L106;

            L103:
                t1 = 0;
                for (i = 0; i < ido; i++)
                {
                    t2 = t1;
                    t3 = t1;
                    for (k = 0; k < l1; k++)
                    {
                        ch[t2] = cc[t3];
                        t2 += ido;
                        t3 += t10;
                    }
                    t1++;
                }

            L106:
                t1 = 0;
                t2 = ipp2 * t0;
                t7 = (t5 = ido << 1);
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1;
                    t4 = t2;
                    t6 = t5;
                    for (k = 0; k < l1; k++)
                    {
                        ch[t3] = cc[t6 - 1] + cc[t6 - 1];
                        ch[t4] = cc[t6] + cc[t6];
                        t3 += ido;
                        t4 += ido;
                        t6 += t10;
                    }
                    t5 += t7;
                }

                if (ido == 1)
                    goto L116;
                if (nbd < l1)
                    goto L112;

                t1 = 0;
                t2 = ipp2 * t0;
                t7 = 0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1;
                    t4 = t2;

                    t7 += (ido << 1);
                    t8 = t7;
                    for (k = 0; k < l1; k++)
                    {
                        t5 = t3;
                        t6 = t4;
                        t9 = t8;
                        t11 = t8;
                        for (i = 2; i < ido; i += 2)
                        {
                            t5 += 2;
                            t6 += 2;
                            t9 += 2;
                            t11 -= 2;
                            ch[t5 - 1] = cc[t9 - 1] + cc[t11 - 1];
                            ch[t6 - 1] = cc[t9 - 1] - cc[t11 - 1];
                            ch[t5] = cc[t9] - cc[t11];
                            ch[t6] = cc[t9] + cc[t11];
                        }
                        t3 += ido;
                        t4 += ido;
                        t8 += t10;
                    }
                }
                goto L116;

            L112:
                t1 = 0;
                t2 = ipp2 * t0;
                t7 = 0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1;
                    t4 = t2;
                    t7 += (ido << 1);
                    t8 = t7;
                    t9 = t7;
                    for (i = 2; i < ido; i += 2)
                    {
                        t3 += 2;
                        t4 += 2;
                        t8 += 2;
                        t9 -= 2;
                        t5 = t3;
                        t6 = t4;
                        t11 = t8;
                        t12 = t9;
                        for (k = 0; k < l1; k++)
                        {
                            ch[t5 - 1] = cc[t11 - 1] + cc[t12 - 1];
                            ch[t6 - 1] = cc[t11 - 1] - cc[t12 - 1];
                            ch[t5] = cc[t11] - cc[t12];
                            ch[t6] = cc[t11] + cc[t12];
                            t5 += ido;
                            t6 += ido;
                            t11 += t10;
                            t12 += t10;
                        }
                    }
                }

            L116:
                ar1 = 1.;
                ai1 = 0.;
                t1 = 0;
                t9 = (t2 = ipp2 * idl1);
                t3 = (ip - 1) * idl1;
                for (l = 1; l < ipph; l++)
                {
                    t1 += idl1;
                    t2 -= idl1;

                    ar1h = dcp * ar1 - dsp * ai1;
                    ai1 = dcp * ai1 + dsp * ar1;
                    ar1 = ar1h;
                    t4 = t1;
                    t5 = t2;
                    t6 = 0;
                    t7 = idl1;
                    t8 = t3;
                    for (ik = 0; ik < idl1; ik++)
                    {
                        c2[t4++] = ch2[t6++] + ar1 * ch2[t7++];
                        c2[t5++] = ai1 * ch2[t8++];
                    }
                    dc2 = ar1;
                    ds2 = ai1;
                    ar2 = ar1;
                    ai2 = ai1;

                    t6 = idl1;
                    t7 = t9 - idl1;
                    for (j = 2; j < ipph; j++)
                    {
                        t6 += idl1;
                        t7 -= idl1;
                        ar2h = dc2 * ar2 - ds2 * ai2;
                        ai2 = dc2 * ai2 + ds2 * ar2;
                        ar2 = ar2h;
                        t4 = t1;
                        t5 = t2;
                        t11 = t6;
                        t12 = t7;
                        for (ik = 0; ik < idl1; ik++)
                        {
                            c2[t4++] += ar2 * ch2[t11++];
                            c2[t5++] += ai2 * ch2[t12++];
                        }
                    }
                }

                t1 = 0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += idl1;
                    t2 = t1;
                    for (ik = 0; ik < idl1; ik++)
                        ch2[ik] += ch2[t2++];
                }

                t1 = 0;
                t2 = ipp2 * t0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1;
                    t4 = t2;
                    for (k = 0; k < l1; k++)
                    {
                        ch[t3] = c1[t3] - c1[t4];
                        ch[t4] = c1[t3] + c1[t4];
                        t3 += ido;
                        t4 += ido;
                    }
                }

                if (ido == 1)
                    goto L132;
                if (nbd < l1)
                    goto L128;

                t1 = 0;
                t2 = ipp2 * t0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1;
                    t4 = t2;
                    for (k = 0; k < l1; k++)
                    {
                        t5 = t3;
                        t6 = t4;
                        for (i = 2; i < ido; i += 2)
                        {
                            t5 += 2;
                            t6 += 2;
                            ch[t5 - 1] = c1[t5 - 1] - c1[t6];
                            ch[t6 - 1] = c1[t5 - 1] + c1[t6];
                            ch[t5] = c1[t5] + c1[t6 - 1];
                            ch[t6] = c1[t5] - c1[t6 - 1];
                        }
                        t3 += ido;
                        t4 += ido;
                    }
                }
                goto L132;

            L128:
                t1 = 0;
                t2 = ipp2 * t0;
                for (j = 1; j < ipph; j++)
                {
                    t1 += t0;
                    t2 -= t0;
                    t3 = t1;
                    t4 = t2;
                    for (i = 2; i < ido; i += 2)
                    {
                        t3 += 2;
                        t4 += 2;
                        t5 = t3;
                        t6 = t4;
                        for (k = 0; k < l1; k++)
                        {
                            ch[t5 - 1] = c1[t5 - 1] - c1[t6];
                            ch[t6 - 1] = c1[t5 - 1] + c1[t6];
                            ch[t5] = c1[t5] + c1[t6 - 1];
                            ch[t6] = c1[t5] - c1[t6 - 1];
                            t5 += ido;
                            t6 += ido;
                        }
                    }
                }

            L132:
                if (ido == 1)
                    return;

                for (ik = 0; ik < idl1; ik++)
                    c2[ik] = ch2[ik];

                t1 = 0;
                for (j = 1; j < ip; j++)
                {
                    t2 = (t1 += t0);
                    for (k = 0; k < l1; k++)
                    {
                        c1[t2] = ch[t2];
                        t2 += ido;
                    }
                }

                if (nbd > l1)
                    goto L139;

                is = -static_cast<ptrdiff_t>(ido) - 1;
                t1 = 0;
                for (j = 1; j < ip; j++)
                {
                    is += ido;
                    t1 += t0;
                    idij = is;
                    t2 = t1;
                    for (i = 2; i < ido; i += 2)
                    {
                        t2 += 2;
                        idij += 2;
                        t3 = t2;
                        for (k = 0; k < l1; k++)
                        {
                            c1[t3 - 1] = wa[idij - 1] * ch[t3 - 1] - wa[idij] * ch[t3];
                            c1[t3] = wa[idij - 1] * ch[t3] + wa[idij] * ch[t3 - 1];
                            t3 += ido;
                        }
                    }
                }
                return;

            L139:
                is = -static_cast<ptrdiff_t>(ido) - 1;
                t1 = 0;
                for (j = 1; j < ip; j++)
                {
                    is += ido;
                    t1 += t0;
                    t2 = t1;
                    for (k = 0; k < l1; k++)
                    {
                        idij = is;
                        t3 = t2;
                        for (i = 2; i < ido; i += 2)
                        {
                            idij += 2;
                            t3 += 2;
                            c1[t3 - 1] = wa[idij - 1] * ch[t3 - 1] - wa[idij] * ch[t3];
                            c1[t3] = wa[idij - 1] * ch[t3] + wa[idij] * ch[t3 - 1];
                        }
                        t2 += ido;
                    }
                }
            }

            // Explicit template instantiations
            template class FftpackContext<float>;
            template class FftpackContext<double>;

        } // namespace fftpack
    } // namespace core
} // namespace dsp
