#pragma once

#include "../IDspStage.h"
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
         * @brief Generalized Sidelobe Canceler (GSC) preprocessor for adaptive beamforming.
         *
         * The GSC architecture converts an N-channel beamforming problem into a 2-channel
         * adaptive filtering problem, perfectly suited for LmsStage or RlsStage.
         *
         * **Architecture**:
         * 1. **Upper Branch (Desired Signal)**: Applies fixed steering weights to focus on target direction
         *    - Output: d[n] = w_steering^T * x[n]
         *
         * 2. **Lower Branch (Noise Reference)**: Applies blocking matrix to cancel desired signal
         *    - Creates N-1 signals where target is cancelled, leaving only noise
         *    - Output: noise_refs = B^T * x[n] (combined into single reference)
         *
         * **Output**: 2 channels ready for LMS/RLS adaptive filtering
         *    - Channel 0: x[n] = noise reference (primary input for adaptive filter)
         *    - Channel 1: d[n] = desired signal (reference for adaptation)
         *
         * **Pipeline Usage**:
         * ```
         * pipeline.GscPreprocessor({
         *     numChannels: 8,
         *     steeringWeights: weights,   // From calculateBeamformerWeights()
         *     blockingMatrix: blocking    // From calculateBeamformerWeights()
         * })
         * .LmsFilter({ numTaps: 32, learningRate: 0.01 })
         * ```
         *
         * **Applications**:
         * - Conference call systems (suppress background noise, focus on speaker)
         * - Acoustic monitoring (isolate target sound source in noisy environment)
         * - Sonar/Radar (adaptive spatial filtering for target tracking)
         * - Microphone arrays (hands-free voice communication)
         *
         * @see LmsStage, RlsStage
         * @see calculateBeamformerWeights() for computing steering weights and blocking matrix
         */
        class GscPreprocessorStage : public IDspStage
        {
        public:
            /**
             * @brief Construct GSC preprocessor with pre-computed beamforming matrices.
             *
             * @param numChannels Number of input channels (microphones/sensors)
             * @param steeringWeights Fixed weights for delay-and-sum beamforming (size: numChannels)
             *                        Points the beam towards the desired signal direction
             * @param blockingMatrix Matrix to create noise-only references (size: numChannels × (numChannels-1))
             *                       Each column creates a signal where desired direction is nulled
             *
             * @throws std::invalid_argument If dimensions are invalid or matrices are mismatched
             *
             * @example
             * // 8-microphone linear array
             * auto weights = calculateBeamformerWeights(8, "linear", 0.0); // 0° target
             * GscPreprocessorStage gsc(8, weights.steering, weights.blocking);
             */
            GscPreprocessorStage(int numChannels,
                                 const std::vector<float> &steeringWeights,
                                 const std::vector<float> &blockingMatrix)
                : m_numChannels(numChannels),
                  m_steering_weights(numChannels),
                  m_blocking_matrix(numChannels, numChannels - 1)
            {
                // Validate inputs
                if (numChannels < 2)
                {
                    throw std::invalid_argument("GscPreprocessor: numChannels must be >= 2");
                }
                if (steeringWeights.size() != static_cast<size_t>(numChannels))
                {
                    throw std::invalid_argument(
                        "GscPreprocessor: steeringWeights size (" + std::to_string(steeringWeights.size()) +
                        ") != numChannels (" + std::to_string(numChannels) + ")");
                }
                if (blockingMatrix.size() != static_cast<size_t>(numChannels * (numChannels - 1)))
                {
                    throw std::invalid_argument(
                        "GscPreprocessor: blockingMatrix size (" + std::to_string(blockingMatrix.size()) +
                        ") != numChannels × (numChannels-1) (" +
                        std::to_string(numChannels * (numChannels - 1)) + ")");
                }

                // Copy steering weights into Eigen vector
                for (int i = 0; i < numChannels; ++i)
                {
                    m_steering_weights(i) = steeringWeights[i];
                }

                // Copy blocking matrix (column-major from input)
                m_blocking_matrix = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>>(
                    blockingMatrix.data(), numChannels, numChannels - 1);
            }

            /**
             * @brief Get stage type identifier.
             */
            const char *getType() const override
            {
                return "gscPreprocessor";
            }

            /**
             * @brief Process N-channel input and output 2-channel GSC signals.
             *
             * **Processing**:
             * 1. For each sample `i`:
             *    - Extract N-channel input vector x[i]
             *    - Compute desired signal: d = w_steering^T * x
             *    - Compute noise references: noise_refs = B^T * x
             *    - Combine noise references into single channel: noise = sum(noise_refs)
             *    - Output 2 channels: [noise, d]
             *
             * 2. Update buffer metadata:
             *    - numSamples → samplesPerChannel × 2
             *    - numChannels → 2
             *
             * **Output Layout** (interleaved 2-channel):
             * - Channel 0: Noise reference x[n] (for adaptive filter primary input)
             * - Channel 1: Desired signal d[n] (for adaptive filter reference)
             *
             * @param buffer Input/output buffer (modified in-place)
             * @param numSamples Total input samples (samplesPerChannel × numChannels)
             * @param numChannels Number of input channels (must match m_numChannels)
             * @param timestamps Optional timestamp array (unused by this stage)
             *
             * @throws std::invalid_argument If numChannels doesn't match constructor value
             */
            void process(float *buffer, size_t numSamples, int numChannels,
                         const float *timestamps = nullptr) override
            {
                if (numChannels != m_numChannels)
                {
                    throw std::invalid_argument(
                        "GscPreprocessor: configured for " + std::to_string(m_numChannels) +
                        " channels, got " + std::to_string(numChannels));
                }

                size_t samplesPerChannel = numSamples / numChannels;

                // Temporary vectors for computation
                Eigen::VectorXf x_n(m_numChannels);
                Eigen::VectorXf noise_refs(m_numChannels - 1);

                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    // 1. Extract N-channel input vector x[i]
                    for (int c = 0; c < m_numChannels; ++c)
                    {
                        x_n(c) = buffer[i * numChannels + c];
                    }

                    // 2. Upper branch: Apply steering weights (desired signal)
                    // d[n] = w_steering^T * x[n]
                    float desired_signal = m_steering_weights.dot(x_n);

                    // 3. Lower branch: Apply blocking matrix (noise references)
                    // noise_refs = B^T * x[n]
                    // Result: (N-1) × 1 vector where each element has target cancelled
                    noise_refs = m_blocking_matrix.transpose() * x_n;

                    // 4. Combine noise references into single channel
                    // Simple approach: sum all noise reference channels
                    // Alternative: could use weighted sum or select strongest
                    float noise_reference = noise_refs.sum();

                    // 5. Write 2-channel output (interleaved)
                    // Channel 0: x[n] for LMS/RLS (noise reference)
                    // Channel 1: d[n] for LMS/RLS (desired signal)
                    buffer[i * numChannels + 0] = noise_reference;
                    buffer[i * numChannels + 1] = desired_signal;

                    // 6. Zero out remaining channels (channels 2 through N-1)
                    // This maintains buffer size consistency with the pipeline
                    for (int c = 2; c < numChannels; ++c)
                    {
                        buffer[i * numChannels + c] = 0.0f;
                    }
                }
            }

            /**
             * @brief Serialize stage state to N-API object.
             *
             * Saves steering weights and blocking matrix for persistence.
             */
            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);

                state.Set("type", Napi::String::New(env, "gscPreprocessor"));
                state.Set("numChannels", Napi::Number::New(env, m_numChannels));

                // Serialize steering weights
                Napi::Float32Array steeringArray = Napi::Float32Array::New(env, m_numChannels);
                for (int i = 0; i < m_numChannels; ++i)
                {
                    steeringArray[i] = m_steering_weights(i);
                }
                state.Set("steeringWeights", steeringArray);

                // Serialize blocking matrix (column-major)
                size_t blockingSize = m_numChannels * (m_numChannels - 1);
                Napi::Float32Array blockingArray = Napi::Float32Array::New(env, blockingSize);
                const float *blockingData = m_blocking_matrix.data();
                for (size_t i = 0; i < blockingSize; ++i)
                {
                    blockingArray[i] = blockingData[i];
                }
                state.Set("blockingMatrix", blockingArray);

                return state;
            }

            /**
             * @brief Deserialize stage state from N-API object.
             *
             * Restores steering weights and blocking matrix.
             */
            void deserializeState(const Napi::Object &state) override
            {
                m_numChannels = state.Get("numChannels").As<Napi::Number>().Int32Value();

                // Restore steering weights
                Napi::Float32Array steeringArray = state.Get("steeringWeights").As<Napi::Float32Array>();
                m_steering_weights.resize(m_numChannels);
                for (int i = 0; i < m_numChannels; ++i)
                {
                    m_steering_weights(i) = steeringArray[i];
                }

                // Restore blocking matrix
                Napi::Float32Array blockingArray = state.Get("blockingMatrix").As<Napi::Float32Array>();
                m_blocking_matrix.resize(m_numChannels, m_numChannels - 1);
                for (size_t i = 0; i < blockingArray.ElementLength(); ++i)
                {
                    m_blocking_matrix.data()[i] = blockingArray[i];
                }
            }

            /**
             * @brief Reset stage state (no-op for stateless transform).
             */
            void reset() override
            {
                // No internal state to clear (matrices are fixed)
            }

            void serializeToon(toon::Serializer &serializer) const override
            {
                serializer.writeInt32(m_numChannels);

                // Serialize steering weights
                for (int i = 0; i < m_numChannels; ++i)
                {
                    serializer.writeFloat(m_steering_weights(i));
                }

                // Serialize blocking matrix (column-major)
                const float *matrixData = m_blocking_matrix.data();
                size_t totalElements = m_numChannels * (m_numChannels - 1);
                for (size_t i = 0; i < totalElements; ++i)
                {
                    serializer.writeFloat(matrixData[i]);
                }
            }

            void deserializeToon(toon::Deserializer &deserializer) override
            {
                int numChannels = deserializer.readInt32();

                if (numChannels != m_numChannels)
                {
                    throw std::runtime_error("GscPreprocessor: Channel count mismatch during TOON deserialization");
                }

                // Restore steering weights
                for (int i = 0; i < m_numChannels; ++i)
                {
                    m_steering_weights(i) = deserializer.readFloat();
                }

                // Restore blocking matrix
                std::vector<float> matrixData(m_numChannels * (m_numChannels - 1));
                for (size_t i = 0; i < matrixData.size(); ++i)
                {
                    matrixData[i] = deserializer.readFloat();
                }
                m_blocking_matrix = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>>(
                    matrixData.data(), m_numChannels, m_numChannels - 1);
            }

            /**
             * @brief Get number of input channels.
             */
            int getNumChannels() const { return m_numChannels; }

        private:
            int m_numChannels;                  ///< Number of input channels (N)
            Eigen::VectorXf m_steering_weights; ///< Fixed steering weights (N × 1)
            Eigen::MatrixXf m_blocking_matrix;  ///< Blocking matrix (N × (N-1))
        };

    } // namespace adapters
} // namespace dsp
