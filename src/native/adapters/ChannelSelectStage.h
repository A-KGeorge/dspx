#pragma once

#include "../IDspStage.h"
#include <vector>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace dsp
{
    namespace adapters
    {
        /**
         * @brief Selects specific channels from multi-channel input by channel indices.
         *
         * Unlike ChannelSelector which keeps the first N channels, this stage allows
         * selecting arbitrary channels by their indices (e.g., channels [0, 3, 7]).
         * The output buffer contains only the selected channels in the order specified.
         *
         * **Use Cases**:
         * - Extract non-contiguous channels (e.g., channels 1, 3, 5 from 8-channel input)
         * - Reorder channels (e.g., swap left/right in stereo: [1, 0])
         * - Duplicate channels (e.g., mono from stereo channel 0: [0, 0])
         * - Complex routing in multi-channel pipelines
         *
         * @example
         * // Select channels 0, 3, 7 from 8-channel EEG data
         * pipeline.ChannelSelect({ channels: [0, 3, 7] })
         *         .Rms({ mode: 'moving', windowSize: 100 });
         *
         * // Swap stereo channels
         * pipeline.ChannelSelect({ channels: [1, 0] })
         *         .process(stereoData);
         *
         * // Convert stereo to mono by duplicating left channel
         * pipeline.ChannelSelect({ channels: [0, 0] });
         */
        class ChannelSelectStage : public IDspStage
        {
        public:
            /**
             * @brief Construct channel selector with specific channel indices.
             *
             * @param channels Vector of channel indices to select (0-based)
             * @param numInputChannels Total number of input channels (for validation)
             * @throws std::invalid_argument if channels is empty or indices are out of range
             */
            ChannelSelectStage(const std::vector<int> &channels, int numInputChannels)
                : m_channels(channels),
                  m_numInputChannels(numInputChannels)
            {
                if (channels.empty())
                {
                    throw std::invalid_argument("ChannelSelect: channels array cannot be empty");
                }
                if (numInputChannels <= 0)
                {
                    throw std::invalid_argument("ChannelSelect: numInputChannels must be > 0");
                }

                // Validate all channel indices
                for (size_t i = 0; i < channels.size(); ++i)
                {
                    if (channels[i] < 0 || channels[i] >= numInputChannels)
                    {
                        throw std::invalid_argument(
                            "ChannelSelect: channel index " + std::to_string(channels[i]) +
                            " out of range [0, " + std::to_string(numInputChannels - 1) + "]");
                    }
                }
            }

            const char *getType() const override
            {
                return "channelSelect";
            }

            /**
             * @brief Indicates this stage changes channel count.
             */
            bool isResizing() const override
            {
                return true;
            }

            /**
             * @brief Returns output channel count (number of selected channels).
             */
            int getOutputChannels() const override
            {
                return static_cast<int>(m_channels.size());
            }

            /**
             * @brief Calculate output size based on selected channels.
             */
            size_t calculateOutputSize(size_t inputSize) const override
            {
                size_t samplesPerChannel = inputSize / m_numInputChannels;
                return samplesPerChannel * m_channels.size();
            }

            /**
             * @brief Process with buffer resizing.
             *
             * Extracts selected channels from each interleaved sample.
             */
            void processResizing(const float *inputBuffer, size_t inputSize,
                                 float *outputBuffer, size_t &outputSize,
                                 int numChannels, const float *timestamps = nullptr) override
            {
                if (numChannels != m_numInputChannels)
                {
                    throw std::invalid_argument(
                        "ChannelSelect: configured for " + std::to_string(m_numInputChannels) +
                        " input channels, got " + std::to_string(numChannels));
                }

                size_t samplesPerChannel = inputSize / numChannels;
                size_t numOutputChannels = m_channels.size();
                outputSize = samplesPerChannel * numOutputChannels;

                // Extract selected channels for each sample
                for (size_t i = 0; i < samplesPerChannel; ++i)
                {
                    for (size_t outCh = 0; outCh < numOutputChannels; ++outCh)
                    {
                        int inCh = m_channels[outCh];
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
                    "ChannelSelect: in-place processing not supported, use processResizing");
            }

            Napi::Object serializeState(Napi::Env env) const override
            {
                Napi::Object state = Napi::Object::New(env);
                state.Set("type", Napi::String::New(env, "channelSelect"));
                state.Set("numInputChannels", Napi::Number::New(env, m_numInputChannels));

                // Serialize channel array
                Napi::Array channelsArray = Napi::Array::New(env, m_channels.size());
                for (size_t i = 0; i < m_channels.size(); ++i)
                {
                    channelsArray.Set(i, Napi::Number::New(env, m_channels[i]));
                }
                state.Set("channels", channelsArray);

                return state;
            }

            void deserializeState(const Napi::Object &state) override
            {
                m_numInputChannels = state.Get("numInputChannels").As<Napi::Number>().Int32Value();

                // Deserialize channel array
                Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
                m_channels.clear();
                for (uint32_t i = 0; i < channelsArray.Length(); ++i)
                {
                    m_channels.push_back(channelsArray.Get(i).As<Napi::Number>().Int32Value());
                }
            }

            void reset() override
            {
                // No state to reset
            }

            int getNumInputChannels() const { return m_numInputChannels; }
            const std::vector<int> &getChannels() const { return m_channels; }

        private:
            std::vector<int> m_channels;
            int m_numInputChannels;
        };

    } // namespace adapters
} // namespace dsp
