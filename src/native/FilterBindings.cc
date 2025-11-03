/**
 * N-API Bindings for FIR and IIR Filters
 */

#include <napi.h>
#include "core/FirFilter.h"
#include "core/IirFilter.h"
#include "core/DifferentiableFilter.h"
#include "utils/NapiUtils.h"
#include <memory>

namespace dsp
{
    // ========== FIR Filter Bindings ==========

    class FirFilterWrapper : public Napi::ObjectWrap<FirFilterWrapper>
    {
    public:
        static inline Napi::FunctionReference constructor;

        static Napi::Object Init(Napi::Env env, Napi::Object exports)
        {
            Napi::Function func = DefineClass(env, "FirFilter", {
                                                                    InstanceMethod("processSample", &FirFilterWrapper::ProcessSample),
                                                                    InstanceMethod("process", &FirFilterWrapper::Process),
                                                                    InstanceMethod("reset", &FirFilterWrapper::Reset),
                                                                    InstanceMethod("getOrder", &FirFilterWrapper::GetOrder),
                                                                    InstanceMethod("getCoefficients", &FirFilterWrapper::GetCoefficients),
                                                                    InstanceMethod("setCoefficients", &FirFilterWrapper::SetCoefficients),
                                                                    InstanceMethod("isStateful", &FirFilterWrapper::IsStateful),
                                                                    StaticMethod("createLowPass", &FirFilterWrapper::CreateLowPass),
                                                                    StaticMethod("createHighPass", &FirFilterWrapper::CreateHighPass),
                                                                    StaticMethod("createBandPass", &FirFilterWrapper::CreateBandPass),
                                                                    StaticMethod("createBandStop", &FirFilterWrapper::CreateBandStop),
                                                                });

            constructor = Napi::Persistent(func);
            constructor.SuppressDestruct();

            exports.Set("FirFilter", func);
            return exports;
        }

        FirFilterWrapper(const Napi::CallbackInfo &info) : Napi::ObjectWrap<FirFilterWrapper>(info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsArray())
            {
                Napi::TypeError::New(env, "Expected coefficients array").ThrowAsJavaScriptException();
                return;
            }

            Napi::Array coeffsArray = info[0].As<Napi::Array>();
            std::vector<float> coeffs;

            for (uint32_t i = 0; i < coeffsArray.Length(); ++i)
            {
                Napi::Value val = coeffsArray[i];
                if (val.IsNumber())
                {
                    coeffs.push_back(val.As<Napi::Number>().FloatValue());
                }
            }

            bool stateful = true;
            if (info.Length() >= 2 && info[1].IsBoolean())
            {
                stateful = info[1].As<Napi::Boolean>().Value();
            }

            m_filter = std::make_unique<core::FirFilter<float>>(coeffs, stateful);
        }

    private:
        std::unique_ptr<core::FirFilter<float>> m_filter;

        Napi::Value ProcessSample(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected number").ThrowAsJavaScriptException();
                return env.Null();
            }

            float input = info[0].As<Napi::Number>().FloatValue();
            float output = m_filter->processSample(input);

