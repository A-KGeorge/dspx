/**
 * IIR Filter Implementation
 */

#define _USE_MATH_DEFINES
#include "IirFilter.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    namespace core
    {

        template <typename T>
        IirFilter<T>::IirFilter(const std::vector<T> &b_coeffs, const std::vector<T> &a_coeffs, bool stateful)
            : m_b_coeffs(b_coeffs), m_a_coeffs(a_coeffs), m_x_index(0), m_y_index(0), m_x_mask(0), m_y_mask(0), m_stateful(stateful)
        {
            if (b_coeffs.empty())
            {
                throw std::invalid_argument("IIR filter requires at least one feedforward coefficient");
            }

            if (stateful)
            {
                // Round state buffers to power-of-2 for O(1) circular buffer access
                // X state needs b_coeffs.size() - 1 history (we exclude current sample)
                size_t x_state_size = b_coeffs.size() > 1 ? b_coeffs.size() - 1 : 1;
                size_t x_power_of_2 = 1;
                while (x_power_of_2 < x_state_size)
                {
                    x_power_of_2 <<= 1;
                }
                m_x_state.resize(x_power_of_2, T(0));
                m_x_mask = x_power_of_2 - 1;

                // Y state needs a_coeffs.size() history
                if (!a_coeffs.empty())
                {
                    size_t y_state_size = a_coeffs.size();
                    size_t y_power_of_2 = 1;
                    while (y_power_of_2 < y_state_size)
                    {
                        y_power_of_2 <<= 1;
                    }
                    m_y_state.resize(y_power_of_2, T(0));
                    m_y_mask = y_power_of_2 - 1;
                }
            }
        }

        template <typename T>
        T IirFilter<T>::processSample(T input)
        {
            if (!m_stateful)
            {
                throw std::runtime_error("processSample() requires stateful mode");
            }

            // Direct Form I with circular buffer (O(1) state update instead of O(N) shifting)
            // Compute feedforward (numerator): b[0]*x[n] + b[1]*x[n-1] + ... + b[M]*x[n-M]
            T output = m_b_coeffs[0] * input;

            // Read from circular buffer using bitwise AND (power-of-2 optimization)
            for (size_t i = 1; i < m_b_coeffs.size(); ++i)
            {
                // x_state stores x[n-1], x[n-2], ..., x[n-M]
                // Read backwards: x[n-i] is at position (m_x_index + m_x_mask + 1 - (i-1)) & m_x_mask
                // Adding buffer size before subtraction prevents underflow
                size_t idx = (m_x_index + m_x_mask + 1 - (i - 1)) & m_x_mask;
                output += m_b_coeffs[i] * m_x_state[idx];
            }

            // Compute feedback (denominator): - (a[1]*y[n-1] + a[2]*y[n-2] + ... + a[N]*y[n-N])
            for (size_t i = 0; i < m_a_coeffs.size(); ++i)
            {
                // y_state stores y[n-1], y[n-2], ..., y[n-N]
                // Read backwards: y[n-(i+1)] is at position (m_y_index + m_y_mask + 1 - i) & m_y_mask
                // Adding buffer size before subtraction prevents underflow
                size_t idx = (m_y_index + m_y_mask + 1 - i) & m_y_mask;
                output -= m_a_coeffs[i] * m_y_state[idx];
            }

            // Update circular buffers (O(1) write operation)
            // Advance indices and store current values
            m_x_index = (m_x_index + 1) & m_x_mask;
            m_x_state[m_x_index] = input;

            m_y_index = (m_y_index + 1) & m_y_mask;
            m_y_state[m_y_index] = output;

            return output;
        }

        template <typename T>
        void IirFilter<T>::process(const T *input, T *output, size_t length, bool stateless)
        {
            if (stateless || !m_stateful)
            {
                // Stateless mode: use temporary circular buffers for batch
                size_t x_state_size = m_b_coeffs.size() > 1 ? m_b_coeffs.size() - 1 : 1;
                size_t x_power_of_2 = 1;
                while (x_power_of_2 < x_state_size)
                {
                    x_power_of_2 <<= 1;
                }
                std::vector<T> x_temp(x_power_of_2, T(0));
                size_t x_mask = x_power_of_2 - 1;
                size_t x_idx = 0;

                size_t y_power_of_2 = 1;
                if (!m_a_coeffs.empty())
                {
                    size_t y_state_size = m_a_coeffs.size();
                    while (y_power_of_2 < y_state_size)
                    {
                        y_power_of_2 <<= 1;
                    }
                }
                std::vector<T> y_temp(y_power_of_2, T(0));
                size_t y_mask = y_power_of_2 - 1;
                size_t y_idx = 0;

                for (size_t n = 0; n < length; ++n)
                {
                    // Feedforward
                    T y = m_b_coeffs[0] * input[n];
                    for (size_t i = 1; i < m_b_coeffs.size(); ++i)
                    {
                        size_t idx = (x_idx + x_mask + 1 - (i - 1)) & x_mask;
                        y += m_b_coeffs[i] * x_temp[idx];
                    }

                    // Feedback
                    for (size_t i = 0; i < m_a_coeffs.size(); ++i)
                    {
                        size_t idx = (y_idx + y_mask + 1 - i) & y_mask;
                        y -= m_a_coeffs[i] * y_temp[idx];
                    }

                    output[n] = y;

                    // Update circular buffers (O(1))
                    x_idx = (x_idx + 1) & x_mask;
                    x_temp[x_idx] = input[n];

                    y_idx = (y_idx + 1) & y_mask;
                    y_temp[y_idx] = y;
                }
            }
            else
            {
                // Stateful mode: inline processing for better performance
                // (eliminates function call overhead compared to calling processSample)
                for (size_t n = 0; n < length; ++n)
                {
                    // Feedforward
                    T y = m_b_coeffs[0] * input[n];
                    for (size_t i = 1; i < m_b_coeffs.size(); ++i)
                    {
                        size_t idx = (m_x_index + m_x_mask + 1 - (i - 1)) & m_x_mask;
                        y += m_b_coeffs[i] * m_x_state[idx];
                    }

                    // Feedback
                    for (size_t i = 0; i < m_a_coeffs.size(); ++i)
                    {
                        size_t idx = (m_y_index + m_y_mask + 1 - i) & m_y_mask;
                        y -= m_a_coeffs[i] * m_y_state[idx];
                    }

                    output[n] = y;

                    // Update state (O(1))
                    m_x_index = (m_x_index + 1) & m_x_mask;
                    m_x_state[m_x_index] = input[n];

                    m_y_index = (m_y_index + 1) & m_y_mask;
                    m_y_state[m_y_index] = y;
                }
            }
        }

        template <typename T>
        void IirFilter<T>::reset()
        {
            if (m_stateful)
            {
                std::fill(m_x_state.begin(), m_x_state.end(), T(0));
                std::fill(m_y_state.begin(), m_y_state.end(), T(0));
                m_x_index = 0;
                m_y_index = 0;
            }
        }

        template <typename T>
        void IirFilter<T>::setCoefficients(const std::vector<T> &b_coeffs, const std::vector<T> &a_coeffs)
        {
            if (b_coeffs.empty())
            {
                throw std::invalid_argument("B coefficients cannot be empty");
            }

            m_b_coeffs = b_coeffs;
            m_a_coeffs = a_coeffs;

            if (m_stateful)
            {
                // Resize circular buffers to power-of-2
                size_t x_state_size = b_coeffs.size() > 1 ? b_coeffs.size() - 1 : 1;
                size_t x_power_of_2 = 1;
                while (x_power_of_2 < x_state_size)
                {
                    x_power_of_2 <<= 1;
                }
                m_x_state.resize(x_power_of_2, T(0));
                m_x_mask = x_power_of_2 - 1;
                m_x_index = 0;

                if (!a_coeffs.empty())
                {
                    size_t y_state_size = a_coeffs.size();
                    size_t y_power_of_2 = 1;
                    while (y_power_of_2 < y_state_size)
                    {
                        y_power_of_2 <<= 1;
                    }
                    m_y_state.resize(y_power_of_2, T(0));
                    m_y_mask = y_power_of_2 - 1;
                    m_y_index = 0;
                }
            }
        }

        template <typename T>
        bool IirFilter<T>::isStable() const
        {
            // Basic stability check: sum of absolute feedback coefficients < 1
            // This is a necessary but not sufficient condition
            T sum = T(0);
            for (const auto &a : m_a_coeffs)
            {
                sum += std::abs(a);
            }
            return sum < T(1);
        }

        template <typename T>
        std::pair<std::vector<T>, std::vector<T>> IirFilter<T>::getState() const
        {
            // Return the full circular buffers (power-of-2 sized)
            // Caller needs to be aware these may be larger than the actual filter order
            return {m_x_state, m_y_state};
        }

        template <typename T>
        void IirFilter<T>::setState(const std::vector<T> &x_state, const std::vector<T> &y_state)
        {
            if (!m_stateful)
            {
                throw std::runtime_error("setState() requires stateful mode");
            }

            // Accept circular buffer state (should match power-of-2 size)
            if (x_state.size() != m_x_state.size())
            {
                throw std::invalid_argument("x_state size must match circular buffer size");
            }
            if (y_state.size() != m_y_state.size())
            {
                throw std::invalid_argument("y_state size must match circular buffer size");
            }

            m_x_state = x_state;
            m_y_state = y_state;
            // Indices reset to 0 for consistency (caller can restore indices if needed)
            m_x_index = 0;
            m_y_index = 0;
        }

        // ========== Filter Design Methods ==========

        template <typename T>
        IirFilter<T> IirFilter<T>::createFirstOrderLowPass(T cutoffFreq)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            // First-order low-pass: H(z) = (b0 + b1*z^-1) / (1 + a1*z^-1)
            // Using bilinear transform from analog RC filter
            T omega_c = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T K = std::tan(omega_c / T(2));

            T b0 = K / (T(1) + K);
            T b1 = K / (T(1) + K);
            T a1 = (K - T(1)) / (T(1) + K);

            return IirFilter<T>({b0, b1}, {a1}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createFirstOrderHighPass(T cutoffFreq)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            // First-order high-pass
            T omega_c = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T K = std::tan(omega_c / T(2));

            T b0 = T(1) / (T(1) + K);
            T b1 = -T(1) / (T(1) + K);
            T a1 = (K - T(1)) / (T(1) + K);

            return IirFilter<T>({b0, b1}, {a1}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createBiquad(T b0, T b1, T b2, T a1, T a2)
        {
            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createButterworthLowPass(T cutoffFreq, int order)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            if (order < 1 || order > 8)
            {
                throw std::invalid_argument("Order must be between 1 and 8");
            }

            // For simplicity, implement 2nd-order Butterworth (biquad)
            if (order == 1)
            {
                return createFirstOrderLowPass(cutoffFreq);
            }

            // 2nd-order Butterworth low-pass
            T omega_c = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T K = std::tan(omega_c / T(2));
            T K2 = K * K;
            T sqrt2 = static_cast<T>(std::sqrt(2.0));

            T norm = T(1) / (T(1) + sqrt2 * K + K2);

            T b0 = K2 * norm;
            T b1 = T(2) * b0;
            T b2 = b0;

            T a1 = T(2) * (K2 - T(1)) * norm;
            T a2 = (T(1) - sqrt2 * K + K2) * norm;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createButterworthHighPass(T cutoffFreq, int order)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            if (order < 1 || order > 8)
            {
                throw std::invalid_argument("Order must be between 1 and 8");
            }

            if (order == 1)
            {
                return createFirstOrderHighPass(cutoffFreq);
            }

            // 2nd-order Butterworth high-pass
            T omega_c = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T K = std::tan(omega_c / T(2));
            T K2 = K * K;
            T sqrt2 = static_cast<T>(std::sqrt(2.0));

            T norm = T(1) / (T(1) + sqrt2 * K + K2);

            T b0 = norm;
            T b1 = -T(2) * norm;
            T b2 = norm;

            T a1 = T(2) * (K2 - T(1)) * norm;
            T a2 = (T(1) - sqrt2 * K + K2) * norm;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createButterworthBandPass(T lowCutoff, T highCutoff, int order)
        {
            if (lowCutoff >= highCutoff)
            {
                throw std::invalid_argument("Low cutoff must be less than high cutoff");
            }

            // Cascade high-pass and low-pass filters
            // This creates a band-pass by allowing frequencies between lowCutoff and highCutoff
            auto hp = createButterworthHighPass(lowCutoff, order);
            auto lp = createButterworthLowPass(highCutoff, order);

            // Cascade the two filters using polynomial multiplication
            return cascadeFilters(hp, lp);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createChebyshevLowPass(T cutoffFreq, int order, T rippleDb)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            if (order < 1 || order > 8)
            {
                throw std::invalid_argument("Order must be between 1 and 8");
            }

            if (rippleDb <= 0 || rippleDb > T(3.0))
            {
                throw std::invalid_argument("Ripple must be between 0 and 3 dB");
            }

            // For simplicity, implement 2nd-order Chebyshev Type I
            if (order == 1)
            {
                // First-order Chebyshev is same as Butterworth
                return createFirstOrderLowPass(cutoffFreq);
            }

            // 2nd-order Chebyshev Type I low-pass
            T omega_c = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T epsilon = std::sqrt(std::pow(T(10), rippleDb / T(10)) - T(1));

            // Chebyshev pole calculation
            T sinh_val = std::sinh(std::asinh(T(1) / epsilon) / T(2));
            T cosh_val = std::cosh(std::asinh(T(1) / epsilon) / T(2));

            T K = std::tan(omega_c / T(2));
            T K2 = K * K;

            // Pole positions for 2nd-order Chebyshev
            T wp = T(2) * sinh_val; // Pole width
            T rp = cosh_val;        // Pole radius

            T norm = T(1) / (T(1) + wp * K + rp * K2);

            T b0 = rp * K2 * norm;
            T b1 = T(2) * b0;
            T b2 = b0;

            T a1 = T(2) * (rp * K2 - T(1)) * norm;
            T a2 = (T(1) - wp * K + rp * K2) * norm;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createChebyshevHighPass(T cutoffFreq, int order, T rippleDb)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            if (order < 1 || order > 8)
            {
                throw std::invalid_argument("Order must be between 1 and 8");
            }

            if (rippleDb <= 0 || rippleDb > T(3.0))
            {
                throw std::invalid_argument("Ripple must be between 0 and 3 dB");
            }

            if (order == 1)
            {
                return createFirstOrderHighPass(cutoffFreq);
            }

            // 2nd-order Chebyshev Type I high-pass
            T omega_c = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T epsilon = std::sqrt(std::pow(T(10), rippleDb / T(10)) - T(1));

            T sinh_val = std::sinh(std::asinh(T(1) / epsilon) / T(2));
            T cosh_val = std::cosh(std::asinh(T(1) / epsilon) / T(2));

            T K = std::tan(omega_c / T(2));
            T K2 = K * K;

            T wp = T(2) * sinh_val;
            T rp = cosh_val;

            T norm = T(1) / (T(1) + wp * K + rp * K2);

            T b0 = norm;
            T b1 = T(-2) * norm;
            T b2 = norm;

            T a1 = T(2) * (rp * K2 - T(1)) * norm;
            T a2 = (T(1) - wp * K + rp * K2) * norm;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createChebyshevBandPass(T lowCutoff, T highCutoff, int order, T rippleDb)
        {
            if (lowCutoff <= 0 || highCutoff >= T(0.5) || lowCutoff >= highCutoff)
            {
                throw std::invalid_argument("Invalid cutoff frequencies");
            }

            if (order < 1 || order > 8)
            {
                throw std::invalid_argument("Order must be between 1 and 8");
            }

            // Cascade high-pass and low-pass Chebyshev filters
            auto hp = createChebyshevHighPass(lowCutoff, order, rippleDb);
            auto lp = createChebyshevLowPass(highCutoff, order, rippleDb);

            // Cascade the two filters using polynomial multiplication
            return cascadeFilters(hp, lp);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createPeakingEQ(T centerFreq, T Q, T gainDb)
        {
            if (centerFreq <= 0 || centerFreq >= T(0.5))
            {
                throw std::invalid_argument("Center frequency must be between 0 and 0.5");
            }

            if (Q <= 0)
            {
                throw std::invalid_argument("Q must be positive");
            }

            // Peaking EQ biquad filter (Robert Bristow-Johnson's Audio EQ Cookbook)
            T omega = T(2) * static_cast<T>(M_PI) * centerFreq;
            T A = std::pow(T(10), gainDb / T(40)); // Linear gain
            T alpha = std::sin(omega) / (T(2) * Q);
            T cos_omega = std::cos(omega);

            T b0 = T(1) + alpha * A;
            T b1 = T(-2) * cos_omega;
            T b2 = T(1) - alpha * A;
            T a0 = T(1) + alpha / A;
            T a1 = T(-2) * cos_omega;
            T a2 = T(1) - alpha / A;

            // Normalize by a0
            b0 /= a0;
            b1 /= a0;
            b2 /= a0;
            a1 /= a0;
            a2 /= a0;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createLowShelf(T cutoffFreq, T gainDb, T Q)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            if (Q <= 0)
            {
                throw std::invalid_argument("Q must be positive");
            }

            // Low-shelf biquad filter (Audio EQ Cookbook)
            T omega = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T A = std::pow(T(10), gainDb / T(40));
            T cos_omega = std::cos(omega);
            T sin_omega = std::sin(omega);
            T alpha = sin_omega / (T(2) * Q);
            T beta = std::sqrt(A) / Q;

            T b0 = A * ((A + T(1)) - (A - T(1)) * cos_omega + beta * sin_omega);
            T b1 = T(2) * A * ((A - T(1)) - (A + T(1)) * cos_omega);
            T b2 = A * ((A + T(1)) - (A - T(1)) * cos_omega - beta * sin_omega);
            T a0 = (A + T(1)) + (A - T(1)) * cos_omega + beta * sin_omega;
            T a1 = T(-2) * ((A - T(1)) + (A + T(1)) * cos_omega);
            T a2 = (A + T(1)) + (A - T(1)) * cos_omega - beta * sin_omega;

            // Normalize by a0
            b0 /= a0;
            b1 /= a0;
            b2 /= a0;
            a1 /= a0;
            a2 /= a0;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::createHighShelf(T cutoffFreq, T gainDb, T Q)
        {
            if (cutoffFreq <= 0 || cutoffFreq > T(1.0))
            {
                throw std::invalid_argument("Cutoff frequency must be between 0 and 1.0");
            }

            if (Q <= 0)
            {
                throw std::invalid_argument("Q must be positive");
            }

            // High-shelf biquad filter (Audio EQ Cookbook)
            T omega = T(2) * static_cast<T>(M_PI) * cutoffFreq;
            T A = std::pow(T(10), gainDb / T(40));
            T cos_omega = std::cos(omega);
            T sin_omega = std::sin(omega);
            T alpha = sin_omega / (T(2) * Q);
            T beta = std::sqrt(A) / Q;

            T b0 = A * ((A + T(1)) + (A - T(1)) * cos_omega + beta * sin_omega);
            T b1 = T(-2) * A * ((A - T(1)) + (A + T(1)) * cos_omega);
            T b2 = A * ((A + T(1)) + (A - T(1)) * cos_omega - beta * sin_omega);
            T a0 = (A + T(1)) - (A - T(1)) * cos_omega + beta * sin_omega;
            T a1 = T(2) * ((A - T(1)) - (A + T(1)) * cos_omega);
            T a2 = (A + T(1)) - (A - T(1)) * cos_omega - beta * sin_omega;

            // Normalize by a0
            b0 /= a0;
            b1 /= a0;
            b2 /= a0;
            a1 /= a0;
            a2 /= a0;

            return IirFilter<T>({b0, b1, b2}, {a1, a2}, true);
        }

        template <typename T>
        IirFilter<T> IirFilter<T>::cascadeFilters(const IirFilter<T> &filter1, const IirFilter<T> &filter2)
        {
            // Get coefficients from both filters
            const auto &b1 = filter1.getBCoefficients();
            const auto &a1 = filter1.getACoefficients();
            const auto &b2 = filter2.getBCoefficients();
            const auto &a2 = filter2.getACoefficients();

            // Cascade: H(z) = H1(z) * H2(z)
            // Numerator: B(z) = B1(z) * B2(z) (polynomial multiplication)
            // Denominator: A(z) = A1(z) * A2(z) (polynomial multiplication)

            // Convolve numerators (b coefficients)
            size_t b_len = b1.size() + b2.size() - 1;
            std::vector<T> b_result(b_len, T(0));

            for (size_t i = 0; i < b1.size(); ++i)
            {
                for (size_t j = 0; j < b2.size(); ++j)
                {
                    b_result[i + j] += b1[i] * b2[j];
                }
            }

            // Convolve denominators (a coefficients)
            // Note: a coefficients don't include a[0]=1, so we need to prepend it
            std::vector<T> a1_full(a1.size() + 1);
            std::vector<T> a2_full(a2.size() + 1);
            a1_full[0] = T(1);
            a2_full[0] = T(1);
            std::copy(a1.begin(), a1.end(), a1_full.begin() + 1);
            std::copy(a2.begin(), a2.end(), a2_full.begin() + 1);

            size_t a_len = a1_full.size() + a2_full.size() - 1;
            std::vector<T> a_result_full(a_len, T(0));

            for (size_t i = 0; i < a1_full.size(); ++i)
            {
                for (size_t j = 0; j < a2_full.size(); ++j)
                {
                    a_result_full[i + j] += a1_full[i] * a2_full[j];
                }
            }

            // Normalize by a[0] and extract a[1], a[2], ...
            T a0 = a_result_full[0];
            std::vector<T> b_normalized(b_result.size());
            std::vector<T> a_normalized(a_result_full.size() - 1);

            for (size_t i = 0; i < b_result.size(); ++i)
            {
                b_normalized[i] = b_result[i] / a0;
            }

            for (size_t i = 1; i < a_result_full.size(); ++i)
            {
                a_normalized[i - 1] = a_result_full[i] / a0;
            }

            return IirFilter<T>(b_normalized, a_normalized, true);
        }

        // Explicit template instantiations
        template class IirFilter<float>;
        template class IirFilter<double>;

    } // namespace core
} // namespace dsp
