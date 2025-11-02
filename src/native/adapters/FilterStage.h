#pragma once

#include "../IDspStage.h"
#include "../core/IirFilter.h"
#include "../core/FirFilter.h"
#include <memory>
#include <vector>
#include <string>

namespace dsp::adapters
{

    class FilterStage : public dsp::IDspStage
    {
    public:
        FilterStage(const std::vector<double> &bCoeffs, const std::vector<double> &aCoeffs);
        ~FilterStage() override = default;

        const char *getType() const override;
        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps) override;
        void reset() override;

        Napi::Object serializeState(Napi::Env env) const override;
        void deserializeState(const Napi::Object &state) override;

    private:
        void initializeFilters(int numChannels);

        // Use variant or separate storage for FIR vs IIR
        bool m_isFir;
        std::vector<std::unique_ptr<dsp::core::FirFilter<float>>> m_firFilters;
        std::vector<std::unique_ptr<dsp::core::IirFilter<float>>> m_iirFilters;
        std::vector<double> m_bCoeffs;
        std::vector<double> m_aCoeffs;
        std::string m_typeStr;
    };

} // namespace dsp::adapters
