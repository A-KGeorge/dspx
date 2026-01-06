#pragma once

#include "../IDspStage.h"
#include "../utils/SimdOps.h"
#include <Eigen/Dense>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

namespace dsp
{
    namespace adapters
    {

        /**
         * @brief Applies a pre-trained linear transformation (PCA, ICA, Whitening) to multi-channel stream.
         *
         * This stage centers data using a saved mean, then applies matrix multiplication:
         *   y = W^T * (x - mean)
         *
         * Can be used for:
         * - PCA: Dimensionality reduction and decorrelation
         * - ICA: Blind source separation
         * - Whitening: Data normalization to identity covariance
         *
         * Example usage:
         *   1. Batch-train: Calculate PCA/ICA/Whitening matrix from training data
         *   2. Pipeline: Create MatrixTransformStage with the trained matrix
         *   3. Stream: Process incoming samples in real-time
         */
        class MatrixTransformStage : public IDspStage
        {
        public:
            /**
             * @brief Construct transformation stage with pre-trained matrix.
             *
             * @param transformationMatrix Flattened transformation matrix (numChannels × numComponents)
             *                             Expected in column-major order (Eigen default)
             * @param meanVector Channel means for centering (size: numChannels)
             * @param numChannels Number of input channels
             * @param numComponents Number of output components (can be < numChannels for PCA)
             * @param transformType Type identifier ("pca", "ica", "whiten") for serialization
             */
            MatrixTransformStage(const std::vector<float> &transformationMatrix,
                                 const std::vector<float> &meanVector,
                                 int numChannels,
                                 int numComponents,
                                 const std::string &transformType = "matrix")
                : m_numChannels(numChannels), m_numComponents(numComponents), m_transformType(transformType), m_mean(numChannels), m_matrix(numChannels, numComponents)
            {
                // Validate inputs
                if (numChannels <= 0)
                {
                    throw std::invalid_argument("numChannels must be positive");
                }
                if (numComponents <= 0 || numComponents > numChannels)
                {
                    throw std::invalid_argument("numComponents must be in range [1, numChannels]");
                }
                if (meanVector.size() != static_cast<size_t>(numChannels))
                {
                    throw std::invalid_argument("Mean vector size (" + std::to_string(meanVector.size()) +
                                                ") != numChannels (" + std::to_string(numChannels) + ")");
                }
                if (transformationMatrix.size() != static_cast<size_t>(numChannels * numComponents))
                {
                    throw std::invalid_argument("Matrix size (" + std::to_string(transformationMatrix.size()) +
                                                ") != numChannels × numComponents (" +
                                                std::to_string(numChannels * numComponents) + ")");
                }

                // Copy mean vector into Eigen vector
                for (int i = 0; i < numChannels; ++i)
                {
                    m_mean(i) = meanVector[i];
                }

                // Copy transformation matrix (column-major from input)
                // Eigen::Map interprets the flat array as column-major by default
                m_matrix = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>>(
                    transformationMatrix.data(), numChannels, numComponents);
            }

            /**
             * @brief Get stage type identifier.
             */
            const char *getType() const override
            {
                return m_transformType.c_str();
            }

            /**
             * @brief Apply transformation to interleaved multi-channel stream.
             *
             * Processes each sample as:
             *   1. Extract input vector x (numChannels)
             *   2. Center: x_centered = x - mean
             *   3. Transform: y = W^T * x_centered
             *   4. Write output vector y (numComponents) back to buffer
             *
             * @param buffer Interleaved input/output buffer (modified in-place)
             * @param numSamples Total number of samples (numSamples = samplesPerChannel × numChannels)
             * @param numChannels Number of channels (must match m_numChannels)
             * @param timestamps Optional timestamp array (unused)
             */
            void process(float *buffer, size_t numSamples, int numChannels,
                         const float *timestamps = nullptr) override
            {
                if (numChannels != m_numChannels)
                {
                    throw std::invalid_argument("Stage configured for " + std::to_string(m_numChannels) +
                                                " channels, got " + std::to_string(numChannels));
                }

                size_t samplesPerChannel = numSamples / numChannels;

                // Ensure scratch buffers for channel data
                ensureScratchBuffers(samplesPerChannel);

                // Deinterleave all channels into planar scratch buffers
                for (int ch = 0; ch < m_numChannels; ++ch)
                {
                    for (size_t i = 0; i < samplesPerChannel; ++i)
                    {
                        m_scratch_channels[ch][i] = buffer[i * numChannels + ch];
                    }
                }

                // Temporary vectors for transformation
                Eigen::VectorXf x_n(m_numChannels);
                Eigen::VectorXf y_n(m_numComponents);

                // Process each sample
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    // 1. Load input vector from planar buffers
                    for (int c = 0; c < m_numChannels; ++c)
                    {
                        x_n(c) = m_scratch_channels[c][i];
                    }

                    // 2. Apply transformation: y = W^T * (x - mean)
                    y_n = m_matrix.transpose() * (x_n - m_mean);

                    // 3. Write to output scratch buffers
                    for (int j = 0; j < m_numComponents; ++j)
                    {
                        m_scratch_output[j][i] = y_n(j);
                    }
                }

                // Interleave output back to buffer
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    // Write transformed components
                    for (int j = 0; j < m_numComponents; ++j)
                    {
                        buffer[i * numChannels + j] = m_scratch_output[j][i];
                    }

                    // Zero out any remaining channels if doing dimensionality reduction
                    for (int j = m_numComponents; j < numChannels; ++j)
                    {
                        buffer[i * numChannels + j] = 0.0f;
                    }
                }
            }

