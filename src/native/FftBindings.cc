/**
 * N-API Bindings for FFT/DFT Engine
 *
 * Exposes all 8 transforms to TypeScript:
 * - fft, ifft, dft, idft (complex)
 * - rfft, irfft, rdft, irdft (real)
 *
 * Plus moving/batched FFT processing
 */

#include <napi.h>
#include "core/FftEngine.h"
#include "core/MovingFftFilter.h"
#include "utils/NapiUtils.h"
#include <memory>

namespace dsp
{

    using Complex = std::complex<float>;

    /**
     * FftProcessor - Wraps FftEngine for TypeScript
     */
    class FftProcessor : public Napi::ObjectWrap<FftProcessor>
    {
    public:
        static Napi::Object Init(Napi::Env env, Napi::Object exports);
        FftProcessor(const Napi::CallbackInfo &info);

    private:
        std::unique_ptr<core::FftEngine<float>> m_engine;
        size_t m_size;

        // TypeScript methods
        Napi::Value Fft(const Napi::CallbackInfo &info);
        Napi::Value Ifft(const Napi::CallbackInfo &info);
        Napi::Value Dft(const Napi::CallbackInfo &info);
        Napi::Value Idft(const Napi::CallbackInfo &info);
        Napi::Value Rfft(const Napi::CallbackInfo &info);
        Napi::Value Irfft(const Napi::CallbackInfo &info);
        Napi::Value Rdft(const Napi::CallbackInfo &info);
        Napi::Value Irdft(const Napi::CallbackInfo &info);

        Napi::Value GetSize(const Napi::CallbackInfo &info);
        Napi::Value GetHalfSize(const Napi::CallbackInfo &info);
        Napi::Value IsPowerOfTwo(const Napi::CallbackInfo &info);

        Napi::Value GetMagnitude(const Napi::CallbackInfo &info);
        Napi::Value GetPhase(const Napi::CallbackInfo &info);
        Napi::Value GetPower(const Napi::CallbackInfo &info);
    };

    /**
     * MovingFftProcessor - Wraps MovingFftFilter for TypeScript
     */
    class MovingFftProcessor : public Napi::ObjectWrap<MovingFftProcessor>
    {
    public:
        static Napi::Object Init(Napi::Env env, Napi::Object exports);
        MovingFftProcessor(const Napi::CallbackInfo &info);

    private:
        std::unique_ptr<core::MovingFftFilter<float>> m_filter;

        Napi::Value AddSample(const Napi::CallbackInfo &info);
        Napi::Value AddSamples(const Napi::CallbackInfo &info);
        Napi::Value ComputeSpectrum(const Napi::CallbackInfo &info);
        Napi::Value Reset(const Napi::CallbackInfo &info);

        Napi::Value GetFftSize(const Napi::CallbackInfo &info);
        Napi::Value GetSpectrumSize(const Napi::CallbackInfo &info);
        Napi::Value GetHopSize(const Napi::CallbackInfo &info);
        Napi::Value GetFillLevel(const Napi::CallbackInfo &info);
        Napi::Value IsReady(const Napi::CallbackInfo &info);

        Napi::Value SetWindowType(const Napi::CallbackInfo &info);
        Napi::Value GetMagnitudeSpectrum(const Napi::CallbackInfo &info);
        Napi::Value GetPowerSpectrum(const Napi::CallbackInfo &info);
        Napi::Value GetPhaseSpectrum(const Napi::CallbackInfo &info);
        Napi::Value GetFrequencyBins(const Napi::CallbackInfo &info);
    };

    // ========== FftProcessor Implementation ==========

