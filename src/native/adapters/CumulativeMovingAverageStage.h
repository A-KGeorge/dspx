#pragma once

#include "../IDspStage.h"
#include "../core/CumulativeMovingAverageFilter.h"
#include "../utils/SimdOps.h"
#include <vector>
#include <stdexcept>
#include <string>

namespace dsp::adapters
{
    enum class CmaMode
    {
        Batch,
        Moving
    };

    class CumulativeMovingAverageStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new Cumulative Moving Average Stage.
         * @param mode The averaging mode (Batch or Moving).
         */
        explicit CumulativeMovingAverageStage(CmaMode mode)
            : m_mode(mode)
        {
        }

        // Return the type identifier for this stage
        const char *getType() const override
        {
            return "cumulativeMovingAverage";
        }

        // This is the implementation of the interface method
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == CmaMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // CmaMode::Moving
            {
                processMoving(buffer, numSamples, numChannels);
            }
        }

        // Serialize the stage's state to a Napi::Object
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == CmaMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);

            if (m_mode == CmaMode::Moving)
            {
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the filter's internal state
                    auto [sum, count] = m_filters[i].getState();

                    channelState.Set("sum", Napi::Number::New(env, sum));
                    channelState.Set("count", Napi::Number::New(env, static_cast<uint32_t>(count)));

                    channelsArray.Set(static_cast<uint32_t>(i), channelState);
                }
                state.Set("channels", channelsArray);
            }

            return state;
        }

        // Deserialize and restore the stage's state
        void deserializeState(const Napi::Object &state) override
        {
            std::string modeStr = state.Get("mode").As<Napi::String>().Utf8Value();
            CmaMode newMode = (modeStr == "moving") ? CmaMode::Moving : CmaMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("CumulativeMovingAverage mode mismatch during deserialization");
            }

            if (m_mode == CmaMode::Moving)
            {
                // Get number of channels
                uint32_t numChannels = state.Get("channels").As<Napi::Array>().Length();

                // Recreate filters
                m_filters.clear();
                for (uint32_t i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back();
                }

                // Restore each channel's state
                Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
                for (uint32_t i = 0; i < numChannels; ++i)
                {
                    Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                    float sum = channelState.Get("sum").As<Napi::Number>().FloatValue();
                    size_t count = channelState.Get("count").As<Napi::Number>().Uint32Value();

                    // Restore the filter's state
                    m_filters[i].setState(sum, count);
                }
            }
        }

        // Reset all filters to initial state
        void reset() override
        {
            for (auto &filter : m_filters)
            {
                filter.clear();
            }
        }

        // TOON Binary Serialization
        void serializeToon(dsp::toon::Serializer &serializer) const override
        {
            serializer.startObject();

            // 1. Mode
            serializer.writeString("mode");
            serializer.writeString((m_mode == CmaMode::Moving) ? "moving" : "batch");

            if (m_mode == CmaMode::Moving)
            {
                // 2. Channels
                serializer.writeString("channels");
                serializer.startArray();

                for (const auto &filter : m_filters)
                {
                    auto [sum, count] = filter.getState();

                    serializer.startObject();

                    serializer.writeString("sum");
                    serializer.writeFloat(sum);

                    serializer.writeString("count");
                    serializer.writeInt32(static_cast<int32_t>(count));

                    serializer.endObject();
                }
                serializer.endArray();
            }

            serializer.endObject();
        }

        // TOON Binary Deserialization
        void deserializeToon(dsp::toon::Deserializer &deserializer) override
        {
            deserializer.consumeToken(dsp::toon::T_OBJECT_START);

            // 1. Mode
            std::string key = deserializer.readString(); // "mode"
            std::string modeStr = deserializer.readString();

            CmaMode newMode = (modeStr == "moving") ? CmaMode::Moving : CmaMode::Batch;
            if (newMode != m_mode)
            {
                throw std::runtime_error("CumulativeMovingAverage mode mismatch during TOON deserialization");
            }

            if (modeStr == "moving")
            {
                // 2. Channels
                key = deserializer.readString(); // "channels"
                deserializer.consumeToken(dsp::toon::T_ARRAY_START);

                m_filters.clear();

                while (deserializer.peekToken() != dsp::toon::T_ARRAY_END)
                {
                    deserializer.consumeToken(dsp::toon::T_OBJECT_START);

                    // Sum
                    deserializer.readString(); // "sum"
                    float sum = deserializer.readFloat();

                    // Count
                    deserializer.readString(); // "count"
                    int32_t count = deserializer.readInt32();

                    // Reconstruct filter
                    m_filters.emplace_back();
                    m_filters.back().setState(sum, static_cast<size_t>(count));

                    deserializer.consumeToken(dsp::toon::T_OBJECT_END);
                }
                deserializer.consumeToken(dsp::toon::T_ARRAY_END);
            }

            deserializer.consumeToken(dsp::toon::T_OBJECT_END);
        }

    private:
        /**
         * @brief Batch mode: Compute cumulative moving average over entire buffer per channel.
         * Each sample in the channel is replaced by the cumulative average up to that point.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            // Process each channel independently
            for (int c = 0; c < numChannels; ++c)
            {
                float sum = 0.0f;
                size_t count = 0;

                // Process all samples for this channel
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    sum += buffer[i];
                    count++;
                    // CMA = sum / count
                    buffer[i] = sum / static_cast<float>(count);
                }
            }
        }

        /**
         * @brief Moving mode: Statefully process samples using CMA filters with continuity.
         * @param buffer The interleaved audio buffer.
         * @param numSamples The total number of samples.
         * @param numChannels The number of channels.
         */
        void processMoving(float *buffer, size_t numSamples, int numChannels)
        {
            // Lazily initialize our filters, one for each channel
            if (m_filters.size() != static_cast<size_t>(numChannels))
            {
                m_filters.clear();
                for (int i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back();
                }
            }

            // Process the buffer sample by sample, de-interleaving
            for (size_t i = 0; i < numSamples; ++i)
            {
                int channel = i % numChannels;
                buffer[i] = m_filters[channel].addSample(buffer[i]);
            }
        }

        CmaMode m_mode;
        // Separate filter instance for each channel's state
        std::vector<dsp::core::CumulativeMovingAverageFilter<float>> m_filters;
    };

} // namespace dsp::adapters
