#include "FilterBankStage.h"
#include <iostream>

namespace dsp::adapters
{

    FilterBankStage::FilterBankStage(const std::vector<FilterDefinition> &definitions, int numInputChannels)
        : m_definitions(definitions), m_numInputChannels(numInputChannels)
    {
        std::cout << "[FilterBankStage] Constructor: " << definitions.size() << " bands, "
                  << numInputChannels << " channels" << std::endl;

        if (definitions.empty())
        {
            throw std::runtime_error("FilterBank: definitions cannot be empty");
        }
        if (numInputChannels <= 0)
        {
            throw std::runtime_error("FilterBank: Invalid channel count");
        }

        initializeFilters();
        std::cout << "[FilterBankStage] Constructor complete" << std::endl;
    }

    FilterBankStage::~FilterBankStage()
    {
        std::cout << "[FilterBankStage] Destructor start: " << m_filters.size() << " channels, "
                  << (m_filters.empty() ? 0 : m_filters[0].size()) << " bands" << std::endl;

        try
        {
            // Explicitly clear filters first to ensure IirFilter destructors run
            // before scratch buffers are destroyed
            for (size_t ch = 0; ch < m_filters.size(); ++ch)
            {
                std::cout << "[FilterBankStage] Clearing channel " << ch << " ("
                          << m_filters[ch].size() << " filters)" << std::endl;
                m_filters[ch].clear(); // Destroys all unique_ptrs in this channel
            }
            std::cout << "[FilterBankStage] Clearing filter matrix" << std::endl;
            m_filters.clear(); // Clear outer vector

            // Clear scratch buffers
            std::cout << "[FilterBankStage] Clearing scratch buffers" << std::endl;
            m_planarInput.clear();
            m_planarOutput.clear();

            std::cout << "[FilterBankStage] Destructor complete" << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[FilterBankStage] ERROR in destructor: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "[FilterBankStage] UNKNOWN ERROR in destructor" << std::endl;
        }
    }

    void FilterBankStage::processResizing(const float *inputBuffer, size_t inputSize,
                                          float *outputBuffer, size_t &outputSize,
                                          int numChannels, const float *timestamps)
    {
        std::cout << "[FilterBankStage] processResizing: inputSize=" << inputSize
                  << ", numChannels=" << numChannels << std::endl;

        if (numChannels != m_numInputChannels)
        {
            throw std::runtime_error("FilterBank: Input channel mismatch");
        }

        size_t samplesPerChannel = inputSize / numChannels;
        std::cout << "[FilterBankStage] samplesPerChannel=" << samplesPerChannel
                  << ", bands=" << m_definitions.size() << std::endl;
        size_t numBands = m_definitions.size();
        size_t totalOutputChannels = numChannels * numBands;

        outputSize = samplesPerChannel * totalOutputChannels;

        // 1. Ensure scratch buffers are properly sized (hot path optimized)
        std::cout << "[FilterBankStage] Calling ensureScratchSize..." << std::endl;
        ensureScratchSize(samplesPerChannel);
        std::cout << "[FilterBankStage] Scratch buffers ready" << std::endl;

        // 2. De-interleave Input (Convert Interleaved → Planar)
        // Use vector of pointers for SimdOps API compatibility
        std::cout << "[FilterBankStage] Preparing input pointers..." << std::endl;
        std::vector<float *> inputPtrs(numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            inputPtrs[i] = m_planarInput[i].data();
            if (inputPtrs[i] == nullptr)
            {
                std::cerr << "[FilterBankStage] ERROR: inputPtrs[" << i << "] is nullptr!" << std::endl;
            }
        }

        std::cout << "[FilterBankStage] De-interleaving (numChannels=" << numChannels << ")..." << std::endl;
        if (numChannels == 2)
        {
            // Specialized SIMD path for stereo
            std::cout << "[FilterBankStage] Using stereo deinterleave" << std::endl;
            dsp::simd::deinterleave2Ch(inputBuffer, inputPtrs[0], inputPtrs[1], samplesPerChannel);
        }
        else
        {
            // Generic N-channel path
            std::cout << "[FilterBankStage] Using N-channel deinterleave" << std::endl;
            dsp::simd::deinterleaveNCh(inputBuffer, inputPtrs.data(), numChannels, samplesPerChannel);
        }
        std::cout << "[FilterBankStage] De-interleave complete" << std::endl;

        std::cout << "[FilterBankStage] De-interleave complete" << std::endl;

        // 3. Process Filters (Planar → Planar)
        // Cache-efficient: Memory is contiguous for each channel's planar data
        std::cout << "[FilterBankStage] Processing filters (channels=" << numChannels
                  << ", bands=" << numBands << ")..." << std::endl;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::cout << "[FilterBankStage] Processing channel " << ch << std::endl;
            for (size_t band = 0; band < numBands; ++band)
            {
                // Calculate linear output index in channel-major order:
                // Channel 0: [Band 0, Band 1, ..., Band M-1]
                // Channel 1: [Band 0, Band 1, ..., Band M-1]
                int outChIndex = (ch * static_cast<int>(numBands)) + static_cast<int>(band);

                // Validate pointers before filter processing
                if (m_planarInput[ch].data() == nullptr)
                {
                    std::cerr << "[FilterBankStage] ERROR: m_planarInput[" << ch << "] is nullptr!" << std::endl;
                }
                if (m_planarOutput[outChIndex].data() == nullptr)
                {
                    std::cerr << "[FilterBankStage] ERROR: m_planarOutput[" << outChIndex << "] is nullptr!" << std::endl;
                }

                // Process filter on contiguous planar data
                m_filters[ch][band]->process(
                    m_planarInput[ch].data(),
                    m_planarOutput[outChIndex].data(),
                    samplesPerChannel);
            }
        }
        std::cout << "[FilterBankStage] Filter processing complete" << std::endl;

