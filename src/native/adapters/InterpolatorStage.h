/**
 * InterpolatorStage.h
 *
 * Polyphase FIR interpolator for efficient upsampling by integer factor L.
 * Implements anti-imaging filter to remove spectral images above Fs_in/2.
 */

#pragma once

#include "../IDspStage.h"
#include "../utils/Toon.h"
#include <vector>
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    class InterpolatorStage : public IDspStage
    {
    public:
        InterpolatorStage(int factor, int order, double sampleRate)
            : interpolationFactor_(factor), filterOrder_(order), sampleRate_(sampleRate),
              numChannels_(0)
        {
            if (interpolationFactor_ < 2)
            {
                throw std::invalid_argument("Interpolation factor must be >= 2");
            }
            if (filterOrder_ < 3 || filterOrder_ % 2 == 0)
            {
                throw std::invalid_argument("Filter order must be odd and >= 3");
            }

            // Design anti-imaging filter (low-pass at Fs_in / 2)
            designLowPassFilter();
        }

        const char *getType() const override
        {
            return "interpolate";
        }

        bool isResizing() const override
        {
            return true;
        }

        double getTimeScaleFactor() const override
        {
            // Interpolation slows down time (stretches signal)
            // Output has MORE samples, so timestamps are scaled DOWN
            return 1.0 / static_cast<double>(interpolationFactor_);
        }

        size_t calculateOutputSize(size_t inputSize) const override
        {
            return inputSize * interpolationFactor_;
        }

        void processResizing(const float *inputBuffer, size_t inputSize,
                             float *outputBuffer, size_t &outputSize,
                             int numChannels, const float *timestamps = nullptr) override
        {
            // Initialize state buffers for new channel configuration if needed
            if (numChannels_ != numChannels)
            {
                initializeStateBuffers(numChannels);
            }

            size_t inputSamplesPerChannel = inputSize / numChannels;
            size_t outputSamplesPerChannel = inputSamplesPerChannel * interpolationFactor_;
            outputSize = outputSamplesPerChannel * numChannels;

            // Process each channel independently
            for (int ch = 0; ch < numChannels; ++ch)
            {
                processChannel(inputBuffer, inputSamplesPerChannel,
                               outputBuffer, outputSamplesPerChannel,
                               ch, numChannels);
            }
        }

        void process(float *buffer, size_t numSamples, int numChannels = 1,
                     const float *timestamps = nullptr) override
        {
            throw std::runtime_error("InterpolatorStage: Should use processResizing, not in-place process");
        }

        void reset() override
        {
            for (auto &buf : stateBuffers_)
            {
                std::fill(buf.begin(), buf.end(), 0.0f);
            }
            std::fill(stateIndices_.begin(), stateIndices_.end(), 0);
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("type", "interpolate");
            state.Set("factor", interpolationFactor_);
            state.Set("order", filterOrder_);
            state.Set("sampleRate", sampleRate_);

            // Serialize state buffers
            Napi::Array stateBuffersArray = Napi::Array::New(env, stateBuffers_.size());
            for (size_t ch = 0; ch < stateBuffers_.size(); ++ch)
            {
                Napi::Array channelBuffer = Napi::Array::New(env, stateBuffers_[ch].size());
                for (size_t i = 0; i < stateBuffers_[ch].size(); ++i)
                {
                    channelBuffer.Set(i, stateBuffers_[ch][i]);
                }
                stateBuffersArray.Set(ch, channelBuffer);
            }
            state.Set("stateBuffers", stateBuffersArray);

            Napi::Array stateIndicesArray = Napi::Array::New(env, stateIndices_.size());
            for (size_t ch = 0; ch < stateIndices_.size(); ++ch)
            {
                stateIndicesArray.Set(ch, static_cast<uint32_t>(stateIndices_[ch]));
            }
            state.Set("stateIndices", stateIndicesArray);

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("stateBuffers"))
            {
                Napi::Array stateBuffersArray = state.Get("stateBuffers").As<Napi::Array>();
                for (size_t ch = 0; ch < stateBuffersArray.Length() && ch < stateBuffers_.size(); ++ch)
                {
                    Napi::Array channelBuffer = stateBuffersArray.Get(ch).As<Napi::Array>();
                    for (size_t i = 0; i < channelBuffer.Length() && i < stateBuffers_[ch].size(); ++i)
                    {
                        stateBuffers_[ch][i] = channelBuffer.Get(i).As<Napi::Number>().FloatValue();
                    }
                }
            }

            if (state.Has("stateIndices"))
            {
                Napi::Array stateIndicesArray = state.Get("stateIndices").As<Napi::Array>();
                for (size_t ch = 0; ch < stateIndicesArray.Length() && ch < stateIndices_.size(); ++ch)
                {
                    stateIndices_[ch] = stateIndicesArray.Get(ch).As<Napi::Number>().Uint32Value();
                }
            }
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.writeInt32(interpolationFactor_);
            s.writeInt32(filterOrder_);
            s.writeDouble(sampleRate_);
            s.writeInt32(numChannels_);

            // Serialize state buffers for each channel
            for (const auto &buf : stateBuffers_)
            {
                s.writeFloatArray(buf);
            }

            // Serialize state indices
            for (size_t idx : stateIndices_)
            {
                s.writeInt32(static_cast<int32_t>(idx));
            }
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            int32_t factor = d.readInt32();
            int32_t order = d.readInt32();
            double sr = d.readDouble();
            int32_t nCh = d.readInt32();

            if (factor != interpolationFactor_ || order != filterOrder_)
            {
                throw std::runtime_error("Interpolator TOON: parameter mismatch");
            }

            if (nCh != numChannels_)
            {
                initializeStateBuffers(nCh);
            }

            // Deserialize state buffers
            for (auto &buf : stateBuffers_)
            {
                buf = d.readFloatArray();
            }

            // Deserialize state indices
            for (size_t &idx : stateIndices_)
            {
                idx = static_cast<size_t>(d.readInt32());
            }
        }

    private:
        int interpolationFactor_;
        int filterOrder_;
        double sampleRate_;
        int numChannels_;

        std::vector<float> filterCoeffs_;
        std::vector<std::vector<float>> stateBuffers_; // One per channel
        std::vector<size_t> stateIndices_;             // One per channel

        void initializeStateBuffers(int numChannels)
        {
            numChannels_ = numChannels;
            stateBuffers_.resize(numChannels);
            stateIndices_.resize(numChannels);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                stateBuffers_[ch].resize(filterOrder_, 0.0f);
                stateIndices_[ch] = 0;
            }
        }

        void designLowPassFilter()
        {
            // Design windowed-sinc FIR anti-imaging filter
            // Cutoff at Fs_in / 2 (same frequency as input Nyquist)
            // The upsampling inserts zeros, so we need to filter out images above Fs_in/2

            filterCoeffs_.resize(filterOrder_);
            int M = filterOrder_ / 2;

            // Cutoff frequency normalized to output sample rate
            // Original Nyquist: sampleRate_ / 2
            // New sample rate: sampleRate_ * interpolationFactor_
            // Normalized cutoff: (sampleRate_ / 2) / (sampleRate_ * interpolationFactor_) = 1 / (2 * interpolationFactor_)
            double fc = 1.0 / (2.0 * interpolationFactor_);

            for (int n = 0; n < filterOrder_; ++n)
            {
                int nMinusM = n - M;

                // Sinc function
                double sinc;
                if (nMinusM == 0)
                {
                    sinc = 2.0 * fc;
                }
                else
                {
                    double x = 2.0 * M_PI * fc * nMinusM;
                    sinc = std::sin(x) / (M_PI * nMinusM);
                }

                // Hamming window
                double window = 0.54 - 0.46 * std::cos(2.0 * M_PI * n / (filterOrder_ - 1));

                // Apply gain correction for interpolation (L samples out per 1 in)
                filterCoeffs_[n] = static_cast<float>(sinc * window * interpolationFactor_);
            }
        }

        void processChannel(const float *inputBuffer, size_t inputSamplesPerChannel,
                            float *outputBuffer, size_t outputSamplesPerChannel,
                            int channel, int numChannels)
        {
            auto &state = stateBuffers_[channel];
            size_t &stateIdx = stateIndices_[channel];

            size_t outIdx = 0;

            // Process each input sample
            for (size_t i = 0; i < inputSamplesPerChannel; ++i)
            {
                // Get input sample (interleaved format)
                float inputSample = inputBuffer[i * numChannels + channel];

                // Insert input sample into state buffer
                state[stateIdx] = inputSample;
                stateIdx = (stateIdx + 1) % filterOrder_;

                // Generate L output samples for this input sample
                for (int phase = 0; phase < interpolationFactor_; ++phase)
                {
                    float output = 0.0f;

                    // Apply polyphase filter
                    // Only use coefficients at positions where upsampled signal would have non-zero values
                    for (int k = 0; k < filterOrder_; ++k)
                    {
                        // Which phase does this coefficient correspond to?
                        // For interpolation: insert (L-1) zeros between samples
                        // Polyphase decomposition: filter[k] applies to input at position floor(k/L)

                        if (k % interpolationFactor_ == phase)
                        {
                            // This coefficient contributes to this phase
                            size_t statePosition = (stateIdx + filterOrder_ - 1 - k / interpolationFactor_) % filterOrder_;
                            output += filterCoeffs_[k] * state[statePosition];
                        }
                    }

                    // Write output sample (interleaved format)
                    outputBuffer[outIdx * numChannels + channel] = output;
                    outIdx++;
                }
            }
        }
    };

} // namespace dsp