    Napi::Object FftProcessor::Init(Napi::Env env, Napi::Object exports)
    {
        Napi::Function func = DefineClass(env, "FftProcessor", {
                                                                   InstanceMethod("fft", &FftProcessor::Fft),
                                                                   InstanceMethod("ifft", &FftProcessor::Ifft),
                                                                   InstanceMethod("dft", &FftProcessor::Dft),
                                                                   InstanceMethod("idft", &FftProcessor::Idft),
                                                                   InstanceMethod("rfft", &FftProcessor::Rfft),
                                                                   InstanceMethod("irfft", &FftProcessor::Irfft),
                                                                   InstanceMethod("rdft", &FftProcessor::Rdft),
                                                                   InstanceMethod("irdft", &FftProcessor::Irdft),

                                                                   InstanceMethod("getSize", &FftProcessor::GetSize),
                                                                   InstanceMethod("getHalfSize", &FftProcessor::GetHalfSize),
                                                                   InstanceMethod("isPowerOfTwo", &FftProcessor::IsPowerOfTwo),

                                                                   InstanceMethod("getMagnitude", &FftProcessor::GetMagnitude),
                                                                   InstanceMethod("getPhase", &FftProcessor::GetPhase),
                                                                   InstanceMethod("getPower", &FftProcessor::GetPower),
                                                               });

        Napi::FunctionReference *constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);

