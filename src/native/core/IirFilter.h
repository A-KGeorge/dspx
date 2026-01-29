/**
 * IIR (Infinite Impulse Response) Filter
 *
 * A recursive filter defined by:
 *   y[n] = (b[0]*x[n] + b[1]*x[n-1] + ... + b[M]*x[n-M])
 *        - (a[1]*y[n-1] + a[2]*y[n-2] + ... + a[N]*y[n-N])
 *
 * Standard form: a[0] = 1 (normalized)
 *
 * Features:
 * - Feedback structure (can be unstable if poles outside unit circle)
 * - Efficient (fewer coefficients than FIR for same frequency response)
 * - Stateful (maintains input/output history) and stateless modes
 * - Common filter designs: Butterworth, Chebyshev, Bessel
 * - Biquad cascade structure for numerical stability
 */

#ifndef DSP_CORE_IIR_FILTER_H
#define DSP_CORE_IIR_FILTER_H

#include <vector>
#include <span>
#include <cstddef>
#include <memory>

namespace dsp
{
    namespace core
    {

        template <typename T = float>
        class IirFilter
        {
        public:
            /**
             * Constructor
             * @param b_coeffs Feedforward coefficients (b[0], b[1], ..., b[M])
             * @param a_coeffs Feedback coefficients (a[1], a[2], ..., a[N])
             *                 Note: a[0] is assumed to be 1 (normalized form)
             * @param stateful If true, maintains state between process calls
             */
            IirFilter(const std::vector<T> &b_coeffs, const std::vector<T> &a_coeffs, bool stateful = true);

            /**
             * Process single sample (stateful mode only)
             * @param input Input sample
             * @return Filtered output sample
             */
            T processSample(T input);

            /**
             * Process batch of samples
             * @param input Input samples
             * @param output Output buffer (must be same size as input)
             * @param length Number of samples
             * @param stateless If true, ignores internal state (batch processing)
             */
            void process(const T *input, T *output, size_t length, bool stateless = false);

            /**
             * Reset filter state (clear history)
             */
            void reset();

            /**
             * Get feedforward order (M)
             */
            size_t getFeedforwardOrder() const { return m_b_coeffs.size() - 1; }

            /**
             * Get feedback order (N)
             */
            size_t getFeedbackOrder() const { return m_a_coeffs.size(); }

            /**
             * Get feedforward coefficients
             */
            const std::vector<T> &getBCoefficients() const { return m_b_coeffs; }

            /**
             * Get feedback coefficients
             */
            const std::vector<T> &getACoefficients() const { return m_a_coeffs; }

            /**
             * Update coefficients (resets state)
             */
            void setCoefficients(const std::vector<T> &b_coeffs, const std::vector<T> &a_coeffs);

            /**
             * Check if filter is stateful
             */
            bool isStateful() const { return m_stateful; }

            /**
             * Check filter stability (all poles inside unit circle)
             * Note: This is a basic check; full stability analysis requires pole computation
             */
            bool isStable() const;

            /**
             * Get internal state (for serialization)
             * @return Pair of (x_state, y_state) vectors
             */
            std::pair<std::vector<T>, std::vector<T>> getState() const;

            /**
             * Set internal state (for deserialization)
             * @param x_state Input history buffer
             * @param y_state Output history buffer
             */
            void setState(const std::vector<T> &x_state, const std::vector<T> &y_state);

            /**
             * Set internal state from spans (zero-copy deserialization)
             * @param x_state Input history buffer span (zero-copy)
             * @param y_state Output history buffer span (zero-copy)
             */
            void setState(std::span<const T> x_state, std::span<const T> y_state);

            // ========== Common IIR Filter Designs ==========

            /**
             * Create Butterworth low-pass filter (maximally flat passband)
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param order Filter order (1-8 recommended)
             * @return IIR filter
             */
            static IirFilter<T> createButterworthLowPass(T cutoffFreq, int order);

            /**
             * Create Butterworth high-pass filter
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param order Filter order (1-8 recommended)
             */
            static IirFilter<T> createButterworthHighPass(T cutoffFreq, int order);

            /**
             * Create Butterworth band-pass filter
             * @param lowCutoff Low cutoff frequency (normalized)
             * @param highCutoff High cutoff frequency (normalized)
             * @param order Filter order (per band, total = 2*order)
             */
            static IirFilter<T> createButterworthBandPass(T lowCutoff, T highCutoff, int order);

