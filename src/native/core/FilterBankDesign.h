#pragma once

#include "IirFilter.h"
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace dsp
{
    namespace core
    {
        /**
         * Filter coefficients structure for a single filter
         */
        struct FilterCoefficients
        {
            std::vector<float> b; // Numerator coefficients
            std::vector<float> a; // Denominator coefficients
        };

        /**
         * Filter Bank Design Engine
         *
         * Generates sets of bandpass filters covering a frequency range according to
         * psychoacoustic (Mel, Bark) or mathematical (Linear, Log) scales.
         *
         * This is a stateless utility that performs frequency warping and filter design
         * without maintaining any processing state.
         */
        class FilterBankDesign
        {
        public:
            enum class Scale
            {
                Linear, // Linear spacing in Hz
                Log,    // Logarithmic spacing
                Mel,    // Mel scale (mimics human hearing)
                Bark    // Bark scale (critical band rate)
            };

            enum class Type
            {
                Butterworth, // Maximally flat passband
                Chebyshev1   // Equiripple passband
            };

            /**
             * Filter bank design options
             */
            struct DesignOptions
            {
                Scale scale;           // Frequency spacing scale
                Type type;             // Filter topology
                int count;             // Number of bands
                double sampleRate;     // Sample rate in Hz
                double minFreq;        // Minimum frequency in Hz
                double maxFreq;        // Maximum frequency in Hz
                int order;             // Filter order per band (steepness)
                double rippleDb = 0.5; // Passband ripple for Chebyshev (dB)
            };

            /**
             * Design a filter bank with specified options
             *
             * @param opts Design options including scale, count, frequency range
             * @return Vector of filter coefficients (one per band)
             *
             * @throws std::invalid_argument if options are invalid
             *
             * @example
             * // Create 24-band Mel-spaced filter bank for speech analysis
             * DesignOptions opts;
             * opts.scale = Scale::Mel;
             * opts.type = Type::Butterworth;
             * opts.count = 24;
             * opts.sampleRate = 44100;
             * opts.minFreq = 20;
             * opts.maxFreq = 8000;
             * opts.order = 2;
             * auto bank = FilterBankDesign::design(opts);
             */
            static std::vector<FilterCoefficients> design(const DesignOptions &opts)
            {
                // Validate inputs
                if (opts.count <= 0)
                {
                    throw std::invalid_argument("Band count must be positive");
                }
                if (opts.minFreq < 0)
                {
                    throw std::invalid_argument("Minimum frequency cannot be negative");
                }
                if (opts.minFreq >= opts.maxFreq)
                {
                    throw std::invalid_argument("Invalid frequency range: minFreq must be < maxFreq");
                }
                if (opts.maxFreq > opts.sampleRate / 2.0)
                {
                    throw std::invalid_argument("Maximum frequency must be <= Nyquist frequency");
                }
                if (opts.order <= 0)
                {
                    throw std::invalid_argument("Filter order must be positive");
                }
                if (opts.sampleRate <= 0)
                {
                    throw std::invalid_argument("Sample rate must be positive");
                }

                // Step 1: Convert frequency boundaries to target scale
                double minVal = toScale(opts.minFreq, opts.scale);
                double maxVal = toScale(opts.maxFreq, opts.scale);
                double step = (maxVal - minVal) / opts.count;

                // Step 2: Generate band edges in target scale, then convert back to Hz
                std::vector<double> boundaries;
                boundaries.reserve(opts.count + 1);

                for (int i = 0; i <= opts.count; ++i)
                {
                    double val = minVal + (i * step);
                    double hz = fromScale(val, opts.scale);
                    boundaries.push_back(hz);
                }

                // Step 3: Create filters for each band
                std::vector<FilterCoefficients> bank;
                bank.reserve(opts.count);

                for (int i = 0; i < opts.count; ++i)
                {
                    double fLow = boundaries[i];
                    double fHigh = boundaries[i + 1];

                    // For the first band starting at 0 Hz, use a small positive value
                    // to avoid DC (bandpass filters can't have 0 Hz as lower bound)
                    if (fLow == 0.0)
                    {
                        fLow = 1.0; // 1 Hz minimum
                    }

                    // Normalize frequencies to [0, 0.5] range (0.5 = Nyquist)
                    double nLow = fLow / opts.sampleRate;
                    double nHigh = fHigh / opts.sampleRate;

                    // Safety clamping to avoid numerical issues
                    nLow = std::max(0.0001, std::min(nLow, 0.4999));
                    nHigh = std::max(0.0001, std::min(nHigh, 0.4999));

                    // Ensure proper ordering after clamping
                    if (nLow >= nHigh)
                    {
                        nHigh = nLow + 0.0001;
                    }

                    // Design bandpass filter using existing IirFilter factory
                    IirFilter<float> filter = (opts.type == Type::Chebyshev1)
                                                  ? IirFilter<float>::createChebyshevBandPass(
                                                        nLow, nHigh, opts.order, opts.rippleDb)
                                                  : IirFilter<float>::createButterworthBandPass(
                                                        nLow, nHigh, opts.order);

                    // Extract coefficients
                    FilterCoefficients coeffs;
                    coeffs.b = filter.getBCoefficients();
                    coeffs.a = filter.getACoefficients();

                    bank.push_back(coeffs);
                }

                return bank;
            }

            /**
             * Get frequency boundaries for a filter bank design
             * Useful for visualization and debugging
             *
             * @param opts Design options
             * @return Vector of boundary frequencies in Hz
             */
            static std::vector<double> getBoundaries(const DesignOptions &opts)
            {
                if (opts.count <= 0)
                {
                    throw std::invalid_argument("Band count must be positive");
                }

                double minVal = toScale(opts.minFreq, opts.scale);
                double maxVal = toScale(opts.maxFreq, opts.scale);
                double step = (maxVal - minVal) / opts.count;

                std::vector<double> boundaries;
                boundaries.reserve(opts.count + 1);

                for (int i = 0; i <= opts.count; ++i)
                {
                    double val = minVal + (i * step);
                    boundaries.push_back(fromScale(val, opts.scale));
                }

                return boundaries;
            }

        private:
            /**
             * Convert frequency from Hz to target scale
             */
            static double toScale(double hz, Scale scale)
            {
                switch (scale)
                {
                case Scale::Linear:
                    return hz;

                case Scale::Log:
                    return std::log10(hz);

                case Scale::Mel:
                    // Mel scale: f_mel = 2595 * log10(1 + f_hz / 700)
                    return 2595.0 * std::log10(1.0 + hz / 700.0);

                case Scale::Bark:
                    // Bark scale (Traunmüller 1990)
                    // z = 26.81 * f / (1960 + f) - 0.53
                    return 26.81 * hz / (1960.0 + hz) - 0.53;

                default:
                    return hz;
                }
            }

            /**
             * Convert from scale back to Hz
             */
            static double fromScale(double val, Scale scale)
            {
                switch (scale)
                {
                case Scale::Linear:
                    return val;

                case Scale::Log:
                    return std::pow(10.0, val);

                case Scale::Mel:
                    // Inverse Mel: f_hz = 700 * (10^(f_mel / 2595) - 1)
                    return 700.0 * (std::pow(10.0, val / 2595.0) - 1.0);

                case Scale::Bark:
                    // Inverse Bark (Traunmüller 1990)
                    // f = 1960 * (z + 0.53) / (26.81 - (z + 0.53))
                    {
                        double adjusted = val + 0.53;
                        return 1960.0 * adjusted / (26.81 - adjusted);
                    }

                default:
                    return val;
                }
            }
        };

    } // namespace core
} // namespace dsp
