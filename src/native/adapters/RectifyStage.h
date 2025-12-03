#pragma once
#include "../IDspStage.h"
#include "../utils/SimdOps.h"
#include "../utils/Toon.h"
#include <cmath>
#include <stdexcept>

namespace dsp::adapters
{
    enum class RectifyMode
    {
        FullWave,
        HalfWave
    };

    class RectifyStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a new Rectify Stage.
         * @param mode The rectification mode (FULL_WAVE or HALF_WAVE).
         */
        explicit RectifyStage(RectifyMode mode = RectifyMode::FullWave)
            : m_mode(mode) {}

        // Delete copy/move semantics
        RectifyStage(const RectifyStage &) = delete;
        RectifyStage &operator=(const RectifyStage &) = delete;
        RectifyStage(RectifyStage &&) noexcept = delete;
        RectifyStage &operator=(RectifyStage &&) noexcept = delete;

        /**
         * @brief Returns the type identifier of this stage.
         * @return A string identifying the stage type ("rectify").
         */
        const char *getType() const override
        {
            return "rectify";
        }

        /**
         * @brief Applies in-place rectification based on the configured mode.
         * Uses SIMD-optimized operations for better performance.
         */
        void process(float *buffer, size_t numSamples, int /*numChannels*/, const float * /*timestamps*/ = nullptr) override
        {
            // Use SIMD-optimized operations for best performance
            switch (m_mode)
            {
            case RectifyMode::FullWave:
                dsp::simd::abs_inplace(buffer, numSamples);
                break;
            case RectifyMode::HalfWave:
                dsp::simd::max_zero_inplace(buffer, numSamples);
                break;
            }
        }

        /**
         * @brief Serializes the stage's configured mode.
         */
        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);
            state.Set("type", "rectify");
            state.Set("mode", m_mode == RectifyMode::FullWave ? "full" : "half");
            return state;
        }

        /**
         * @brief Deserializes and restores the stage's configured mode.
         */
        void deserializeState(const Napi::Object &state) override
        {
            std::string mode = state.Get("mode").As<Napi::String>().Utf8Value();
            if (mode == "full")
                m_mode = RectifyMode::FullWave;
            else if (mode == "half")
                m_mode = RectifyMode::HalfWave;
            else
                throw std::runtime_error("Invalid rectify mode");
        }

        inline void serializeToon(dsp::toon::Serializer &s) const override
        {
            s.writeInt32(static_cast<int32_t>(m_mode));
        }

        inline void deserializeToon(dsp::toon::Deserializer &d) override
        {
            int32_t mode_int = d.readInt32();
            if (mode_int < 0 || mode_int > 1)
                throw std::runtime_error("Invalid mode in RectifyStage deserialization");
            m_mode = static_cast<RectifyMode>(mode_int);
        }

        void reset() override {} // No internal buffers

    private:
        RectifyMode m_mode;
    };
} // namespace dsp::adapters
