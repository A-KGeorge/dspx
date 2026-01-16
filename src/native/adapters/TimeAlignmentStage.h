/**
 * TimeAlignmentStage.h
 *
 * Production-grade irregular timestamp resampling stage.
 * Converts irregularly-sampled data to uniform time grid using true time-based interpolation.
 *
 * Key features:
 * - Time-based coordinate system (not index-based)
 * - Gap detection and configurable policies
 * - Clock drift compensation
 * - Multiple interpolation methods
 * - SIMD-optimized search and interpolation
 */

#pragma once

#include "../IDspStage.h"
#include <vector>
#include <string>

namespace dsp
{
    namespace adapters
    {
        /**
         * Gap detection and handling policy
         */
        enum class GapPolicy
        {
            ERROR,       // Throw exception when gap detected
            ZERO_FILL,   // Fill gaps with zeros
            HOLD,        // Hold last valid value
            INTERPOLATE, // Linear interpolation across gap
            EXTRAPOLATE  // Extrapolate beyond last sample
        };

        /**
         * Interpolation method for regular samples
         */
        enum class InterpolationMethod
        {
            LINEAR, // Linear interpolation (fast, C0 continuous)
            CUBIC,  // Cubic spline (smooth, C1 continuous)
            SINC    // Windowed sinc (ideal lowpass, band-limited)
        };

        /**
         * Clock drift compensation method
         */
        enum class DriftCompensation
        {
            NONE,       // Use provided targetSampleRate as-is
            REGRESSION, // Linear regression over input timestamps
            PLL         // Phase-locked loop (exponential moving average)
        };

        /**
         * TimeAlignmentStage: Production-grade irregular timestamp resampling
         *
         * This stage solves the problems identified in Gemini's analysis:
         * 1. Time-based coordinate system (not index-based)
         * 2. Gap detection and handling policies
         * 3. Clock drift compensation
         * 4. Proper SIMD optimization for irregular data
         * 5. Configurable extrapolation/error handling
         *
         * Usage:
         *   auto stage = TimeAlignmentStage(
         *       100.0f,                           // targetSampleRate = 100 Hz
         *       InterpolationMethod::LINEAR,      // interpolation method
         *       GapPolicy::INTERPOLATE,           // gap policy
         *       2.0f,                             // gapThreshold (2x expected interval)
         *       DriftCompensation::REGRESSION     // drift compensation
         *   );
         */
        class TimeAlignmentStage : public IDspStage
        {
        public:
            /**
             * Constructor
             * @param targetSampleRate Target uniform sample rate in Hz (e.g., 100.0 = 100 Hz)
             * @param interpMethod Interpolation method for regular samples
             * @param gapPolicy How to handle detected gaps (missing samples)
             * @param gapThreshold Gap detection threshold (multiplier of expected interval)
             * @param driftComp Clock drift compensation method
             */
            TimeAlignmentStage(
                float targetSampleRate,
                InterpolationMethod interpMethod = InterpolationMethod::LINEAR,
                GapPolicy gapPolicy = GapPolicy::INTERPOLATE,
                float gapThreshold = 2.0f,
                DriftCompensation driftComp = DriftCompensation::NONE);

            /**
             * Process irregular data → uniform grid
             * This is a resizing stage, so we override processResizing instead of process
             */
            void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
            {
                // TimeAlignmentStage must use processResizing - this should not be called
                throw std::runtime_error("TimeAlignmentStage::process() called - use processResizing()");
            }

            void processResizing(
                const float *inputBuffer,
                size_t inputSize,
                float *outputBuffer,
                size_t &outputSize,
                int numChannels,
                const float *timestamps = nullptr) override;

            void reset() override;

            // IDspStage serialization interface - Napi::Object version
            Napi::Object serializeState(Napi::Env env) const override;
            void deserializeState(const Napi::Object &state) override;

            // TOON binary serialization
            void serializeToon(toon::Serializer &serializer) const override;
            void deserializeToon(toon::Deserializer &deserializer) override;

            // IDspStage resizing interface
            bool isResizing() const override;
            size_t calculateOutputSize(size_t inputSize) const override;
            double getTimeScaleFactor() const override;
            const char *getType() const override { return "timeAlignment"; }

            /**
             * Get statistics from last processing
             */
            struct Statistics
            {
                size_t inputSamples;       // Input sample count
                size_t outputSamples;      // Output sample count
                size_t gapsDetected;       // Number of gaps detected
                float estimatedSampleRate; // Estimated input sample rate (Hz)
                float timeSpanMs;          // Total time span processed (ms)
                float minGapDurationMs;    // Smallest gap detected (ms)
                float maxGapDurationMs;    // Largest gap detected (ms)
                float avgIntervalMs;       // Average input interval (ms)
                float stdDevIntervalMs;    // Std dev of input intervals (ms)
            };

            Statistics getStatistics() const { return m_stats; }

        private:
            // Configuration
            float m_targetSampleRate;
            InterpolationMethod m_interpMethod;
            GapPolicy m_gapPolicy;
            float m_gapThreshold;
            DriftCompensation m_driftComp;

            // Statistics
            Statistics m_stats;

            // Drift compensation state
            float m_estimatedSampleRate;
            size_t m_driftWindowSize;

            // Cached values from last processing
            mutable double m_lastTimeScaleFactor;
            mutable float m_lastStartTime;
            mutable float m_lastEndTime;

            // Helper methods
            void estimateSampleRate(const float *timestamps, size_t numSamples, int channels);
            void detectGaps(const float *timestamps, size_t numSamples, int channels, std::vector<size_t> &gapIndices);
            float interpolate(float targetTime, const float *timestamps, const float *samples,
                              size_t numSamples, int channels, int channel, size_t &searchStart);
            void interpolateLinear(float targetTime, const float *timestamps, const float *samples,
                                   size_t numSamples, int channels, int channel, size_t &searchStart, float &output);
            void interpolateCubic(float targetTime, const float *timestamps, const float *samples,
                                  size_t numSamples, int channels, int channel, size_t &searchStart, float &output);
            void interpolateSinc(float targetTime, const float *timestamps, const float *samples,
                                 size_t numSamples, int channels, int channel, size_t &searchStart, float &output);

            // SIMD-optimized search
            size_t findBracketingInterval(float targetTime, const float *timestamps, size_t numSamples, int channels, size_t searchStart);
        };

    } // namespace adapters
} // namespace dsp
