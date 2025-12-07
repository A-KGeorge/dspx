#pragma once

#include "../IDspStage.h"
#include "../utils/Toon.h"
#include "../utils/SimdOps.h"
#include <stdexcept>
#include <string>
#include <cmath>

namespace dsp::adapters
{
    /**
     * @brief Square Stage - Computes element-wise squaring of a signal.
     *
     * Implements: y[n] = x[n]^2
     *
     * **Use Cases:**
     * - Energy calculation (signal power)
     * - Non-linear signal transformation
     * - Envelope detection
     * - Part of Pan-Tompkins QRS detection algorithm
     *
     * **Note:** Squaring amplifies large values and suppresses small ones.
     * This stage is stateless - no mode selection needed.
     */
    class SquareStage : public IDspStage
    {
    public:
        SquareStage()
        {
            // No parameters needed - stateless operation
        }

        const char *getType() const override
        {
            return "square";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            // Stateless squaring operation
            dsp::simd::square_inplace(buffer, numSamples);
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            // No state to serialize - squaring is stateless
            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            // No state to deserialize
        }

        void reset() override
        {
            // No state to reset
        }

        void serializeToon(dsp::toon::Serializer &s) const override
        {
            // No state to serialize
        }

        void deserializeToon(dsp::toon::Deserializer &d) override
        {
            // No state to deserialize
        }

        bool isResizing() const override { return false; }

    private:
        // No member variables - stateless operation
    };

} // namespace dsp::adapters