            /**
             * @brief Serialize stage state to N-API object.
             *
             * Saves transformation matrix and mean vector for persistence.
             */
            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);

                state.Set("type", Napi::String::New(env, m_transformType));
                state.Set("numChannels", Napi::Number::New(env, m_numChannels));
                state.Set("numComponents", Napi::Number::New(env, m_numComponents));

                // Serialize mean vector
                Napi::Float32Array meanArray = Napi::Float32Array::New(env, m_numChannels);
                for (int i = 0; i < m_numChannels; ++i)
                {
                    meanArray[i] = m_mean(i);
                }
                state.Set("mean", meanArray);

                // Serialize transformation matrix (column-major)
                size_t totalElements = m_numChannels * m_numComponents;
                Napi::Float32Array matrixArray = Napi::Float32Array::New(env, totalElements);
                const float *matrixData = m_matrix.data();
                for (size_t i = 0; i < totalElements; ++i)
                {
                    matrixArray[i] = matrixData[i];
                }
                state.Set("matrix", matrixArray);

                return state;
            }

            /**
             * @brief Deserialize stage state from N-API object.
             *
             * Restores transformation matrix and mean vector.
             */
            void deserializeState(const Napi::Object &state) override
            {
                // Extract parameters
                m_transformType = state.Get("type").As<Napi::String>().Utf8Value();
                m_numChannels = state.Get("numChannels").As<Napi::Number>().Int32Value();
                m_numComponents = state.Get("numComponents").As<Napi::Number>().Int32Value();

                // Restore mean vector
                Napi::Float32Array meanArray = state.Get("mean").As<Napi::Float32Array>();
                m_mean.resize(m_numChannels);
                for (int i = 0; i < m_numChannels; ++i)
                {
                    m_mean(i) = meanArray[i];
                }

                // Restore transformation matrix
                Napi::Float32Array matrixArray = state.Get("matrix").As<Napi::Float32Array>();
                m_matrix.resize(m_numChannels, m_numComponents);
                for (size_t i = 0; i < matrixArray.ElementLength(); ++i)
                {
                    m_matrix.data()[i] = matrixArray[i];
                }
            }

            /**
             * @brief Reset stage state (no-op for stateless transformation).
             */
            void reset() override
            {
                // No internal state to clear (matrix and mean are fixed)
            }

            void serializeToon(toon::Serializer &serializer) const override
            {
                serializer.writeString(m_transformType);
                serializer.writeInt32(m_numChannels);
                serializer.writeInt32(m_numComponents);

                // Serialize mean vector
                for (int i = 0; i < m_numChannels; ++i)
                {
                    serializer.writeFloat(m_mean(i));
                }

                // Serialize transformation matrix (column-major)
                const float *matrixData = m_matrix.data();
                size_t totalElements = m_numChannels * m_numComponents;
                for (size_t i = 0; i < totalElements; ++i)
                {
                    serializer.writeFloat(matrixData[i]);
                }
            }

            void deserializeToon(toon::Deserializer &deserializer) override
            {
                std::string transformType = deserializer.readString();
                int numChannels = deserializer.readInt32();
                int numComponents = deserializer.readInt32();

                if (numChannels != m_numChannels || numComponents != m_numComponents)
                {
                    throw std::runtime_error("MatrixTransform: Dimension mismatch during TOON deserialization");
                }

                // Restore mean vector
                for (int i = 0; i < m_numChannels; ++i)
                {
                    m_mean(i) = deserializer.readFloat();
                }

                // Restore transformation matrix
                std::vector<float> matrixData(m_numChannels * m_numComponents);
                for (size_t i = 0; i < matrixData.size(); ++i)
                {
                    matrixData[i] = deserializer.readFloat();
                }
                m_matrix = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>>(
                    matrixData.data(), m_numChannels, m_numComponents);
            }

            /**
             * @brief Get number of input channels.
             */
            int getNumChannels() const { return m_numChannels; }

            /**
             * @brief Get number of output components.
             */
            int getNumComponents() const { return m_numComponents; }

            /**
             * @brief Get transformation type.
             */
            const std::string &getTransformType() const { return m_transformType; }

        private:
            void ensureScratchBuffers(size_t samplesPerChannel)
            {
                if (m_scratch_channels.size() != static_cast<size_t>(m_numChannels) ||
                    (m_scratch_channels.size() > 0 && m_scratch_channels[0].size() < samplesPerChannel))
                {
                    size_t new_capacity = samplesPerChannel * 2;
                    m_scratch_channels.resize(m_numChannels);
                    m_scratch_output.resize(m_numComponents);
                    for (int ch = 0; ch < m_numChannels; ++ch)
                    {
                        m_scratch_channels[ch].resize(new_capacity);
                    }
                    for (int ch = 0; ch < m_numComponents; ++ch)
                    {
                        m_scratch_output[ch].resize(new_capacity);
                    }
                }
            }

            int m_numChannels;           ///< Number of input channels
            int m_numComponents;         ///< Number of output components
            std::string m_transformType; ///< Type identifier ("pca", "ica", "whiten")
            Eigen::VectorXf m_mean;      ///< Channel mean vector (C × 1)
            Eigen::MatrixXf m_matrix;    ///< Transformation matrix (C × N)

            // Pre-allocated scratch buffers (planar layout)
            std::vector<std::vector<float>> m_scratch_channels; ///< Input channels
            std::vector<std::vector<float>> m_scratch_output;   ///< Output components
        };

    } // namespace adapters
} // namespace dsp