        exports.Set("FftProcessor", func);
        return exports;
    }

    FftProcessor::FftProcessor(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<FftProcessor>(info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsNumber())
        {
            Napi::TypeError::New(env, "Expected FFT size (number)").ThrowAsJavaScriptException();
            return;
        }

        m_size = info[0].As<Napi::Number>().Uint32Value();
        m_engine = std::make_unique<core::FftEngine<float>>(m_size);
    }

    Napi::Value FftProcessor::Fft(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Expect: { real: Float32Array, imag: Float32Array }
        if (info.Length() < 1 || !info[0].IsObject())
        {
            Napi::TypeError::New(env, "Expected complex input { real, imag }").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Object inputObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = inputObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = inputObj.Get("imag").As<Napi::Float32Array>();

        if (realIn.ElementLength() != m_size || imagIn.ElementLength() != m_size)
        {
            Napi::TypeError::New(env, "Input arrays must match FFT size").ThrowAsJavaScriptException();
            return env.Null();
        }

        // OPTIMIZATION: Allocate output arrays first
        Napi::Float32Array realOut = Napi::Float32Array::New(env, m_size);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, m_size);

        // Stack allocation for small sizes
        if (m_size <= 1024)
        {
            Complex stackBuf[2048]; // input + output
            Complex *input = stackBuf;
            Complex *output = stackBuf + 1024;

            for (size_t i = 0; i < m_size; ++i)
            {
                input[i] = Complex(realIn[i], imagIn[i]);
            }

            m_engine->fft(input, output);

            for (size_t i = 0; i < m_size; ++i)
            {
                realOut[i] = output[i].real();
                imagOut[i] = output[i].imag();
            }
        }
        else
        {
            std::vector<Complex> input(m_size);
            std::vector<Complex> output(m_size);

            for (size_t i = 0; i < m_size; ++i)
            {
                input[i] = Complex(realIn[i], imagIn[i]);
            }

            m_engine->fft(input.data(), output.data());

            for (size_t i = 0; i < m_size; ++i)
            {
                realOut[i] = output[i].real();
                imagOut[i] = output[i].imag();
            }
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value FftProcessor::Ifft(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object inputObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = inputObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = inputObj.Get("imag").As<Napi::Float32Array>();

        std::vector<Complex> input(m_size);
        std::vector<Complex> output(m_size);

        for (size_t i = 0; i < m_size; ++i)
        {
            input[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->ifft(input.data(), output.data());

        Napi::Float32Array realOut = Napi::Float32Array::New(env, m_size);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, m_size);

        for (size_t i = 0; i < m_size; ++i)
        {
            realOut[i] = output[i].real();
            imagOut[i] = output[i].imag();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value FftProcessor::Dft(const Napi::CallbackInfo &info)
    {
        // Same as FFT but uses DFT algorithm
        Napi::Env env = info.Env();

        Napi::Object inputObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = inputObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = inputObj.Get("imag").As<Napi::Float32Array>();

        std::vector<Complex> input(m_size);
        std::vector<Complex> output(m_size);

        for (size_t i = 0; i < m_size; ++i)
        {
            input[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->dft(input.data(), output.data());

        Napi::Float32Array realOut = Napi::Float32Array::New(env, m_size);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, m_size);

        for (size_t i = 0; i < m_size; ++i)
        {
            realOut[i] = output[i].real();
            imagOut[i] = output[i].imag();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value FftProcessor::Idft(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object inputObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = inputObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = inputObj.Get("imag").As<Napi::Float32Array>();

        std::vector<Complex> input(m_size);
        std::vector<Complex> output(m_size);

        for (size_t i = 0; i < m_size; ++i)
        {
            input[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->idft(input.data(), output.data());

        Napi::Float32Array realOut = Napi::Float32Array::New(env, m_size);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, m_size);

        for (size_t i = 0; i < m_size; ++i)
        {
            realOut[i] = output[i].real();
            imagOut[i] = output[i].imag();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value FftProcessor::Rfft(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Expect: Float32Array (real input only)
        if (info.Length() < 1 || !info[0].IsTypedArray())
        {
            Napi::TypeError::New(env, "Expected Float32Array").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Float32Array input = info[0].As<Napi::Float32Array>();

        if (input.ElementLength() != m_size)
        {
            Napi::TypeError::New(env, "Input must match FFT size").ThrowAsJavaScriptException();
            return env.Null();
        }

        size_t halfSize = m_engine->getHalfSize();

        // OPTIMIZATION: Allocate output arrays first, then write directly
        Napi::Float32Array realOut = Napi::Float32Array::New(env, halfSize);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, halfSize);

        // Stack allocation for small sizes, heap for large
        if (halfSize <= 1024)
        {
            Complex stackOutput[1024];
            m_engine->rfft(input.Data(), stackOutput);

            // Direct write to TypedArrays (no intermediate vector)
            for (size_t i = 0; i < halfSize; ++i)
            {
                realOut[i] = stackOutput[i].real();
                imagOut[i] = stackOutput[i].imag();
            }
        }
        else
        {
            std::vector<Complex> output(halfSize);
            m_engine->rfft(input.Data(), output.data());

            for (size_t i = 0; i < halfSize; ++i)
            {
                realOut[i] = output[i].real();
                imagOut[i] = output[i].imag();
            }
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value FftProcessor::Irfft(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object inputObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = inputObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = inputObj.Get("imag").As<Napi::Float32Array>();

        size_t halfSize = m_engine->getHalfSize();

        // OPTIMIZATION: Allocate output first
        Napi::Float32Array result = Napi::Float32Array::New(env, m_size);

        // Stack allocation for small sizes
        if (halfSize <= 1024)
        {
            Complex stackInput[1024];
            for (size_t i = 0; i < halfSize; ++i)
            {
                stackInput[i] = Complex(realIn[i], imagIn[i]);
            }

            // Write directly to output TypedArray data
            m_engine->irfft(stackInput, result.Data());
        }
        else
        {
            std::vector<Complex> input(halfSize);
            for (size_t i = 0; i < halfSize; ++i)
            {
                input[i] = Complex(realIn[i], imagIn[i]);
            }

            m_engine->irfft(input.data(), result.Data());
        }

        return result;
    }

    Napi::Value FftProcessor::Rdft(const Napi::CallbackInfo &info)
    {
        // Same as RFFT but uses RDFT algorithm
        Napi::Env env = info.Env();

        Napi::Float32Array input = info[0].As<Napi::Float32Array>();

        size_t halfSize = m_engine->getHalfSize();
        std::vector<Complex> output(halfSize);

        m_engine->rdft(input.Data(), output.data());

        Napi::Float32Array realOut = Napi::Float32Array::New(env, halfSize);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, halfSize);

        for (size_t i = 0; i < halfSize; ++i)
        {
            realOut[i] = output[i].real();
            imagOut[i] = output[i].imag();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value FftProcessor::Irdft(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object inputObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = inputObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = inputObj.Get("imag").As<Napi::Float32Array>();

        size_t halfSize = m_engine->getHalfSize();
        std::vector<Complex> input(halfSize);
        std::vector<float> output(m_size);

        for (size_t i = 0; i < halfSize; ++i)
        {
            input[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->irdft(input.data(), output.data());

        Napi::Float32Array result = Napi::Float32Array::New(env, m_size);
        std::copy(output.begin(), output.end(), result.Data());

        return result;
    }

    Napi::Value FftProcessor::GetSize(const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(info.Env(), m_size);
    }

    Napi::Value FftProcessor::GetHalfSize(const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(info.Env(), m_engine->getHalfSize());
    }

    Napi::Value FftProcessor::IsPowerOfTwo(const Napi::CallbackInfo &info)
    {
        return Napi::Boolean::New(info.Env(), m_engine->isPowerOfTwo());
    }

    Napi::Value FftProcessor::GetMagnitude(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object spectrumObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = spectrumObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = spectrumObj.Get("imag").As<Napi::Float32Array>();

        size_t length = realIn.ElementLength();
        std::vector<Complex> spectrum(length);
        std::vector<float> magnitudes(length);

        for (size_t i = 0; i < length; ++i)
        {
            spectrum[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->getMagnitude(spectrum.data(), magnitudes.data(), length);

        Napi::Float32Array result = Napi::Float32Array::New(env, length);
        std::copy(magnitudes.begin(), magnitudes.end(), result.Data());

        return result;
    }

    Napi::Value FftProcessor::GetPhase(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object spectrumObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = spectrumObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = spectrumObj.Get("imag").As<Napi::Float32Array>();

        size_t length = realIn.ElementLength();
        std::vector<Complex> spectrum(length);
        std::vector<float> phases(length);

        for (size_t i = 0; i < length; ++i)
        {
            spectrum[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->getPhase(spectrum.data(), phases.data(), length);

        Napi::Float32Array result = Napi::Float32Array::New(env, length);
        std::copy(phases.begin(), phases.end(), result.Data());

        return result;
    }

    Napi::Value FftProcessor::GetPower(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        Napi::Object spectrumObj = info[0].As<Napi::Object>();
        Napi::Float32Array realIn = spectrumObj.Get("real").As<Napi::Float32Array>();
        Napi::Float32Array imagIn = spectrumObj.Get("imag").As<Napi::Float32Array>();

        size_t length = realIn.ElementLength();
        std::vector<Complex> spectrum(length);
        std::vector<float> power(length);

        for (size_t i = 0; i < length; ++i)
        {
            spectrum[i] = Complex(realIn[i], imagIn[i]);
        }

        m_engine->getPower(spectrum.data(), power.data(), length);

        Napi::Float32Array result = Napi::Float32Array::New(env, length);
        std::copy(power.begin(), power.end(), result.Data());

        return result;
    }

    // ========== MovingFftProcessor Implementation ==========

    Napi::Object MovingFftProcessor::Init(Napi::Env env, Napi::Object exports)
    {
        Napi::Function func = DefineClass(env, "MovingFftProcessor", {
                                                                         InstanceMethod("addSample", &MovingFftProcessor::AddSample),
                                                                         InstanceMethod("addSamples", &MovingFftProcessor::AddSamples),
                                                                         InstanceMethod("computeSpectrum", &MovingFftProcessor::ComputeSpectrum),
                                                                         InstanceMethod("reset", &MovingFftProcessor::Reset),

                                                                         InstanceMethod("getFftSize", &MovingFftProcessor::GetFftSize),
                                                                         InstanceMethod("getSpectrumSize", &MovingFftProcessor::GetSpectrumSize),
                                                                         InstanceMethod("getHopSize", &MovingFftProcessor::GetHopSize),
                                                                         InstanceMethod("getFillLevel", &MovingFftProcessor::GetFillLevel),
                                                                         InstanceMethod("isReady", &MovingFftProcessor::IsReady),

                                                                         InstanceMethod("setWindowType", &MovingFftProcessor::SetWindowType),
                                                                         InstanceMethod("getMagnitudeSpectrum", &MovingFftProcessor::GetMagnitudeSpectrum),
                                                                         InstanceMethod("getPowerSpectrum", &MovingFftProcessor::GetPowerSpectrum),
                                                                         InstanceMethod("getPhaseSpectrum", &MovingFftProcessor::GetPhaseSpectrum),
                                                                         InstanceMethod("getFrequencyBins", &MovingFftProcessor::GetFrequencyBins),
                                                                     });

        Napi::FunctionReference *constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        exports.Set("MovingFftProcessor", func);
        return exports;
    }

    MovingFftProcessor::MovingFftProcessor(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<MovingFftProcessor>(info)
    {
        Napi::Env env = info.Env();

        // Expect: { fftSize, hopSize?, mode?, windowType?, realInput? }
        if (info.Length() < 1 || !info[0].IsObject())
        {
            Napi::TypeError::New(env, "Expected options object").ThrowAsJavaScriptException();
            return;
        }

        Napi::Object options = info[0].As<Napi::Object>();

        size_t fftSize = options.Get("fftSize").As<Napi::Number>().Uint32Value();
        size_t hopSize = options.Has("hopSize") ? options.Get("hopSize").As<Napi::Number>().Uint32Value() : fftSize;

        // Parse mode
        core::FftMode mode = core::FftMode::Batched;
        if (options.Has("mode"))
        {
            std::string modeStr = options.Get("mode").As<Napi::String>().Utf8Value();
            if (modeStr == "moving")
            {
                mode = core::FftMode::Moving;
            }
        }

        // Parse window type
        core::WindowType windowType = core::WindowType::Hann;
        if (options.Has("windowType"))
        {
            std::string windowStr = options.Get("windowType").As<Napi::String>().Utf8Value();
            if (windowStr == "none")
                windowType = core::WindowType::None;
            else if (windowStr == "hann")
                windowType = core::WindowType::Hann;
            else if (windowStr == "hamming")
                windowType = core::WindowType::Hamming;
            else if (windowStr == "blackman")
                windowType = core::WindowType::Blackman;
            else if (windowStr == "bartlett")
                windowType = core::WindowType::Bartlett;
        }

        bool realInput = options.Has("realInput") ? options.Get("realInput").As<Napi::Boolean>().Value() : true;

        m_filter = std::make_unique<core::MovingFftFilter<float>>(
            fftSize, hopSize, mode, windowType, realInput);
    }

    Napi::Value MovingFftProcessor::AddSample(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsNumber())
        {
            Napi::TypeError::New(env, "Expected sample (number)").ThrowAsJavaScriptException();
            return env.Null();
        }

        float sample = info[0].As<Napi::Number>().FloatValue();
        size_t spectrumSize = m_filter->getSpectrumSize();
        std::vector<Complex> spectrum(spectrumSize);

        bool ready = m_filter->addSample(sample, spectrum.data());

        if (ready)
        {
            Napi::Float32Array realOut = Napi::Float32Array::New(env, spectrumSize);
            Napi::Float32Array imagOut = Napi::Float32Array::New(env, spectrumSize);

            for (size_t i = 0; i < spectrumSize; ++i)
            {
                realOut[i] = spectrum[i].real();
                imagOut[i] = spectrum[i].imag();
            }

            Napi::Object result = Napi::Object::New(env);
            result.Set("real", realOut);
            result.Set("imag", imagOut);
            return result;
        }

        return env.Null();
    }

    Napi::Value MovingFftProcessor::AddSamples(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 2 || !info[0].IsTypedArray() || !info[1].IsFunction())
        {
            Napi::TypeError::New(env, "Expected (Float32Array, callback)").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Float32Array samples = info[0].As<Napi::Float32Array>();
        Napi::Function callback = info[1].As<Napi::Function>();

        size_t spectrumSize = m_filter->getSpectrumSize();

        size_t numSpectra = m_filter->addSamples(
            samples.Data(),
            samples.ElementLength(),
            [&](const Complex *spectrum, size_t size)
            {
                Napi::Float32Array realOut = Napi::Float32Array::New(env, size);
                Napi::Float32Array imagOut = Napi::Float32Array::New(env, size);

                for (size_t i = 0; i < size; ++i)
                {
                    realOut[i] = spectrum[i].real();
                    imagOut[i] = spectrum[i].imag();
                }

                Napi::Object result = Napi::Object::New(env);
                result.Set("real", realOut);
                result.Set("imag", imagOut);

                callback.Call({result, Napi::Number::New(env, size)});
            });

        return Napi::Number::New(env, numSpectra);
    }

    Napi::Value MovingFftProcessor::ComputeSpectrum(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        size_t spectrumSize = m_filter->getSpectrumSize();
        std::vector<Complex> spectrum(spectrumSize);

        try
        {
            m_filter->computeSpectrum(spectrum.data());
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Float32Array realOut = Napi::Float32Array::New(env, spectrumSize);
        Napi::Float32Array imagOut = Napi::Float32Array::New(env, spectrumSize);

        for (size_t i = 0; i < spectrumSize; ++i)
        {
            realOut[i] = spectrum[i].real();
            imagOut[i] = spectrum[i].imag();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("real", realOut);
        result.Set("imag", imagOut);

        return result;
    }

    Napi::Value MovingFftProcessor::Reset(const Napi::CallbackInfo &info)
    {
        m_filter->reset();
        return info.Env().Undefined();
    }

    Napi::Value MovingFftProcessor::GetFftSize(const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(info.Env(), m_filter->getFftSize());
    }

    Napi::Value MovingFftProcessor::GetSpectrumSize(const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(info.Env(), m_filter->getSpectrumSize());
    }

    Napi::Value MovingFftProcessor::GetHopSize(const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(info.Env(), m_filter->getHopSize());
    }

    Napi::Value MovingFftProcessor::GetFillLevel(const Napi::CallbackInfo &info)
    {
        return Napi::Number::New(info.Env(), m_filter->getFillLevel());
    }

    Napi::Value MovingFftProcessor::IsReady(const Napi::CallbackInfo &info)
    {
        return Napi::Boolean::New(info.Env(), m_filter->isReady());
    }

    Napi::Value MovingFftProcessor::SetWindowType(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsString())
        {
            Napi::TypeError::New(env, "Expected window type (string)").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::string windowStr = info[0].As<Napi::String>().Utf8Value();
        core::WindowType windowType = core::WindowType::Hann;

        if (windowStr == "none")
            windowType = core::WindowType::None;
        else if (windowStr == "hann")
            windowType = core::WindowType::Hann;
        else if (windowStr == "hamming")
            windowType = core::WindowType::Hamming;
        else if (windowStr == "blackman")
            windowType = core::WindowType::Blackman;
        else if (windowStr == "bartlett")
            windowType = core::WindowType::Bartlett;
        else
        {
            Napi::TypeError::New(env, "Invalid window type").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        m_filter->setWindowType(windowType);
        return env.Undefined();
    }

    Napi::Value MovingFftProcessor::GetMagnitudeSpectrum(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        size_t spectrumSize = m_filter->getSpectrumSize();
        std::vector<float> magnitudes(spectrumSize);

        m_filter->getMagnitudeSpectrum(magnitudes.data());

        Napi::Float32Array result = Napi::Float32Array::New(env, spectrumSize);
        std::copy(magnitudes.begin(), magnitudes.end(), result.Data());

        return result;
    }

    Napi::Value MovingFftProcessor::GetPowerSpectrum(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        size_t spectrumSize = m_filter->getSpectrumSize();
        std::vector<float> power(spectrumSize);

        m_filter->getPowerSpectrum(power.data());

        Napi::Float32Array result = Napi::Float32Array::New(env, spectrumSize);
        std::copy(power.begin(), power.end(), result.Data());

        return result;
    }

    Napi::Value MovingFftProcessor::GetPhaseSpectrum(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        size_t spectrumSize = m_filter->getSpectrumSize();
        std::vector<float> phases(spectrumSize);

        m_filter->getPhaseSpectrum(phases.data());

        Napi::Float32Array result = Napi::Float32Array::New(env, spectrumSize);
        std::copy(phases.begin(), phases.end(), result.Data());

        return result;
    }

    Napi::Value MovingFftProcessor::GetFrequencyBins(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsNumber())
        {
            Napi::TypeError::New(env, "Expected sample rate (number)").ThrowAsJavaScriptException();
            return env.Null();
        }

        float sampleRate = info[0].As<Napi::Number>().FloatValue();
        size_t spectrumSize = m_filter->getSpectrumSize();
        std::vector<float> frequencies(spectrumSize);

        m_filter->getFrequencyBins(sampleRate, frequencies.data());

        Napi::Float32Array result = Napi::Float32Array::New(env, spectrumSize);
        std::copy(frequencies.begin(), frequencies.end(), result.Data());

        return result;
    }

    // Export init function (add to existing module init)
    void InitFftBindings(Napi::Env env, Napi::Object exports)
    {
        FftProcessor::Init(env, exports);
        MovingFftProcessor::Init(env, exports);
    }

} // namespace dsp
