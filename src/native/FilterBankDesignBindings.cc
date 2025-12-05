/**
 * Filter Bank Design Bindings
 *
 * N-API bindings for filter bank design utilities.
 * Exposes stateless filter bank generation to TypeScript.
 */

#include <napi.h>
#include "core/FilterBankDesign.h"

namespace dsp
{
    /**
     * Design a filter bank (N-API binding)
     *
     * @param info[0] - Options object with:
     *   - scale: 'linear' | 'log' | 'mel' | 'bark'
     *   - type: 'butterworth' | 'chebyshev'
     *   - count: number of bands
     *   - sampleRate: sample rate in Hz
     *   - frequencyRange: [minFreq, maxFreq]
     *   - order: filter order (default: 2)
     *   - rippleDb: Chebyshev ripple (default: 0.5)
     *
     * @return Array of { b: number[], a: number[] } coefficient objects
     */
    Napi::Value DesignFilterBank(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate input
        if (info.Length() < 1 || !info[0].IsObject())
        {
            Napi::TypeError::New(env, "Options object required")
                .ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Object options = info[0].As<Napi::Object>();

        try
        {
            // Parse options
            core::FilterBankDesign::DesignOptions opts;

            // Parse scale
            if (!options.Has("scale") || !options.Get("scale").IsString())
            {
                throw std::invalid_argument("scale (string) is required");
            }
            std::string scaleStr = options.Get("scale").As<Napi::String>().Utf8Value();

            if (scaleStr == "mel")
            {
                opts.scale = core::FilterBankDesign::Scale::Mel;
            }
            else if (scaleStr == "bark")
            {
                opts.scale = core::FilterBankDesign::Scale::Bark;
            }
            else if (scaleStr == "log")
            {
                opts.scale = core::FilterBankDesign::Scale::Log;
            }
            else if (scaleStr == "linear")
            {
                opts.scale = core::FilterBankDesign::Scale::Linear;
            }
            else
            {
                throw std::invalid_argument("Invalid scale: must be 'linear', 'log', 'mel', or 'bark'");
            }

            // Parse type (optional, defaults to butterworth)
            opts.type = core::FilterBankDesign::Type::Butterworth;
            if (options.Has("type") && options.Get("type").IsString())
            {
                std::string typeStr = options.Get("type").As<Napi::String>().Utf8Value();
                if (typeStr == "chebyshev" || typeStr == "chebyshev1")
                {
                    opts.type = core::FilterBankDesign::Type::Chebyshev1;
                }
                else if (typeStr != "butterworth")
                {
                    throw std::invalid_argument("Invalid type: must be 'butterworth' or 'chebyshev'");
                }
            }

            // Parse count
            if (!options.Has("count") || !options.Get("count").IsNumber())
            {
                throw std::invalid_argument("count (number) is required");
            }
            opts.count = options.Get("count").As<Napi::Number>().Int32Value();

            // Parse sampleRate
            if (!options.Has("sampleRate") || !options.Get("sampleRate").IsNumber())
            {
                throw std::invalid_argument("sampleRate (number) is required");
            }
            opts.sampleRate = options.Get("sampleRate").As<Napi::Number>().DoubleValue();

            // Parse frequencyRange
            if (!options.Has("frequencyRange") || !options.Get("frequencyRange").IsArray())
            {
                throw std::invalid_argument("frequencyRange ([min, max]) is required");
            }
            Napi::Array range = options.Get("frequencyRange").As<Napi::Array>();
            if (range.Length() < 2)
            {
                throw std::invalid_argument("frequencyRange must have at least 2 elements");
            }
            opts.minFreq = range.Get((uint32_t)0).As<Napi::Number>().DoubleValue();
            opts.maxFreq = range.Get((uint32_t)1).As<Napi::Number>().DoubleValue();

            // Parse order (optional, defaults to 2)
            opts.order = 2;
            if (options.Has("order") && options.Get("order").IsNumber())
            {
                opts.order = options.Get("order").As<Napi::Number>().Int32Value();
            }

            // Parse rippleDb (optional, defaults to 0.5)
            opts.rippleDb = 0.5;
            if (options.Has("rippleDb") && options.Get("rippleDb").IsNumber())
            {
                opts.rippleDb = options.Get("rippleDb").As<Napi::Number>().DoubleValue();
            }

            // Design the filter bank
            auto bank = core::FilterBankDesign::design(opts);

            // Convert to JavaScript array
            Napi::Array result = Napi::Array::New(env, bank.size());

            for (size_t i = 0; i < bank.size(); ++i)
            {
                Napi::Object coeffs = Napi::Object::New(env);

                // Convert b coefficients
                Napi::Array b = Napi::Array::New(env, bank[i].b.size());
                for (size_t j = 0; j < bank[i].b.size(); ++j)
                {
                    b.Set(j, Napi::Number::New(env, bank[i].b[j]));
                }
                coeffs.Set("b", b);

                // Convert a coefficients
                Napi::Array a = Napi::Array::New(env, bank[i].a.size());
                for (size_t j = 0; j < bank[i].a.size(); ++j)
                {
                    a.Set(j, Napi::Number::New(env, bank[i].a[j]));
                }
                coeffs.Set("a", a);

                result.Set(i, coeffs);
            }

            return result;
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(env, std::string("Filter bank design failed: ") + e.what())
                .ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    /**
     * Get filter bank frequency boundaries (N-API binding)
     *
     * Useful for visualization and debugging
     *
     * @param info[0] - Same options object as designFilterBank
     * @return Array of boundary frequencies in Hz
     */
    Napi::Value GetFilterBankBoundaries(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsObject())
        {
            Napi::TypeError::New(env, "Options object required")
                .ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Object options = info[0].As<Napi::Object>();

        try
        {
            // Parse options (same as DesignFilterBank)
            core::FilterBankDesign::DesignOptions opts;

            std::string scaleStr = options.Get("scale").As<Napi::String>().Utf8Value();
            if (scaleStr == "mel")
                opts.scale = core::FilterBankDesign::Scale::Mel;
            else if (scaleStr == "bark")
                opts.scale = core::FilterBankDesign::Scale::Bark;
            else if (scaleStr == "log")
                opts.scale = core::FilterBankDesign::Scale::Log;
            else
                opts.scale = core::FilterBankDesign::Scale::Linear;

            opts.count = options.Get("count").As<Napi::Number>().Int32Value();

            Napi::Array range = options.Get("frequencyRange").As<Napi::Array>();
            opts.minFreq = range.Get((uint32_t)0).As<Napi::Number>().DoubleValue();
            opts.maxFreq = range.Get((uint32_t)1).As<Napi::Number>().DoubleValue();

            // Get boundaries
            auto boundaries = core::FilterBankDesign::getBoundaries(opts);

            // Convert to JavaScript array
            Napi::Array result = Napi::Array::New(env, boundaries.size());
            for (size_t i = 0; i < boundaries.size(); ++i)
            {
                result.Set(i, Napi::Number::New(env, boundaries[i]));
            }

            return result;
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(env, std::string("Get boundaries failed: ") + e.what())
                .ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    /**
     * Register filter bank design bindings
     * Call this from your main addon Init function
     */
    void RegisterFilterBankDesignBindings(Napi::Env env, Napi::Object exports)
    {
        exports.Set("designFilterBank", Napi::Function::New(env, DesignFilterBank));
        exports.Set("getFilterBankBoundaries", Napi::Function::New(env, GetFilterBankBoundaries));
    }

} // namespace dsp
