#include <napi.h>
#include "utils/SimdOps.h"
#include <stdexcept>

namespace dsp
{
    namespace bindings
    {
        /**
         * @brief N-API binding for SIMD-accelerated dot product
         *
         * Takes two Float32Arrays of equal length and computes their dot product.
         * Uses SIMD (AVX2/SSE2) for maximum performance.
         */
        Napi::Value DotProduct(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            // Validate arguments
            if (info.Length() < 2)
            {
                Napi::TypeError::New(env, "Expected 2 arguments: two Float32Arrays")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            // Check for undefined or null arguments
            if (info[0].IsUndefined() || info[0].IsNull() || info[1].IsUndefined() || info[1].IsNull())
            {
                Napi::TypeError::New(env, "Arguments must be Float32Arrays")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            if (!info[0].IsTypedArray() || !info[1].IsTypedArray())
            {
                Napi::TypeError::New(env, "Arguments must be Float32Arrays")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            Napi::TypedArray typedArray0 = info[0].As<Napi::TypedArray>();
            Napi::TypedArray typedArray1 = info[1].As<Napi::TypedArray>();

            if (typedArray0.TypedArrayType() != napi_float32_array ||
                typedArray1.TypedArrayType() != napi_float32_array)
            {
                Napi::TypeError::New(env, "Arguments must be Float32Arrays")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            Napi::Float32Array array0 = typedArray0.As<Napi::Float32Array>();
            Napi::Float32Array array1 = typedArray1.As<Napi::Float32Array>();

            if (array0.ElementLength() != array1.ElementLength())
            {
                Napi::TypeError::New(env, "Arrays must have the same length")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            size_t length = array0.ElementLength();
            const float *data0 = array0.Data();
            const float *data1 = array1.Data();

            // Compute dot product using SIMD-optimized function with double precision
            double result = simd::dot_product(data0, data1, length);

            return Napi::Number::New(env, result);
        }

        /**
         * @brief Initialize utility function bindings
         */
        Napi::Object InitUtilityBindings(Napi::Env env, Napi::Object exports)
        {
            exports.Set("dotProduct", Napi::Function::New(env, DotProduct));
            return exports;
        }

    } // namespace bindings
} // namespace dsp
