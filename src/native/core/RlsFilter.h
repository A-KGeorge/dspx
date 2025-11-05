#pragma once

#include "../utils/CircularBufferArray.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace dsp
{
    namespace core
    {

        /**
         * @brief Recursive Least Squares (RLS) adaptive filter core implementation.
         *
         * RLS provides faster convergence than LMS/NLMS but requires O(N^2) complexity
         * due to maintaining an N×N inverse covariance matrix.
         *
         * Algorithm:
         *   1. Calculate gain vector: k = (P * x) / (λ + x^T * P * x)
         *   2. Calculate error: e = d - w^T * x
         *   3. Update weights: w = w + k * e
         *   4. Update covariance: P = (1/λ) * (P - k * x^T * P)
         */
        class RlsFilter
        {
        public:
            /**
             * @param numTaps Number of filter taps (filter order)
             * @param lambda Forgetting factor (0 < λ ≤ 1, typically 0.98-0.9999)
             * @param delta Regularization parameter for P matrix initialization (typically 0.01-1.0)
             */
            RlsFilter(size_t numTaps, float lambda, float delta = 0.01f)
                : m_numTaps(numTaps), m_lambda(lambda), m_delta(delta), m_buffer(numTaps)
            {

                if (numTaps == 0)
                {
                    throw std::invalid_argument("RlsFilter: numTaps must be > 0");
                }
                if (lambda <= 0.0f || lambda > 1.0f)
                {
                    throw std::invalid_argument("RlsFilter: lambda must be in (0, 1]");
                }
                if (delta <= 0.0f)
                {
                    throw std::invalid_argument("RlsFilter: delta must be > 0");
                }

                m_weights.resize(numTaps, 0.0f);

                // Initialize P matrix as δ * I (identity matrix scaled by delta)
                m_inverseCov.resize(numTaps * numTaps, 0.0f);
                for (size_t i = 0; i < numTaps; ++i)
                {
                    m_inverseCov[i * numTaps + i] = delta;
                }
            }

            /**
             * Process a single sample through the RLS filter
             * @param input Primary input signal x[n]
             * @param desired Desired/reference signal d[n]
             * @return Error signal e[n] = d[n] - y[n]
             */
            float processSample(float input, float desired)
            {
                // Add new input to circular buffer
                m_buffer.pushOverwrite(input);

                // Get input vector x_n from buffer (oldest to newest)
                std::vector<float> x_n = m_buffer.toVector();

                // Pad with zeros if buffer not yet full
                if (x_n.size() < m_numTaps)
                {
                    x_n.resize(m_numTaps, 0.0f);
                }

                // 1. Calculate P * x
                std::vector<float> Px = matrixVectorMultiply(m_inverseCov, x_n);

                // 2. Calculate x^T * P * x (scalar)
                float xT_Px = dotProduct(x_n, Px);

                // 3. Calculate gain denominator: λ + x^T * P * x
                float gainDenom = m_lambda + xT_Px;

                // 4. Calculate Kalman gain vector: k = (P * x) / (λ + x^T * P * x)
                std::vector<float> k(m_numTaps);
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    k[i] = Px[i] / gainDenom;
                }

                // 5. Calculate filter output: y = w^T * x
                float y = dotProduct(m_weights, x_n);

                // 6. Calculate error: e = d - y
                float error = desired - y;

                // 7. Update weights: w = w + k * e
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    m_weights[i] += k[i] * error;
                }

                // 8. Update inverse covariance matrix: P = (1/λ) * (P - k * x^T * P)
                //    This is the most computationally expensive step
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    for (size_t j = 0; j < m_numTaps; ++j)
                    {
                        // P[i][j] = (1/λ) * (P[i][j] - k[i] * (x^T * P)[j])
                        // where (x^T * P)[j] = Px[j] (already computed)
                        m_inverseCov[i * m_numTaps + j] =
                            (1.0f / m_lambda) * (m_inverseCov[i * m_numTaps + j] - k[i] * Px[j]);
                    }
                }

                return error;
            }

            /**
             * Reset filter to initial state (zero weights, reinitialized P matrix)
             */
            void reset()
            {
                std::fill(m_weights.begin(), m_weights.end(), 0.0f);
                m_buffer.clear();

                // Reinitialize P matrix as δ * I
                std::fill(m_inverseCov.begin(), m_inverseCov.end(), 0.0f);
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    m_inverseCov[i * m_numTaps + i] = m_delta;
                }
            }

            // Getters for state serialization
            const std::vector<float> &getWeights() const { return m_weights; }
            const std::vector<float> &getInverseCov() const { return m_inverseCov; }
            const utils::CircularBufferArray<float> &getBuffer() const { return m_buffer; }

            size_t getNumTaps() const { return m_numTaps; }
            float getLambda() const { return m_lambda; }
            float getDelta() const { return m_delta; }

            // Setters for state deserialization
            void setWeights(const std::vector<float> &weights)
            {
                if (weights.size() != m_numTaps)
                {
                    throw std::invalid_argument("RlsFilter::setWeights: size mismatch");
                }
                m_weights = weights;
            }

            void setInverseCov(const std::vector<float> &inverseCov)
            {
                if (inverseCov.size() != m_numTaps * m_numTaps)
                {
                    throw std::invalid_argument("RlsFilter::setInverseCov: size mismatch");
                }
                m_inverseCov = inverseCov;
            }

            void setBuffer(const std::vector<float> &bufferData)
            {
                if (bufferData.size() != m_numTaps)
                {
                    throw std::invalid_argument("RlsFilter::setBuffer: size mismatch");
                }
                m_buffer.clear();
                for (const auto &val : bufferData)
                {
                    m_buffer.push(val);
                }
            }

        private:
            size_t m_numTaps;
            float m_lambda;                             // Forgetting factor
            float m_delta;                              // Regularization parameter
            std::vector<float> m_weights;               // Filter coefficients (N×1)
            std::vector<float> m_inverseCov;            // Inverse covariance matrix P (N×N, stored row-major)
            utils::CircularBufferArray<float> m_buffer; // Input signal buffer

            /**
             * Dot product: a^T * b
             */
            float dotProduct(const std::vector<float> &a, const std::vector<float> &b) const
            {
                float sum = 0.0f;
                for (size_t i = 0; i < a.size(); ++i)
                {
                    sum += a[i] * b[i];
                }
                return sum;
            }

            /**
             * Matrix-vector multiplication: P * x
             * P is stored row-major as a flattened N×N matrix
             */
            std::vector<float> matrixVectorMultiply(
                const std::vector<float> &P,
                const std::vector<float> &x) const
            {

                std::vector<float> result(m_numTaps, 0.0f);
                for (size_t i = 0; i < m_numTaps; ++i)
                {
                    for (size_t j = 0; j < m_numTaps; ++j)
                    {
                        result[i] += P[i * m_numTaps + j] * x[j];
                    }
                }
                return result;
            }
        };

    } // namespace core
} // namespace dsp
