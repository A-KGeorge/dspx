#pragma once

#include "../IDspStage.h"
#include "../core/ExponentialMovingAverageFilter.h"
#include "../utils/SimdOps.h"
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string>

namespace dsp::adapters
{
    enum class EmaMode
    {
        Batch,
        Moving
    };

    class ExponentialMovingAverageStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new Exponential Moving Average Stage.
         * @param mode The averaging mode (Batch or Moving).
         * @param alpha The smoothing factor (0 < α ≤ 1).
         */
        explicit ExponentialMovingAverageStage(EmaMode mode, float alpha)
            : m_mode(mode),
              m_alpha(alpha)
        {
            if (alpha <= 0.0f || alpha > 1.0f)
            {
                throw std::invalid_argument("ExponentialMovingAverage: alpha must be in range (0, 1]");
            }
        }

        // Return the type identifier for this stage
        const char *getType() const override
        {
            return "exponentialMovingAverage";
        }

        // This is the implementation of the interface method
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_mode == EmaMode::Batch)
            {
                processBatch(buffer, numSamples, numChannels);
            }
            else // EmaMode::Moving
            {
                processMoving(buffer, numSamples, numChannels);
            }
        }

        // Serialize the stage's state to a Napi::Object
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            std::string modeStr = (m_mode == EmaMode::Moving) ? "moving" : "batch";
            state.Set("mode", modeStr);
            state.Set("alpha", Napi::Number::New(env, m_alpha));

            if (m_mode == EmaMode::Moving)
            {
                state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

                // Serialize each channel's filter state
                Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
                for (size_t i = 0; i < m_filters.size(); ++i)
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get the filter's internal state
                    auto [ema, initialized] = m_filters[i].getState();

                    channelState.Set("ema", Napi::Number::New(env, ema));
                    channelState.Set("initialized", Napi::Boolean::New(env, initialized));

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
            EmaMode newMode = (modeStr == "moving") ? EmaMode::Moving : EmaMode::Batch;

            if (newMode != m_mode)
            {
                throw std::runtime_error("ExponentialMovingAverage mode mismatch during deserialization");
            }

            float alpha = state.Get("alpha").As<Napi::Number>().FloatValue();
            if (std::abs(alpha - m_alpha) > 1e-6f)
            {
                throw std::runtime_error("ExponentialMovingAverage alpha mismatch during deserialization");
            }

            if (m_mode == EmaMode::Moving)
            {
                // Get number of channels
                uint32_t numChannels = state.Get("channels").As<Napi::Array>().Length();

                // Recreate filters
                m_filters.clear();
                for (uint32_t i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back(m_alpha);
                }

                // Restore each channel's state
                Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
                for (uint32_t i = 0; i < numChannels; ++i)
                {
                    Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                    float ema = channelState.Get("ema").As<Napi::Number>().FloatValue();
                    bool initialized = channelState.Get("initialized").As<Napi::Boolean>().Value();

                    // Restore the filter's state
                    m_filters[i].setState(ema, initialized);
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
            serializer.writeString((m_mode == EmaMode::Moving) ? "moving" : "batch");

            // 2. Alpha
            serializer.writeString("alpha");
            serializer.writeFloat(m_alpha);

            if (m_mode == EmaMode::Moving)
            {
                // 3. Channels
                serializer.writeString("channels");
                serializer.startArray();

                for (const auto &filter : m_filters)
                {
                    auto [ema, initialized] = filter.getState();

                    serializer.startObject();

                    serializer.writeString("ema");
                    serializer.writeFloat(ema);

                    serializer.writeString("initialized");
                    serializer.writeBool(initialized);

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

            EmaMode newMode = (modeStr == "moving") ? EmaMode::Moving : EmaMode::Batch;
            if (newMode != m_mode)
            {
                throw std::runtime_error("ExponentialMovingAverage mode mismatch during TOON deserialization");
            }

            // 2. Alpha
            key = deserializer.readString(); // "alpha"
            float alpha = deserializer.readFloat();
            if (std::abs(alpha - m_alpha) > 1e-6f)
            {
                throw std::runtime_error("ExponentialMovingAverage alpha mismatch during TOON deserialization");
            }

            if (modeStr == "moving")
            {
                // 3. Channels
                key = deserializer.readString(); // "channels"
                deserializer.consumeToken(dsp::toon::T_ARRAY_START);

                m_filters.clear();

                while (deserializer.peekToken() != dsp::toon::T_ARRAY_END)
                {
                    deserializer.consumeToken(dsp::toon::T_OBJECT_START);

                    // EMA value
                    deserializer.readString(); // "ema"
                    float ema = deserializer.readFloat();

                    // Initialized flag
                    deserializer.readString(); // "initialized"
                    bool initialized = deserializer.readBool();

                    // Reconstruct filter
                    m_filters.emplace_back(m_alpha);
                    m_filters.back().setState(ema, initialized);

                    deserializer.consumeToken(dsp::toon::T_OBJECT_END);
                }
                deserializer.consumeToken(dsp::toon::T_ARRAY_END);
            }

            deserializer.consumeToken(dsp::toon::T_OBJECT_END);
        }

    private:
        /**
         * @brief Batch mode: Compute exponential moving average over entire buffer per channel.
         * Each sample in the channel is replaced by the progressive EMA.
         */
        void processBatch(float *buffer, size_t numSamples, int numChannels)
        {
            // Process each channel independently
            for (int c = 0; c < numChannels; ++c)
            {
                // Initialize EMA with first sample of this channel
                float ema = (c < numSamples) ? buffer[c] : 0.0f;

                // Process all samples for this channel
                for (size_t i = c; i < numSamples; i += numChannels)
                {
                    // EMA formula: EMA(t) = α * value(t) + (1 - α) * EMA(t-1)
                    ema = m_alpha * buffer[i] + (1.0f - m_alpha) * ema;
                    buffer[i] = ema;
                }
            }
        }

        /**
         * @brief Moving mode: Statefully process samples using EMA filters with continuity.
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
                    m_filters.emplace_back(m_alpha);
                }
            }

            // Process the buffer sample by sample, de-interleaving
            for (size_t i = 0; i < numSamples; ++i)
            {
                int channel = i % numChannels;
                buffer[i] = m_filters[channel].addSample(buffer[i]);
            }
        }

        EmaMode m_mode;
        float m_alpha;
        // Separate filter instance for each channel's state
        std::vector<dsp::core::ExponentialMovingAverageFilter<float>> m_filters;
    };

} // namespace dsp::adapters