            return Napi::Number::New(env, output);
        }

        Napi::Value Process(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsTypedArray())
            {
                Napi::TypeError::New(env, "Expected Float32Array").ThrowAsJavaScriptException();
                return env.Null();
            }

            Napi::Float32Array inputArray = info[0].As<Napi::Float32Array>();
            size_t length = inputArray.ElementLength();

            bool stateless = false;
            if (info.Length() >= 2 && info[1].IsBoolean())
            {
                stateless = info[1].As<Napi::Boolean>().Value();
            }

            Napi::Float32Array outputArray = Napi::Float32Array::New(env, length);

            m_filter->process(inputArray.Data(), outputArray.Data(), length, stateless);

            return outputArray;
        }

        Napi::Value Reset(const Napi::CallbackInfo &info)
        {
            m_filter->reset();
            return info.Env().Undefined();
        }

        Napi::Value GetOrder(const Napi::CallbackInfo &info)
        {
            return Napi::Number::New(info.Env(), m_filter->getOrder());
        }

        Napi::Value GetCoefficients(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();
            const auto &coeffs = m_filter->getCoefficients();

            Napi::Array result = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                result[i] = Napi::Number::New(env, coeffs[i]);
            }

            return result;
        }

        Napi::Value SetCoefficients(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsArray())
            {
                Napi::TypeError::New(env, "Expected coefficients array").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            Napi::Array coeffsArray = info[0].As<Napi::Array>();
            std::vector<float> coeffs;

            for (uint32_t i = 0; i < coeffsArray.Length(); ++i)
            {
                Napi::Value val = coeffsArray[i];
                if (val.IsNumber())
                {
                    coeffs.push_back(val.As<Napi::Number>().FloatValue());
                }
            }

            m_filter->setCoefficients(coeffs);
            return env.Undefined();
        }

        Napi::Value IsStateful(const Napi::CallbackInfo &info)
        {
            return Napi::Boolean::New(info.Env(), m_filter->isStateful());
        }

        static Napi::Value CreateLowPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber())
            {
                Napi::TypeError::New(env, "Expected cutoffFreq and numTaps").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            size_t numTaps = info[1].As<Napi::Number>().Uint32Value();

            std::string windowType = "hamming";
            if (info.Length() >= 3 && info[2].IsString())
            {
                windowType = info[2].As<Napi::String>().Utf8Value();
            }

            auto filter = core::FirFilter<float>::createLowPass(cutoffFreq, numTaps, windowType);
            auto coeffs = filter.getCoefficients();

            Napi::Array coeffsArray = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                coeffsArray[i] = Napi::Number::New(env, coeffs[i]);
            }

            return constructor.New({coeffsArray});
        }

        static Napi::Value CreateHighPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber())
            {
                Napi::TypeError::New(env, "Expected cutoffFreq and numTaps").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            size_t numTaps = info[1].As<Napi::Number>().Uint32Value();

            std::string windowType = "hamming";
            if (info.Length() >= 3 && info[2].IsString())
            {
                windowType = info[2].As<Napi::String>().Utf8Value();
            }

            auto filter = core::FirFilter<float>::createHighPass(cutoffFreq, numTaps, windowType);
            auto coeffs = filter.getCoefficients();

            Napi::Array coeffsArray = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                coeffsArray[i] = Napi::Number::New(env, coeffs[i]);
            }

            return constructor.New({coeffsArray});
        }

        static Napi::Value CreateBandPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 3)
            {
                Napi::TypeError::New(env, "Expected lowCutoff, highCutoff, numTaps").ThrowAsJavaScriptException();
                return env.Null();
            }

            float lowCutoff = info[0].As<Napi::Number>().FloatValue();
            float highCutoff = info[1].As<Napi::Number>().FloatValue();
            size_t numTaps = info[2].As<Napi::Number>().Uint32Value();

            std::string windowType = "hamming";
            if (info.Length() >= 4 && info[3].IsString())
            {
                windowType = info[3].As<Napi::String>().Utf8Value();
            }

            auto filter = core::FirFilter<float>::createBandPass(lowCutoff, highCutoff, numTaps, windowType);
            auto coeffs = filter.getCoefficients();

            Napi::Array coeffsArray = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                coeffsArray[i] = Napi::Number::New(env, coeffs[i]);
            }

            return constructor.New({coeffsArray});
        }

        static Napi::Value CreateBandStop(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 3)
            {
                Napi::TypeError::New(env, "Expected lowCutoff, highCutoff, numTaps").ThrowAsJavaScriptException();
                return env.Null();
            }

            float lowCutoff = info[0].As<Napi::Number>().FloatValue();
            float highCutoff = info[1].As<Napi::Number>().FloatValue();
            size_t numTaps = info[2].As<Napi::Number>().Uint32Value();

            std::string windowType = "hamming";
            if (info.Length() >= 4 && info[3].IsString())
            {
                windowType = info[3].As<Napi::String>().Utf8Value();
            }

            auto filter = core::FirFilter<float>::createBandStop(lowCutoff, highCutoff, numTaps, windowType);
            auto coeffs = filter.getCoefficients();

            Napi::Array coeffsArray = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                coeffsArray[i] = Napi::Number::New(env, coeffs[i]);
            }

            return constructor.New({coeffsArray});
        }
    };

    // ========== IIR Filter Bindings ==========

    class IirFilterWrapper : public Napi::ObjectWrap<IirFilterWrapper>
    {
    public:
        static inline Napi::FunctionReference constructor;

        static Napi::Object Init(Napi::Env env, Napi::Object exports)
        {
            Napi::Function func = DefineClass(env, "IirFilter", {
                                                                    InstanceMethod("processSample", &IirFilterWrapper::ProcessSample),
                                                                    InstanceMethod("process", &IirFilterWrapper::Process),
                                                                    InstanceMethod("reset", &IirFilterWrapper::Reset),
                                                                    InstanceMethod("getFeedforwardOrder", &IirFilterWrapper::GetFeedforwardOrder),
                                                                    InstanceMethod("getFeedbackOrder", &IirFilterWrapper::GetFeedbackOrder),
                                                                    InstanceMethod("getOrder", &IirFilterWrapper::GetOrder),
                                                                    InstanceMethod("getBCoefficients", &IirFilterWrapper::GetBCoefficients),
                                                                    InstanceMethod("getACoefficients", &IirFilterWrapper::GetACoefficients),
                                                                    InstanceMethod("setCoefficients", &IirFilterWrapper::SetCoefficients),
                                                                    InstanceMethod("isStateful", &IirFilterWrapper::IsStateful),
                                                                    InstanceMethod("isStable", &IirFilterWrapper::IsStable),
                                                                    StaticMethod("createFirstOrderLowPass", &IirFilterWrapper::CreateFirstOrderLowPass),
                                                                    StaticMethod("createFirstOrderHighPass", &IirFilterWrapper::CreateFirstOrderHighPass),
                                                                    StaticMethod("createButterworthLowPass", &IirFilterWrapper::CreateButterworthLowPass),
                                                                    StaticMethod("createButterworthHighPass", &IirFilterWrapper::CreateButterworthHighPass),
                                                                    StaticMethod("createButterworthBandPass", &IirFilterWrapper::CreateButterworthBandPass),
                                                                    StaticMethod("createBiquad", &IirFilterWrapper::CreateBiquad),
                                                                    StaticMethod("createChebyshevLowPass", &IirFilterWrapper::CreateChebyshevLowPass),
                                                                    StaticMethod("createChebyshevHighPass", &IirFilterWrapper::CreateChebyshevHighPass),
                                                                    StaticMethod("createChebyshevBandPass", &IirFilterWrapper::CreateChebyshevBandPass),
                                                                    StaticMethod("createPeakingEQ", &IirFilterWrapper::CreatePeakingEQ),
                                                                    StaticMethod("createLowShelf", &IirFilterWrapper::CreateLowShelf),
                                                                    StaticMethod("createHighShelf", &IirFilterWrapper::CreateHighShelf),
                                                                });

            constructor = Napi::Persistent(func);
            constructor.SuppressDestruct();

            exports.Set("IirFilter", func);
            return exports;
        }
        IirFilterWrapper(const Napi::CallbackInfo &info) : Napi::ObjectWrap<IirFilterWrapper>(info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2 || !info[0].IsArray() || !info[1].IsArray())
            {
                Napi::TypeError::New(env, "Expected b_coeffs and a_coeffs arrays").ThrowAsJavaScriptException();
                return;
            }

            Napi::Array bArray = info[0].As<Napi::Array>();
            Napi::Array aArray = info[1].As<Napi::Array>();

            std::vector<float> b_coeffs, a_coeffs;

            for (uint32_t i = 0; i < bArray.Length(); ++i)
            {
                b_coeffs.push_back(bArray.Get(i).As<Napi::Number>().FloatValue());
            }

            for (uint32_t i = 0; i < aArray.Length(); ++i)
            {
                a_coeffs.push_back(aArray.Get(i).As<Napi::Number>().FloatValue());
            }

            bool stateful = true;
            if (info.Length() >= 3 && info[2].IsBoolean())
            {
                stateful = info[2].As<Napi::Boolean>().Value();
            }

            m_filter = std::make_unique<core::IirFilter<float>>(b_coeffs, a_coeffs, stateful);
        }

    private:
        std::unique_ptr<core::IirFilter<float>> m_filter;

        Napi::Value ProcessSample(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected number").ThrowAsJavaScriptException();
                return env.Null();
            }

            float input = info[0].As<Napi::Number>().FloatValue();
            float output = m_filter->processSample(input);

            return Napi::Number::New(env, output);
        }

        Napi::Value Process(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsTypedArray())
            {
                Napi::TypeError::New(env, "Expected Float32Array").ThrowAsJavaScriptException();
                return env.Null();
            }

            Napi::Float32Array inputArray = info[0].As<Napi::Float32Array>();
            size_t length = inputArray.ElementLength();

            bool stateless = false;
            if (info.Length() >= 2 && info[1].IsBoolean())
            {
                stateless = info[1].As<Napi::Boolean>().Value();
            }

            Napi::Float32Array outputArray = Napi::Float32Array::New(env, length);

            m_filter->process(inputArray.Data(), outputArray.Data(), length, stateless);

            return outputArray;
        }

        Napi::Value Reset(const Napi::CallbackInfo &info)
        {
            m_filter->reset();
            return info.Env().Undefined();
        }

        Napi::Value GetFeedforwardOrder(const Napi::CallbackInfo &info)
        {
            return Napi::Number::New(info.Env(), m_filter->getFeedforwardOrder());
        }

        Napi::Value GetFeedbackOrder(const Napi::CallbackInfo &info)
        {
            return Napi::Number::New(info.Env(), m_filter->getFeedbackOrder());
        }

        Napi::Value GetOrder(const Napi::CallbackInfo &info)
        {
            // Return the maximum of feedforward and feedback orders
            size_t order = std::max(m_filter->getFeedforwardOrder(), m_filter->getFeedbackOrder());
            return Napi::Number::New(info.Env(), order);
        }

        Napi::Value GetBCoefficients(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();
            const auto &coeffs = m_filter->getBCoefficients();

            Napi::Array result = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                result[i] = Napi::Number::New(env, coeffs[i]);
            }

            return result;
        }

        Napi::Value GetACoefficients(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();
            const auto &coeffs = m_filter->getACoefficients();

            Napi::Array result = Napi::Array::New(env, coeffs.size());
            for (size_t i = 0; i < coeffs.size(); ++i)
            {
                result[i] = Napi::Number::New(env, coeffs[i]);
            }

            return result;
        }

        Napi::Value SetCoefficients(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2 || !info[0].IsArray() || !info[1].IsArray())
            {
                Napi::TypeError::New(env, "Expected b_coeffs and a_coeffs arrays").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            Napi::Array bArray = info[0].As<Napi::Array>();
            Napi::Array aArray = info[1].As<Napi::Array>();

            std::vector<float> b_coeffs, a_coeffs;

            for (uint32_t i = 0; i < bArray.Length(); ++i)
            {
                b_coeffs.push_back(bArray.Get(i).As<Napi::Number>().FloatValue());
            }

            for (uint32_t i = 0; i < aArray.Length(); ++i)
            {
                a_coeffs.push_back(aArray.Get(i).As<Napi::Number>().FloatValue());
            }

            m_filter->setCoefficients(b_coeffs, a_coeffs);
            return env.Undefined();
        }

        Napi::Value IsStateful(const Napi::CallbackInfo &info)
        {
            return Napi::Boolean::New(info.Env(), m_filter->isStateful());
        }

        Napi::Value IsStable(const Napi::CallbackInfo &info)
        {
            return Napi::Boolean::New(info.Env(), m_filter->isStable());
        }

        // Static factory methods
        static Napi::Value CreateFirstOrderLowPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected cutoffFreq").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            auto filter = core::IirFilter<float>::createFirstOrderLowPass(cutoffFreq);

            auto b_coeffs = filter.getBCoefficients();
            auto a_coeffs = filter.getACoefficients();

            Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
            Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

            for (size_t i = 0; i < b_coeffs.size(); ++i)
            {
                bArray[i] = Napi::Number::New(env, b_coeffs[i]);
            }

            for (size_t i = 0; i < a_coeffs.size(); ++i)
            {
                aArray[i] = Napi::Number::New(env, a_coeffs[i]);
            }

            return constructor.New({bArray, aArray});
        }

        static Napi::Value CreateFirstOrderHighPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected cutoffFreq").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            auto filter = core::IirFilter<float>::createFirstOrderHighPass(cutoffFreq);

            auto b_coeffs = filter.getBCoefficients();
            auto a_coeffs = filter.getACoefficients();

            Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
            Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

            for (size_t i = 0; i < b_coeffs.size(); ++i)
            {
                bArray[i] = Napi::Number::New(env, b_coeffs[i]);
            }

            for (size_t i = 0; i < a_coeffs.size(); ++i)
            {
                aArray[i] = Napi::Number::New(env, a_coeffs[i]);
            }

            return constructor.New({bArray, aArray});
        }

        static Napi::Value CreateButterworthLowPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected cutoffFreq and order").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            int order = info[1].As<Napi::Number>().Int32Value();

            auto filter = core::IirFilter<float>::createButterworthLowPass(cutoffFreq, order);

            auto b_coeffs = filter.getBCoefficients();
            auto a_coeffs = filter.getACoefficients();

            Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
            Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

            for (size_t i = 0; i < b_coeffs.size(); ++i)
            {
                bArray[i] = Napi::Number::New(env, b_coeffs[i]);
            }

            for (size_t i = 0; i < a_coeffs.size(); ++i)
            {
                aArray[i] = Napi::Number::New(env, a_coeffs[i]);
            }

            return constructor.New({bArray, aArray});
        }

        static Napi::Value CreateButterworthHighPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected cutoffFreq and order").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            int order = info[1].As<Napi::Number>().Int32Value();

            auto filter = core::IirFilter<float>::createButterworthHighPass(cutoffFreq, order);

            auto b_coeffs = filter.getBCoefficients();
            auto a_coeffs = filter.getACoefficients();

            Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
            Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

            for (size_t i = 0; i < b_coeffs.size(); ++i)
            {
                bArray[i] = Napi::Number::New(env, b_coeffs[i]);
            }

            for (size_t i = 0; i < a_coeffs.size(); ++i)
            {
                aArray[i] = Napi::Number::New(env, a_coeffs[i]);
            }

            return constructor.New({bArray, aArray});
        }

        static Napi::Value CreateButterworthBandPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 3)
            {
                Napi::TypeError::New(env, "Expected lowCutoff, highCutoff, order").ThrowAsJavaScriptException();
                return env.Null();
            }

            float lowCutoff = info[0].As<Napi::Number>().FloatValue();
            float highCutoff = info[1].As<Napi::Number>().FloatValue();
            int order = info[2].As<Napi::Number>().Int32Value();

            auto filter = core::IirFilter<float>::createButterworthBandPass(lowCutoff, highCutoff, order);

            auto b_coeffs = filter.getBCoefficients();
            auto a_coeffs = filter.getACoefficients();

            Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
            Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

            for (size_t i = 0; i < b_coeffs.size(); ++i)
            {
                bArray[i] = Napi::Number::New(env, b_coeffs[i]);
            }

            for (size_t i = 0; i < a_coeffs.size(); ++i)
            {
                aArray[i] = Napi::Number::New(env, a_coeffs[i]);
            }

            return constructor.New({bArray, aArray});
        }

        static Napi::Value CreateBiquad(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 5)
            {
                Napi::TypeError::New(env, "Expected b0, b1, b2, a1, a2").ThrowAsJavaScriptException();
                return env.Null();
            }

            float b0 = info[0].As<Napi::Number>().FloatValue();
            float b1 = info[1].As<Napi::Number>().FloatValue();
            float b2 = info[2].As<Napi::Number>().FloatValue();
            float a1 = info[3].As<Napi::Number>().FloatValue();
            float a2 = info[4].As<Napi::Number>().FloatValue();

            auto filter = core::IirFilter<float>::createBiquad(b0, b1, b2, a1, a2);

            auto b_coeffs = filter.getBCoefficients();
            auto a_coeffs = filter.getACoefficients();

            Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
            Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

            for (size_t i = 0; i < b_coeffs.size(); ++i)
            {
                bArray[i] = Napi::Number::New(env, b_coeffs[i]);
            }

            for (size_t i = 0; i < a_coeffs.size(); ++i)
            {
                aArray[i] = Napi::Number::New(env, a_coeffs[i]);
            }

            return constructor.New({bArray, aArray});
        }

        static Napi::Value CreateChebyshevLowPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected cutoffFreq, order, and optional rippleDb").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            int order = info[1].As<Napi::Number>().Int32Value();
            float rippleDb = 0.5f; // Default ripple

            if (info.Length() >= 3 && info[2].IsNumber())
            {
                rippleDb = info[2].As<Napi::Number>().FloatValue();
            }

            try
            {
                auto filter = core::IirFilter<float>::createChebyshevLowPass(cutoffFreq, order, rippleDb);

                auto b_coeffs = filter.getBCoefficients();
                auto a_coeffs = filter.getACoefficients();

                Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
                Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

                for (size_t i = 0; i < b_coeffs.size(); ++i)
                {
                    bArray[i] = Napi::Number::New(env, b_coeffs[i]);
                }

                for (size_t i = 0; i < a_coeffs.size(); ++i)
                {
                    aArray[i] = Napi::Number::New(env, a_coeffs[i]);
                }

                return constructor.New({bArray, aArray});
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        static Napi::Value CreateChebyshevHighPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected cutoffFreq, order, and optional rippleDb").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            int order = info[1].As<Napi::Number>().Int32Value();
            float rippleDb = 0.5f;

            if (info.Length() >= 3 && info[2].IsNumber())
            {
                rippleDb = info[2].As<Napi::Number>().FloatValue();
            }

            try
            {
                auto filter = core::IirFilter<float>::createChebyshevHighPass(cutoffFreq, order, rippleDb);

                auto b_coeffs = filter.getBCoefficients();
                auto a_coeffs = filter.getACoefficients();

                Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
                Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

                for (size_t i = 0; i < b_coeffs.size(); ++i)
                {
                    bArray[i] = Napi::Number::New(env, b_coeffs[i]);
                }

                for (size_t i = 0; i < a_coeffs.size(); ++i)
                {
                    aArray[i] = Napi::Number::New(env, a_coeffs[i]);
                }

                return constructor.New({bArray, aArray});
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        static Napi::Value CreateChebyshevBandPass(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 3)
            {
                Napi::TypeError::New(env, "Expected lowCutoff, highCutoff, order, and optional rippleDb").ThrowAsJavaScriptException();
                return env.Null();
            }

            float lowCutoff = info[0].As<Napi::Number>().FloatValue();
            float highCutoff = info[1].As<Napi::Number>().FloatValue();
            int order = info[2].As<Napi::Number>().Int32Value();
            float rippleDb = 0.5f;

            if (info.Length() >= 4 && info[3].IsNumber())
            {
                rippleDb = info[3].As<Napi::Number>().FloatValue();
            }

            try
            {
                auto filter = core::IirFilter<float>::createChebyshevBandPass(lowCutoff, highCutoff, order, rippleDb);

                auto b_coeffs = filter.getBCoefficients();
                auto a_coeffs = filter.getACoefficients();

                Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
                Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

                for (size_t i = 0; i < b_coeffs.size(); ++i)
                {
                    bArray[i] = Napi::Number::New(env, b_coeffs[i]);
                }

                for (size_t i = 0; i < a_coeffs.size(); ++i)
                {
                    aArray[i] = Napi::Number::New(env, a_coeffs[i]);
                }

                return constructor.New({bArray, aArray});
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        static Napi::Value CreatePeakingEQ(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 3)
            {
                Napi::TypeError::New(env, "Expected centerFreq, Q, and gainDb").ThrowAsJavaScriptException();
                return env.Null();
            }

            float centerFreq = info[0].As<Napi::Number>().FloatValue();
            float Q = info[1].As<Napi::Number>().FloatValue();
            float gainDb = info[2].As<Napi::Number>().FloatValue();

            try
            {
                auto filter = core::IirFilter<float>::createPeakingEQ(centerFreq, Q, gainDb);

                auto b_coeffs = filter.getBCoefficients();
                auto a_coeffs = filter.getACoefficients();

                Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
                Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

                for (size_t i = 0; i < b_coeffs.size(); ++i)
                {
                    bArray[i] = Napi::Number::New(env, b_coeffs[i]);
                }

                for (size_t i = 0; i < a_coeffs.size(); ++i)
                {
                    aArray[i] = Napi::Number::New(env, a_coeffs[i]);
                }

                return constructor.New({bArray, aArray});
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        static Napi::Value CreateLowShelf(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected cutoffFreq, gainDb, and optional Q").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            float gainDb = info[1].As<Napi::Number>().FloatValue();
            float Q = 0.707f; // Default Q

            if (info.Length() >= 3 && info[2].IsNumber())
            {
                Q = info[2].As<Napi::Number>().FloatValue();
            }

            try
            {
                auto filter = core::IirFilter<float>::createLowShelf(cutoffFreq, gainDb, Q);

                auto b_coeffs = filter.getBCoefficients();
                auto a_coeffs = filter.getACoefficients();

                Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
                Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

                for (size_t i = 0; i < b_coeffs.size(); ++i)
                {
                    bArray[i] = Napi::Number::New(env, b_coeffs[i]);
                }

                for (size_t i = 0; i < a_coeffs.size(); ++i)
                {
                    aArray[i] = Napi::Number::New(env, a_coeffs[i]);
                }

                return constructor.New({bArray, aArray});
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        static Napi::Value CreateHighShelf(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected cutoffFreq, gainDb, and optional Q").ThrowAsJavaScriptException();
                return env.Null();
            }

            float cutoffFreq = info[0].As<Napi::Number>().FloatValue();
            float gainDb = info[1].As<Napi::Number>().FloatValue();
            float Q = 0.707f;

            if (info.Length() >= 3 && info[2].IsNumber())
            {
                Q = info[2].As<Napi::Number>().FloatValue();
            }

            try
            {
                auto filter = core::IirFilter<float>::createHighShelf(cutoffFreq, gainDb, Q);

                auto b_coeffs = filter.getBCoefficients();
                auto a_coeffs = filter.getACoefficients();

                Napi::Array bArray = Napi::Array::New(env, b_coeffs.size());
                Napi::Array aArray = Napi::Array::New(env, a_coeffs.size());

                for (size_t i = 0; i < b_coeffs.size(); ++i)
                {
                    bArray[i] = Napi::Number::New(env, b_coeffs[i]);
                }

                for (size_t i = 0; i < a_coeffs.size(); ++i)
                {
                    aArray[i] = Napi::Number::New(env, a_coeffs[i]);
                }

                return constructor.New({bArray, aArray});
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }
    };

    // ========== Differentiable Filter Bindings ==========

    class DifferentiableFilterWrapper : public Napi::ObjectWrap<DifferentiableFilterWrapper>
    {
    public:
        static inline Napi::FunctionReference constructor;

        static Napi::Object Init(Napi::Env env, Napi::Object exports)
        {
            Napi::Function func = DefineClass(env, "DifferentiableFilter", {
                                                                               InstanceMethod("init", &DifferentiableFilterWrapper::Init_),
                                                                               InstanceMethod("process", &DifferentiableFilterWrapper::Process),
                                                                               InstanceMethod("filter", &DifferentiableFilterWrapper::Filter),
                                                                               InstanceMethod("reset", &DifferentiableFilterWrapper::Reset),
                                                                               InstanceMethod("getWeights", &DifferentiableFilterWrapper::GetWeights),
                                                                               InstanceMethod("setWeights", &DifferentiableFilterWrapper::SetWeights),
                                                                               InstanceMethod("setLearningRate", &DifferentiableFilterWrapper::SetLearningRate),
                                                                               InstanceMethod("getLearningRate", &DifferentiableFilterWrapper::GetLearningRate),
                                                                               InstanceMethod("getNumTaps", &DifferentiableFilterWrapper::GetNumTaps),
                                                                           });

            constructor = Napi::Persistent(func);
            constructor.SuppressDestruct();

            exports.Set("DifferentiableFilter", func);
            return exports;
        }

        DifferentiableFilterWrapper(const Napi::CallbackInfo &info) : Napi::ObjectWrap<DifferentiableFilterWrapper>(info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected numTaps (number)").ThrowAsJavaScriptException();
                return;
            }

            size_t numTaps = info[0].As<Napi::Number>().Uint32Value();

            float mu = 0.01f;
            if (info.Length() >= 2 && info[1].IsNumber())
            {
                mu = info[1].As<Napi::Number>().FloatValue();
            }

            float lambda = 0.0f;
            if (info.Length() >= 3 && info[2].IsNumber())
            {
                lambda = info[2].As<Napi::Number>().FloatValue();
            }

            bool normalized = false;
            if (info.Length() >= 4 && info[3].IsBoolean())
            {
                normalized = info[3].As<Napi::Boolean>().Value();
            }

            try
            {
                m_filter = std::make_unique<core::DifferentiableFilter<float>>(numTaps, mu, lambda, normalized);
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            }
        }

    private:
        std::unique_ptr<core::DifferentiableFilter<float>> m_filter;

        Napi::Value Init_(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected numChannels (number)").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            size_t numChannels = info[0].As<Napi::Number>().Uint32Value();
            m_filter->init(numChannels);

            return env.Undefined();
        }

        Napi::Value Process(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            // Arguments: input, desired, output, error, adapt
            if (info.Length() < 4)
            {
                Napi::TypeError::New(env, "Expected 4-5 arguments: input, desired, output, error, [adapt]").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            if (!info[0].IsTypedArray() || !info[1].IsTypedArray() ||
                !info[2].IsTypedArray() || !info[3].IsTypedArray())
            {
                Napi::TypeError::New(env, "All arrays must be Float32Array").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            auto inputArray = info[0].As<Napi::Float32Array>();
            auto desiredArray = info[1].As<Napi::Float32Array>();
            auto outputArray = info[2].As<Napi::Float32Array>();
            auto errorArray = info[3].As<Napi::Float32Array>();

            bool adapt = true;
            if (info.Length() >= 5 && info[4].IsBoolean())
            {
                adapt = info[4].As<Napi::Boolean>().Value();
            }

            size_t numSamples = inputArray.ElementLength() / m_filter->getNumChannels();

            try
            {
                m_filter->process(
                    inputArray.Data(),
                    desiredArray.Data(),
                    outputArray.Data(),
                    errorArray.Data(),
                    numSamples,
                    adapt);
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            }

            return env.Undefined();
        }

        Napi::Value Filter(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2 || !info[0].IsTypedArray() || !info[1].IsTypedArray())
            {
                Napi::TypeError::New(env, "Expected input and output Float32Arrays").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            auto inputArray = info[0].As<Napi::Float32Array>();
            auto outputArray = info[1].As<Napi::Float32Array>();

            size_t numSamples = inputArray.ElementLength() / m_filter->getNumChannels();

            try
            {
                m_filter->filter(inputArray.Data(), outputArray.Data(), numSamples);
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            }

            return env.Undefined();
        }

        Napi::Value Reset(const Napi::CallbackInfo &info)
        {
            m_filter->reset();
            return info.Env().Undefined();
        }

        Napi::Value GetWeights(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected channel (number)").ThrowAsJavaScriptException();
                return env.Null();
            }

            size_t channel = info[0].As<Napi::Number>().Uint32Value();

            try
            {
                const auto &weights = m_filter->getWeights(channel);
                Napi::Float32Array result = Napi::Float32Array::New(env, weights.size());

                for (size_t i = 0; i < weights.size(); ++i)
                {
                    result[i] = weights[i];
                }

                return result;
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        Napi::Value SetWeights(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsTypedArray())
            {
                Napi::TypeError::New(env, "Expected channel (number) and weights (Float32Array)").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            size_t channel = info[0].As<Napi::Number>().Uint32Value();
            auto weightsArray = info[1].As<Napi::Float32Array>();

            std::vector<float> weights(weightsArray.ElementLength());
            for (size_t i = 0; i < weights.size(); ++i)
            {
                weights[i] = weightsArray[i];
            }

            try
            {
                m_filter->setWeights(channel, weights);
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            }

            return env.Undefined();
        }

        Napi::Value SetLearningRate(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            if (info.Length() < 1 || !info[0].IsNumber())
            {
                Napi::TypeError::New(env, "Expected mu (number)").ThrowAsJavaScriptException();
                return env.Undefined();
            }

            float mu = info[0].As<Napi::Number>().FloatValue();

            try
            {
                m_filter->setLearningRate(mu);
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            }

            return env.Undefined();
        }

        Napi::Value GetLearningRate(const Napi::CallbackInfo &info)
        {
            return Napi::Number::New(info.Env(), m_filter->getLearningRate());
        }

        Napi::Value GetNumTaps(const Napi::CallbackInfo &info)
        {
            return Napi::Number::New(info.Env(), m_filter->getNumTaps());
        }
    };

    // ============================================================================
    // Module initialization
    void InitFilterBindings(Napi::Env env, Napi::Object exports)
    {
        FirFilterWrapper::Init(env, exports);
        IirFilterWrapper::Init(env, exports);
        DifferentiableFilterWrapper::Init(env, exports);
    }

} // namespace dsp
