#pragma once
#include "../IDspStage.h"
#include "../core/IirFilter.h"
#include "../utils/SimdOps.h"
#include <vector>
#include <memory>
#include <stdexcept>

namespace dsp::adapters
{
    /**
     * @brief Lightweight struct to pass filter coefficients from JavaScript
     */
    struct FilterDefinition
    {
        std::vector<double> b; // Feedforward coefficients
        std::vector<double> a; // Feedback coefficients
    };

    /**
     * @brief Filter Bank Stage - Splits N input channels into N × M sub-bands
     *
     * Architecture:
     * - Input: N channels (interleaved)
     * - Output: N × M channels (interleaved, channel-major layout)
     * - Layout: [ (Ch1_Band1, Ch1_Band2, ...), (Ch2_Band1, Ch2_Band2, ...) ]
     *
     * Optimization Strategy (Planar-Core):
     * 1. De-interleave input to planar scratch buffers (cache-efficient)
     * 2. Process filters on contiguous planar data (SIMD-friendly)
     * 3. Interleave output back to standard format
     *
     * Use Cases:
     * - Speech recognition: Mel-scale filter banks (20-40 bands)
     * - Audio compression: Bark-scale for psychoacoustic analysis
     * - Musical analysis: Octave bands (log scale)
     * - Research: Linear-scale frequency analysis
     *
     * @example
     * // 2 input channels → 20 output channels (10 bands per channel)
     * // Input:  [L, R, L, R, ...]
     * // Output: [L_B1, L_B2, ..., L_B10, R_B1, R_B2, ..., R_B10, ...]
     */
    class FilterBankStage : public dsp::IDspStage
    {
    public:
        /**
         * @brief Construct a filter bank stage
         * @param bandDefinitions Array of filter coefficient definitions (one per band)
         * @param numInputChannels Number of input channels
         */
        FilterBankStage(const std::vector<FilterDefinition> &bandDefinitions, int numInputChannels);

        /**
         * @brief Destructor - ensures proper cleanup of filters and scratch buffers
         */
        virtual ~FilterBankStage();

        const char *getType() const override { return "filterBank"; }

        /**
         * @brief This stage expands the buffer size (N channels → N×M channels)
         * @return Always true
         */
        bool isResizing() const override { return true; }

        /**
         * @brief Returns output channel count = inputChannels × numBands
         * @return Output channel count
         */
        int getOutputChannels() const override
        {
            return m_numInputChannels * static_cast<int>(m_definitions.size());
        }

        /**
         * @brief Calculate output buffer size for resizing
         * @param inputSize Input buffer size in samples
         * @return Output buffer size in samples
         */
        size_t calculateOutputSize(size_t inputSize) const override
        {
            return (inputSize / m_numInputChannels) * getOutputChannels();
        }

        /**
         * @brief Standard process() throws because this stage requires resizing
         */
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps) override
        {
            throw std::runtime_error("FilterBank requires processResizing");
        }

        /**
         * @brief Process audio data with buffer resizing
         *
         * Algorithm:
         * 1. De-interleave input to planar scratch buffers
         * 2. Apply each band's filter to each input channel (planar processing)
         * 3. Interleave results to output in channel-major order
         *
         * @param inputBuffer Source interleaved buffer
         * @param inputSize Input size in samples
         * @param outputBuffer Destination buffer (caller must allocate)
         * @param outputSize Reference to store actual output size
         * @param numChannels Number of input channels
         * @param timestamps Optional timestamp array
         */
        void processResizing(const float *inputBuffer, size_t inputSize,
                             float *outputBuffer, size_t &outputSize,
                             int numChannels, const float *timestamps) override;

        /**
         * @brief Reset all filter states to initial conditions
         */
        void reset() override;

        /**
         * @brief Serialize filter bank state to JavaScript object
         * @param env N-API environment
         * @return Napi::Object containing all filter states
         */
        Napi::Object serializeState(Napi::Env env) const override;

        /**
         * @brief Restore filter bank state from JavaScript object
         * @param state Napi::Object containing serialized filter states
         */
        void deserializeState(const Napi::Object &state) override;

        /**
         * @brief Serialize state to TOON binary format
         * @param serializer TOON serializer instance
         */
        void serializeToon(toon::Serializer &serializer) const override;

        /**
         * @brief Deserialize state from TOON binary format
         * @param deserializer TOON deserializer instance
         */
        void deserializeToon(toon::Deserializer &deserializer) override;

    private:
        /**
         * @brief Initialize the 2D matrix of IIR filters [channel][band]
         */
        void initializeFilters();

        /**
         * @brief Ensure scratch buffers are allocated and sized correctly
         * @param samplesPerChannel Number of samples per channel to process
         */
        void ensureScratchSize(size_t samplesPerChannel);

        // Configuration
        int m_numInputChannels;
        std::vector<FilterDefinition> m_definitions;

        // 2D Matrix of filters: m_filters[channelIndex][bandIndex]
        std::vector<std::vector<std::unique_ptr<dsp::core::IirFilter<float>>>> m_filters;

        // Optimization: Persistent scratch buffers in planar layout
        // m_planarInput[channel][sample] - de-interleaved input
        std::vector<std::vector<float>> m_planarInput;

        // m_planarOutput[flatOutputChannelIndex][sample] - filtered planar data
        std::vector<std::vector<float>> m_planarOutput;
    };

} // namespace dsp::adapters
