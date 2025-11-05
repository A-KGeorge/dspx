#pragma once

#include "../IDspStage.h"
#include <vector>
#include <stdexcept>
#include <string>
#include <cstring>

namespace dsp
{
    namespace adapters
    {
        /**
         * @brief Merges or duplicates channels to create multi-channel output.
         *
         * This stage can:
         * - Convert mono to stereo by duplicating a channel
         * - Merge multiple mono streams into multi-channel output
         * - Create custom channel mappings (e.g., copy channel 0 to channels 0 and 2)
         *
         * The `mapping` parameter specifies which input channel goes to each output channel.
         * For example, mapping [0, 0] duplicates channel 0 to create stereo from mono.
         * Mapping [0, 1, 2] keeps 3 channels as-is, while [0, 0, 0, 0] creates 4-channel
         * output all from input channel 0.
         *
         * **Use Cases**:
         * - Mono to stereo conversion
         * - Channel duplication for multi-channel algorithms
         * - Custom channel routing and mixing
         * - Preparing data for multi-channel filters
         *
         * @example
         * // Mono to stereo (duplicate channel 0)
         * pipeline.ChannelMerge({ mapping: [0, 0] })
         *         .process(monoData);
         *
         * // 3-channel to 6-channel by duplicating each channel
         * pipeline.ChannelMerge({ mapping: [0, 0, 1, 1, 2, 2] })
         *         .process(data);
         *
         * // Custom routing: input [A, B, C] -> output [A, C, B, A]
         * pipeline.ChannelMerge({ mapping: [0, 2, 1, 0] });
         */
        class ChannelMergeStage : public IDspStage
        {
        public:
            /**
             * @brief Construct channel merger with mapping.
             *
             * @param mapping Vector specifying which input channel goes to each output channel.
             *                Length of mapping determines output channel count.
             *                Each value is an input channel index (0-based).
             * @param numInputChannels Total number of input channels (for validation)
             * @throws std::invalid_argument if mapping is empty or indices are out of range
             */
            ChannelMergeStage(const std::vector<int> &mapping, int numInputChannels)
                : m_mapping(mapping),
                  m_numInputChannels(numInputChannels)
            {
                if (mapping.empty())
                {
                    throw std::invalid_argument("ChannelMerge: mapping array cannot be empty");
                }
                if (numInputChannels <= 0)
                {
                    throw std::invalid_argument("ChannelMerge: numInputChannels must be > 0");
                }

                // Validate all mapping indices
                for (size_t i = 0; i < mapping.size(); ++i)
                {
                    if (mapping[i] < 0 || mapping[i] >= numInputChannels)
                    {
                        throw std::invalid_argument(
                            "ChannelMerge: mapping index " + std::to_string(mapping[i]) +
                            " out of range [0, " + std::to_string(numInputChannels - 1) + "]");
                    }
                }
            }

            const char *getType() const override
            {
                return "channelMerge";
            }

            /**
             * @brief Indicates this stage changes channel count.
             */
            bool isResizing() const override
            {
                return true;
            }

            /**
             * @brief Returns output channel count (length of mapping).
             */
            int getOutputChannels() const override
            {
                return static_cast<int>(m_mapping.size());
            }

            /**
             * @brief Calculate output size based on channel mapping.
             */
            size_t calculateOutputSize(size_t inputSize) const override
            {
                size_t samplesPerChannel = inputSize / m_numInputChannels;
                return samplesPerChannel * m_mapping.size();
            }

            /**
             * @brief Process with buffer resizing.
             *
             * Maps input channels to output channels according to mapping.
             */
            void processResizing(const float *inputBuffer, size_t inputSize,
                                 float *outputBuffer, size_t &outputSize,
                                 int numChannels, const float *timestamps = nullptr) override
            {
                if (numChannels != m_numInputChannels)
                {
                    throw std::invalid_argument(
                        "ChannelMerge: configured for " + std::to_string(m_numInputChannels) +
                        " input channels, got " + std::to_string(numChannels));
                }

                size_t samplesPerChannel = inputSize / numChannels;
                size_t numOutputChannels = m_mapping.size();
                outputSize = samplesPerChannel * numOutputChannels;

                // Apply channel mapping for each sample
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    for (size_t outCh = 0; outCh < numOutputChannels; ++outCh)
                    {
                        int inCh = m_mapping[outCh];
                        outputBuffer[i * numOutputChannels + outCh] =
                            inputBuffer[i * numChannels + inCh];
                    }
                }
            }

            /**
             * @brief In-place process (not supported for resizing stage).
             */
            void process(float *buffer, size_t numSamples, int numChannels,
                         const float *timestamps = nullptr) override
            {
                throw std::runtime_error(
                    "ChannelMerge: in-place processing not supported, use processResizing");
            }

            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);
                state.Set("type", Napi::String::New(env, "channelMerge"));
                state.Set("numInputChannels", Napi::Number::New(env, m_numInputChannels));

                // Serialize mapping array
                Napi::Array mappingArray = Napi::Array::New(env, m_mapping.size());
                for (size_t i = 0; i < m_mapping.size(); ++i)
                {
                    mappingArray.Set(i, Napi::Number::New(env, m_mapping[i]));
                }
                state.Set("mapping", mappingArray);

                return state;
            }

            void deserializeState(const Napi::Object &state) override
            {
                m_numInputChannels = state.Get("numInputChannels").As<Napi::Number>().Int32Value();

                // Deserialize mapping array
                Napi::Array mappingArray = state.Get("mapping").As<Napi::Array>();
                m_mapping.clear();
                for (uint32_t i = 0; i < mappingArray.Length(); ++i)
                {
                    m_mapping.push_back(mappingArray.Get(i).As<Napi::Number>().Int32Value());
                }
            }

            void reset() override
            {
                // No state to reset
            }

            int getNumInputChannels() const { return m_numInputChannels; }
            const std::vector<int> &getMapping() const { return m_mapping; }

        private:
            std::vector<int> m_mapping;
            int m_numInputChannels;
        };

    } // namespace adapters
} // namespace dsp
