#include "FilterStage.h"
#include <stdexcept>

namespace dsp::adapters
{

    FilterStage::FilterStage(const std::vector<double> &bCoeffs, const std::vector<double> &aCoeffs)
        : m_bCoeffs(bCoeffs), m_aCoeffs(aCoeffs)
    {
        if (bCoeffs.empty() || aCoeffs.empty())
        {
            throw std::invalid_argument("Filter coefficients cannot be empty.");
        }

        // Determine if this is a FIR filter
        // FIR filter has aCoeffs = [1.0] (no feedback)
        m_isFir = (aCoeffs.size() == 1 && std::abs(aCoeffs[0] - 1.0) < 1e-10);

        std::string type = m_isFir ? "fir" : "iir";
        m_typeStr = "filter:" + type;
    }

    const char *FilterStage::getType() const
    {
        return m_typeStr.c_str();
    }

    void FilterStage::reset()
    {
        if (m_isFir)
        {
            for (auto &filter : m_firFilters)
            {
                if (filter)
                {
                    filter->reset();
                }
            }
        }
        else
        {
            for (auto &filter : m_iirFilters)
            {
                if (filter)
                {
                    filter->reset();
                }
            }
        }
    }

    void FilterStage::initializeFilters(int numChannels)
    {
        if (m_isFir)
        {
            if (m_firFilters.empty())
            {
                m_firFilters.resize(numChannels);
                std::vector<float> bCoeffsFloat(m_bCoeffs.begin(), m_bCoeffs.end());
                for (int i = 0; i < numChannels; ++i)
                {
                    m_firFilters[i] = std::make_unique<dsp::core::FirFilter<float>>(bCoeffsFloat, true);
                }
            }
            else if (m_firFilters.size() != static_cast<size_t>(numChannels))
            {
                throw std::runtime_error("FilterStage: Channel count changed during processing. This is not supported.");
            }
        }
        else
        {
            if (m_iirFilters.empty())
            {
                m_iirFilters.resize(numChannels);
                std::vector<float> bCoeffsFloat(m_bCoeffs.begin(), m_bCoeffs.end());
                std::vector<float> aCoeffsFloat(m_aCoeffs.begin(), m_aCoeffs.end());
                for (int i = 0; i < numChannels; ++i)
                {
                    m_iirFilters[i] = std::make_unique<dsp::core::IirFilter<float>>(bCoeffsFloat, aCoeffsFloat);
                }
            }
            else if (m_iirFilters.size() != static_cast<size_t>(numChannels))
            {
                throw std::runtime_error("FilterStage: Channel count changed during processing. This is not supported.");
            }
        }
    }

    void FilterStage::process(float *buffer, size_t numSamples, int numChannels, const float *timestamps)
    {
        initializeFilters(numChannels);

        // numSamples is the TOTAL buffer size (samples * channels)
        // Calculate samples per channel
        size_t samplesPerChannel = numSamples / numChannels;

        if (m_isFir)
        {
            for (int j = 0; j < numChannels; ++j)
            {
                if (m_firFilters[j])
                {
                    // Create a temporary buffer for this channel's output
                    std::vector<float> outputBuffer(samplesPerChannel);
                    // Create a temporary buffer for this channel's input
                    std::vector<float> inputBuffer(samplesPerChannel);
                    for (size_t i = 0; i < samplesPerChannel; ++i)
                    {
                        inputBuffer[i] = buffer[i * numChannels + j];
                    }

                    // Process the entire block for this channel
                    m_firFilters[j]->process(inputBuffer.data(), outputBuffer.data(), samplesPerChannel);

                    // Copy the processed data back into the main buffer
                    for (size_t i = 0; i < samplesPerChannel; ++i)
                    {
                        buffer[i * numChannels + j] = outputBuffer[i];
                    }
                }
            }
        }
        else
        {
            for (int j = 0; j < numChannels; ++j)
            {
                if (m_iirFilters[j])
                {
                    // Create a temporary buffer for this channel's output
                    std::vector<float> outputBuffer(samplesPerChannel);
                    // Create a temporary buffer for this channel's input
                    std::vector<float> inputBuffer(samplesPerChannel);
                    for (size_t i = 0; i < samplesPerChannel; ++i)
                    {
                        inputBuffer[i] = buffer[i * numChannels + j];
                    }

                    // Process the entire block for this channel
                    m_iirFilters[j]->process(inputBuffer.data(), outputBuffer.data(), samplesPerChannel);

                    // Copy the processed data back into the main buffer
                    for (size_t i = 0; i < samplesPerChannel; ++i)
                    {
                        buffer[i * numChannels + j] = outputBuffer[i];
                    }
                }
            }
        }
    }

