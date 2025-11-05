#include <napi.h>
#include "utils/SimdOps.h"
#include <Eigen/Dense>
#include <stdexcept>
#include <vector>

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
         * @brief Detrend signal by removing linear trend using least-squares regression
         *
         * Fits a line y = mx + b to the signal and subtracts it, removing linear trends.
         * Useful for removing DC offset and drift from signals.
         *
         * @param signal Float32Array containing the signal data
         * @param options Optional object with:
         *   - type: "linear" (default) or "constant" (remove mean only)
         *
         * @return Float32Array with detrended signal (same length as input)
         */
        Napi::Value Detrend(const Napi::CallbackInfo &info)
        {
            Napi::Env env = info.Env();

            // Validate arguments
            if (info.Length() < 1)
            {
                Napi::TypeError::New(env, "Expected at least 1 argument: signal (Float32Array)")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            // Check for undefined or null signal
            if (info[0].IsUndefined() || info[0].IsNull())
            {
                Napi::TypeError::New(env, "Signal must be a Float32Array")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            if (!info[0].IsTypedArray())
            {
                Napi::TypeError::New(env, "Signal must be a Float32Array")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            Napi::TypedArray typedArray = info[0].As<Napi::TypedArray>();
            if (typedArray.TypedArrayType() != napi_float32_array)
            {
                Napi::TypeError::New(env, "Signal must be a Float32Array")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }

            Napi::Float32Array signal = typedArray.As<Napi::Float32Array>();
            size_t n = signal.ElementLength();

            if (n == 0)
            {
                // Empty input -> empty output
                return Napi::Float32Array::New(env, 0);
            }

            // Parse options (if provided)
            std::string detrendType = "linear"; // Default
            if (info.Length() >= 2 && info[1].IsObject())
            {
                Napi::Object options = info[1].As<Napi::Object>();
                if (options.Has("type"))
                {
                    Napi::Value typeValue = options.Get("type");
                    if (typeValue.IsString())
                    {
                        detrendType = typeValue.As<Napi::String>().Utf8Value();
                        if (detrendType != "linear" && detrendType != "constant")
                        {
                            Napi::TypeError::New(env, "Detrend type must be 'linear' or 'constant'")
                                .ThrowAsJavaScriptException();
                            return env.Null();
                        }
                    }
                }
            }

            const float *data = signal.Data();

            // Create output array
            Napi::Float32Array output = Napi::Float32Array::New(env, n);
            float *outputData = output.Data();

            if (detrendType == "constant")
            {
                // Remove mean (constant detrend)
                double sum = 0.0;
                for (size_t i = 0; i < n; ++i)
                {
                    sum += data[i];
                }
                float mean = static_cast<float>(sum / n);

                for (size_t i = 0; i < n; ++i)
                {
                    outputData[i] = data[i] - mean;
                }
            }
            else // "linear"
            {
                // Use Eigen for least-squares linear regression
                // Fit y = mx + b using normal equations: (X^T X)^-1 X^T y

                // Build design matrix X = [1, x] where x = [0, 1, 2, ..., n-1]
                Eigen::MatrixXd X(n, 2);
                Eigen::VectorXd y(n);

                for (size_t i = 0; i < n; ++i)
                {
                    X(i, 0) = 1.0;                    // Intercept term
                    X(i, 1) = static_cast<double>(i); // Index as x-coordinate
                    y(i) = data[i];
                }

                // Solve: coeffs = (X^T X)^-1 X^T y
                Eigen::Vector2d coeffs = (X.transpose() * X).ldlt().solve(X.transpose() * y);

                double intercept = coeffs(0); // b
                double slope = coeffs(1);     // m

                // Subtract fitted line: y_detrended = y - (mx + b)
                for (size_t i = 0; i < n; ++i)
                {
                    float fitted = static_cast<float>(slope * i + intercept);
                    outputData[i] = data[i] - fitted;
                }
            }

            return output;
        }

        /**
         * @brief Initialize utility function bindings
         */
        Napi::Object InitUtilityBindings(Napi::Env env, Napi::Object exports)
        {
            exports.Set("dotProduct", Napi::Function::New(env, DotProduct));
            exports.Set("detrend", Napi::Function::New(env, Detrend));
            return exports;
        }

    } // namespace bindings
} // namespace dsp
