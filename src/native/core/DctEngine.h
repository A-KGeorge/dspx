/**
 * Discrete Cosine Transform (DCT) Engine
 *
 * Implements Type-II DCT (most common in audio processing):
 *   X[k] = sum_{n=0}^{N-1} x[n] * cos(π * k * (n + 0.5) / N)
 *
 * Features:
 * - Pre-computed cosine table for performance
 * - Forward DCT (time → frequency)
 * - Inverse DCT (frequency → time)
 * - Optimized for MFCC coefficient extraction
 *
 * The DCT is used in MFCC computation as the final step to:
 * 1. Decorrelate Mel energies
 * 2. Compress information into lower-order coefficients
 * 3. Provide compact representation suitable for ML models
 */

#ifndef DSP_CORE_DCT_ENGINE_H
#define DSP_CORE_DCT_ENGINE_H

#include <vector>
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    namespace core
    {

        template <typename T = float>
        class DctEngine
        {
        public:
            /**
             * Constructor
             * @param size DCT size (number of input/output coefficients)
             */
            explicit DctEngine(size_t size);

            ~DctEngine() = default;

            /**
             * Forward DCT Type-II
             * Transforms time-domain signal to DCT coefficients
             *
             * @param input Input signal (size N)
             * @param output DCT coefficients (size N)
             */
            void dct(const T *input, T *output);

            /**
             * Inverse DCT Type-III (inverse of Type-II)
             * Transforms DCT coefficients back to time-domain
             *
             * @param input DCT coefficients (size N)
             * @param output Time-domain signal (size N)
             */
            void idct(const T *input, T *output);

            /**
             * Get DCT size
             */
            size_t getSize() const { return m_size; }

        private:
            size_t m_size; // DCT size

            // Pre-computed cosine table for DCT
            // cosTable[k][n] = cos(π * k * (n + 0.5) / N)
            std::vector<std::vector<T>> m_cosineTable;

            /**
             * Initialize cosine lookup table
             */
            void initCosineTable();
        };

        // ========== Implementation ==========

        template <typename T>
        DctEngine<T>::DctEngine(size_t size)
            : m_size(size)
        {
            if (size == 0)
            {
                throw std::invalid_argument("DCT size must be > 0");
            }

            // Pre-compute cosine table
            initCosineTable();
        }

        template <typename T>
        void DctEngine<T>::initCosineTable()
        {
            m_cosineTable.resize(m_size);

            const T pi = static_cast<T>(M_PI);
            const T N = static_cast<T>(m_size);

            for (size_t k = 0; k < m_size; ++k)
            {
                m_cosineTable[k].resize(m_size);
                for (size_t n = 0; n < m_size; ++n)
                {
                    // DCT-II formula: cos(π * k * (n + 0.5) / N)
                    m_cosineTable[k][n] = std::cos(pi * static_cast<T>(k) * (static_cast<T>(n) + 0.5) / N);
                }
            }
        }

        template <typename T>
        void DctEngine<T>::dct(const T *input, T *output)
        {
            // DCT-II: X[k] = sum_{n=0}^{N-1} x[n] * cos(π * k * (n + 0.5) / N)
            const T sqrt2 = std::sqrt(static_cast<T>(2.0));
            const T sqrtN = std::sqrt(static_cast<T>(m_size));

            for (size_t k = 0; k < m_size; ++k)
            {
                T sum = 0;

                for (size_t n = 0; n < m_size; ++n)
                {
                    sum += input[n] * m_cosineTable[k][n];
                }

                // Orthonormal scaling
                if (k == 0)
                {
                    output[k] = sum / sqrtN;
                }
                else
                {
                    output[k] = sum * sqrt2 / sqrtN;
                }
            }
        }

        template <typename T>
        void DctEngine<T>::idct(const T *input, T *output)
        {
            // DCT-III (inverse of DCT-II): x[n] = sum_{k=0}^{N-1} X[k] * cos(π * k * (n + 0.5) / N)
            const T sqrt2 = std::sqrt(static_cast<T>(2.0));
            const T sqrtN = std::sqrt(static_cast<T>(m_size));

            for (size_t n = 0; n < m_size; ++n)
            {
                T sum = input[0] / sqrtN; // DC component

                for (size_t k = 1; k < m_size; ++k)
                {
                    sum += input[k] * sqrt2 / sqrtN * m_cosineTable[k][n];
                }

                output[n] = sum;
            }
        }

        // Explicit template instantiations
        template class DctEngine<float>;
        template class DctEngine<double>;

    } // namespace core
} // namespace dsp

#endif // DSP_CORE_DCT_ENGINE_H
