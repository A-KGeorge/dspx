/**
 * ResamplerStage.h
 *
 * Rational resampling by factor L/M for arbitrary sample rate conversion.
 * Combines polyphase interpolation (L) and decimation (M) with single anti-aliasing filter.
 */

#pragma once

#include "../IDspStage.h"
#include <vector>
#include <cmath>
#include <stdexcept>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dsp
{
    class ResamplerStage : public IDspStage
    {
    public:
        ResamplerStage(int upFactor, int downFactor, int order, double sampleRate)
            : upFactor_(upFactor), downFactor_(downFactor), filterOrder_(order),
              sampleRate_(sampleRate), numChannels_(0), phaseAccumulator_(0)
        {
            if (upFactor < 1)
            {
                throw std::invalid_argument("Interpolation factor L must be >= 1");
            }
            if (downFactor < 1)
            {
                throw std::invalid_argument("Decimation factor M must be >= 1");
            }
            if (filterOrder_ < 3 || filterOrder_ % 2 == 0)
            {
                throw std::invalid_argument("Filter order must be odd and >= 3");
            }

            // Reduce L/M to simplest form
            int gcd = std::gcd(upFactor_, downFactor_);
            upFactor_ /= gcd;
            downFactor_ /= gcd;

            // Design combined anti-aliasing/anti-imaging filter
            designLowPassFilter();
        }

        const char *getType() const override
        {
            return "resample";
        }

        bool isResizing() const override
        {
            return true;
        }

        double getTimeScaleFactor() const override
        {
            // Resampling adjusts time by the ratio of rates
            // time_scale = output_rate / input_rate = downFactor / upFactor
            return static_cast<double>(downFactor_) / static_cast<double>(upFactor_);
        }

        size_t calculateOutputSize(size_t inputSize) const override
        {
            // After interpolation: inputSize * upFactor_
            // After decimation: (inputSize * upFactor_ + phaseAccumulator_) / downFactor_
            // Conservative estimate (may be slightly larger than actual)
            return ((inputSize * upFactor_) / downFactor_) + 1;
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

            // Process each channel independently
            std::vector<size_t> channelOutputSizes(numChannels, 0);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                processChannel(inputBuffer, inputSamplesPerChannel,
                               outputBuffer, channelOutputSizes[ch],
                               ch, numChannels);
            }

            // All channels should produce the same number of samples
            outputSize = channelOutputSizes[0] * numChannels;
        }

        void process(float *buffer, size_t numSamples, int numChannels = 1,
                     const float *timestamps = nullptr) override
        {
            throw std::runtime_error("ResamplerStage: Should use processResizing, not in-place process");
        }

        void reset() override
        {
            for (auto &buf : stateBuffers_)
            {
                std::fill(buf.begin(), buf.end(), 0.0f);
            }
            std::fill(stateIndices_.begin(), stateIndices_.end(), 0);
            std::fill(phaseAccumulators_.begin(), phaseAccumulators_.end(), 0);
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("type", "resample");
            state.Set("upFactor", upFactor_);
            state.Set("downFactor", downFactor_);
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

            Napi::Array phaseAccumulatorsArray = Napi::Array::New(env, phaseAccumulators_.size());
            for (size_t ch = 0; ch < phaseAccumulators_.size(); ++ch)
            {
                phaseAccumulatorsArray.Set(ch, static_cast<uint32_t>(phaseAccumulators_[ch]));
            }
            state.Set("phaseAccumulators", phaseAccumulatorsArray);

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

            if (state.Has("phaseAccumulators"))
            {
                Napi::Array phaseAccumulatorsArray = state.Get("phaseAccumulators").As<Napi::Array>();
                for (size_t ch = 0; ch < phaseAccumulatorsArray.Length() && ch < phaseAccumulators_.size(); ++ch)
                {
                    phaseAccumulators_[ch] = phaseAccumulatorsArray.Get(ch).As<Napi::Number>().Uint32Value();
                }
            }
        }

    private:
        int upFactor_;
        int downFactor_;
        int filterOrder_;
        double sampleRate_;
        int numChannels_;
        size_t phaseAccumulator_;

        std::vector<float> filterCoeffs_;
        std::vector<std::vector<float>> stateBuffers_; // One per channel
        std::vector<size_t> stateIndices_;             // One per channel
        std::vector<size_t> phaseAccumulators_;        // One per channel for decimation phase

        void initializeStateBuffers(int numChannels)
        {
            numChannels_ = numChannels;
            stateBuffers_.resize(numChannels);
            stateIndices_.resize(numChannels);
            phaseAccumulators_.resize(numChannels);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                stateBuffers_[ch].resize(filterOrder_, 0.0f);
                stateIndices_[ch] = 0;
                phaseAccumulators_[ch] = 0;
            }
        }

        void designLowPassFilter()
        {
            // Design windowed-sinc FIR filter
            // Cutoff at min(Fs_in/2, Fs_out/2) to prevent both aliasing and imaging
            // After interpolation: Fs_intermediate = Fs_in * L
            // After decimation: Fs_out = Fs_intermediate / M = Fs_in * L / M
            //
            // Need to filter at: min(Fs_in/2, Fs_out/2)
            // = min(Fs_in/2, Fs_in * L / (2*M))
            // = Fs_in * min(1/2, L/(2*M))
            // = Fs_in * min(M, L) / (2*M)
            //
            // Normalized to Fs_intermediate:
            // fc = (Fs_in * min(M, L) / (2*M)) / (Fs_in * L)
            //    = min(M, L) / (2*M*L)

            filterCoeffs_.resize(filterOrder_);
            int M = filterOrder_ / 2;

            double fc = static_cast<double>(std::min(downFactor_, upFactor_)) / (2.0 * downFactor_ * upFactor_);

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

                // Apply gain correction for interpolation
                filterCoeffs_[n] = static_cast<float>(sinc * window * upFactor_);
            }
        }

        void processChannel(const float *inputBuffer, size_t inputSamplesPerChannel,
                            float *outputBuffer, size_t &outputSamplesPerChannel,
                            int channel, int numChannels)
        {
            auto &state = stateBuffers_[channel];
            size_t &stateIdx = stateIndices_[channel];
            size_t &phaseAcc = phaseAccumulators_[channel];

            size_t outIdx = 0;

            // Process each input sample
            for (size_t i = 0; i < inputSamplesPerChannel; ++i)
            {
                // Get input sample (interleaved format)
                float inputSample = inputBuffer[i * numChannels + channel];

                // Insert input sample into state buffer
                state[stateIdx] = inputSample;
                stateIdx = (stateIdx + 1) % filterOrder_;

                // Generate L interpolated samples
                for (int phase = 0; phase < upFactor_; ++phase)
                {
                    // Only output every M-th sample (combined interpolate + decimate)
                    if (phaseAcc % downFactor_ == 0)
                    {
                        float output = 0.0f;

                        // Apply polyphase filter for this phase
                        for (int k = 0; k < filterOrder_; ++k)
                        {
                            if (k % upFactor_ == phase)
                            {
                                // This coefficient contributes to this phase
                                size_t statePosition = (stateIdx + filterOrder_ - 1 - k / upFactor_) % filterOrder_;
                                output += filterCoeffs_[k] * state[statePosition];
                            }
                        }

                        // Write output sample (interleaved format)
                        outputBuffer[outIdx * numChannels + channel] = output;
                        outIdx++;
                    }

                    phaseAcc++;
                }
            }

            // Normalize phase accumulator to prevent overflow
            phaseAcc %= downFactor_;

            outputSamplesPerChannel = outIdx;
        }
    };

} // namespace dsp
