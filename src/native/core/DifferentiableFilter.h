#ifndef DSP_DIFFERENTIABLE_FILTER_H
#define DSP_DIFFERENTIABLE_FILTER_H

#include <vector>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include "../utils/SimdOps.h"

// Include ARM NEON intrinsics if available
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace dsp
{
    namespace core
    {

        /**
         * @brief Differentiable Adaptive LMS (Least Mean Squares) Filter
         *
         * An adaptive FIR filter that learns optimal coefficients in real-time using
         * the LMS algorithm. The filter adjusts its weights based on the error between
         * the filtered output and a desired signal.
         *
         * Key features:
         * - Adaptive coefficient learning with gradient descent
         * - SIMD-accelerated convolution for filtering
         * - Configurable learning rate (mu) and regularization (lambda)
         * - Normalized LMS (NLMS) option for stable convergence
         * - Per-channel independent adaptation
         *
         * Use cases:
         * - Noise cancellation (adaptive noise filtering)
         * - Echo cancellation
         * - Channel equalization
         * - Predictive filtering
         * - System identification
         *
         * Algorithm:
         *   y[n] = w^T * x[n]           (filtering)
         *   e[n] = d[n] - y[n]          (error)
         *   w[n+1] = w[n] + mu * e[n] * x[n]  (weight update)
         */
        template <typename T = float>
        class DifferentiableFilter
        {
        private:
            size_t m_numTaps; // Filter order
            size_t m_numChannels;
            T m_mu;            // Learning rate (step size)
            T m_lambda;        // Regularization parameter (leaky LMS)
            bool m_normalized; // Use Normalized LMS
            T m_epsilon;       // Small constant to prevent division by zero in NLMS

            // Per-channel state
            std::vector<std::vector<T>> m_weights;     // Adaptive filter coefficients
            std::vector<std::vector<T>> m_inputBuffer; // Input sample history
            std::vector<size_t> m_writeIndices;        // Circular buffer write positions
            std::vector<T> m_inputPower;               // Running estimate of input power (for NLMS)

        public:
            DifferentiableFilter(size_t numTaps, T mu = 0.01, T lambda = 0.0, bool normalized = false)
                : m_numTaps(numTaps), m_numChannels(0), m_mu(mu), m_lambda(lambda), m_normalized(normalized), m_epsilon(1e-6)
            {
                if (numTaps == 0)
                {
                    throw std::invalid_argument("DifferentiableFilter: numTaps must be > 0");
                }
                if (mu <= 0.0 || mu > 1.0)
                {
                    throw std::invalid_argument("DifferentiableFilter: learning rate mu must be in (0, 1]");
                }
                if (lambda < 0.0 || lambda >= 1.0)
                {
                    throw std::invalid_argument("DifferentiableFilter: regularization lambda must be in [0, 1)");
                }
            }

            void init(size_t numChannels)
            {
                m_numChannels = numChannels;
                m_weights.resize(numChannels);
                m_inputBuffer.resize(numChannels);
                m_writeIndices.resize(numChannels, 0);
                m_inputPower.resize(numChannels, 1.0); // Initialize to 1 to avoid div by zero

                for (size_t ch = 0; ch < numChannels; ++ch)
                {
                    // Initialize weights to small random values or zeros
                    m_weights[ch].resize(m_numTaps, 0.0);

                    // Initialize input buffer
                    m_inputBuffer[ch].resize(m_numTaps, 0.0);
                }
            }

            /**
             * @brief Process samples through adaptive filter
             *
             * @param input Input signal (x[n])
             * @param desired Desired signal (d[n]) - target for adaptation
             * @param output Filtered output (y[n])
             * @param error Error signal (e[n] = d[n] - y[n])
             * @param numSamples Number of samples to process
             * @param adapt If true, update filter weights based on error
             */
            void process(const T *input, const T *desired, T *output, T *error,
                         size_t numSamples, bool adapt = true)
            {
                if (m_numChannels == 0)
                {
                    throw std::runtime_error("DifferentiableFilter not initialized");
                }

                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    const T *channelInput = input + ch * numSamples;
                    const T *channelDesired = desired + ch * numSamples;
                    T *channelOutput = output + ch * numSamples;
                    T *channelError = error + ch * numSamples;

                    processChannel(ch, channelInput, channelDesired, channelOutput,
                                   channelError, numSamples, adapt);
                }
            }

            /**
             * @brief Process without adaptation (inference mode)
             */
            void filter(const T *input, T *output, size_t numSamples)
            {
                if (m_numChannels == 0)
                {
                    throw std::runtime_error("DifferentiableFilter not initialized");
                }

                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    const T *channelInput = input + ch * numSamples;
                    T *channelOutput = output + ch * numSamples;

                    filterChannel(ch, channelInput, channelOutput, numSamples);
                }
            }

            void reset()
            {
                for (size_t ch = 0; ch < m_numChannels; ++ch)
                {
                    std::fill(m_weights[ch].begin(), m_weights[ch].end(), 0.0);
                    std::fill(m_inputBuffer[ch].begin(), m_inputBuffer[ch].end(), 0.0);
                    m_writeIndices[ch] = 0;
                    m_inputPower[ch] = 1.0;
                }
            }

            // Getters
            const std::vector<T> &getWeights(size_t channel) const
            {
                if (channel >= m_numChannels)
                {
                    throw std::out_of_range("Channel index out of range");
                }
                return m_weights[channel];
            }

            void setWeights(size_t channel, const std::vector<T> &weights)
            {
                if (channel >= m_numChannels)
                {
                    throw std::out_of_range("Channel index out of range");
                }
                if (weights.size() != m_numTaps)
                {
                    throw std::invalid_argument("Weight vector size mismatch");
                }
                m_weights[channel] = weights;
            }

            void setLearningRate(T mu)
            {
                if (mu <= 0.0 || mu > 1.0)
                {
                    throw std::invalid_argument("Learning rate mu must be in (0, 1]");
                }
                m_mu = mu;
            }

            T getLearningRate() const { return m_mu; }
            size_t getNumTaps() const { return m_numTaps; }
            size_t getNumChannels() const { return m_numChannels; }

        private:
            void processChannel(size_t channel, const T *input, const T *desired,
                                T *output, T *error, size_t numSamples, bool adapt)
            {
                auto &weights = m_weights[channel];
                auto &inputBuffer = m_inputBuffer[channel];
                size_t &writeIdx = m_writeIndices[channel];
                T &inputPower = m_inputPower[channel];

                for (size_t n = 0; n < numSamples; ++n)
                {
                    // Add new sample to input buffer
                    inputBuffer[writeIdx] = input[n];
                    writeIdx = (writeIdx + 1) % m_numTaps;

                    // Compute filter output: y[n] = w^T * x[n]
                    T y = computeOutput(inputBuffer, weights, writeIdx);
                    output[n] = y;

                    // Compute error: e[n] = d[n] - y[n]
                    T e = desired[n] - y;
                    error[n] = e;

                    if (adapt)
                    {
                        // Update filter weights using LMS or NLMS
                        updateWeights(inputBuffer, weights, writeIdx, e, inputPower, input[n]);
                    }
                }
            }

            void filterChannel(size_t channel, const T *input, T *output, size_t numSamples)
            {
                auto &weights = m_weights[channel];
                auto &inputBuffer = m_inputBuffer[channel];
                size_t &writeIdx = m_writeIndices[channel];

                for (size_t n = 0; n < numSamples; ++n)
                {
                    // Add new sample to input buffer
                    inputBuffer[writeIdx] = input[n];
                    writeIdx = (writeIdx + 1) % m_numTaps;

                    // Compute filter output: y[n] = w^T * x[n]
                    output[n] = computeOutput(inputBuffer, weights, writeIdx);
                }
            }

            T computeOutput(const std::vector<T> &inputBuffer, const std::vector<T> &weights,
                            size_t writeIdx) const
            {
                // Collect input samples in reverse chronological order
                std::vector<T> x(m_numTaps);
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    size_t idx = (writeIdx + m_numTaps - 1 - i) % m_numTaps;
                    x[i] = inputBuffer[idx];
                }

                // Compute dot product using SIMD
                return simd::dot_product(weights.data(), x.data(), m_numTaps);
            }

            void updateWeights(const std::vector<T> &inputBuffer, std::vector<T> &weights,
                               size_t writeIdx, T error, T &inputPower, T newSample)
            {
                // Compute step size
                T mu_n = m_mu;

                if (m_normalized)
                {
                    // Update input power estimate (exponential moving average)
                    T alpha = 0.99; // Smoothing factor
                    inputPower = alpha * inputPower + (1.0 - alpha) * newSample * newSample;

                    // Normalized step size: mu_n = mu / (epsilon + ||x||^2)
                    T normFactor = m_epsilon + inputPower * static_cast<T>(m_numTaps);
                    mu_n = m_mu / normFactor;
                }

                // Update weights: w[n+1] = (1 - mu*lambda) * w[n] + mu * e[n] * x[n]
                // where (1 - mu*lambda) is the leakage factor for regularization
                T leakage = 1.0 - m_mu * m_lambda;
                T mu_error = mu_n * error;

#if defined(__ARM_NEON) || defined(__aarch64__)
                // NEON-optimized weight update for ARM processors
                const size_t simd_width = 4;
                const size_t simd_count = m_numTaps / simd_width;
                const size_t simd_end = simd_count * simd_width;

                float32x4_t leakage_vec = vdupq_n_f32(leakage);
                float32x4_t mu_error_vec = vdupq_n_f32(mu_error);

                // Vectorized update: weights[i] = leakage * weights[i] + mu_error * x[i]
                for (size_t i = 0; i < simd_end; i += simd_width)
                {
                    // Get indices for input buffer (circular)
                    size_t idx0 = (writeIdx + m_numTaps - 1 - i) % m_numTaps;
                    size_t idx1 = (writeIdx + m_numTaps - 2 - i) % m_numTaps;
                    size_t idx2 = (writeIdx + m_numTaps - 3 - i) % m_numTaps;
                    size_t idx3 = (writeIdx + m_numTaps - 4 - i) % m_numTaps;

                    // Load 4 input samples (must be done individually due to circular buffer)
                    float x_vals[4] = {
                        static_cast<float>(inputBuffer[idx0]),
                        static_cast<float>(inputBuffer[idx1]),
                        static_cast<float>(inputBuffer[idx2]),
                        static_cast<float>(inputBuffer[idx3])};
                    float32x4_t x = vld1q_f32(x_vals);

                    // Load 4 weights
                    float32x4_t w = vld1q_f32(reinterpret_cast<const float *>(&weights[i]));

                    // Apply leakage: w *= leakage
                    w = vmulq_f32(w, leakage_vec);

                    // Fused multiply-add: w += mu_error * x
                    w = vmlaq_f32(w, mu_error_vec, x);

                    // Store updated weights
                    vst1q_f32(reinterpret_cast<float *>(&weights[i]), w);
                }

                // Handle remainder (scalar)
                for (size_t i = simd_end; i < m_numTaps; ++i)
                {
                    size_t idx = (writeIdx + m_numTaps - 1 - i) % m_numTaps;
                    T x_i = inputBuffer[idx];
                    weights[i] = leakage * weights[i] + mu_error * x_i;
                }

#else
                // Scalar weight update for non-ARM platforms
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    size_t idx = (writeIdx + m_numTaps - 1 - i) % m_numTaps;
                    T x_i = inputBuffer[idx];

                    // Apply leaky LMS update
                    weights[i] = leakage * weights[i] + mu_n * error * x_i;
                }
#endif
            }
        };

    } // namespace core
} // namespace dsp

#endif // DSP_DIFFERENTIABLE_FILTER_H