            /**
             * Create first-order low-pass filter (simple RC filter)
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             */
            static IirFilter<T> createFirstOrderLowPass(T cutoffFreq);

            /**
             * Create first-order high-pass filter
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             */
            static IirFilter<T> createFirstOrderHighPass(T cutoffFreq);

            /**
             * Create biquad filter from biquad coefficients
             * y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
             */
            static IirFilter<T> createBiquad(T b0, T b1, T b2, T a1, T a2);

            /**
             * Create Chebyshev Type I low-pass filter (passband ripple)
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param order Filter order (1-8 recommended)
             * @param rippleDb Passband ripple in dB (0.1 to 3.0, typical: 0.5)
             * @return IIR filter
             */
            static IirFilter<T> createChebyshevLowPass(T cutoffFreq, int order, T rippleDb = T(0.5));

            /**
             * Create Chebyshev Type I high-pass filter (passband ripple)
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param order Filter order (1-8 recommended)
             * @param rippleDb Passband ripple in dB (0.1 to 3.0, typical: 0.5)
             */
            static IirFilter<T> createChebyshevHighPass(T cutoffFreq, int order, T rippleDb = T(0.5));

            /**
             * Create Chebyshev Type I band-pass filter (passband ripple)
             * @param lowCutoff Low cutoff frequency (normalized)
             * @param highCutoff High cutoff frequency (normalized)
             * @param order Filter order (per band, total = 2*order)
             * @param rippleDb Passband ripple in dB (0.1 to 3.0, typical: 0.5)
             */
            static IirFilter<T> createChebyshevBandPass(T lowCutoff, T highCutoff, int order, T rippleDb = T(0.5));

            /**
             * Create peaking EQ biquad filter
             * @param centerFreq Center frequency (normalized: 0 to 0.5)
             * @param Q Quality factor (bandwidth = centerFreq/Q, typical: 0.5-10)
             * @param gainDb Gain in dB (negative for cut, positive for boost)
             */
            static IirFilter<T> createPeakingEQ(T centerFreq, T Q, T gainDb);

            /**
             * Create low-shelf biquad filter
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param gainDb Gain in dB for low frequencies
             * @param Q Shelf slope (typical: 0.7)
             */
            static IirFilter<T> createLowShelf(T cutoffFreq, T gainDb, T Q = T(0.707));

            /**
             * Create high-shelf biquad filter
             * @param cutoffFreq Cutoff frequency (normalized: 0 to 0.5)
             * @param gainDb Gain in dB for high frequencies
             * @param Q Shelf slope (typical: 0.7)
             */
            static IirFilter<T> createHighShelf(T cutoffFreq, T gainDb, T Q = T(0.707));

        private:
            std::vector<T> m_b_coeffs; // Feedforward coefficients (b[0], b[1], ..., b[M])
            std::vector<T> m_a_coeffs; // Feedback coefficients (a[1], a[2], ..., a[N])
            std::vector<T> m_x_state;  // Input history circular buffer (x[n-1], x[n-2], ...)
            std::vector<T> m_y_state;  // Output history circular buffer (y[n-1], y[n-2], ...)
            size_t m_x_index;          // Current write position in x_state circular buffer
            size_t m_y_index;          // Current write position in y_state circular buffer
            size_t m_x_mask;           // Bitmask for x_state (power-of-2 optimization)
            size_t m_y_mask;           // Bitmask for y_state (power-of-2 optimization)
            bool m_stateful;           // Whether to maintain state between calls

            /**
             * Bilinear transform: convert analog to digital filter
             * s -> 2/T * (1 - z^-1) / (1 + z^-1)
             */
            static void bilinearTransform(T wc, int order, std::vector<T> &b, std::vector<T> &a);

            /**
             * Cascade two IIR filters by convolving their transfer functions
             * Result: H(z) = H1(z) * H2(z)
             * @param filter1 First filter
             * @param filter2 Second filter
             * @return Cascaded filter with combined coefficients
             */
            static IirFilter<T> cascadeFilters(const IirFilter<T> &filter1, const IirFilter<T> &filter2);
        };

    } // namespace core
} // namespace dsp

#endif // DSP_CORE_IIR_FILTER_H
