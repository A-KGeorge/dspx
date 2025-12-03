/**
 * DecimatorStage.h
 *
 * Polyphase FIR decimator for efficient downsampling by integer factor M.
 * Applies anti-aliasing low-pass filter and keeps only every M-th sample.
 *
 * Algorithm:
 * 1. Design anti-aliasing FIR filter with cutoff at π/M
 * 2. Use polyphase decomposition for efficiency (only computes needed outputs)
 * 3. Downsample by M (keep every M-th sample)
 * 4. Maintain state across process() calls for streaming
 *
 * Efficiency: Polyphase structure computes only the samples we keep,
 * reducing computation by factor of M.
 */

#pragma once

#include "../IDspStage.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{

    /**
     * Decimator stage: Downsample signal by integer factor M
     * Includes anti-aliasing FIR low-pass filter
     */
    class DecimatorStage : public IDspStage
    {
    public:
        /**
         * Construct decimator
         * @param factor Decimation factor M (output rate = input rate / M)
         * @param order FIR filter order (must be odd)
         * @param sampleRate Input sample rate in Hz
         */
        DecimatorStage(int factor, int order, double sampleRate)
            : decimationFactor_(factor), filterOrder_(order), sampleRate_(sampleRate), phaseIndex_(0), numChannels_(1)
        {
            if (factor < 2)
            {
                throw std::invalid_argument("Decimation factor must be >= 2");
            }
            if (order < 3 || order % 2 == 0)
            {
                throw std::invalid_argument("Filter order must be odd and >= 3");
            }

            // Design anti-aliasing filter: cutoff at Fs/(2*M) to prevent aliasing
            double outputSampleRate = sampleRate / factor;
            double cutoffFreq = outputSampleRate / 2.0; // Nyquist of output rate

            designLowPassFilter(cutoffFreq, sampleRate);

            // State buffer for polyphase filtering (will be resized for channels)
            initializeStateBuffers(1);
        }

        const char *getType() const override
        {
            return "decimate";
        }

        bool isResizing() const override
        {
            return true; // Decimation changes buffer size
        }

        double getTimeScaleFactor() const override
        {
            // Decimation speeds up time (compresses signal)
            // Output has FEWER samples, so timestamps are scaled UP
            return static_cast<double>(decimationFactor_);
        }

        size_t calculateOutputSize(size_t inputSize) const override
        {
            // Output size is approximately input size / M
            // Account for phase offset
            return (inputSize + phaseIndex_) / decimationFactor_;
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // This shouldn't be called for resizing stages
            throw std::runtime_error("DecimatorStage::process() should not be called - use processResizing()");
        }

        void processResizing(const float *inputBuffer, size_t inputSize,
                             float *outputBuffer, size_t &outputSize,
                             int numChannels, const float *timestamps = nullptr) override
        {
            outputSize = 0;

            if (inputSize == 0)
            {
                return;
            }

            // Initialize state buffers if channel count changed
            if (numChannels != numChannels_)
            {
                initializeStateBuffers(numChannels);
            }

            // inputSize is total buffer size (samples * channels)
            size_t samplesPerChannel = inputSize / numChannels;

            // Process each input sample frame
            size_t outputFrames = 0;
            for (size_t i = 0; i < samplesPerChannel; ++i)
            {
                // Increment phase counter
                phaseIndex_++;

                // Process each channel - add to state buffer
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    size_t inputIdx = i * numChannels + ch;
                    size_t channelStateOffset = ch * filterOrder_;
                    stateBuffer_[channelStateOffset + stateIndices_[ch]] = inputBuffer[inputIdx];
                    stateIndices_[ch] = (stateIndices_[ch] + 1) % filterOrder_;
                }

                // Only compute output every M samples (decimation)
                if (phaseIndex_ >= decimationFactor_)
                {
                    phaseIndex_ = 0;

                    // Generate output for each channel
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        size_t channelStateOffset = ch * filterOrder_;

                        // Apply FIR filter
                        float sum = 0.0f;
                        for (int tap = 0; tap < filterOrder_; ++tap)
                        {
                            int bufferIdx = (stateIndices_[ch] - 1 - tap + filterOrder_) % filterOrder_;
                            sum += stateBuffer_[channelStateOffset + bufferIdx] * polyphaseCoeffs_[tap];
                        }

                        outputBuffer[outputFrames * numChannels + ch] = sum;
                    }

                    outputFrames++;
                }
            }

            outputSize = outputFrames * numChannels;
        }

        void reset() override
        {
            std::fill(stateBuffer_.begin(), stateBuffer_.end(), 0.0f);
            std::fill(stateIndices_.begin(), stateIndices_.end(), 0);
            phaseIndex_ = 0;
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("type", "decimate");
            state.Set("factor", decimationFactor_);
            state.Set("order", filterOrder_);
            state.Set("sampleRate", sampleRate_);
            state.Set("phaseIndex", phaseIndex_);
            state.Set("numChannels", numChannels_);

            Napi::Array stateArray = Napi::Array::New(env, stateBuffer_.size());
            for (size_t i = 0; i < stateBuffer_.size(); ++i)
            {
                stateArray[i] = stateBuffer_[i];
            }
            state.Set("stateBuffer", stateArray);

            Napi::Array indicesArray = Napi::Array::New(env, stateIndices_.size());
            for (size_t i = 0; i < stateIndices_.size(); ++i)
            {
                indicesArray[i] = static_cast<double>(stateIndices_[i]);
            }
            state.Set("stateIndices", indicesArray);

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            phaseIndex_ = state.Get("phaseIndex").As<Napi::Number>().Int32Value();
            int numCh = state.Get("numChannels").As<Napi::Number>().Int32Value();

            if (numCh != numChannels_)
            {
                initializeStateBuffers(numCh);
            }

            Napi::Array stateArray = state.Get("stateBuffer").As<Napi::Array>();
            for (size_t i = 0; i < stateBuffer_.size() && i < stateArray.Length(); ++i)
            {
                stateBuffer_[i] = stateArray.Get(i).As<Napi::Number>().FloatValue();
            }

            Napi::Array indicesArray = state.Get("stateIndices").As<Napi::Array>();
            for (size_t i = 0; i < stateIndices_.size() && i < indicesArray.Length(); ++i)
            {
                stateIndices_[i] = indicesArray.Get(i).As<Napi::Number>().Uint32Value();
            }
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.startObject();

            s.writeString("factor");
            s.writeInt32(decimationFactor_);

            s.writeString("order");
            s.writeInt32(filterOrder_);

            s.writeString("sampleRate");
            s.writeDouble(sampleRate_);

            s.writeString("phaseIndex");
            s.writeInt32(phaseIndex_);

            s.writeString("numChannels");
            s.writeInt32(numChannels_);

            s.writeString("stateBuffer");
            s.writeFloatArray(stateBuffer_);

            s.writeString("stateIndices");
            s.startArray();
            for (size_t idx : stateIndices_)
            {
                s.writeInt32(static_cast<int32_t>(idx));
            }
            s.endArray();

            s.endObject();
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            d.consumeToken(dsp::toon::T_OBJECT_START);

            std::string key = d.readString(); // "factor"
            int factor = d.readInt32();
            if (factor != decimationFactor_)
            {
                throw std::runtime_error("Decimator factor mismatch during TOON deserialization");
            }

            key = d.readString(); // "order"
            int order = d.readInt32();
            if (order != filterOrder_)
            {
                throw std::runtime_error("Decimator order mismatch during TOON deserialization");
            }

            key = d.readString(); // "sampleRate"
            d.readDouble();       // Skip sampleRate

            key = d.readString(); // "phaseIndex"
            phaseIndex_ = d.readInt32();

            key = d.readString(); // "numChannels"
            int numCh = d.readInt32();

            if (numCh != numChannels_)
            {
                initializeStateBuffers(numCh);
            }

            key = d.readString(); // "stateBuffer"
            stateBuffer_ = d.readFloatArray();

            key = d.readString(); // "stateIndices"
            d.consumeToken(dsp::toon::T_ARRAY_START);
            stateIndices_.clear();
            while (d.peekToken() != dsp::toon::T_ARRAY_END)
            {
                stateIndices_.push_back(static_cast<size_t>(d.readInt32()));
            }
            d.consumeToken(dsp::toon::T_ARRAY_END);

            d.consumeToken(dsp::toon::T_OBJECT_END);
        }

        /**
         * Get decimation factor
         */
        int getFactor() const
        {
            return decimationFactor_;
        }

        /**
         * Get filter order
         */
        int getOrder() const
        {
            return filterOrder_;
        }

    private:
        /**
         * Initialize state buffers for the given number of channels
         */
        void initializeStateBuffers(int numChannels)
        {
            numChannels_ = numChannels;
            stateBuffer_.resize(filterOrder_ * numChannels, 0.0f);
            stateIndices_.resize(numChannels, 0);
        }

        /**
         * Design anti-aliasing low-pass filter using windowed sinc method
         */
        void designLowPassFilter(double cutoffFreq, double inputSampleRate)
        {
            const int M = filterOrder_;

            polyphaseCoeffs_.resize(M);

            const int center = M / 2;
            const double fc = cutoffFreq / inputSampleRate;
            const double omega_c = 2.0 * M_PI * fc;

            // Design prototype filter at input rate
            for (int n = 0; n < M; ++n)
            {
                // Sinc function
                double t = n - center;
                double sinc_val;
                if (std::abs(t) < 1e-10)
                {
                    sinc_val = omega_c / M_PI;
                }
                else
                {
                    sinc_val = std::sin(omega_c * t) / (M_PI * t);
                }

                // Hamming window
                double window = 0.54 - 0.46 * std::cos(2.0 * M_PI * n / (M - 1));

                polyphaseCoeffs_[n] = static_cast<float>(sinc_val * window);
            }

            // Normalize coefficients
            float sum = 0.0f;
            for (float c : polyphaseCoeffs_)
            {
                sum += c;
            }
            for (float &c : polyphaseCoeffs_)
            {
                c /= sum;
            }
        }

        int decimationFactor_;
        int filterOrder_;
        double sampleRate_;
        int numChannels_;

        std::vector<float> stateBuffer_;
        std::vector<float> polyphaseCoeffs_;
        std::vector<size_t> stateIndices_;
        int phaseIndex_;
    };

} // namespace dsp
