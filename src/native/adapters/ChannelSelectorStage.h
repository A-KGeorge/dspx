#pragma once

#include "../IDspStage.h"
#include <vector>
#include <stdexcept>
#include <string>

namespace dsp
{
    namespace adapters
    {
        /**
         * @brief Selects a subset of channels from multi-channel input.
         *
         * This stage extracts specific channels and zeros out the rest,
         * effectively reducing the active channel count while maintaining
         * buffer size compatibility with the pipeline.
         *
         * **Use Cases**:
         * - After GscPreprocessor: Extract 2 active channels for LMS/RLS
         * - After CSP: Extract transformed components
         * - Channel routing in complex pipelines
         *
         * @example
         * // After GSC (which outputs data in channels 0-1, zeros in 2-7)
         * pipeline.GscPreprocessor({ ... })
         *         .ChannelSelector({ numInputChannels: 8, numOutputChannels: 2 })
         *         .LmsFilter({ numTaps: 32, learningRate: 0.01 });
         */
        class ChannelSelectorStage : public IDspStage
        {
        public:
            /**
             * @brief Construct channel selector.
             *
             * @param numInputChannels Number of input channels
             * @param numOutputChannels Number of channels to keep (1 to numInputChannels)
             *                          Keeps channels 0 through numOutputChannels-1
             */
            ChannelSelectorStage(int numInputChannels, int numOutputChannels)
                : m_numInputChannels(numInputChannels),
                  m_numOutputChannels(numOutputChannels)
            {
                if (numInputChannels <= 0)
                {
                    throw std::invalid_argument("ChannelSelector: numInputChannels must be > 0");
                }
                if (numOutputChannels <= 0 || numOutputChannels > numInputChannels)
                {
                    throw std::invalid_argument(
                        "ChannelSelector: numOutputChannels must be in range [1, " +
                        std::to_string(numInputChannels) + "], got " +
                        std::to_string(numOutputChannels));
                }
            }

            const char *getType() const override
            {
                return "channelSelector";
            }

            /**
             * @brief Indicates this stage changes effective channel count.
             *
             * By marking as resizing, we tell the pipeline to allocate a new buffer
             * with the reduced size.
             */
            bool isResizing() const override
            {
                return true;
            }

            /**
             * @brief Returns output channel count (for pipeline to update m_channels).
             */
            int getOutputChannels() const override
            {
                return m_numOutputChannels;
            }

            /**
             * @brief Calculate output size (reduced by channel count ratio).
             */
            size_t calculateOutputSize(size_t inputSize) const override
            {
                size_t samplesPerChannel = inputSize / m_numInputChannels;
                return samplesPerChannel * m_numOutputChannels;
            }

            /**
             * @brief Process with buffer resizing.
             *
             * Extracts first numOutputChannels from each interleaved sample.
             */
            void processResizing(const float *inputBuffer, size_t inputSize,
                                 float *outputBuffer, size_t &outputSize,
                                 int numChannels, const float *timestamps = nullptr) override
            {
                if (numChannels != m_numInputChannels)
                {
                    throw std::invalid_argument(
                        "ChannelSelector: configured for " + std::to_string(m_numInputChannels) +
                        " input channels, got " + std::to_string(numChannels));
                }

                size_t samplesPerChannel = inputSize / numChannels;
                outputSize = samplesPerChannel * m_numOutputChannels;

                // Extract selected channels
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    for (int ch = 0; ch < m_numOutputChannels; ++ch)
                    {
                        outputBuffer[i * m_numOutputChannels + ch] =
                            inputBuffer[i * numChannels + ch];
                    }
                }
            }

            /**
             * @brief In-place process (not used, but required by interface).
             */
            void process(float *buffer, size_t numSamples, int numChannels,
                         const float *timestamps = nullptr) override
            {
                throw std::runtime_error(
                    "ChannelSelector: in-place processing not supported, use processResizing");
            }

            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);
                state.Set("type", Napi::String::New(env, "channelSelector"));
                state.Set("numInputChannels", Napi::Number::New(env, m_numInputChannels));
                state.Set("numOutputChannels", Napi::Number::New(env, m_numOutputChannels));
                return state;
            }

            void deserializeState(const Napi::Object &state) override
            {
                m_numInputChannels = state.Get("numInputChannels").As<Napi::Number>().Int32Value();
                m_numOutputChannels = state.Get("numOutputChannels").As<Napi::Number>().Int32Value();
            }

            void reset() override
            {
                // No state to reset
            }

            void serializeToon(toon::Serializer &serializer) const override
            {
                serializer.writeInt32(m_numInputChannels);
                serializer.writeInt32(m_numOutputChannels);
            }

            void deserializeToon(toon::Deserializer &deserializer) override
            {
                int numInputChannels = deserializer.readInt32();
                int numOutputChannels = deserializer.readInt32();

                if (numInputChannels != m_numInputChannels || numOutputChannels != m_numOutputChannels)
                {
                    throw std::runtime_error("ChannelSelector: Channel count mismatch during TOON deserialization");
                }
            }

            int getNumInputChannels() const { return m_numInputChannels; }
            int getNumOutputChannels() const { return m_numOutputChannels; }

        private:
            int m_numInputChannels;
            int m_numOutputChannels;
        };

    } // namespace adapters
} // namespace dsp
