#pragma once

#include "../IDspStage.h"
#include "../core/WaveformLengthFilter.h"
#include "../utils/NapiUtils.h"
#include "../utils/Toon.h"
#include <vector>
#include <string>
#include <stdexcept>

namespace dsp::adapters
{
    class WaveformLengthStage : public IDspStage
    {
    public:
        explicit WaveformLengthStage(size_t window_size) : m_window_size(window_size)
        {
            if (window_size == 0)
            {
                throw std::invalid_argument("WaveformLength: window size must be greater than 0");
            }
        }

        const char *getType() const override { return "waveformLength"; }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (m_filters.size() != numChannels)
            {
                m_filters.clear();
                for (int i = 0; i < numChannels; ++i)
                {
                    m_filters.emplace_back(m_window_size);
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
            state.Set("numChannels", static_cast<uint32_t>(m_filters.size()));

            Napi::Array channelsArray = Napi::Array::New(env, m_filters.size());
            for (size_t i = 0; i < m_filters.size(); ++i)
            {
                Napi::Object channelState = Napi::Object::New(env);
                // getState() returns: { {buffer, runningSum}, prevSample }
                auto state = m_filters[i].getState();
                const auto &internalState = state.first; // std::pair<std::vector<float>, double>
                float prevSample = state.second;

                const std::vector<float> &bufferData = internalState.first;
                double runningSum = internalState.second;

                channelState.Set("buffer", dsp::utils::VectorToNapiArray(env, bufferData));
                channelState.Set("runningSum", Napi::Number::New(env, runningSum));
                channelState.Set("previousSample", Napi::Number::New(env, prevSample));
                channelsArray.Set(static_cast<uint32_t>(i), channelState);
            }
            state.Set("channels", channelsArray);
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            size_t windowSize = state.Get("windowSize").As<Napi::Number>().Uint32Value();
            if (windowSize != m_window_size)
            {
                throw std::runtime_error("Window size mismatch during deserialization");
            }

            uint32_t numChannels = state.Get("channels").As<Napi::Array>().Length();
            m_filters.clear();
            for (uint32_t i = 0; i < numChannels; ++i)
            {
                m_filters.emplace_back(m_window_size);
            }

            Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
            for (uint32_t i = 0; i < numChannels; ++i)
            {
                Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();
                std::vector<float> bufferData = dsp::utils::NapiArrayToVector<float>(channelState.Get("buffer").As<Napi::Array>());
                double runningSum = channelState.Get("runningSum").As<Napi::Number>().DoubleValue();
                float prevSample = channelState.Get("previousSample").As<Napi::Number>().FloatValue();

                if (!dsp::core::SumPolicy<float>::validateState(runningSum, bufferData))
                {
                    throw std::runtime_error("WaveformLength running sum validation failed");
                }

                m_filters[i].setState(bufferData, runningSum, prevSample);
            }
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.writeInt32(static_cast<int32_t>(m_window_size));
            s.writeInt32(static_cast<int32_t>(m_filters.size()));

            for (const auto &filter : m_filters)
            {
                auto state = filter.getState();
                const auto &internalState = state.first;
                float prevSample = state.second;

                const std::vector<float> &bufferData = internalState.first;
                double runningSum = internalState.second;

                s.writeFloatArray(bufferData);
                s.writeDouble(runningSum);
                s.writeFloat(prevSample);
            }
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            size_t windowSize = static_cast<size_t>(d.readInt32());
            if (windowSize != m_window_size)
            {
                throw std::runtime_error("WaveformLength TOON: window size mismatch");
            }

            int32_t numChannels = d.readInt32();
            m_filters.clear();
            for (int32_t i = 0; i < numChannels; ++i)
            {
                m_filters.emplace_back(m_window_size);
            }

            for (auto &filter : m_filters)
            {
                std::vector<float> bufferData = d.readFloatArray();
                double runningSum = d.readDouble();
                float prevSample = d.readFloat();

                if (!dsp::core::SumPolicy<float>::validateState(runningSum, bufferData))
                {
                    throw std::runtime_error("WaveformLength TOON: validation failed");
                }

                filter.setState(bufferData, runningSum, prevSample);
            }
        }

    private:
        size_t m_window_size;
        std::vector<dsp::core::WaveformLengthFilter<float>> m_filters;
    };

} // namespace dsp::adapters