#pragma once
#include <napi.h>
#include <cstring> // for std::memcpy

namespace dsp
{
    // This abstract class is the key.
    // Every filter you add will implement this.
    class IDspStage
    {
    public:
        virtual ~IDspStage() = default;

        /**
         * @brief Returns the type identifier of this stage.
         *
         * @return A string identifying the stage type (e.g., "movingAverage", "notchFilter").
         */
        virtual const char *getType() const = 0;

        /**
         * @brief Returns true if this stage changes the buffer size (e.g., resampling).
         *        If true, the pipeline will call processResizing() instead of process().
         *
         * @return True if this stage resizes buffers, false for in-place processing.
         */
        virtual bool isResizing() const { return false; }

        /**
         * @brief Returns the time scale factor for timestamp adjustment (for resizing stages).
         *        - Factor > 1.0: time is stretched (interpolation)
         *        - Factor < 1.0: time is compressed (decimation)
         *        - Factor = 1.0: no time adjustment needed
         *
         * @return The time scale factor.
         */
        virtual double getTimeScaleFactor() const { return 1.0; }

        /**
         * @brief Calculates the output size for a given input size (for resizing stages only).
         *
         * @param inputSize The input buffer size in samples.
         * @return The output buffer size in samples.
         */
        virtual size_t calculateOutputSize(size_t inputSize) const { return inputSize; }

        /**
         * @brief Processes a chunk of audio data in-place.
         *
         * @param buffer The interleaved audio buffer.
         * @param numSamples The total number of samples (e.g., 1024).
         * @param numChannels The number of channels (e.g., 1, 2, 4).
         * @param timestamps Optional array of timestamps (in milliseconds) for each sample.
         *                   If nullptr, uses sample-based processing (legacy mode).
         *                   If provided, must have length equal to numSamples.
         */
        virtual void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) = 0;

        /**
         * @brief Processes data with buffer resizing (for resampling stages).
         *
         * @param inputBuffer The input buffer.
         * @param inputSize The input size in samples.
         * @param outputBuffer The output buffer (caller must allocate using calculateOutputSize).
         * @param outputSize Reference to store actual output size.
         * @param numChannels The number of channels.
         * @param timestamps Optional timestamps array.
         */
        virtual void processResizing(const float *inputBuffer, size_t inputSize,
                                     float *outputBuffer, size_t &outputSize,
                                     int numChannels, const float *timestamps = nullptr)
        {
            // Default implementation for non-resizing stages: just copy
            outputSize = inputSize;
            std::memcpy(outputBuffer, inputBuffer, inputSize * sizeof(float));
        }

        /**
         * @brief Serializes the stage's internal state to a Napi::Object.
         *
         * @param env The N-API environment for creating JavaScript objects.
         * @return Napi::Object containing the serialized state.
         */
        virtual Napi::Object serializeState(Napi::Env env) const = 0;

        /**
         * @brief Deserializes and restores the stage's internal state.
         *
         * @param state The Napi::Object containing the serialized state.
         */
        virtual void deserializeState(const Napi::Object &state) = 0;

        /**
         * @brief Resets the stage's internal state to initial values.
         */
        virtual void reset() = 0;
    };

} // namespace dsp