        std::cout << "[FilterBankStage] Filter processing complete" << std::endl;

        // 4. Interleave Output (Convert Planar → Interleaved)
        std::cout << "[FilterBankStage] Preparing output pointers (totalOutputChannels="
                  << totalOutputChannels << ")..." << std::endl;
        std::vector<const float *> outputPtrs(totalOutputChannels);
        for (size_t i = 0; i < totalOutputChannels; ++i)
        {
            outputPtrs[i] = m_planarOutput[i].data();
            if (outputPtrs[i] == nullptr)
            {
                std::cerr << "[FilterBankStage] ERROR: outputPtrs[" << i << "] is nullptr!" << std::endl;
            }
        }

        std::cout << "[FilterBankStage] Interleaving output..." << std::endl;
        dsp::simd::interleaveNCh(outputPtrs.data(), outputBuffer, static_cast<int>(totalOutputChannels), samplesPerChannel);
        std::cout << "[FilterBankStage] processResizing complete, outputSize=" << outputSize << std::endl;
    }

    void FilterBankStage::initializeFilters()
    {
        m_filters.resize(m_numInputChannels);
        for (int ch = 0; ch < m_numInputChannels; ++ch)
        {
            m_filters[ch].clear();
            for (const auto &def : m_definitions)
            {
                // Convert double coefficients to float for IirFilter
                std::vector<float> b_float(def.b.begin(), def.b.end());
                std::vector<float> a_float(def.a.begin(), def.a.end());

                // Create stateful IIR filter for each band
                m_filters[ch].push_back(
                    std::make_unique<dsp::core::IirFilter<float>>(b_float, a_float, true));
            }
        }
    }

    void FilterBankStage::ensureScratchSize(size_t samplesPerChannel)
    {
        std::cout << "[FilterBankStage] ensureScratchSize: samplesPerChannel=" << samplesPerChannel << std::endl;

        // Ensure input buffers exist and are large enough
        if (m_planarInput.size() != static_cast<size_t>(m_numInputChannels))
        {
            std::cout << "[FilterBankStage] Resizing planarInput from " << m_planarInput.size()
                      << " to " << m_numInputChannels << std::endl;
            m_planarInput.resize(m_numInputChannels);
        }
        for (size_t i = 0; i < m_planarInput.size(); ++i)
        {
            if (m_planarInput[i].size() < samplesPerChannel)
            {
                std::cout << "[FilterBankStage] Resizing planarInput[" << i << "] from "
                          << m_planarInput[i].size() << " to " << samplesPerChannel << std::endl;
                m_planarInput[i].resize(samplesPerChannel, 0.0f); // Initialize with zeros
            }
        }

        // Ensure output buffers exist and are large enough
        size_t numOut = m_numInputChannels * m_definitions.size();
        std::cout << "[FilterBankStage] Output channels needed: " << numOut << std::endl;

        if (m_planarOutput.size() != numOut)
        {
            std::cout << "[FilterBankStage] Resizing planarOutput from " << m_planarOutput.size()
                      << " to " << numOut << std::endl;
            m_planarOutput.resize(numOut);
        }
        for (size_t i = 0; i < m_planarOutput.size(); ++i)
        {
            if (m_planarOutput[i].size() < samplesPerChannel)
            {
                std::cout << "[FilterBankStage] Resizing planarOutput[" << i << "] from "
                          << m_planarOutput[i].size() << " to " << samplesPerChannel << std::endl;
                m_planarOutput[i].resize(samplesPerChannel, 0.0f); // Initialize with zeros
            }
        }
        std::cout << "[FilterBankStage] ensureScratchSize complete" << std::endl;
    }

    void FilterBankStage::reset()
    {
        for (auto &ch : m_filters)
        {
            for (auto &filter : ch)
            {
                filter->reset();
            }
        }
    }

    Napi::Object FilterBankStage::serializeState(Napi::Env env) const
    {
        Napi::Object state = Napi::Object::New(env);
        state.Set("type", getType());
        state.Set("numInputChannels", m_numInputChannels);
        state.Set("numBands", m_definitions.size());

        // Serialize filter states as nested arrays: channels[bands[state]]
        Napi::Array channels = Napi::Array::New(env, m_filters.size());
        for (size_t ch = 0; ch < m_filters.size(); ++ch)
        {
            Napi::Array bands = Napi::Array::New(env, m_filters[ch].size());
            for (size_t band = 0; band < m_filters[ch].size(); ++band)
            {
                const auto &filter = m_filters[ch][band];

                Napi::Object filterState = Napi::Object::New(env);

                // Get filter state (input and output history)
                auto [inputHistory, outputHistory] = filter->getState();

                // Serialize input history
                Napi::Array inputArray = Napi::Array::New(env, inputHistory.size());
                for (size_t i = 0; i < inputHistory.size(); ++i)
                {
                    inputArray.Set(i, inputHistory[i]);
                }
                filterState.Set("inputHistory", inputArray);

                // Serialize output history
                Napi::Array outputArray = Napi::Array::New(env, outputHistory.size());
                for (size_t i = 0; i < outputHistory.size(); ++i)
                {
                    outputArray.Set(i, outputHistory[i]);
                }
                filterState.Set("outputHistory", outputArray);

                bands.Set(band, filterState);
            }
            channels.Set(ch, bands);
        }
        state.Set("filterStates", channels);

        return state;
    }

    void FilterBankStage::deserializeState(const Napi::Object &state)
    {
        if (!state.Has("filterStates"))
        {
            throw std::runtime_error("FilterBank: Missing filterStates in serialized data");
        }

        Napi::Array channels = state.Get("filterStates").As<Napi::Array>();
        if (channels.Length() != m_filters.size())
        {
            throw std::runtime_error("FilterBank: Channel count mismatch in deserialization");
        }

        for (size_t ch = 0; ch < channels.Length(); ++ch)
        {
            Napi::Array bands = channels.Get(ch).As<Napi::Array>();
            if (bands.Length() != m_filters[ch].size())
            {
                throw std::runtime_error("FilterBank: Band count mismatch in deserialization");
            }

            for (size_t band = 0; band < bands.Length(); ++band)
            {
                Napi::Object filterState = bands.Get(band).As<Napi::Object>();

                // Deserialize input history
                std::vector<float> inputHistory;
                if (filterState.Has("inputHistory"))
                {
                    Napi::Array inputArray = filterState.Get("inputHistory").As<Napi::Array>();
                    inputHistory.reserve(inputArray.Length());
                    for (size_t i = 0; i < inputArray.Length(); ++i)
                    {
                        inputHistory.push_back(inputArray.Get(i).As<Napi::Number>().FloatValue());
                    }
                }

                // Deserialize output history
                std::vector<float> outputHistory;
                if (filterState.Has("outputHistory"))
                {
                    Napi::Array outputArray = filterState.Get("outputHistory").As<Napi::Array>();
                    outputHistory.reserve(outputArray.Length());
                    for (size_t i = 0; i < outputArray.Length(); ++i)
                    {
                        outputHistory.push_back(outputArray.Get(i).As<Napi::Number>().FloatValue());
                    }
                }

                // Restore filter state
                m_filters[ch][band]->setState(inputHistory, outputHistory);
            }
        }
    }

    void FilterBankStage::serializeToon(toon::Serializer &serializer) const
    {
        // Serialize configuration
        serializer.writeInt32(m_numInputChannels);
        serializer.writeInt32(static_cast<int32_t>(m_definitions.size()));

        // Serialize each filter's state
        for (const auto &ch : m_filters)
        {
            for (const auto &filter : ch)
            {
                // Get filter state (input and output history)
                auto [inputHistory, outputHistory] = filter->getState();

                // Write input history
                serializer.writeInt32(static_cast<int32_t>(inputHistory.size()));
                for (float val : inputHistory)
                {
                    serializer.writeFloat(val);
                }

                // Write output history
                serializer.writeInt32(static_cast<int32_t>(outputHistory.size()));
                for (float val : outputHistory)
                {
                    serializer.writeFloat(val);
                }
            }
        }
    }

    void FilterBankStage::deserializeToon(toon::Deserializer &deserializer)
    {
        // Read and validate configuration
        int numChannels = deserializer.readInt32();
        int32_t numBands = deserializer.readInt32();

        if (numChannels != m_numInputChannels)
        {
            throw std::runtime_error("FilterBank TOON: Channel count mismatch");
        }
        if (static_cast<size_t>(numBands) != m_definitions.size())
        {
            throw std::runtime_error("FilterBank TOON: Band count mismatch");
        }

        // Restore each filter's state
        for (auto &ch : m_filters)
        {
            for (auto &filter : ch)
            {
                // Read input history
                int32_t inputSize = deserializer.readInt32();
                std::vector<float> inputHistory;
                inputHistory.reserve(inputSize);
                for (int32_t i = 0; i < inputSize; ++i)
                {
                    inputHistory.push_back(deserializer.readFloat());
                }

                // Read output history
                int32_t outputSize = deserializer.readInt32();
                std::vector<float> outputHistory;
                outputHistory.reserve(outputSize);
                for (int32_t i = 0; i < outputSize; ++i)
                {
                    outputHistory.push_back(deserializer.readFloat());
                }

                // Restore filter state
                filter->setState(inputHistory, outputHistory);
            }
        }
    }

} // namespace dsp::adapters
