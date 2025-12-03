#pragma once

#include "../IDspStage.h"
#include "../core/SscFilter.h"
#include "../utils/NapiUtils.h"
#include "../utils/Toon.h"
#include <vector>
#include <string>
#include <stdexcept>

namespace dsp::adapters
{
    class SscStage : public IDspStage
    {
    public:
        explicit SscStage(size_t window_size, float threshold)
            : m_window_size(window_size), m_threshold(threshold)
        {
            if (window_size == 0)
            {
                throw std::invalid_argument("SSC: window size must be greater than 0");
            }
        }

        const char *getType() const override { return "slopeSignChange"; }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_filters.size() != numChannels)
            {
                m_filters.clear();
                for (int i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back(m_window_size, m_threshold);
                }
            }

            for (size_t i = 0; i < numSamples; ++i)
            {
                int channel = i % numChannels;
                buffer[i] = m_filters[channel].addSample(buffer[i]);
            }
        }

        void reset() override
        {
            for (auto &filter : m_filters)
            {
                filter.clear();
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("windowSize", static_cast<uint32_t>(m_window_size));
            state.Set("threshold", Napi::Number::New(env, m_threshold));
            state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

            Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
            for (size_t i = 0; i < m_filters.size(); ++i)
            {
                Napi::Object channelState = Napi::Object::New(env);
                auto [internalState, filterState] = m_filters[i].getState();
                auto [bufferData, runningCount] = internalState;

                channelState.Set("buffer", dsp::utils::VectorToNapiArray(env, bufferData));
                channelState.Set("runningCount", static_cast<uint32_t>(runningCount));
                channelState.Set("sample_minus_1", Napi::Number::New(env, filterState.sample_minus_1));
                channelState.Set("sample_minus_2", Napi::Number::New(env, filterState.sample_minus_2));
                channelState.Set("init_count", Napi::Number::New(env, filterState.init_count));

                channelsArray.Set(static_cast<uint32_t>(i), channelState);
            }
            state.Set("channels", channelsArray);
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            size_t windowSize = state.Get("windowSize").As<Napi::Number>().Uint32Value();
            float threshold = state.Get("threshold").As<Napi::Number>().FloatValue();
            if (windowSize != m_window_size || threshold != m_threshold)
            {
                throw std::runtime_error("SSC parameter mismatch during deserialization");
            }

            uint32_t numChannels = state.Get("channels").As<Napi::Array>().Length();
            m_filters.clear();
            for (uint32_t i = 0; i < numChannels; ++i)
            {
                m_filters.emplace_back(m_window_size, m_threshold);
            }

            Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
            for (uint32_t i = 0; i < numChannels; ++i)
            {
                Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();
                std::vector<bool> bufferData = dsp::utils::NapiArrayToVector<bool>(channelState.Get("buffer").As<Napi::Array>());
                size_t runningCount = channelState.Get("runningCount").As<Napi::Number>().Uint32Value();

                dsp::core::SscFilter<float>::SscFilterState filterState;
                filterState.sample_minus_1 = channelState.Get("sample_minus_1").As<Napi::Number>().FloatValue();
                filterState.sample_minus_2 = channelState.Get("sample_minus_2").As<Napi::Number>().FloatValue();
                filterState.init_count = channelState.Get("init_count").As<Napi::Number>().Int32Value();

                if (!dsp::core::CounterPolicy::validateState(runningCount, bufferData))
                {
                    throw std::runtime_error("SSC running count validation failed");
                }

                m_filters[i].setState(bufferData, runningCount, filterState);
            }
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.writeInt32(static_cast<int32_t>(m_window_size));
            s.writeFloat(m_threshold);
            s.writeInt32(static_cast<int32_t>(m_filters.size()));

            for (const auto &filter : m_filters)
            {
                auto [internalState, filterState] = filter.getState();
                auto [bufferData, runningCount] = internalState;

                // Serialize bool vector as byte array
                s.writeInt32(static_cast<int32_t>(bufferData.size()));
                for (bool b : bufferData)
                {
                    s.writeBool(b);
                }
                s.writeInt32(static_cast<int32_t>(runningCount));
                s.writeFloat(filterState.sample_minus_1);
                s.writeFloat(filterState.sample_minus_2);
                s.writeInt32(filterState.init_count);
            }
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            size_t windowSize = static_cast<size_t>(d.readInt32());
            float threshold = d.readFloat();
            if (windowSize != m_window_size || threshold != m_threshold)
            {
                throw std::runtime_error("SSC TOON: parameter mismatch");
            }

            int32_t numChannels = d.readInt32();
            m_filters.clear();
            for (int32_t i = 0; i < numChannels; ++i)
            {
                m_filters.emplace_back(m_window_size, m_threshold);
            }

            for (auto &filter : m_filters)
            {
                int32_t bufSize = d.readInt32();
                std::vector<bool> bufferData(bufSize);
                for (int32_t j = 0; j < bufSize; ++j)
                {
                    bufferData[j] = d.readBool();
                }
                size_t runningCount = static_cast<size_t>(d.readInt32());

                dsp::core::SscFilter<float>::SscFilterState filterState;
                filterState.sample_minus_1 = d.readFloat();
                filterState.sample_minus_2 = d.readFloat();
                filterState.init_count = d.readInt32();

                if (!dsp::core::CounterPolicy::validateState(runningCount, bufferData))
                {
                    throw std::runtime_error("SSC TOON: validation failed");
                }

                filter.setState(bufferData, runningCount, filterState);
            }
        }

    private:
        size_t m_window_size;
        float m_threshold;
        std::vector<dsp::core::SscFilter<float>> m_filters;
    };

} // namespace dsp::adapters