    Napi::Object FilterStage::serializeState(Napi::Env env) const
    {
        Napi::Object state = Napi::Object::New(env);
        state.Set("filterType", m_isFir ? "fir" : "iir");

        if (m_isFir)
        {
            state.Set("numChannels", static_cast<uint32_t>(m_firFilters.size()));

            Napi::Array channelsArray = Napi::Array::New(env, m_firFilters.size());
            for (size_t i = 0; i < m_firFilters.size(); ++i)
            {
                if (m_firFilters[i])
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get FIR filter state
                    auto [stateBuffer, stateIndex] = m_firFilters[i]->getState();

                    // Serialize state buffer
                    Napi::Array stateArray = Napi::Array::New(env, stateBuffer.size());
                    for (size_t j = 0; j < stateBuffer.size(); ++j)
                    {
                        stateArray.Set(j, Napi::Number::New(env, stateBuffer[j]));
                    }

                    channelState.Set("stateBuffer", stateArray);
                    channelState.Set("stateIndex", static_cast<uint32_t>(stateIndex));

                    channelsArray.Set(static_cast<uint32_t>(i), channelState);
                }
            }
            state.Set("channels", channelsArray);
        }
        else // IIR
        {
            state.Set("numChannels", static_cast<uint32_t>(m_iirFilters.size()));

            Napi::Array channelsArray = Napi::Array::New(env, m_iirFilters.size());
            for (size_t i = 0; i < m_iirFilters.size(); ++i)
            {
                if (m_iirFilters[i])
                {
                    Napi::Object channelState = Napi::Object::New(env);

                    // Get IIR filter state
                    auto [xState, yState] = m_iirFilters[i]->getState();

                    // Serialize x state buffer
                    Napi::Array xStateArray = Napi::Array::New(env, xState.size());
                    for (size_t j = 0; j < xState.size(); ++j)
                    {
                        xStateArray.Set(j, Napi::Number::New(env, xState[j]));
                    }

                    // Serialize y state buffer
                    Napi::Array yStateArray = Napi::Array::New(env, yState.size());
                    for (size_t j = 0; j < yState.size(); ++j)
                    {
                        yStateArray.Set(j, Napi::Number::New(env, yState[j]));
                    }

                    channelState.Set("xState", xStateArray);
                    channelState.Set("yState", yStateArray);

                    channelsArray.Set(static_cast<uint32_t>(i), channelState);
                }
            }
            state.Set("channels", channelsArray);
        }

        return state;
    }

    void FilterStage::deserializeState(const Napi::Object &state)
    {
        if (!state.Has("channels"))
        {
            throw std::runtime_error("FilterStage state missing 'channels' field");
        }

        Napi::Array channelsArray = state.Get("channels").As<Napi::Array>();
        uint32_t numChannels = channelsArray.Length();

        // Initialize filters if not already done
        initializeFilters(static_cast<int>(numChannels));

        if (m_isFir)
        {
            for (uint32_t i = 0; i < numChannels && i < m_firFilters.size(); ++i)
            {
                if (m_firFilters[i])
                {
                    Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                    // Deserialize state buffer
                    Napi::Array stateArray = channelState.Get("stateBuffer").As<Napi::Array>();
                    std::vector<float> stateBuffer(stateArray.Length());
                    for (uint32_t j = 0; j < stateArray.Length(); ++j)
                    {
                        stateBuffer[j] = stateArray.Get(j).As<Napi::Number>().FloatValue();
                    }

                    size_t stateIndex = channelState.Get("stateIndex").As<Napi::Number>().Uint32Value();

                    m_firFilters[i]->setState(stateBuffer, stateIndex);
                }
            }
        }
        else // IIR
        {
            for (uint32_t i = 0; i < numChannels && i < m_iirFilters.size(); ++i)
            {
                if (m_iirFilters[i])
                {
                    Napi::Object channelState = channelsArray.Get(i).As<Napi::Object>();

                    // Deserialize x state buffer
                    Napi::Array xStateArray = channelState.Get("xState").As<Napi::Array>();
                    std::vector<float> xState(xStateArray.Length());
                    for (uint32_t j = 0; j < xStateArray.Length(); ++j)
                    {
                        xState[j] = xStateArray.Get(j).As<Napi::Number>().FloatValue();
                    }

                    // Deserialize y state buffer
                    Napi::Array yStateArray = channelState.Get("yState").As<Napi::Array>();
                    std::vector<float> yState(yStateArray.Length());
                    for (uint32_t j = 0; j < yStateArray.Length(); ++j)
                    {
                        yState[j] = yStateArray.Get(j).As<Napi::Number>().FloatValue();
                    }

                    m_iirFilters[i]->setState(xState, yState);
                }
            }
        }
    }

} // namespace dsp::adapters
