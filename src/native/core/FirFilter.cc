/**
 * FIR Filter Implementation with SIMD-Optimized Convolution
 * Now uses SlidingWindowFilter infrastructure for consistency
 */

#define _USE_MATH_DEFINES
#include "FirFilter.h"
#include "../utils/SimdOps.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    namespace core
    {

        template <typename T>
        FirFilter<T>::FirFilter(const std::vector<T> &coefficients, bool stateful)
            : m_coefficients(coefficients), m_stateIndex(0), m_stateMask(0), m_stateful(stateful)
        {
            if (coefficients.empty())
            {
                throw std::invalid_argument("FIR filter requires at least one coefficient");
            }

#if defined(__ARM_NEON) || defined(__aarch64__)
            // Auto-select NEON for float32 + small-medium taps (where transposed form wins)
            // For large taps (>128), circular buffer's O(1) state update is better than O(N) shift
            m_useNeon = false;
            if constexpr (std::is_same_v<T, float>)
            {
                if (stateful && coefficients.size() >= 8 && coefficients.size() <= 128)
                {
                    m_neonFilter = std::make_unique<FirFilterNeon>(coefficients);
                    m_useNeon = true;
                }
            }
#endif

            if (stateful)
            {
                // Round up to next power of 2 for efficient circular buffer (enables bitwise AND instead of modulo)
                size_t stateSize = coefficients.size();
                size_t powerOf2 = 1;
                while (powerOf2 < stateSize)
                {
                    powerOf2 <<= 1;
                }

                m_state.resize(powerOf2, T(0));
                m_stateMask = powerOf2 - 1; // For fast modulo: index & mask == index % size
            }
        }

        template <typename T>
        T FirFilter<T>::processSample(T input)
        {
            if (!m_stateful)
            {
                throw std::runtime_error("processSample() requires stateful mode");
            }

#if defined(__ARM_NEON) || defined(__aarch64__)
            // Use NEON filter if available
            if (m_useNeon && m_neonFilter)
            {
                return static_cast<T>(m_neonFilter->processSample(static_cast<float>(input)));
            }
#endif

            // Store input in circular buffer
            m_state[m_stateIndex] = input;

            // Compute output directly from circular buffer (no copy needed)
            T output = T(0);

            // Use unrolled scalar loop for both float and double
            // Modern compilers auto-vectorize this better than manual SIMD with data gather
            const size_t numCoeffs = m_coefficients.size();
            size_t i = 0;

            // Process 4 coefficients at a time (loop unrolling for ILP)
            for (; i + 3 < numCoeffs; i += 4)
            {
                output += m_coefficients[i] * m_state[(m_stateIndex - i) & m_stateMask];
                output += m_coefficients[i + 1] * m_state[(m_stateIndex - i - 1) & m_stateMask];
                output += m_coefficients[i + 2] * m_state[(m_stateIndex - i - 2) & m_stateMask];
                output += m_coefficients[i + 3] * m_state[(m_stateIndex - i - 3) & m_stateMask];
            }

            // Handle remaining coefficients
            for (; i < numCoeffs; ++i)
            {
                output += m_coefficients[i] * m_state[(m_stateIndex - i) & m_stateMask];
            }

            // Advance circular buffer index (bitwise AND instead of modulo)
            m_stateIndex = (m_stateIndex + 1) & m_stateMask;

            return output;
        }

        template <typename T>
        void FirFilter<T>::process(const T *input, T *output, size_t length, bool stateless)
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            // Use NEON batch processing for stateful mode
            if (!stateless && m_stateful && m_useNeon && m_neonFilter)
            {
                for (size_t i = 0; i < length; ++i)
                {
                    output[i] = static_cast<T>(m_neonFilter->processSample(static_cast<float>(input[i])));
                }
                return;
            }
#endif

            if (stateless || !m_stateful)
            {
                // Stateless mode: each output depends only on current window
                for (size_t n = 0; n < length; ++n)
                {
                    T sum = T(0);

                    if constexpr (std::is_same_v<T, float>)
                    {
                        // Use SIMD for stateless convolution too
                        size_t available = std::min(m_coefficients.size(), n + 1);

                        if (available == m_coefficients.size())
                        {
                            // Full window available - direct SIMD dot product
                            sum = simd::dot_product(&input[n - available + 1],
                                                    m_coefficients.data(),
                                                    available);
                        }
                        else
                        {
                            // Partial window - scalar for simplicity
                            for (size_t i = 0; i < available; ++i)
                            {
                                sum += m_coefficients[i] * input[n - i];
                            }
                        }
                    }
                    else
                    {
                        // Scalar for double
                        for (size_t i = 0; i < m_coefficients.size() && i <= n; ++i)
                        {
                            sum += m_coefficients[i] * input[n - i];
                        }
                    }

                    output[n] = sum;
                }
            }
            else
            {
                // Stateful mode: optimized inline block processing
                // Direct access to state buffer with bitwise masking - no copying
                const size_t numCoeffs = m_coefficients.size();

                for (size_t i = 0; i < length; ++i)
                {
                    // Update state with new sample
                    m_stateIndex = (m_stateIndex + 1) & m_stateMask;
                    m_state[m_stateIndex] = input[i];

                    // Compute convolution directly from circular buffer (no copy)
                    // Loop unrolling for instruction-level parallelism
                    T sum = 0;
                    size_t j = 0;

                    // Process 4 coefficients at a time (unrolled)
                    for (; j + 3 < numCoeffs; j += 4)
                    {
                        sum += m_coefficients[j] * m_state[(m_stateIndex - j) & m_stateMask];
                        sum += m_coefficients[j + 1] * m_state[(m_stateIndex - j - 1) & m_stateMask];
                        sum += m_coefficients[j + 2] * m_state[(m_stateIndex - j - 2) & m_stateMask];
                        sum += m_coefficients[j + 3] * m_state[(m_stateIndex - j - 3) & m_stateMask];
                    }

                    // Handle remaining samples
                    for (; j < numCoeffs; ++j)
                    {
                        sum += m_coefficients[j] * m_state[(m_stateIndex - j) & m_stateMask];
                    }

                    output[i] = sum;
                }
            }
        }

        template <typename T>
        void FirFilter<T>::reset()
        {
#if defined(__ARM_NEON) || defined(__aarch64__)
            if (m_useNeon && m_neonFilter)
            {
                m_neonFilter->reset();
            }
#endif

            if (m_stateful)
            {
                std::fill(m_state.begin(), m_state.end(), T(0));
                m_stateIndex = 0;
            }
        }

        template <typename T>
        void FirFilter<T>::setCoefficients(const std::vector<T> &coefficients)
        {
            if (coefficients.empty())
            {
                throw std::invalid_argument("Coefficients cannot be empty");
            }

            m_coefficients = coefficients;

#if defined(__ARM_NEON) || defined(__aarch64__)
            // Update NEON filter if in use
            m_useNeon = false;
            if constexpr (std::is_same_v<T, float>)
            {
                if (m_stateful && coefficients.size() >= 8 && coefficients.size() <= 128)
                {
                    m_neonFilter = std::make_unique<FirFilterNeon>(coefficients);
                    m_useNeon = true;
                }
                else
                {
                    m_neonFilter.reset();
                }
            }
#endif

            if (m_stateful)
            {
                // Round up to next power of 2
                size_t stateSize = coefficients.size();
                size_t powerOf2 = 1;
                while (powerOf2 < stateSize)
                {
                    powerOf2 <<= 1;
                }

                m_state.resize(powerOf2, T(0));
                m_stateMask = powerOf2 - 1;
                m_stateIndex = 0;
            }
        }

        template <typename T>
        std::pair<std::vector<T>, size_t> FirFilter<T>::getState() const
        {
            return {m_state, m_stateIndex};
        }

        template <typename T>
        void FirFilter<T>::setState(const std::vector<T> &state, size_t stateIndex)
        {
            if (!m_stateful)
            {
                throw std::runtime_error("setState() requires stateful mode");
            }

            // Validate size - must match the power-of-2 buffer size, not coefficient count
            if (state.size() != m_state.size())
            {
                throw std::invalid_argument("state size must match internal buffer size");
            }
            if (stateIndex >= state.size() && !state.empty())
            {
                throw std::invalid_argument("stateIndex out of range");
            }

            m_state = state;
            m_stateIndex = stateIndex;
        }

        // ========== Filter Design Methods ==========

        template <typename T>
        std::vector<T> FirFilter<T>::generateSincLowPass(T cutoffFreq, size_t numTaps)
        {
            if (numTaps % 2 == 0)
            {
                ++numTaps; // Ensure odd for symmetric impulse response
            }

            std::vector<T> impulse(numTaps);
            int M = static_cast<int>(numTaps - 1);
            int M_half = M / 2;

            for (int n = 0; n < static_cast<int>(numTaps); ++n)
            {
                int n_shifted = n - M_half;

                if (n_shifted == 0)
                {
                    // sinc(0) = 1
                    impulse[n] = T(2) * cutoffFreq;
                }
                else
                {
                    // sinc(x) = sin(πx) / (πx)
                    T x = T(2) * static_cast<T>(M_PI) * cutoffFreq * static_cast<T>(n_shifted);
                    impulse[n] = std::sin(x) / (static_cast<T>(M_PI) * static_cast<T>(n_shifted));
                }
            }

            return impulse;
        }

        template <typename T>
        void FirFilter<T>::applyWindow(std::vector<T> &impulse, const std::string &windowType)
        {
            const size_t N = impulse.size();
            const T pi = static_cast<T>(M_PI);

            for (size_t n = 0; n < N; ++n)
            {
                T window = T(1);
                const T nf = static_cast<T>(n);
                const T Nf = static_cast<T>(N);

                if (windowType == "hamming")
                {
                    window = T(0.54) - T(0.46) * std::cos(T(2) * pi * nf / (Nf - T(1)));
                }
                else if (windowType == "hann")
                {
                    window = T(0.5) * (T(1) - std::cos(T(2) * pi * nf / (Nf - T(1))));
                }
                else if (windowType == "blackman")
                {
                    window = T(0.42) - T(0.5) * std::cos(T(2) * pi * nf / (Nf - T(1))) + T(0.08) * std::cos(T(4) * pi * nf / (Nf - T(1)));
                }
                else if (windowType == "bartlett")
                {
                    window = T(1) - std::abs(T(2) * nf / (Nf - T(1)) - T(1));
                }

                impulse[n] *= window;
            }
        }

        template <typename T>
        FirFilter<T> FirFilter<T>::createLowPass(T cutoffFreq, size_t numTaps, const std::string &windowType)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0 (normalized)");
            }

            auto impulse = generateSincLowPass(cutoffFreq, numTaps);
            applyWindow(impulse, windowType);

            // Normalize to unit gain at DC
            T sum = T(0);
            for (const auto &val : impulse)
            {
                sum += val;
            }

            for (auto &val : impulse)
            {
                val /= sum;
            }

            return FirFilter<T>(impulse, true);
        }

        template <typename T>
        FirFilter<T> FirFilter<T>::createHighPass(T cutoffFreq, size_t numTaps, const std::string &windowType)
        {
            // High-pass = delta - low-pass (spectral inversion)
            auto lowPass = createLowPass(cutoffFreq, numTaps, windowType);
            std::vector<T> coeffs = lowPass.getCoefficients(); // Explicit copy

            // Spectral inversion
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                coeffs[i] = -coeffs[i];
            }

            // Add impulse at center
            coeffs[coeffs.size() / 2] += T(1);

            return FirFilter<T>(coeffs, true);
        }

        template <typename T>
        FirFilter<T> FirFilter<T>::createBandPass(T lowCutoff, T highCutoff, size_t numTaps, const std::string &windowType)
        {
            if (lowCutoff >= highCutoff)
            {
                throw std::invalid_argument("Low cutoff must be less than high cutoff");
            }

            // Band-pass = low-pass(high) - low-pass(low)
            auto lpHigh = createLowPass(highCutoff, numTaps, windowType);
            auto lpLow = createLowPass(lowCutoff, numTaps, windowType);

            std::vector<T> coeffsHigh = lpHigh.getCoefficients(); // Explicit copy
            std::vector<T> coeffsLow = lpLow.getCoefficients();   // Explicit copy

            std::vector<T> bandPass(coeffsHigh.size());
            for (size_t i = 0; i < coeffsHigh.size(); ++i)
            {
                bandPass[i] = coeffsHigh[i] - coeffsLow[i];
            }

            return FirFilter<T>(bandPass, true);
        }

        template <typename T>
        FirFilter<T> FirFilter<T>::createBandStop(T lowCutoff, T highCutoff, size_t numTaps, const std::string &windowType)
        {
            // Band-stop = low-pass(low) + high-pass(high)
            auto lpLow = createLowPass(lowCutoff, numTaps, windowType);
            auto hpHigh = createHighPass(highCutoff, numTaps, windowType);

            std::vector<T> coeffsLow = lpLow.getCoefficients();   // Explicit copy
            std::vector<T> coeffsHigh = hpHigh.getCoefficients(); // Explicit copy

            std::vector<T> bandStop(coeffsLow.size());
            for (size_t i = 0; i < coeffsLow.size(); ++i)
            {
                bandStop[i] = coeffsLow[i] + coeffsHigh[i];
            }

            return FirFilter<T>(bandStop, true);
        }

        // Explicit template instantiations
        template class FirFilter<float>;
        template class FirFilter<double>;

    } // namespace core
} // namespace dsp
