#include "DspPipeline.h"
#include "adapters/MovingAverageStage.h"            // Moving Average method
#include "adapters/ExponentialMovingAverageStage.h" // Exponential Moving Average method
#include "adapters/CumulativeMovingAverageStage.h"  // Cumulative Moving Average method
#include "adapters/RmsStage.h"                      // RMS method
#include "adapters/RectifyStage.h"                  // Rectify method
#include "adapters/VarianceStage.h"                 // Variance method
#include "adapters/ZScoreNormalizeStage.h"          // Z-Score Normalize method
#include "adapters/MeanAbsoluteValueStage.h"        // Mean Absolute Value method
#include "adapters/WaveformLengthStage.h"           // Waveform Length method
#include "adapters/SscStage.h"                      // Slope Sign Change method
#include "adapters/WampStage.h"                     // Willison Amplitude method
#include "adapters/FilterStage.h"                   // Filter stage (FIR/IIR)
#include "adapters/InterpolatorStage.h"             // Interpolator (upsample)
#include "adapters/DecimatorStage.h"                // Decimator (downsample)
#include "adapters/ResamplerStage.h"                // Resampler (rational rate conversion)
#include "adapters/ConvolutionStage.h"              // Convolution stage
#include "adapters/LinearRegressionStage.h"         // Linear Regression stage
#include "adapters/LmsStage.h"                      // LMS Adaptive Filter stage
#include "adapters/RlsStage.h"                      // RLS Adaptive Filter stage
#include "adapters/WaveletTransformStage.h"         // Wavelet Transform stage
#include "adapters/HilbertEnvelopeStage.h"          // Hilbert Envelope stage
#include "adapters/StftStage.h"                     // STFT (Short-Time Fourier Transform) stage
#include "adapters/FftStage.h"                      // FFT (Fast Fourier Transform) stage
#include "adapters/MelSpectrogramStage.h"           // Mel Spectrogram stage
#include "adapters/MfccStage.h"                     // MFCC (Mel-Frequency Cepstral Coefficients) stage
#include "adapters/MatrixTransformStage.h"          // Matrix Transform stage (PCA/ICA/Whitening)
#include "adapters/GscPreprocessorStage.h"          // GSC Preprocessor for adaptive beamforming
#include "adapters/ChannelSelectorStage.h"          // Channel selector for reducing channel count
#include "adapters/ChannelSelectStage.h"            // Channel selector by indices (select/reorder)
#include "adapters/ChannelMergeStage.h"             // Channel merger/duplicator (merge/expand)
#include "adapters/FilterBankStage.h"               // Filter Bank stage (split channels into frequency bands)
#include "adapters/ClipDetectionStage.h"            // Clip detection stage
#include "adapters/PeakDetectionStage.h"            // Peak detection stage
#include "adapters/DifferentiatorStage.h"           // Differentiator stage
#include "adapters/SquareStage.h"                   // Square stage
#include "adapters/AmplifyStage.h"                  // Amplify (Gain) stage
#include "adapters/IntegratorStage.h"               // Integrator stage
#include "adapters/SnrStage.h"                      // SNR stage
#include "adapters/KalmanFilterStage.h"             // Kalman Filter stage
#include "adapters/TimeAlignmentStage.h"            // Time Alignment stage

#include <iostream>
#include <thread> // For std::this_thread in debug code

namespace dsp
{
    // Forward declarations for bindings
    extern void InitFftBindings(Napi::Env env, Napi::Object exports);
    extern void InitFilterBindings(Napi::Env env, Napi::Object exports);
}

#include <iostream>
#include <ctime>
#include <cstdlib>
#include "utils/Toon.h"

// Helper function to check debug flag
inline bool isDebugEnabled()
{
    return std::getenv("DSPX_DEBUG") != nullptr;
}

// SIMD optimizations for timestamp interpolation
// Priority: AVX2 (8-wide) > SSE (4-wide) > NEON (4-wide) > Scalar
#if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
#include <immintrin.h>
#define HAS_AVX2 1
#define HAS_SSE 0
#define HAS_NEON 0
#elif defined(__SSE__) || defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))
#include <emmintrin.h> // SSE2
#include <xmmintrin.h> // SSE
#define HAS_AVX2 0
#define HAS_SSE 1
#define HAS_NEON 0
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define HAS_AVX2 0
#define HAS_SSE 0
#define HAS_NEON 1
#else
#define HAS_AVX2 0
#define HAS_SSE 0
#define HAS_NEON 0
#endif

namespace dsp
{

    // N-API Boilerplate: Init function
    Napi::Object DspPipeline::Init(Napi::Env env, Napi::Object exports)
    {
        Napi::Function func = DefineClass(env, "DspPipeline", {
                                                                  // Pipeline building
                                                                  InstanceMethod("addStage", &DspPipeline::AddStage),
                                                                  InstanceMethod("addFilterStage", &DspPipeline::AddFilterStage),

                                                                  // Processing
                                                                  InstanceMethod("process", &DspPipeline::ProcessAsync),
                                                                  InstanceMethod("processSync", &DspPipeline::ProcessSync),

                                                                  // State management (for Redis persistence from TypeScript)
                                                                  InstanceMethod("saveState", &DspPipeline::SaveState),
                                                                  InstanceMethod("loadState", &DspPipeline::LoadState),
                                                                  InstanceMethod("clearState", &DspPipeline::ClearState),
                                                                  InstanceMethod("listState", &DspPipeline::ListState),

                                                                  // Lifecycle management
                                                                  InstanceMethod("dispose", &DspPipeline::Dispose),
                                                              });

        exports.Set("DspPipeline", func);
        return exports;
    }

    // N-API Boilerplate: Constructor
    DspPipeline::DspPipeline(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<DspPipeline>(info)
    {
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::Constructor - this=" << this
                      << ", creating pipeline" << std::endl;
        }
        // Initialize the lock
        m_isBusy = std::make_shared<std::atomic<bool>>(false);
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::Constructor - m_isBusy=" << m_isBusy.get() << std::endl;
        }
        InitializeStageFactories();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::Constructor - complete, this=" << this << std::endl;
        }
    }

    /**
     * Initialize the stage factory map with all available stages
     * This is where the methods get exposed to TypeScript
     */
    void DspPipeline::InitializeStageFactories()
    {
        // Factory for Moving Average stage
        m_stageFactories["movingAverage"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::AverageMode mode = (modeStr == "moving") ? dsp::adapters::AverageMode::Moving : dsp::adapters::AverageMode::Batch;

            size_t windowSize = 0;
            double windowDurationMs = 0.0;

            if (mode == dsp::adapters::AverageMode::Moving)
            {
                // Accept either windowSize or windowDuration
                if (params.Has("windowSize"))
                {
                    windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
                }
                else if (params.Has("windowDuration"))
                {
                    // Store the duration - will be converted to windowSize on first process() call
                    // using the actual sample rate derived from timestamps
                    windowDurationMs = params.Get("windowDuration").As<Napi::Number>().DoubleValue();
                }
                else
                {
                    throw std::invalid_argument("MovingAverage: either 'windowSize' or 'windowDuration' is required for 'moving' mode");
                }
            }

            return std::make_unique<dsp::adapters::MovingAverageStage>(mode, windowSize, windowDurationMs);
        };

        // Factory for Exponential Moving Average stage
        m_stageFactories["exponentialMovingAverage"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::EmaMode mode = (modeStr == "moving") ? dsp::adapters::EmaMode::Moving : dsp::adapters::EmaMode::Batch;

            // Parse alpha parameter (required, must be in range (0, 1])
            if (!params.Has("alpha"))
            {
                throw std::invalid_argument("ExponentialMovingAverage: 'alpha' parameter is required");
            }
            double alpha = params.Get("alpha").As<Napi::Number>().DoubleValue();
            if (alpha <= 0.0 || alpha > 1.0)
            {
                throw std::invalid_argument("ExponentialMovingAverage: 'alpha' must be in range (0, 1]");
            }

            return std::make_unique<dsp::adapters::ExponentialMovingAverageStage>(mode, static_cast<float>(alpha));
        };

        // Factory for Cumulative Moving Average stage
        m_stageFactories["cumulativeMovingAverage"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::CmaMode mode = (modeStr == "moving") ? dsp::adapters::CmaMode::Moving : dsp::adapters::CmaMode::Batch;

            return std::make_unique<dsp::adapters::CumulativeMovingAverageStage>(mode);
        };

        // Factory for RMS stage
        m_stageFactories["rms"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::RmsMode mode = (modeStr == "moving") ? dsp::adapters::RmsMode::Moving : dsp::adapters::RmsMode::Batch;

            size_t windowSize = 0;
            double windowDurationMs = 0.0;

            if (mode == dsp::adapters::RmsMode::Moving)
            {
                if (params.Has("windowSize"))
                {
                    windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
                }
                else if (params.Has("windowDuration"))
                {
                    windowDurationMs = params.Get("windowDuration").As<Napi::Number>().DoubleValue();
                }
                else
                {
                    throw std::invalid_argument("RMS: either 'windowSize' or 'windowDuration' is required for 'moving' mode");
                }
            }

            return std::make_unique<dsp::adapters::RmsStage>(mode, windowSize, windowDurationMs);
        };

        // Factory for Rectify stage
        m_stageFactories["rectify"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::RectifyMode mode = (modeStr == "half") ? dsp::adapters::RectifyMode::HalfWave : dsp::adapters::RectifyMode::FullWave;
            return std::make_unique<dsp::adapters::RectifyStage>(mode);
        };

        // Factory for Variance stage
        m_stageFactories["variance"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::VarianceMode mode = (modeStr == "moving") ? dsp::adapters::VarianceMode::Moving : dsp::adapters::VarianceMode::Batch;

            size_t windowSize = 0;
            double windowDurationMs = 0.0;

            if (mode == dsp::adapters::VarianceMode::Moving)
            {
                if (params.Has("windowSize"))
                {
                    windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
                }
                else if (params.Has("windowDuration"))
                {
                    windowDurationMs = params.Get("windowDuration").As<Napi::Number>().DoubleValue();
                }
                else
                {
                    throw std::invalid_argument("Variance: either 'windowSize' or 'windowDuration' is required for 'moving' mode");
                }
            }

            return std::make_unique<dsp::adapters::VarianceStage>(mode, windowSize, windowDurationMs);
        };

        // Factory for zScoreNormalize stage
        m_stageFactories["zScoreNormalize"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::ZScoreNormalizeMode mode = (modeStr == "moving") ? dsp::adapters::ZScoreNormalizeMode::Moving : dsp::adapters::ZScoreNormalizeMode::Batch;

            size_t windowSize = 0;
            double windowDurationMs = 0.0;

            if (mode == dsp::adapters::ZScoreNormalizeMode::Moving)
            {
                if (params.Has("windowSize"))
                {
                    windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
                }
                else if (params.Has("windowDuration"))
                {
                    windowDurationMs = params.Get("windowDuration").As<Napi::Number>().DoubleValue();
                }
                else
                {
                    throw std::invalid_argument("ZScoreNormalize: either 'windowSize' or 'windowDuration' is required for 'moving' mode");
                }
            }

            // Get optional epsilon, default to 1e-6
            float epsilon = 1e-6f;
            if (params.Has("epsilon"))
            {
                epsilon = params.Get("epsilon").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<dsp::adapters::ZScoreNormalizeStage>(mode, windowSize, windowDurationMs, epsilon);
        };

        // Factory for Mean Absolute Value stage
        m_stageFactories["meanAbsoluteValue"] = [](const Napi::Object &params)
        {
            std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
            dsp::adapters::MavMode mode = (modeStr == "moving") ? dsp::adapters::MavMode::Moving : dsp::adapters::MavMode::Batch;

            size_t windowSize = 0;
            double windowDurationMs = 0.0;

            if (mode == dsp::adapters::MavMode::Moving)
            {
                if (params.Has("windowSize"))
                {
                    windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
                }
                else if (params.Has("windowDuration"))
                {
                    windowDurationMs = params.Get("windowDuration").As<Napi::Number>().DoubleValue();
                }
                else
                {
                    throw std::invalid_argument("MeanAbsoluteValue: either 'windowSize' or 'windowDuration' is required for 'moving' mode");
                }
            }

            return std::make_unique<dsp::adapters::MeanAbsoluteValueStage>(mode, windowSize, windowDurationMs);
        };

        // Factory for Waveform Length stage
        m_stageFactories["waveformLength"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("WaveformLength: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
            return std::make_unique<dsp::adapters::WaveformLengthStage>(windowSize);
        };

        // Factory for Slope Sign Change (SSC) stage
        m_stageFactories["slopeSignChange"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("SlopeSignChange: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();

            float threshold = 0.0f;
            if (params.Has("threshold"))
            {
                threshold = params.Get("threshold").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<dsp::adapters::SscStage>(windowSize, threshold);
        };

        // Factory for Willison Amplitude (WAMP) stage
        m_stageFactories["willisonAmplitude"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("WillisonAmplitude: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();

            float threshold = 0.0f;
            if (params.Has("threshold"))
            {
                threshold = params.Get("threshold").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<dsp::adapters::WampStage>(windowSize, threshold);
        };

        // Factory for Interpolator stage (upsample by L)
        m_stageFactories["interpolate"] = [](const Napi::Object &params)
        {
            if (!params.Has("factor"))
            {
                throw std::invalid_argument("Interpolate: 'factor' is required");
            }
            if (!params.Has("sampleRate"))
            {
                throw std::invalid_argument("Interpolate: 'sampleRate' is required");
            }

            int factor = params.Get("factor").As<Napi::Number>().Int32Value();
            double sampleRate = params.Get("sampleRate").As<Napi::Number>().DoubleValue();

            // Optional filter order (default to 51)
            int order = 51;
            if (params.Has("order"))
            {
                order = params.Get("order").As<Napi::Number>().Int32Value();
            }

            return std::make_unique<dsp::InterpolatorStage>(factor, order, sampleRate);
        };

        // Factory for Decimator stage (downsample by M)
        m_stageFactories["decimate"] = [](const Napi::Object &params)
        {
            if (!params.Has("factor"))
            {
                throw std::invalid_argument("Decimate: 'factor' is required");
            }
            if (!params.Has("sampleRate"))
            {
                throw std::invalid_argument("Decimate: 'sampleRate' is required");
            }

            int factor = params.Get("factor").As<Napi::Number>().Int32Value();
            double sampleRate = params.Get("sampleRate").As<Napi::Number>().DoubleValue();

            // Optional filter order (default to 51)
            int order = 51;
            if (params.Has("order"))
            {
                order = params.Get("order").As<Napi::Number>().Int32Value();
            }

            return std::make_unique<dsp::DecimatorStage>(factor, order, sampleRate);
        };

        // Factory for Resampler stage (rational resampling L/M)
        m_stageFactories["resample"] = [](const Napi::Object &params)
        {
            if (!params.Has("upFactor"))
            {
                throw std::invalid_argument("Resample: 'upFactor' is required");
            }
            if (!params.Has("downFactor"))
            {
                throw std::invalid_argument("Resample: 'downFactor' is required");
            }
            if (!params.Has("sampleRate"))
            {
                throw std::invalid_argument("Resample: 'sampleRate' is required");
            }

            int upFactor = params.Get("upFactor").As<Napi::Number>().Int32Value();
            int downFactor = params.Get("downFactor").As<Napi::Number>().Int32Value();
            double sampleRate = params.Get("sampleRate").As<Napi::Number>().DoubleValue();

            // Optional filter order (default to 51)
            int order = 51;
            if (params.Has("order"))
            {
                order = params.Get("order").As<Napi::Number>().Int32Value();
            }

            return std::make_unique<dsp::ResamplerStage>(upFactor, downFactor, order, sampleRate);
        };

        // Factory for Convolution stage
        m_stageFactories["convolution"] = [](const Napi::Object &params)
        {
            if (!params.Has("kernel"))
            {
                throw std::invalid_argument("Convolution: 'kernel' is required");
            }

            // Extract kernel
            Napi::Value kernelValue = params.Get("kernel");
            std::vector<float> kernel;

            if (kernelValue.IsTypedArray())
            {
                Napi::TypedArray typedArray = kernelValue.As<Napi::TypedArray>();
                if (typedArray.TypedArrayType() == napi_float32_array)
                {
                    Napi::Float32Array kernelArray = kernelValue.As<Napi::Float32Array>();
                    kernel.resize(kernelArray.ElementLength());
                    for (size_t i = 0; i < kernelArray.ElementLength(); ++i)
                    {
                        kernel[i] = kernelArray[i];
                    }
                }
                else
                {
                    throw std::invalid_argument("Convolution: kernel must be Float32Array");
                }
            }
            else if (kernelValue.IsArray())
            {
                Napi::Array kernelArray = kernelValue.As<Napi::Array>();
                kernel.resize(kernelArray.Length());
                for (uint32_t i = 0; i < kernelArray.Length(); ++i)
                {
                    kernel[i] = kernelArray.Get(i).As<Napi::Number>().FloatValue();
                }
            }
            else
            {
                throw std::invalid_argument("Convolution: kernel must be an array or Float32Array");
            }

            // Mode: 'moving' or 'batch' (default: 'moving')
            dsp::adapters::ConvolutionMode mode = dsp::adapters::ConvolutionMode::Moving;
            if (params.Has("mode"))
            {
                std::string modeStr = params.Get("mode").As<Napi::String>().Utf8Value();
                if (modeStr == "batch")
                {
                    mode = dsp::adapters::ConvolutionMode::Batch;
                }
            }

            // Method: 'auto', 'direct', or 'fft' (default: 'auto')
            dsp::adapters::ConvolutionMethod method = dsp::adapters::ConvolutionMethod::Auto;
            if (params.Has("method"))
            {
                std::string methodStr = params.Get("method").As<Napi::String>().Utf8Value();
                if (methodStr == "direct")
                {
                    method = dsp::adapters::ConvolutionMethod::Direct;
                }
                else if (methodStr == "fft")
                {
                    method = dsp::adapters::ConvolutionMethod::FFT;
                }
            }

            // Auto threshold (default: 64)
            size_t autoThreshold = 64;
            if (params.Has("autoThreshold"))
            {
                autoThreshold = params.Get("autoThreshold").As<Napi::Number>().Uint32Value();
            }

            return std::make_unique<dsp::adapters::ConvolutionStage>(kernel, mode, method, autoThreshold);
        };

        // Factory for Linear Regression stage (slope output)
        m_stageFactories["linearRegressionSlope"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("LinearRegressionSlope: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
            return std::make_unique<dsp::adapters::LinearRegressionSlope>(windowSize);
        };

        // Factory for Linear Regression stage (intercept output)
        m_stageFactories["linearRegressionIntercept"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("LinearRegressionIntercept: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
            return std::make_unique<dsp::adapters::LinearRegressionIntercept>(windowSize);
        };

        // Factory for Linear Regression stage (residuals output)
        m_stageFactories["linearRegressionResiduals"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("LinearRegressionResiduals: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
            return std::make_unique<dsp::adapters::LinearRegressionResiduals>(windowSize);
        };

        // Factory for Linear Regression stage (predictions output)
        m_stageFactories["linearRegressionPredictions"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("LinearRegressionPredictions: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();
            return std::make_unique<dsp::adapters::LinearRegressionPredictions>(windowSize);
        };

        // Factory for LMS Adaptive Filter stage
        m_stageFactories["lmsFilter"] = [](const Napi::Object &params)
        {
            if (!params.Has("numTaps"))
            {
                throw std::invalid_argument("LmsFilter: 'numTaps' is required");
            }
            size_t numTaps = params.Get("numTaps").As<Napi::Number>().Uint32Value();

            // Optional parameters with defaults
            float learningRate = 0.01f; // Default mu
            if (params.Has("learningRate") || params.Has("mu"))
            {
                learningRate = params.Has("learningRate")
                                   ? params.Get("learningRate").As<Napi::Number>().FloatValue()
                                   : params.Get("mu").As<Napi::Number>().FloatValue();
            }

            bool normalized = false;
            if (params.Has("normalized"))
            {
                normalized = params.Get("normalized").As<Napi::Boolean>().Value();
            }

            float lambda = 0.0f;
            if (params.Has("lambda"))
            {
                lambda = params.Get("lambda").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<dsp::LmsStage>(numTaps, learningRate, normalized, lambda);
        };

        // Factory for RLS Adaptive Filter stage
        m_stageFactories["rlsFilter"] = [](const Napi::Object &params)
        {
            if (!params.Has("numTaps"))
            {
                throw std::invalid_argument("RlsFilter: 'numTaps' is required");
            }
            size_t numTaps = params.Get("numTaps").As<Napi::Number>().Uint32Value();

            if (!params.Has("lambda"))
            {
                throw std::invalid_argument("RlsFilter: 'lambda' (forgetting factor) is required");
            }
            float lambda = params.Get("lambda").As<Napi::Number>().FloatValue();

            // Optional delta parameter (regularization)
            float delta = 0.01f; // Default
            if (params.Has("delta"))
            {
                delta = params.Get("delta").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<dsp::adapters::RlsStage>(numTaps, lambda, delta);
        };

        // Factory for Wavelet Transform stage
        m_stageFactories["waveletTransform"] = [](const Napi::Object &params)
        {
            if (!params.Has("wavelet"))
            {
                throw std::invalid_argument("WaveletTransform: 'wavelet' is required (e.g., 'haar', 'db2', 'db4')");
            }
            std::string waveletName = params.Get("wavelet").As<Napi::String>().Utf8Value();

            return std::make_unique<dsp::adapters::WaveletTransformStage>(waveletName);
        };

        // Factory for Hilbert Envelope stage
        m_stageFactories["hilbertEnvelope"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("HilbertEnvelope: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();

            size_t hopSize = windowSize / 2; // Default: 50% overlap
            if (params.Has("hopSize"))
            {
                hopSize = params.Get("hopSize").As<Napi::Number>().Uint32Value();
            }

            return std::make_unique<dsp::adapters::HilbertEnvelopeStage>(windowSize, hopSize);
        };

        // Factory for STFT (Short-Time Fourier Transform) stage
        m_stageFactories["stft"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("STFT: 'windowSize' is required");
            }
            size_t windowSize = params.Get("windowSize").As<Napi::Number>().Uint32Value();

            // Get optional parameters with defaults
            size_t hopSize = windowSize / 2; // Default: 50% overlap
            if (params.Has("hopSize"))
            {
                hopSize = params.Get("hopSize").As<Napi::Number>().Uint32Value();
            }

            std::string method = "fft"; // Default: fft
            if (params.Has("method"))
            {
                method = params.Get("method").As<Napi::String>().Utf8Value();
            }

            std::string type = "real"; // Default: real
            if (params.Has("type"))
            {
                type = params.Get("type").As<Napi::String>().Utf8Value();
            }

            bool forward = true; // Default: forward transform
            if (params.Has("forward"))
            {
                forward = params.Get("forward").As<Napi::Boolean>().Value();
            }

            std::string output = "magnitude"; // Default: magnitude
            if (params.Has("output"))
            {
                output = params.Get("output").As<Napi::String>().Utf8Value();
            }

            std::string window = "hann"; // Default: hann window
            if (params.Has("window"))
            {
                window = params.Get("window").As<Napi::String>().Utf8Value();
            }

            return std::make_unique<dsp::adapters::StftStage>(
                windowSize, hopSize, method, type, forward, output, window);
        };

        // Factory for FFT (Fast Fourier Transform) stage
        m_stageFactories["fft"] = [](const Napi::Object &params)
        {
            if (!params.Has("size"))
            {
                throw std::invalid_argument("FFT: 'size' is required");
            }
            size_t size = params.Get("size").As<Napi::Number>().Uint32Value();

            // Parse transform type (fft, dft, rfft, rdft, etc.)
            std::string typeStr = "rfft"; // Default: real FFT
            if (params.Has("type"))
            {
                typeStr = params.Get("type").As<Napi::String>().Utf8Value();
            }
            dsp::adapters::FftStage::TransformType type =
                dsp::adapters::FftStage::parseTransformType(typeStr);

            // Parse forward/inverse
            bool forward = true; // Default: forward
            if (params.Has("forward"))
            {
                forward = params.Get("forward").As<Napi::Boolean>().Value();
            }

            // Parse output format
            std::string outputStr = "magnitude"; // Default: magnitude
            if (params.Has("output"))
            {
                outputStr = params.Get("output").As<Napi::String>().Utf8Value();
            }
            dsp::adapters::FftStage::OutputFormat output =
                dsp::adapters::FftStage::parseOutputFormat(outputStr);

            return std::make_unique<dsp::adapters::FftStage>(size, type, forward, output);
        };

        // Factory for Mel Spectrogram stage
        m_stageFactories["melSpectrogram"] = [](const Napi::Object &params)
        {
            if (!params.Has("filterbankMatrix") || !params.Has("numBins") || !params.Has("numMelBands"))
            {
                throw std::invalid_argument("MelSpectrogram: requires 'filterbankMatrix', 'numBins', 'numMelBands'");
            }

            Napi::Float32Array filterbankArray = params.Get("filterbankMatrix").As<Napi::Float32Array>();
            size_t numBins = params.Get("numBins").As<Napi::Number>().Uint32Value();
            size_t numMelBands = params.Get("numMelBands").As<Napi::Number>().Uint32Value();

            std::vector<float> filterbank(filterbankArray.Data(),
                                          filterbankArray.Data() + filterbankArray.ElementLength());

            return std::make_unique<dsp::adapters::MelSpectrogramStage>(
                filterbank, numBins, numMelBands);
        };

        // Factory for MFCC stage
        m_stageFactories["mfcc"] = [](const Napi::Object &params)
        {
            if (!params.Has("numMelBands"))
            {
                throw std::invalid_argument("MFCC: 'numMelBands' is required");
            }

            size_t numMelBands = params.Get("numMelBands").As<Napi::Number>().Uint32Value();

            // Optional parameters with defaults
            size_t numCoefficients = 13; // Default: 13 MFCCs
            if (params.Has("numCoefficients"))
            {
                numCoefficients = params.Get("numCoefficients").As<Napi::Number>().Uint32Value();
            }

            bool useLogEnergy = true; // Default: apply log
            if (params.Has("useLogEnergy"))
            {
                useLogEnergy = params.Get("useLogEnergy").As<Napi::Boolean>().Value();
            }

            float lifterCoefficient = 0.0f; // Default: no liftering
            if (params.Has("lifterCoefficient"))
            {
                lifterCoefficient = params.Get("lifterCoefficient").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<dsp::adapters::MfccStage>(
                numMelBands, numCoefficients, useLogEnergy, lifterCoefficient);
        };

        // Factory for PCA Transform stage
        m_stageFactories["pcaTransform"] = [](const Napi::Object &params)
        {
            if (!params.Has("pcaMatrix") || !params.Has("mean") ||
                !params.Has("numChannels") || !params.Has("numComponents"))
            {
                throw std::invalid_argument("PcaTransform: requires 'pcaMatrix', 'mean', 'numChannels', 'numComponents'");
            }

            Napi::Float32Array matrixArray = params.Get("pcaMatrix").As<Napi::Float32Array>();
            Napi::Float32Array meanArray = params.Get("mean").As<Napi::Float32Array>();
            int numChannels = params.Get("numChannels").As<Napi::Number>().Int32Value();
            int numComponents = params.Get("numComponents").As<Napi::Number>().Int32Value();

            std::vector<float> matrix(matrixArray.Data(), matrixArray.Data() + matrixArray.ElementLength());
            std::vector<float> mean(meanArray.Data(), meanArray.Data() + meanArray.ElementLength());

            return std::make_unique<dsp::adapters::MatrixTransformStage>(
                matrix, mean, numChannels, numComponents, "pca");
        };

        // Factory for ICA Transform stage
        m_stageFactories["icaTransform"] = [](const Napi::Object &params)
        {
            if (!params.Has("icaMatrix") || !params.Has("mean") ||
                !params.Has("numChannels") || !params.Has("numComponents"))
            {
                throw std::invalid_argument("IcaTransform: requires 'icaMatrix', 'mean', 'numChannels', 'numComponents'");
            }

            Napi::Float32Array matrixArray = params.Get("icaMatrix").As<Napi::Float32Array>();
            Napi::Float32Array meanArray = params.Get("mean").As<Napi::Float32Array>();
            int numChannels = params.Get("numChannels").As<Napi::Number>().Int32Value();
            int numComponents = params.Get("numComponents").As<Napi::Number>().Int32Value();

            std::vector<float> matrix(matrixArray.Data(), matrixArray.Data() + matrixArray.ElementLength());
            std::vector<float> mean(meanArray.Data(), meanArray.Data() + meanArray.ElementLength());

            return std::make_unique<dsp::adapters::MatrixTransformStage>(
                matrix, mean, numChannels, numComponents, "ica");
        };

        // Factory for Whitening Transform stage
        m_stageFactories["whiteningTransform"] = [](const Napi::Object &params)
        {
            if (!params.Has("whiteningMatrix") || !params.Has("mean") ||
                !params.Has("numChannels") || !params.Has("numComponents"))
            {
                throw std::invalid_argument("WhiteningTransform: requires 'whiteningMatrix', 'mean', 'numChannels', 'numComponents'");
            }

            Napi::Float32Array matrixArray = params.Get("whiteningMatrix").As<Napi::Float32Array>();
            Napi::Float32Array meanArray = params.Get("mean").As<Napi::Float32Array>();
            int numChannels = params.Get("numChannels").As<Napi::Number>().Int32Value();
            int numComponents = params.Get("numComponents").As<Napi::Number>().Int32Value();

            std::vector<float> matrix(matrixArray.Data(), matrixArray.Data() + matrixArray.ElementLength());
            std::vector<float> mean(meanArray.Data(), meanArray.Data() + meanArray.ElementLength());

            return std::make_unique<dsp::adapters::MatrixTransformStage>(
                matrix, mean, numChannels, numComponents, "whiten");
        };

        // Factory for GSC Preprocessor stage (adaptive beamforming)
        m_stageFactories["gscPreprocessor"] = [](const Napi::Object &params)
        {
            if (!params.Has("numChannels") || !params.Has("steeringWeights") ||
                !params.Has("blockingMatrix"))
            {
                throw std::invalid_argument("GscPreprocessor: requires 'numChannels', 'steeringWeights', 'blockingMatrix'");
            }

            int numChannels = params.Get("numChannels").As<Napi::Number>().Int32Value();
            Napi::Float32Array steeringArray = params.Get("steeringWeights").As<Napi::Float32Array>();
            Napi::Float32Array blockingArray = params.Get("blockingMatrix").As<Napi::Float32Array>();

            std::vector<float> steeringWeights(steeringArray.Data(), steeringArray.Data() + steeringArray.ElementLength());
            std::vector<float> blockingMatrix(blockingArray.Data(), blockingArray.Data() + blockingArray.ElementLength());

            return std::make_unique<dsp::adapters::GscPreprocessorStage>(
                numChannels, steeringWeights, blockingMatrix);
        };

        // Factory for Channel Selector stage
        m_stageFactories["channelSelector"] = [](const Napi::Object &params)
        {
            if (!params.Has("numInputChannels") || !params.Has("numOutputChannels"))
            {
                throw std::invalid_argument("ChannelSelector: requires 'numInputChannels', 'numOutputChannels'");
            }

            int numInputChannels = params.Get("numInputChannels").As<Napi::Number>().Int32Value();
            int numOutputChannels = params.Get("numOutputChannels").As<Napi::Number>().Int32Value();

            return std::make_unique<dsp::adapters::ChannelSelectorStage>(
                numInputChannels, numOutputChannels);
        };

        // Channel Select (by indices) factory
        m_stageFactories["channelSelect"] = [](const Napi::Object &params)
        {
            if (!params.Has("channels") || !params.Has("numInputChannels"))
            {
                throw std::invalid_argument("ChannelSelect: requires 'channels' array and 'numInputChannels'");
            }

            Napi::Array channelsArray = params.Get("channels").As<Napi::Array>();
            std::vector<int> channels;
            for (uint32_t i = 0; i < channelsArray.Length(); ++i)
            {
                channels.push_back(channelsArray.Get(i).As<Napi::Number>().Int32Value());
            }

            int numInputChannels = params.Get("numInputChannels").As<Napi::Number>().Int32Value();

            return std::make_unique<dsp::adapters::ChannelSelectStage>(
                channels, numInputChannels);
        };

        // Channel Merge factory
        m_stageFactories["channelMerge"] = [](const Napi::Object &params)
        {
            if (!params.Has("mapping") || !params.Has("numInputChannels"))
            {
                throw std::invalid_argument("ChannelMerge: requires 'mapping' array and 'numInputChannels'");
            }

            Napi::Array mappingArray = params.Get("mapping").As<Napi::Array>();
            std::vector<int> mapping;
            for (uint32_t i = 0; i < mappingArray.Length(); ++i)
            {
                mapping.push_back(mappingArray.Get(i).As<Napi::Number>().Int32Value());
            }

            int numInputChannels = params.Get("numInputChannels").As<Napi::Number>().Int32Value();

            return std::make_unique<dsp::adapters::ChannelMergeStage>(
                mapping, numInputChannels);
        };

        // ===================================================================
        // Filter Bank Stage
        // ===================================================================
        m_stageFactories["filterBank"] = [](const Napi::Object &params)
        {
            if (!params.Has("definitions") || !params.Has("inputChannels"))
            {
                throw std::invalid_argument("FilterBank: requires 'definitions' array and 'inputChannels'");
            }

            // Extract input channel count
            int inputChannels = params.Get("inputChannels").As<Napi::Number>().Int32Value();

            // Extract filter definitions array
            Napi::Array defsArray = params.Get("definitions").As<Napi::Array>();
            std::vector<dsp::adapters::FilterDefinition> definitions;
            definitions.reserve(defsArray.Length());

            for (uint32_t i = 0; i < defsArray.Length(); ++i)
            {
                Napi::Object defObj = defsArray.Get(i).As<Napi::Object>();

                // Extract 'b' coefficients (feedforward)
                if (!defObj.Has("b"))
                {
                    throw std::invalid_argument("FilterBank: Each definition must have 'b' coefficients");
                }
                Napi::Array bArray = defObj.Get("b").As<Napi::Array>();
                std::vector<double> b;
                b.reserve(bArray.Length());
                for (uint32_t j = 0; j < bArray.Length(); ++j)
                {
                    b.push_back(bArray.Get(j).As<Napi::Number>().DoubleValue());
                }

                // Extract 'a' coefficients (feedback)
                if (!defObj.Has("a"))
                {
                    throw std::invalid_argument("FilterBank: Each definition must have 'a' coefficients");
                }
                Napi::Array aArray = defObj.Get("a").As<Napi::Array>();
                std::vector<double> a;
                a.reserve(aArray.Length());
                for (uint32_t j = 0; j < aArray.Length(); ++j)
                {
                    a.push_back(aArray.Get(j).As<Napi::Number>().DoubleValue());
                }

                definitions.push_back({b, a});
            }

            return std::make_unique<dsp::adapters::FilterBankStage>(definitions, inputChannels);
        };

        // ===================================================================
        // Clip Detection Stage
        // ===================================================================
        m_stageFactories["clipDetection"] = [](const Napi::Object &params)
        {
            if (!params.Has("threshold"))
            {
                throw std::invalid_argument("ClipDetection: requires 'threshold' parameter");
            }

            float threshold = params.Get("threshold").As<Napi::Number>().FloatValue();

            return std::make_unique<dsp::adapters::ClipDetectionStage>(threshold);
        };

        // ===================================================================
        // Peak Detection Stage
        // ===================================================================
        m_stageFactories["peakDetection"] = [](const Napi::Object &params)
        {
            if (!params.Has("threshold"))
            {
                throw std::invalid_argument("PeakDetection: requires 'threshold' parameter");
            }

            if (!params.Has("mode"))
            {
                throw std::invalid_argument("PeakDetection: requires 'mode' parameter");
            }

            float threshold = params.Get("threshold").As<Napi::Number>().FloatValue();

            // Optional mode and domain parameters
            std::string mode = params.Has("mode")
                                   ? params.Get("mode").As<Napi::String>().Utf8Value()
                                   : "moving";
            std::string domain = params.Has("domain")
                                     ? params.Get("domain").As<Napi::String>().Utf8Value()
                                     : "time";

            // Get new optional windowSize and minPeakDistance
            int windowSize = params.Has("windowSize")
                                 ? params.Get("windowSize").As<Napi::Number>().Int32Value()
                                 : 3;
            int minPeakDistance = params.Has("minPeakDistance")
                                      ? params.Get("minPeakDistance").As<Napi::Number>().Int32Value()
                                      : 1;

            return std::make_unique<dsp::adapters::PeakDetectionStage>(threshold, mode, domain, windowSize, minPeakDistance);
        };

        // ===================================================================
        // Differentiator Stage
        // ===================================================================
        m_stageFactories["differentiator"] = [](const Napi::Object &params)
        {
            // No parameters needed
            return std::make_unique<dsp::adapters::DifferentiatorStage>();
        };

        // ===================================================================
        // Square Stage
        // ===================================================================
        m_stageFactories["square"] = [](const Napi::Object &params)
        {
            // Stateless operation - no parameters needed
            return std::make_unique<dsp::adapters::SquareStage>();
        };

        // Amplify (Gain) stage
        m_stageFactories["amplify"] = [](const Napi::Object &params)
        {
            float gain = 1.0f; // Default gain (no change)

            if (params.Has("gain"))
            {
                gain = params.Get("gain").As<Napi::Number>().FloatValue();

                if (gain <= 0.0f)
                {
                    throw std::invalid_argument("Amplify gain must be positive");
                }
            }

            return std::make_unique<dsp::adapters::AmplifyStage>(gain);
        };

        // Integrator stage (IIR leaky integrator)
        m_stageFactories["integrator"] = [](const Napi::Object &params)
        {
            float alpha = 0.99f; // Default leakage coefficient

            if (params.Has("alpha"))
            {
                alpha = params.Get("alpha").As<Napi::Number>().FloatValue();

                if (alpha <= 0.0f || alpha > 1.0f)
                {
                    throw std::invalid_argument("Integrator alpha must be in range (0, 1]");
                }
            }

            return std::make_unique<dsp::adapters::IntegratorStage>(alpha);
        };

        // SNR stage (Signal-to-Noise Ratio in dB)
        m_stageFactories["snr"] = [](const Napi::Object &params)
        {
            if (!params.Has("windowSize"))
            {
                throw std::invalid_argument("SNR stage requires 'windowSize' parameter");
            }

            size_t window_size = params.Get("windowSize").As<Napi::Number>().Uint32Value();

            if (window_size == 0)
            {
                throw std::invalid_argument("SNR windowSize must be greater than 0");
            }

            return std::make_unique<dsp::adapters::SnrStage>(window_size);
        };

        // ===================================================================
        // GENERIC FILTER RESTORATION (FIR & IIR)
        // ===================================================================
        auto filterFactory = [](const Napi::Object &params)
        {
            if (!params.Has("bCoeffs") || !params.Has("aCoeffs"))
            {
                throw std::invalid_argument("FilterStage: State missing 'bCoeffs' or 'aCoeffs'. Cannot reconstruct.");
            }

            Napi::Array bArray = params.Get("bCoeffs").As<Napi::Array>();
            Napi::Array aArray = params.Get("aCoeffs").As<Napi::Array>();

            std::vector<double> bCoeffs(bArray.Length());
            for (uint32_t i = 0; i < bArray.Length(); ++i)
            {
                bCoeffs[i] = bArray.Get(i).As<Napi::Number>().DoubleValue();
            }

            std::vector<double> aCoeffs(aArray.Length());
            for (uint32_t i = 0; i < aArray.Length(); ++i)
            {
                aCoeffs[i] = aArray.Get(i).As<Napi::Number>().DoubleValue();
            }

            return std::make_unique<dsp::adapters::FilterStage>(bCoeffs, aCoeffs);
        };

        m_stageFactories["filter:fir"] = filterFactory;
        m_stageFactories["filter:iir"] = filterFactory;

        // ===================================================================
        // Kalman Filter Stage
        // ===================================================================
        m_stageFactories["kalmanFilter"] = [](const Napi::Object &params)
        {
            int dimensions = 2;
            float processNoise = 1e-5f;
            float measurementNoise = 1e-2f;
            float initialError = 1.0f;

            if (params.Has("dimensions"))
            {
                dimensions = params.Get("dimensions").As<Napi::Number>().Int32Value();
            }
            if (params.Has("processNoise"))
            {
                processNoise = params.Get("processNoise").As<Napi::Number>().FloatValue();
            }
            if (params.Has("measurementNoise"))
            {
                measurementNoise = params.Get("measurementNoise").As<Napi::Number>().FloatValue();
            }
            if (params.Has("initialError"))
            {
                initialError = params.Get("initialError").As<Napi::Number>().FloatValue();
            }

            return std::make_unique<adapters::KalmanFilterStage>(
                dimensions, processNoise, measurementNoise, initialError);
        };

        // ===================================================================
        // Time Alignment Stage
        // ===================================================================
        m_stageFactories["timeAlignment"] = [](const Napi::Object &params)
        {
            float targetSampleRate = params.Has("targetSampleRate")
                                         ? params.Get("targetSampleRate").As<Napi::Number>().FloatValue()
                                         : 1000.0f;

            adapters::InterpolationMethod interpMethod = adapters::InterpolationMethod::LINEAR;
            if (params.Has("interpolationMethod"))
            {
                std::string method = params.Get("interpolationMethod").As<Napi::String>().Utf8Value();
                if (method == "cubic")
                    interpMethod = adapters::InterpolationMethod::CUBIC;
                else if (method == "sinc")
                    interpMethod = adapters::InterpolationMethod::SINC;
            }

            adapters::GapPolicy gapPolicy = adapters::GapPolicy::INTERPOLATE;
            if (params.Has("gapPolicy"))
            {
                std::string policy = params.Get("gapPolicy").As<Napi::String>().Utf8Value();
                if (policy == "error")
                    gapPolicy = adapters::GapPolicy::ERROR;
                else if (policy == "zero-fill")
                    gapPolicy = adapters::GapPolicy::ZERO_FILL;
                else if (policy == "hold")
                    gapPolicy = adapters::GapPolicy::HOLD;
                else if (policy == "extrapolate")
                    gapPolicy = adapters::GapPolicy::EXTRAPOLATE;
            }

            float gapThreshold = params.Has("gapThreshold")
                                     ? params.Get("gapThreshold").As<Napi::Number>().FloatValue()
                                     : 1.5f;

            adapters::DriftCompensation driftComp = adapters::DriftCompensation::NONE;
            if (params.Has("driftCompensation"))
            {
                std::string drift = params.Get("driftCompensation").As<Napi::String>().Utf8Value();
                if (drift == "regression")
                    driftComp = adapters::DriftCompensation::REGRESSION;
                else if (drift == "pll")
                    driftComp = adapters::DriftCompensation::PLL;
            }

            return std::make_unique<adapters::TimeAlignmentStage>(
                targetSampleRate, interpMethod, gapPolicy, gapThreshold, driftComp);
        };
    }

    /**
     * This is the "Factory" method.
     * TS calls: native.addStage("movingAverage", { windowSize: 100 })
     */
    Napi::Value DspPipeline::AddStage(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::AddStage - this=" << this << std::endl;
        }

        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] AddStage - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (*m_isBusy)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] AddStage - pipeline busy, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Cannot add stage while processing").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // 1. Get arguments from TypeScript
        std::string stageName = info[0].As<Napi::String>();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] AddStage - stageName=" << stageName << ", this=" << this << std::endl;
        }
        Napi::Object params = info[1].As<Napi::Object>();

        // 2. Look up the stage factory in the map
        auto it = m_stageFactories.find(stageName);
        if (it != m_stageFactories.end())
        {
            try
            {
                // Factory found - create and add the stage
                m_stages.push_back(it->second(params));
            }
            catch (const std::invalid_argument &e)
            {
                // Validation error in constructor - throw as JavaScript TypeError
                Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Undefined();
            }
            catch (const std::exception &e)
            {
                // Other errors - throw as JavaScript Error
                Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        else
        {
            // Unknown stage type - throw error
            Napi::TypeError::New(env, "Unknown stage type: " + stageName).ThrowAsJavaScriptException();
        }

        return env.Undefined();
    }

    /**
     * Add a generic filter stage to the pipeline.
     * TS calls: native.addFilterStage(bCoeffs, aCoeffs)
     */
    Napi::Value DspPipeline::AddFilterStage(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::AddFilterStage - this=" << this << std::endl;
        }

        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] AddFilterStage - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (*m_isBusy)
        {
            Napi::Error::New(env, "Cannot add filter stage while processing").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (info.Length() < 2 || !info[0].IsTypedArray() || !info[1].IsTypedArray())
        {
            Napi::TypeError::New(env, "Expected two Float64Arrays (b and a coefficients) as arguments").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        try
        {
            // 1. Get coefficients from TypeScript (zero-copy)
            Napi::Float64Array bCoeffsArray = info[0].As<Napi::Float64Array>();
            Napi::Float64Array aCoeffsArray = info[1].As<Napi::Float64Array>();

            // 2. Convert to std::vector<double>
            std::vector<double> bCoeffs(bCoeffsArray.Data(), bCoeffsArray.Data() + bCoeffsArray.ElementLength());
            std::vector<double> aCoeffs(aCoeffsArray.Data(), aCoeffsArray.Data() + aCoeffsArray.ElementLength());

            // 3. Create and add the stage
            m_stages.push_back(std::make_unique<dsp::adapters::FilterStage>(bCoeffs, aCoeffs));
        }
        catch (const std::invalid_argument &e)
        {
            Napi::TypeError::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        return env.Undefined();
    }

    /**
     * SIMD-optimized timestamp interpolation for resizing stages
     * Multi-platform support:
     * - AVX2 (x86_64): 8-wide vectorization
     * - SSE2 (x86): 4-wide vectorization
     * - NEON (ARM): 4-wide vectorization
     * - Scalar fallback for all other platforms
     *
     * @param timestamps Source timestamp array (channel-major layout)
     * @param prevNumSamples Number of samples in source
     * @param prevChannels Number of channels in source
     * @param numOutputSamples Number of samples to generate
     * @param outputChannels Number of channels in output
     * @param timeScale Time scaling factor from stage
     * @param output Output timestamp vector
     */
    inline void interpolateTimestampsSIMD(
        const float *timestamps,
        size_t prevNumSamples,
        int prevChannels,
        size_t numOutputSamples,
        int outputChannels,
        double timeScale,
        std::vector<float> &output)
    {
#if HAS_AVX2
        // ========================================
        // AVX2 Implementation (8-wide)
        // ========================================
        // Process 8 output samples at a time with AVX2
        const size_t simdWidth = 8;
        const size_t simdIterations = numOutputSamples / simdWidth;
        const size_t remainder = numOutputSamples % simdWidth;

        // Precompute constants for SIMD
        const __m256 vTimeScale = _mm256_set1_ps(static_cast<float>(timeScale));
        const __m256i vPrevChannels = _mm256_set1_epi32(prevChannels);
        const __m256 vPrevNumSamples = _mm256_set1_ps(static_cast<float>(prevNumSamples));
        const __m256 vOne = _mm256_set1_ps(1.0f);

        // SIMD loop: Process 8 timestamps at once
        for (size_t iter = 0; iter < simdIterations; ++iter)
        {
            size_t baseIdx = iter * simdWidth;

            // Generate indices: [baseIdx, baseIdx+1, ..., baseIdx+7]
            __m256 vIdx = _mm256_set_ps(
                static_cast<float>(baseIdx + 7),
                static_cast<float>(baseIdx + 6),
                static_cast<float>(baseIdx + 5),
                static_cast<float>(baseIdx + 4),
                static_cast<float>(baseIdx + 3),
                static_cast<float>(baseIdx + 2),
                static_cast<float>(baseIdx + 1),
                static_cast<float>(baseIdx + 0));

            // Calculate input time: i * timeScale
            __m256 vInputTime = _mm256_mul_ps(vIdx, vTimeScale);

            // Extract integer and fractional parts
            __m256i vInputIdx = _mm256_cvttps_epi32(vInputTime);
            __m256 vInputIdxFloat = _mm256_cvtepi32_ps(vInputIdx);
            __m256 vFrac = _mm256_sub_ps(vInputTime, vInputIdxFloat);

            // Process each of the 8 values (can't easily vectorize the conditional logic)
            alignas(32) float inputTimes[8];
            alignas(32) int inputIndices[8];
            alignas(32) float fracs[8];

            _mm256_store_ps(inputTimes, vInputTime);
            _mm256_store_si256((__m256i *)inputIndices, vInputIdx);
            _mm256_store_ps(fracs, vFrac);

            for (size_t j = 0; j < simdWidth; ++j)
            {
                size_t i = baseIdx + j;
                size_t inputIdx = inputIndices[j];
                float frac = fracs[j];
                float timestamp;

                if (inputIdx >= prevNumSamples)
                {
                    size_t lastIdx = prevNumSamples - 1;
                    timestamp = timestamps[lastIdx * prevChannels] +
                                static_cast<float>((inputTimes[j] - lastIdx) * timeScale);
                }
                else if (inputIdx + 1 >= prevNumSamples)
                {
                    timestamp = timestamps[inputIdx * prevChannels];
                }
                else
                {
                    float t0 = timestamps[inputIdx * prevChannels];
                    float t1 = timestamps[(inputIdx + 1) * prevChannels];
                    timestamp = t0 + frac * (t1 - t0);
                }

                // Replicate timestamp across all output channels
                for (int ch = 0; ch < outputChannels; ++ch)
                {
                    output[i * outputChannels + ch] = timestamp;
                }
            }
        }

        // Handle remainder samples with scalar code
        for (size_t i = simdIterations * simdWidth; i < numOutputSamples; ++i)
        {
            double inputTime = i * timeScale;
            size_t inputIdx = static_cast<size_t>(inputTime);
            double frac = inputTime - inputIdx;
            float timestamp;

            if (inputIdx >= prevNumSamples)
            {
                size_t lastIdx = prevNumSamples - 1;
                timestamp = timestamps[lastIdx * prevChannels] +
                            static_cast<float>((inputTime - lastIdx) * timeScale);
            }
            else if (inputIdx + 1 >= prevNumSamples)
            {
                timestamp = timestamps[inputIdx * prevChannels];
            }
            else
            {
                float t0 = timestamps[inputIdx * prevChannels];
                float t1 = timestamps[(inputIdx + 1) * prevChannels];
                timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
            }

            for (int ch = 0; ch < outputChannels; ++ch)
            {
                output[i * outputChannels + ch] = timestamp;
            }
        }
#elif HAS_SSE
        // ========================================
        // SSE2 Implementation (4-wide)
        // ========================================
        const size_t simdWidth = 4;
        const size_t simdIterations = numOutputSamples / simdWidth;

        const __m128 vTimeScale = _mm_set1_ps(static_cast<float>(timeScale));
        const __m128 vPrevNumSamples = _mm_set1_ps(static_cast<float>(prevNumSamples));

        for (size_t iter = 0; iter < simdIterations; ++iter)
        {
            size_t baseIdx = iter * simdWidth;

            // Generate indices [baseIdx, baseIdx+1, baseIdx+2, baseIdx+3]
            alignas(16) float indices[4] = {
                static_cast<float>(baseIdx),
                static_cast<float>(baseIdx + 1),
                static_cast<float>(baseIdx + 2),
                static_cast<float>(baseIdx + 3)};
            __m128 vIndices = _mm_load_ps(indices);
            __m128 vInputTime = _mm_mul_ps(vIndices, vTimeScale);

            // Convert to int and back to get integer part
            __m128i vInputIdx = _mm_cvttps_epi32(vInputTime);
            __m128 vInputIdxFloat = _mm_cvtepi32_ps(vInputIdx);
            __m128 vFrac = _mm_sub_ps(vInputTime, vInputIdxFloat);

            // Store for scalar processing
            alignas(16) float inputTimes[4];
            _mm_store_ps(inputTimes, vInputTime);
            alignas(16) int inputIndices[4];
            _mm_store_si128(reinterpret_cast<__m128i *>(inputIndices), vInputIdx);
            alignas(16) float fractions[4];
            _mm_store_ps(fractions, vFrac);

            // Process each sample
            for (size_t j = 0; j < simdWidth; ++j)
            {
                size_t i = baseIdx + j;
                size_t inputIdx = inputIndices[j];
                double frac = fractions[j];
                float timestamp;

                if (inputIdx >= prevNumSamples)
                {
                    size_t lastIdx = prevNumSamples - 1;
                    timestamp = timestamps[lastIdx * prevChannels] +
                                static_cast<float>((inputTimes[j] - lastIdx) * timeScale);
                }
                else if (inputIdx + 1 >= prevNumSamples)
                {
                    timestamp = timestamps[inputIdx * prevChannels];
                }
                else
                {
                    float t0 = timestamps[inputIdx * prevChannels];
                    float t1 = timestamps[(inputIdx + 1) * prevChannels];
                    timestamp = t0 + frac * (t1 - t0);
                }

                for (int ch = 0; ch < outputChannels; ++ch)
                {
                    output[i * outputChannels + ch] = timestamp;
                }
            }
        }

        // Handle remainder
        for (size_t i = simdIterations * simdWidth; i < numOutputSamples; ++i)
        {
            double inputTime = i * timeScale;
            size_t inputIdx = static_cast<size_t>(inputTime);
            double frac = inputTime - inputIdx;
            float timestamp;

            if (inputIdx >= prevNumSamples)
            {
                size_t lastIdx = prevNumSamples - 1;
                timestamp = timestamps[lastIdx * prevChannels] +
                            static_cast<float>((inputTime - lastIdx) * timeScale);
            }
            else if (inputIdx + 1 >= prevNumSamples)
            {
                timestamp = timestamps[inputIdx * prevChannels];
            }
            else
            {
                float t0 = timestamps[inputIdx * prevChannels];
                float t1 = timestamps[(inputIdx + 1) * prevChannels];
                timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
            }

            for (int ch = 0; ch < outputChannels; ++ch)
            {
                output[i * outputChannels + ch] = timestamp;
            }
        }
#elif HAS_NEON
        // ========================================
        // ARM NEON Implementation (4-wide)
        // ========================================
        const size_t simdWidth = 4;
        const size_t simdIterations = numOutputSamples / simdWidth;

        const float32x4_t vTimeScale = vdupq_n_f32(static_cast<float>(timeScale));
        const float32x4_t vPrevNumSamples = vdupq_n_f32(static_cast<float>(prevNumSamples));

        for (size_t iter = 0; iter < simdIterations; ++iter)
        {
            size_t baseIdx = iter * simdWidth;

            // Generate indices
            alignas(16) float indices[4] = {
                static_cast<float>(baseIdx),
                static_cast<float>(baseIdx + 1),
                static_cast<float>(baseIdx + 2),
                static_cast<float>(baseIdx + 3)};
            float32x4_t vIndices = vld1q_f32(indices);
            float32x4_t vInputTime = vmulq_f32(vIndices, vTimeScale);

            // Extract integer and fractional parts
            int32x4_t vInputIdx = vcvtq_s32_f32(vInputTime);
            float32x4_t vInputIdxFloat = vcvtq_f32_s32(vInputIdx);
            float32x4_t vFrac = vsubq_f32(vInputTime, vInputIdxFloat);

            // Store for processing
            alignas(16) float inputTimes[4];
            vst1q_f32(inputTimes, vInputTime);
            alignas(16) int inputIndices[4];
            vst1q_s32(inputIndices, vInputIdx);
            alignas(16) float fractions[4];
            vst1q_f32(fractions, vFrac);

            // Process each sample
            for (size_t j = 0; j < simdWidth; ++j)
            {
                size_t i = baseIdx + j;
                size_t inputIdx = inputIndices[j];
                double frac = fractions[j];
                float timestamp;

                if (inputIdx >= prevNumSamples)
                {
                    size_t lastIdx = prevNumSamples - 1;
                    timestamp = timestamps[lastIdx * prevChannels] +
                                static_cast<float>((inputTimes[j] - lastIdx) * timeScale);
                }
                else if (inputIdx + 1 >= prevNumSamples)
                {
                    timestamp = timestamps[inputIdx * prevChannels];
                }
                else
                {
                    float t0 = timestamps[inputIdx * prevChannels];
                    float t1 = timestamps[(inputIdx + 1) * prevChannels];
                    timestamp = t0 + frac * (t1 - t0);
                }

                for (int ch = 0; ch < outputChannels; ++ch)
                {
                    output[i * outputChannels + ch] = timestamp;
                }
            }
        }

        // Handle remainder
        for (size_t i = simdIterations * simdWidth; i < numOutputSamples; ++i)
        {
            double inputTime = i * timeScale;
            size_t inputIdx = static_cast<size_t>(inputTime);
            double frac = inputTime - inputIdx;
            float timestamp;

            if (inputIdx >= prevNumSamples)
            {
                size_t lastIdx = prevNumSamples - 1;
                timestamp = timestamps[lastIdx * prevChannels] +
                            static_cast<float>((inputTime - lastIdx) * timeScale);
            }
            else if (inputIdx + 1 >= prevNumSamples)
            {
                timestamp = timestamps[inputIdx * prevChannels];
            }
            else
            {
                float t0 = timestamps[inputIdx * prevChannels];
                float t1 = timestamps[(inputIdx + 1) * prevChannels];
                timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
            }

            for (int ch = 0; ch < outputChannels; ++ch)
            {
                output[i * outputChannels + ch] = timestamp;
            }
        }
#elif HAS_SSE
        // ========================================
        // SSE2 Implementation (4-wide)
        // ========================================
        const size_t simdWidth = 4;
        const size_t simdIterations = numOutputSamples / simdWidth;

        const __m128 vTimeScale = _mm_set1_ps(static_cast<float>(timeScale));
        const __m128 vPrevNumSamples = _mm_set1_ps(static_cast<float>(prevNumSamples));

        for (size_t iter = 0; iter < simdIterations; ++iter)
        {
            size_t baseIdx = iter * simdWidth;

            // Generate indices [baseIdx, baseIdx+1, baseIdx+2, baseIdx+3]
            alignas(16) float indices[4] = {
                static_cast<float>(baseIdx),
                static_cast<float>(baseIdx + 1),
                static_cast<float>(baseIdx + 2),
                static_cast<float>(baseIdx + 3)};
            __m128 vIndices = _mm_load_ps(indices);
            __m128 vInputTime = _mm_mul_ps(vIndices, vTimeScale);

            // Convert to int and back to get integer part
            __m128i vInputIdx = _mm_cvttps_epi32(vInputTime);
            __m128 vInputIdxFloat = _mm_cvtepi32_ps(vInputIdx);
            __m128 vFrac = _mm_sub_ps(vInputTime, vInputIdxFloat);

            // Store for scalar processing
            alignas(16) float inputTimes[4];
            _mm_store_ps(inputTimes, vInputTime);
            alignas(16) int inputIndices[4];
            _mm_store_si128(reinterpret_cast<__m128i *>(inputIndices), vInputIdx);
            alignas(16) float fractions[4];
            _mm_store_ps(fractions, vFrac);

            // Process each sample
            for (size_t j = 0; j < simdWidth; ++j)
            {
                size_t i = baseIdx + j;
                size_t inputIdx = inputIndices[j];
                double frac = fractions[j];
                float timestamp;

                if (inputIdx >= prevNumSamples)
                {
                    size_t lastIdx = prevNumSamples - 1;
                    timestamp = timestamps[lastIdx * prevChannels] +
                                static_cast<float>((inputTimes[j] - lastIdx) * timeScale);
                }
                else if (inputIdx + 1 >= prevNumSamples)
                {
                    timestamp = timestamps[inputIdx * prevChannels];
                }
                else
                {
                    float t0 = timestamps[inputIdx * prevChannels];
                    float t1 = timestamps[(inputIdx + 1) * prevChannels];
                    timestamp = t0 + frac * (t1 - t0);
                }

                for (int ch = 0; ch < outputChannels; ++ch)
                {
                    output[i * outputChannels + ch] = timestamp;
                }
            }
        }

        // Handle remainder
        for (size_t i = simdIterations * simdWidth; i < numOutputSamples; ++i)
        {
            double inputTime = i * timeScale;
            size_t inputIdx = static_cast<size_t>(inputTime);
            double frac = inputTime - inputIdx;
            float timestamp;

            if (inputIdx >= prevNumSamples)
            {
                size_t lastIdx = prevNumSamples - 1;
                timestamp = timestamps[lastIdx * prevChannels] +
                            static_cast<float>((inputTime - lastIdx) * timeScale);
            }
            else if (inputIdx + 1 >= prevNumSamples)
            {
                timestamp = timestamps[inputIdx * prevChannels];
            }
            else
            {
                float t0 = timestamps[inputIdx * prevChannels];
                float t1 = timestamps[(inputIdx + 1) * prevChannels];
                timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
            }

            for (int ch = 0; ch < outputChannels; ++ch)
            {
                output[i * outputChannels + ch] = timestamp;
            }
        }
#elif HAS_NEON
        // ========================================
        // ARM NEON Implementation (4-wide)
        // ========================================
        const size_t simdWidth = 4;
        const size_t simdIterations = numOutputSamples / simdWidth;

        const float32x4_t vTimeScale = vdupq_n_f32(static_cast<float>(timeScale));
        const float32x4_t vPrevNumSamples = vdupq_n_f32(static_cast<float>(prevNumSamples));

        for (size_t iter = 0; iter < simdIterations; ++iter)
        {
            size_t baseIdx = iter * simdWidth;

            // Generate indices
            alignas(16) float indices[4] = {
                static_cast<float>(baseIdx),
                static_cast<float>(baseIdx + 1),
                static_cast<float>(baseIdx + 2),
                static_cast<float>(baseIdx + 3)};
            float32x4_t vIndices = vld1q_f32(indices);
            float32x4_t vInputTime = vmulq_f32(vIndices, vTimeScale);

            // Extract integer and fractional parts
            int32x4_t vInputIdx = vcvtq_s32_f32(vInputTime);
            float32x4_t vInputIdxFloat = vcvtq_f32_s32(vInputIdx);
            float32x4_t vFrac = vsubq_f32(vInputTime, vInputIdxFloat);

            // Store for processing
            alignas(16) float inputTimes[4];
            vst1q_f32(inputTimes, vInputTime);
            alignas(16) int inputIndices[4];
            vst1q_s32(inputIndices, vInputIdx);
            alignas(16) float fractions[4];
            vst1q_f32(fractions, vFrac);

            // Process each sample
            for (size_t j = 0; j < simdWidth; ++j)
            {
                size_t i = baseIdx + j;
                size_t inputIdx = inputIndices[j];
                double frac = fractions[j];
                float timestamp;

                if (inputIdx >= prevNumSamples)
                {
                    size_t lastIdx = prevNumSamples - 1;
                    timestamp = timestamps[lastIdx * prevChannels] +
                                static_cast<float>((inputTimes[j] - lastIdx) * timeScale);
                }
                else if (inputIdx + 1 >= prevNumSamples)
                {
                    timestamp = timestamps[inputIdx * prevChannels];
                }
                else
                {
                    float t0 = timestamps[inputIdx * prevChannels];
                    float t1 = timestamps[(inputIdx + 1) * prevChannels];
                    timestamp = t0 + frac * (t1 - t0);
                }

                for (int ch = 0; ch < outputChannels; ++ch)
                {
                    output[i * outputChannels + ch] = timestamp;
                }
            }
        }

        // Handle remainder
        for (size_t i = simdIterations * simdWidth; i < numOutputSamples; ++i)
        {
            double inputTime = i * timeScale;
            size_t inputIdx = static_cast<size_t>(inputTime);
            double frac = inputTime - inputIdx;
            float timestamp;

            if (inputIdx >= prevNumSamples)
            {
                size_t lastIdx = prevNumSamples - 1;
                timestamp = timestamps[lastIdx * prevChannels] +
                            static_cast<float>((inputTime - lastIdx) * timeScale);
            }
            else if (inputIdx + 1 >= prevNumSamples)
            {
                timestamp = timestamps[inputIdx * prevChannels];
            }
            else
            {
                float t0 = timestamps[inputIdx * prevChannels];
                float t1 = timestamps[(inputIdx + 1) * prevChannels];
                timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
            }

            for (int ch = 0; ch < outputChannels; ++ch)
            {
                output[i * outputChannels + ch] = timestamp;
            }
        }
#else
        // ========================================
        // Scalar Fallback (universal)
        // ========================================
        for (size_t i = 0; i < numOutputSamples; ++i)
        {
            double inputTime = i * timeScale;
            size_t inputIdx = static_cast<size_t>(inputTime);
            double frac = inputTime - inputIdx;
            float timestamp;

            if (inputIdx >= prevNumSamples)
            {
                size_t lastIdx = prevNumSamples - 1;
                timestamp = timestamps[lastIdx * prevChannels] +
                            static_cast<float>((inputTime - lastIdx) * timeScale);
            }
            else if (inputIdx + 1 >= prevNumSamples)
            {
                timestamp = timestamps[inputIdx * prevChannels];
            }
            else
            {
                float t0 = timestamps[inputIdx * prevChannels];
                float t1 = timestamps[(inputIdx + 1) * prevChannels];
                timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
            }

            for (int ch = 0; ch < outputChannels; ++ch)
            {
                output[i * outputChannels + ch] = timestamp;
            }
        }
#endif
    }

    /**
     * AsyncWorker for processing DSP pipeline in background thread
     */
    class ProcessWorker : public Napi::AsyncWorker
    {
    public:
        ProcessWorker(Napi::Env env,
                      Napi::Promise::Deferred deferred,
                      std::vector<std::unique_ptr<IDspStage>> &stages,
                      float *data,
                      float *timestamps,
                      double sampleRate,
                      size_t numSamples,
                      int channels,
                      Napi::Reference<Napi::Float32Array> &&bufferRef,
                      Napi::Reference<Napi::Float32Array> &&timestampRef,
                      std::shared_ptr<std::atomic<bool>> busyLock)
            : Napi::AsyncWorker(env),
              m_deferred(std::move(deferred)),
              m_stages(stages),
              m_data(data),
              m_timestamps(timestamps),
              m_sampleRate(sampleRate),
              m_numSamples(numSamples),
              m_channels(channels),
              m_bufferRef(std::move(bufferRef)),
              m_timestampRef(std::move(timestampRef)),
              m_busyLock(busyLock)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessWorker::ProcessWorker - this=" << this << std::endl;
            }
            m_stageCount = m_stages.size();
            m_stageTypes.reserve(m_stageCount);
            for (const auto &stage : m_stages)
            {
                m_stageTypes.push_back(stage->getType());
            }
        }

    protected:
        // This runs on a worker thread (not blocking the event loop)
        void Execute() override
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessWorker::Execute - START, this=" << this
                          << ", data=" << m_data << ", numSamples=" << m_numSamples
                          << ", channels=" << m_channels << std::endl;
                std::cout << "[WORKER-" << std::this_thread::get_id() << "] Execute START (stages="
                          << m_stages.size() << ")" << std::endl;
            }

            // CRITICAL FIX: Use a unique_ptr for timestamp ownership
            std::vector<float> generatedTimestamps;
            std::unique_ptr<std::vector<float>> allocatedTimestamps;

            try
            {
                // 1. Generate Timestamps if missing
                if (m_timestamps == nullptr)
                {
                    if (isDebugEnabled())
                    {
                        std::cout << "[DEBUG] Execute - generating timestamps, sampleRate=" << m_sampleRate << std::endl;
                    }

                    generatedTimestamps.resize(m_numSamples);
                    double dt = (m_sampleRate > 0.0) ? (1000.0 / m_sampleRate) : 1.0;

                    for (size_t i = 0; i < m_numSamples; ++i)
                    {
                        generatedTimestamps[i] = static_cast<float>(i * dt);
                    }

                    m_timestamps = generatedTimestamps.data();
                    if (isDebugEnabled())
                    {
                        std::cout << "[DEBUG] Execute - timestamps generated, addr=" << m_timestamps << std::endl;
                    }
                }

                // 2. Process the buffer through all stages
                float *currentBuffer = m_data;
                size_t currentSize = m_numSamples;
                float *tempBuffer = nullptr;
                bool usingTempBuffer = false;

                const bool debugStageDumps = std::getenv("DSPX_DEBUG_STAGE_DUMPS") != nullptr;

                if (isDebugEnabled())
                {
                    std::cout << "[DEBUG] Execute - processing through " << m_stages.size() << " stages" << std::endl;
                }
                for (size_t stageIdx = 0; stageIdx < m_stages.size(); ++stageIdx)
                {
                    const auto &stage = m_stages[stageIdx];

                    if (isDebugEnabled())
                    {
                        std::cout << "[DEBUG] Execute - stage " << stageIdx << ", type="
                                  << stage->getType() << ", addr=" << stage.get()
                                  << ", isResizing=" << stage->isResizing() << std::endl;
                    }

                    if (stage->isResizing())
                    {
                        // Calculate output size estimate
                        size_t estimatedSize = stage->calculateOutputSize(currentSize);

                        // Allocate buffer with estimate
                        float *outputBuffer = new float[estimatedSize];

                        if (isDebugEnabled())
                        {
                            std::cout << "[DEBUG] Execute - allocated output buffer, size=" << estimatedSize
                                      << ", addr=" << outputBuffer << std::endl;
                        }

                        // CRITICAL: Save the PREVIOUS size before processResizing updates currentSize
                        size_t prevSize = currentSize;

                        size_t actualOutputSize = 0;
                        stage->processResizing(currentBuffer, currentSize,
                                               outputBuffer, actualOutputSize,
                                               m_channels, m_timestamps);

                        // Safety check: if actual size exceeds estimate, reallocate
                        if (actualOutputSize > estimatedSize)
                        {
                            std::cerr << "[WARNING] Stage calculateOutputSize() underestimated: "
                                      << "estimated=" << estimatedSize
                                      << ", actual=" << actualOutputSize << std::endl;

                            // Reallocate with correct size and copy data
                            float *newBuffer = new float[actualOutputSize];
                            std::memcpy(newBuffer, outputBuffer, estimatedSize * sizeof(float));
                            delete[] outputBuffer;
                            outputBuffer = newBuffer;
                        }

                        if (isDebugEnabled())
                        {
                            std::cout << "[DEBUG] Execute - stage " << stageIdx << " resized: "
                                      << prevSize << " -> " << actualOutputSize // Use prevSize!
                                      << ", buffer=" << outputBuffer << std::endl;
                        }

                        // Free previous temp buffer if we owned it
                        if (usingTempBuffer && tempBuffer != nullptr)
                        {
                            if (isDebugEnabled())
                            {
                                std::cout << "[DEBUG] Execute - freeing previous temp buffer=" << tempBuffer << std::endl;
                            }
                            delete[] tempBuffer;
                        }

                        // Update buffer tracking
                        tempBuffer = outputBuffer;
                        currentBuffer = outputBuffer;
                        currentSize = actualOutputSize;
                        usingTempBuffer = true;

                        // Save previous channel count BEFORE updating
                        int prevChannels = m_channels;

                        // Update channel count if stage changed it
                        int outputChannels = stage->getOutputChannels();
                        if (outputChannels > 0)
                        {
                            if (isDebugEnabled())
                            {
                                std::cout << "[DEBUG] Execute - channels changed: " << m_channels
                                          << " -> " << outputChannels << std::endl;
                            }
                            m_channels = outputChannels;
                        }

                        // Re-interpolate timestamps if needed
                        if (m_timestamps != nullptr)
                        {
                            if (isDebugEnabled())
                            {
                                std::cout << "[DEBUG] Execute - reinterpolating timestamps" << std::endl;
                            }

                            double timeScale = stage->getTimeScaleFactor();
                            size_t numOutputSamples = actualOutputSize / m_channels;

                            // CRITICAL FIX: Use prevSize and prevChannels!
                            size_t prevNumSamples = prevSize / prevChannels;

                            // Create new timestamp vector
                            auto newTimestamps = std::make_unique<std::vector<float>>(actualOutputSize);

                            // Use SIMD-optimized interpolation
                            interpolateTimestampsSIMD(
                                m_timestamps,
                                prevNumSamples,
                                prevChannels,
                                numOutputSamples,
                                m_channels,
                                timeScale,
                                *newTimestamps);

                            // CRITICAL FIX: Transfer ownership safely
                            allocatedTimestamps = std::move(newTimestamps);
                            m_timestamps = allocatedTimestamps->data();

                            if (isDebugEnabled())
                            {
                                std::cout << "[DEBUG] Execute - timestamps reinterpolated (SIMD), new addr="
                                          << m_timestamps << std::endl;
                            }
                        }
                    }
                    else
                    {
                        // In-place processing
                        if (isDebugEnabled())
                        {
                            std::cout << "[DEBUG] Execute - stage " << stageIdx << " in-place processing, buffer="
                                      << currentBuffer << ", size=" << currentSize << std::endl;
                        }
                        stage->process(currentBuffer, currentSize, m_channels, m_timestamps);

                        if (debugStageDumps)
                        {
                            const char *stype = stage->getType();
                            size_t toShow = std::min<size_t>(8, currentSize);
                            if (isDebugEnabled())
                            {
                                std::cout << "[DUMP] after '" << stype << "':";
                            }
                            for (size_t i = 0; i < toShow; ++i)
                            {
                                std::cout << (i == 0 ? ' ' : ',') << currentBuffer[i];
                            }
                            std::cout << std::endl;
                        }
                    }
                }

                m_finalBuffer = currentBuffer;
                m_finalSize = currentSize;
                m_ownsBuffer = usingTempBuffer;

                if (isDebugEnabled())
                {
                    std::cout << "[DEBUG] Execute - COMPLETE, finalBuffer=" << m_finalBuffer
                              << ", finalSize=" << m_finalSize << ", ownsBuffer=" << m_ownsBuffer << std::endl;
                }
            }
            catch (const std::exception &e)
            {
                if (isDebugEnabled())
                {
                    std::cout << "[DEBUG] Execute - EXCEPTION: " << e.what() << ", this=" << this << std::endl;
                    std::cout << "[WORKER-" << std::this_thread::get_id() << "] EXCEPTION: " << e.what() << std::endl;
                }
                SetError(e.what());
            }
        } // This runs on the main thread after Execute() completes

        void OnOK() override
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessWorker::OnOK - START, this=" << this
                          << ", finalBuffer=" << (void *)m_finalBuffer << ", finalSize=" << m_finalSize << std::endl;
            }
            *m_busyLock = false; // unlock the pipeline

            Napi::Env env = Env();

            // Create a new Float32Array with the final buffer size
            Napi::Float32Array outputArray = Napi::Float32Array::New(env, m_finalSize);

            // Copy final data to the output array
            std::memcpy(outputArray.Data(), m_finalBuffer, m_finalSize * sizeof(float));

            // Clean up temporary buffer if we allocated one
            if (m_ownsBuffer)
            {
                if (isDebugEnabled())
                {
                    std::cout << "[DEBUG] OnOK - deleting owned temp buffer=" << (void *)m_finalBuffer << std::endl;
                }
                delete[] m_finalBuffer;
            }

            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessWorker::OnOK - resolving promise, this=" << this << std::endl;
            }
            // Resolve the promise with the processed buffer
            m_deferred.Resolve(outputArray);
        }

        void OnError(const Napi::Error &error) override
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessWorker::OnError - START, this=" << this
                          << ", error=" << error.Message() << std::endl;
            }
            m_deferred.Reject(error.Value());
            *m_busyLock = false; // unlock the pipeline
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessWorker::OnError - promise rejected, this=" << this << std::endl;
            }
        }

    private:
        Napi::Promise::Deferred m_deferred;
        std::vector<std::unique_ptr<IDspStage>> &m_stages;
        size_t m_stageCount;
        std::vector<std::string> m_stageTypes;
        float *m_data;
        float *m_timestamps;
        double m_sampleRate;
        size_t m_numSamples;
        int m_channels;
        Napi::Reference<Napi::Float32Array> m_bufferRef;
        Napi::Reference<Napi::Float32Array> m_timestampRef;

        // For handling resized buffers
        float *m_finalBuffer = nullptr;
        size_t m_finalSize = 0;
        bool m_ownsBuffer = false;

        // For managing allocated timestamp buffer
        std::unique_ptr<float[]> m_timestampBuffer;

        std::shared_ptr<std::atomic<bool>> m_busyLock; // Pointer to the busy lock
    };

    /**
     * This is the "Process" method.
     * TS calls:
     *   await native.process(buffer, timestamps, { channels: 4 })
     * or (legacy):
     *   await native.process(buffer, { sampleRate: 2000, channels: 4 })
     * Returns a Promise that resolves when processing is complete.
     */
    Napi::Value DspPipeline::ProcessAsync(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::ProcessAsync - this=" << this << std::endl;
        }

        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessAsync - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (*m_isBusy)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessAsync - pipeline busy, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is busy: Cannot call process() while another operation is running.").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (!info[0].IsTypedArray())
        {
            Napi::TypeError::New(env, "Argument 0 must be a Float32Array").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::Float32Array jsBuffer = info[0].As<Napi::Float32Array>();
        float *data = jsBuffer.Data();
        size_t numSamples = jsBuffer.ElementLength();

        Napi::Float32Array jsTimestamps;
        float *timestamps = nullptr;
        Napi::Object options;
        double sampleRate = 0.0;

        if (info.Length() >= 3 && info[1].IsTypedArray())
        {
            // Mode A: Explicit Timestamps
            jsTimestamps = info[1].As<Napi::Float32Array>();
            timestamps = jsTimestamps.Data();
            options = info[2].As<Napi::Object>();

            if (jsTimestamps.ElementLength() != numSamples)
            {
                Napi::TypeError::New(env, "Timestamp array length must match sample array length").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        else
        {
            // Mode B: Implicit Timestamps
            // If info[1] exists and is an object, use it as options.
            if (info.Length() >= 2 && info[1].IsObject())
            {
                options = info[1].As<Napi::Object>();
            }
            else
            {
                options = Napi::Object::New(env);
            }
        }

        // Extract options safely
        if (options.Has("sampleRate"))
        {
            sampleRate = options.Get("sampleRate").As<Napi::Number>().DoubleValue();
        }

        int channels = 1;
        if (options.Has("channels"))
        {
            channels = options.Get("channels").As<Napi::Number>().Uint32Value();
        }

        Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
        Napi::Promise promise = deferred.Promise();

        Napi::Reference<Napi::Float32Array> bufferRef = Napi::Reference<Napi::Float32Array>::New(jsBuffer, 1);
        Napi::Reference<Napi::Float32Array> timestampRef;
        if (timestamps != nullptr)
        {
            timestampRef = Napi::Reference<Napi::Float32Array>::New(jsTimestamps, 1);
        }

        *m_isBusy = true; // lock the pipeline

        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] ProcessAsync - creating worker, data=" << (void *)data
                      << ", numSamples=" << numSamples << ", channels=" << channels
                      << ", this=" << this << std::endl;
        }

        ProcessWorker *worker = new ProcessWorker(env, std::move(deferred), m_stages, data, timestamps, sampleRate, numSamples, channels, std::move(bufferRef), std::move(timestampRef), m_isBusy);

        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] ProcessAsync - queuing worker=" << (void *)worker
                      << ", this=" << this << std::endl;
        }

        worker->Queue();

        return promise;
    }

    /**
     * This is the "ProcessSync" method.
     * TS calls:
     *  await native.processSync(buffer, timestamps, { channels: 4 })
     * or (legacy):
     *  await native.processSync(buffer, { sampleRate: 2000, channels: 4 })
     * Returns the processed buffer directly.
     */

    Napi::Value DspPipeline::ProcessSync(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::ProcessSync - this=" << this << std::endl;
        }

        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessSync - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (*m_isBusy)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ProcessSync - pipeline busy, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is busy: Cannot call processSync() while an async operation is running.").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (info.Length() < 1 || !info[0].IsTypedArray())
        {
            Napi::TypeError::New(env, "Buffer required (Float32Array)").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        Napi::Float32Array jsBuffer = info[0].As<Napi::Float32Array>();
        float *data = jsBuffer.Data();
        size_t numSamples = jsBuffer.ElementLength();

        Napi::Float32Array jsTimestamps;
        float *timestamps = nullptr;
        Napi::Object options;
        double sampleRate = 0.0;

        if (info.Length() >= 3 && info[1].IsTypedArray())
        {
            // Mode A: Explicit Timestamps
            jsTimestamps = info[1].As<Napi::Float32Array>();
            timestamps = jsTimestamps.Data();
            options = info[2].As<Napi::Object>();

            if (jsTimestamps.ElementLength() != numSamples)
            {
                Napi::TypeError::New(env, "Timestamp array length must match sample array length").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        else
        {
            // Mode B: Implicit Timestamps
            // If info[1] exists and is an object, use it as options.
            if (info.Length() >= 2 && info[1].IsObject())
            {
                options = info[1].As<Napi::Object>();
            }
            else
            {
                options = Napi::Object::New(env);
            }
        }

        // Extract options safely
        if (options.Has("sampleRate"))
        {
            sampleRate = options.Get("sampleRate").As<Napi::Number>().DoubleValue();
        }

        int channels = 1;
        if (options.Has("channels"))
        {
            channels = options.Get("channels").As<Napi::Number>().Uint32Value();
        }

        Napi::Reference<Napi::Float32Array> bufferRef = Napi::Reference<Napi::Float32Array>::New(jsBuffer, 1);
        Napi::Reference<Napi::Float32Array> timestampRef;
        if (timestamps != nullptr)
        {
            timestampRef = Napi::Reference<Napi::Float32Array>::New(jsTimestamps, 1);
        }

        // --- Core Processing Logic (Direct Execution) ---

        float *currentData = data;
        size_t currentSize = numSamples;
        std::vector<float> tempBuffer; // Safe RAII container
        bool isDetached = false;

        try
        {
            for (const auto &stage : m_stages)
            {
                if (stage->isResizing())
                {
                    size_t outputSize = stage->calculateOutputSize(currentSize);
                    std::vector<float> nextBuffer(outputSize);
                    size_t actualOutputSize = 0;

                    stage->processResizing(currentData, currentSize, nextBuffer.data(), actualOutputSize, channels, timestamps);

                    tempBuffer = std::move(nextBuffer);
                    currentData = tempBuffer.data();
                    currentSize = actualOutputSize;
                    isDetached = true;

                    if (stage->getOutputChannels() > 0)
                        channels = stage->getOutputChannels();
                }
                else
                {
                    stage->process(currentData, currentSize, channels, timestamps);
                }
            }
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // 4. Return
        if (isDetached)
        {
            Napi::Float32Array outputArray = Napi::Float32Array::New(env, currentSize);
            std::memcpy(outputArray.Data(), currentData, currentSize * sizeof(float));
            return outputArray;
        }
        else
        {
            return jsBuffer;
        }
    }

    /**
     * Save current pipeline state as JSON string
     * TypeScript will handle storing this in Redis
     *
     * Returns: JSON string with pipeline configuration and stage states
     */
    Napi::Value DspPipeline::SaveState(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::SaveState - this=" << this
                      << ", stages=" << m_stages.size() << std::endl;
        }

        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] SaveState - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // Check for format option
        bool useToon = false;
        if (info.Length() > 0 && info[0].IsObject())
        {
            Napi::Object options = info[0].As<Napi::Object>();
            if (options.Has("format"))
            {
                std::string fmt = options.Get("format").As<Napi::String>().Utf8Value();
                if (fmt == "toon")
                    useToon = true;
            }
        }

        if (useToon)
        {
            // --- Original compact binary TOON path ---
            try
            {
                dsp::toon::Serializer serializer;
                serializer.startObject();
                serializer.writeString("timestamp");
                serializer.writeDouble(static_cast<double>(std::time(nullptr)));
                serializer.writeString("stageCount");
                serializer.writeInt32(static_cast<int32_t>(m_stages.size()));
                serializer.writeString("stages");
                serializer.startArray();
                for (const auto &stage : m_stages)
                {
                    serializer.startObject();
                    serializer.writeString("type");
                    serializer.writeString(stage->getType());
                    serializer.writeString("state");
                    stage->serializeToon(serializer);
                    serializer.endObject();
                }
                serializer.endArray();
                serializer.endObject();
                return Napi::Buffer<uint8_t>::Copy(env, serializer.buffer.data(), serializer.buffer.size());
            }
            catch (const std::exception &e)
            {
                Napi::Error::New(env, std::string("TOON Save Failed: ") + e.what()).ThrowAsJavaScriptException();
                return env.Null();
            }
        }
        else
        {
            // --- Legacy JSON Path ---
            Napi::Object stateObj = Napi::Object::New(env);

            // Save timestamp
            stateObj.Set("timestamp", static_cast<double>(std::time(nullptr)));

            // Save pipeline configuration and full state
            Napi::Array stagesArray = Napi::Array::New(env, m_stages.size());

            for (size_t i = 0; i < m_stages.size(); ++i)
            {
                Napi::Object stageConfig = Napi::Object::New(env);

                stageConfig.Set("index", static_cast<uint32_t>(i));
                stageConfig.Set("type", m_stages[i]->getType());

                // Serialize the stage's internal state
                stageConfig.Set("state", m_stages[i]->serializeState(env));

                stagesArray.Set(static_cast<uint32_t>(i), stageConfig);
            }

            stateObj.Set("stages", stagesArray);
            stateObj.Set("stageCount", static_cast<uint32_t>(m_stages.size()));

            // Convert to JSON string using JavaScript's JSON.stringify
            Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
            Napi::Function stringify = JSON.Get("stringify").As<Napi::Function>();
            return stringify.Call(JSON, {stateObj});
        }
    }

    /**
     * Load pipeline state from JSON string or TOON Buffer with validation.
     *
     * RESILIENCE FEATURES:
     * - Validates stage types and count before loading
     * - TOON path: Validates upfront, deserializes in-place
     * - JSON path: Build newStages → Validate → Atomic swap (fully transactional)
     * - Prevents pipeline corruption from incompatible state
     *
     * BEHAVIOR:
     * - Only loads state if saved stages match current stages EXACTLY (type and count)
     * - TOON: Validates all stages before deserialization, fails fast on mismatch
     * - JSON: Builds temporary stages, only swaps if fully successful
     * - Throws error with detailed message if validation or loading fails
     *
     * ERROR HANDLING:
     * - Stage count mismatch: Abort immediately (TOON may have partial changes)
     * - Stage type mismatch: Abort immediately (TOON may have partial changes)
     * - Deserialization error: Throws (TOON may have partial state changes)
     * - Note: TOON format modifies stages in-place, cannot rollback
     *
     * @param info[0] - JSON string or TOON Buffer containing pipeline state
     * @returns Boolean - true if successful
     * @throws Error if validation or deserialization fails
     */
    Napi::Value DspPipeline::LoadState(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::LoadState - this=" << this
                      << ", current stages=" << m_stages.size() << std::endl;
        }
        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] LoadState - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (info.Length() < 1)
        {
            Napi::TypeError::New(env, "Expected state (String or Buffer) as first argument")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // --- TOON Path ---
        // Accept Node Buffer, Uint8Array, or ArrayBuffer
        const uint8_t *toonDataPtr = nullptr;
        size_t toonDataLen = 0;

        if (info[0].IsBuffer())
        {
            Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
            toonDataPtr = buffer.Data();
            toonDataLen = buffer.Length();
        }
        else if (info[0].IsTypedArray())
        {
            Napi::TypedArray ta = info[0].As<Napi::TypedArray>();
            Napi::ArrayBuffer ab = ta.ArrayBuffer();
            toonDataPtr = static_cast<const uint8_t *>(ab.Data()) + ta.ByteOffset();
            toonDataLen = ta.ByteLength();
        }
        else if (info[0].IsArrayBuffer())
        {
            Napi::ArrayBuffer ab = info[0].As<Napi::ArrayBuffer>();
            toonDataPtr = static_cast<const uint8_t *>(ab.Data());
            toonDataLen = ab.ByteLength();
        }

        if (toonDataPtr != nullptr && toonDataLen > 0)
        {
            // TOON binary path with upfront validation
            try
            {
                const bool debugToon = std::getenv("DSPX_DEBUG_TOON") != nullptr;

                if (debugToon)
                {
                    std::cout << "[TOON] Parsing and validating TOON buffer" << std::endl;
                }

                dsp::toon::Deserializer deserializer(toonDataPtr, toonDataLen);
                deserializer.consumeToken(dsp::toon::T_OBJECT_START);
                std::string key = deserializer.readString();
                double timestamp = deserializer.readDouble();
                key = deserializer.readString();
                int32_t savedStageCount = deserializer.readInt32();

                // Validate stage count upfront
                if (static_cast<size_t>(savedStageCount) != m_stages.size())
                {
                    throw std::runtime_error("TOON Load: Stage count mismatch. Saved state has " +
                                             std::to_string(savedStageCount) + " stages, current has " +
                                             std::to_string(m_stages.size()) + ".");
                }

                key = deserializer.readString();
                deserializer.consumeToken(dsp::toon::T_ARRAY_START);

                // Validate stage types and deserialize
                size_t stageIdx = 0;
                while (deserializer.peekToken() != dsp::toon::T_ARRAY_END)
                {
                    if (stageIdx >= m_stages.size())
                    {
                        throw std::runtime_error("TOON Load: Unexpected extra stage in buffer at index " +
                                                 std::to_string(stageIdx) + ".");
                    }

                    deserializer.consumeToken(dsp::toon::T_OBJECT_START);
                    deserializer.readString(); // "type" key
                    std::string savedType = deserializer.readString();

                    // Validate type matches BEFORE loading state
                    std::string currentType = m_stages[stageIdx]->getType();
                    if (currentType != savedType)
                    {
                        throw std::runtime_error("TOON Load: Stage type mismatch at index " +
                                                 std::to_string(stageIdx) + ". Expected '" + currentType +
                                                 "', got '" + savedType + "'.");
                    }

                    deserializer.readString(); // "state" key

                    if (debugToon)
                    {
                        std::cout << "[TOON] Loading state into stage[" << stageIdx
                                  << "]: type='" << savedType << "'" << std::endl;
                    }

                    // Deserialize into the existing stage
                    m_stages[stageIdx]->deserializeToon(deserializer);

                    deserializer.consumeToken(dsp::toon::T_OBJECT_END);
                    stageIdx++;
                }

                // Final validation
                if (stageIdx != m_stages.size())
                {
                    throw std::runtime_error("TOON Load: Stage count mismatch after parsing. Expected " +
                                             std::to_string(m_stages.size()) + " stages, parsed " +
                                             std::to_string(stageIdx) + ".");
                }

                deserializer.consumeToken(dsp::toon::T_ARRAY_END);
                deserializer.consumeToken(dsp::toon::T_OBJECT_END);

                if (debugToon)
                {
                    std::cout << "[TOON] Successfully loaded " << stageIdx << " stages." << std::endl;
                }

                return Napi::Boolean::New(env, true);
            }
            catch (const std::exception &e)
            {
                std::string errorMsg = std::string("TOON Load failed: ") + e.what();
                Napi::Error::New(env, errorMsg).ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }

        // --- Legacy JSON Path ---
        if (!info[0].IsString())
        {
            Napi::TypeError::New(env, "Expected state JSON string or Buffer")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::string stateJson = info[0].As<Napi::String>().Utf8Value();

        try
        {
            Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
            Napi::Function parse = JSON.Get("parse").As<Napi::Function>();
            Napi::Object stateObj = parse.Call(JSON, {Napi::String::New(env, stateJson)}).As<Napi::Object>();

            if (!stateObj.Has("stages"))
            {
                Napi::Error::New(env, "Invalid state: missing 'stages' field")
                    .ThrowAsJavaScriptException();
                return Napi::Boolean::New(env, false);
            }

            Napi::Array stagesArray = stateObj.Get("stages").As<Napi::Array>();
            uint32_t savedStageCount = stagesArray.Length();

            // Temporary container for the new merged pipeline structure
            std::vector<std::unique_ptr<IDspStage>> newStages;

            // Index to track our position in the *current* pipeline (m_stages)
            size_t currentIdx = 0;

            for (uint32_t i = 0; i < savedStageCount; ++i)
            {
                Napi::Object stageConfig = stagesArray.Get(i).As<Napi::Object>();

                if (!stageConfig.Has("type") || !stageConfig.Has("state"))
                {
                    continue;
                }

                std::string savedType = stageConfig.Get("type").As<Napi::String>().Utf8Value();
                Napi::Object stageState = stageConfig.Get("state").As<Napi::Object>();

                // 1. Try to match with the current pipeline stage
                bool matched = false;
                if (currentIdx < m_stages.size())
                {
                    // Check if types match (e.g. both are "movingAverage")
                    if (std::string(m_stages[currentIdx]->getType()) == savedType)
                    {
                        // MATCH: Restore state into the existing stage instance
                        // This preserves any specific config the user might have set on the current stage
                        m_stages[currentIdx]->deserializeState(stageState);
                        newStages.push_back(std::move(m_stages[currentIdx]));
                        currentIdx++;
                        matched = true;
                    }
                }

                // 2. If mismatch, try to reconstruct the stage from the saved state
                if (!matched)
                {
                    auto it = m_stageFactories.find(savedType);
                    if (it != m_stageFactories.end())
                    {
                        try
                        {
                            // Attempt to create stage using the state object as the config params.
                            // (Assumes the state object contains necessary constructor args like 'windowSize')
                            auto newStage = it->second(stageState);

                            // Restore the full internal buffer state
                            // NOTE: Validation errors here should propagate (fail-fast)
                            newStage->deserializeState(stageState);

                            // Add to our new pipeline
                            newStages.push_back(std::move(newStage));
                        }
                        catch (const std::exception &e)
                        {
                            // Check if this is a validation error (contains keywords like "validation", "mismatch")
                            std::string errorMsg = e.what();
                            if (errorMsg.find("validation") != std::string::npos ||
                                errorMsg.find("mismatch") != std::string::npos ||
                                errorMsg.find("Validation") != std::string::npos ||
                                errorMsg.find("Mismatch") != std::string::npos)
                            {
                                // Validation error - propagate it (fail the entire load)
                                throw;
                            }

                            // Construction error (e.g., missing params) - log warning and skip
                            std::cerr << "Warning: Failed to reconstruct stage " << savedType
                                      << ": " << e.what() << std::endl;
                        }
                    }
                    else
                    {
                        // Factory not found (e.g. generic FilterStage 'filter:fir' which requires explicit coefficients)
                        std::cerr << "Warning: Unknown stage type '" << savedType
                                  << "' in saved state (no factory available). Skipping." << std::endl;
                    }
                }
            }

            // 3. Append any remaining stages from the user's current pipeline definition
            while (currentIdx < m_stages.size())
            {
                newStages.push_back(std::move(m_stages[currentIdx]));
                currentIdx++;
            }

            // Replace the pipeline with the new merged structure
            m_stages = std::move(newStages);

            return Napi::Boolean::New(env, true);
        }
        catch (const std::exception &e)
        {
            Napi::Error::New(env, std::string("Failed to load state: ") + e.what())
                .ThrowAsJavaScriptException();
            return Napi::Boolean::New(env, false);
        }
    }

    /**
     * Clear all pipeline state (reset all stages)
     * This resets filters to their initial state without removing them
     */
    Napi::Value DspPipeline::ClearState(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::ClearState - this=" << this
                      << ", stages=" << m_stages.size() << std::endl;
        }

        // Check if pipeline is disposed
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ClearState - pipeline disposed, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // Reset all stages
        for (size_t i = 0; i < m_stages.size(); ++i)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] ClearState - resetting stage " << i
                          << ", type=" << m_stages[i]->getType()
                          << ", addr=" << m_stages[i].get() << std::endl;
            }
            m_stages[i]->reset();
        }

        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] Pipeline state cleared (" << m_stages.size()
                      << " stages reset), this=" << this << std::endl;
        }

        return env.Undefined();
    }

    /**
     * List current pipeline state (summary information)
     * Returns a simplified view of the pipeline configuration
     * Useful for debugging and monitoring without parsing full JSON
     *
     * Returns: Object with pipeline summary (stage count, types, window sizes, etc.)
     */
    Napi::Value DspPipeline::ListState(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Check if pipeline is disposed
        if (m_disposed)
        {
            Napi::Error::New(env, "Pipeline is disposed").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        Napi::Object summary = Napi::Object::New(env);

        // Basic pipeline info
        summary.Set("stageCount", static_cast<uint32_t>(m_stages.size()));
        summary.Set("timestamp", static_cast<double>(std::time(nullptr)));

        // Create array of stage summaries
        Napi::Array stagesArray = Napi::Array::New(env, m_stages.size());

        for (size_t i = 0; i < m_stages.size(); ++i)
        {
            Napi::Object stageSummary = Napi::Object::New(env);

            // Basic stage info
            stageSummary.Set("index", static_cast<uint32_t>(i));
            stageSummary.Set("type", m_stages[i]->getType());

            // Get full state to extract key info
            Napi::Object fullState = m_stages[i]->serializeState(env);

            // Extract common fields (windowSize, numChannels, mode)
            if (fullState.Has("windowSize"))
            {
                stageSummary.Set("windowSize", fullState.Get("windowSize"));
            }

            if (fullState.Has("numChannels"))
            {
                stageSummary.Set("numChannels", fullState.Get("numChannels"));
            }

            if (fullState.Has("mode"))
            {
                stageSummary.Set("mode", fullState.Get("mode"));
            }

            // Add buffer occupancy info for stateful filters
            if (fullState.Has("channels"))
            {
                Napi::Array channels = fullState.Get("channels").As<Napi::Array>();
                if (channels.Length() > 0)
                {
                    Napi::Object firstChannel = channels.Get(uint32_t(0)).As<Napi::Object>();
                    if (firstChannel.Has("buffer"))
                    {
                        Napi::Array buffer = firstChannel.Get("buffer").As<Napi::Array>();
                        stageSummary.Set("bufferSize", buffer.Length());
                    }
                }
                stageSummary.Set("channelCount", channels.Length());
            }

            stagesArray.Set(static_cast<uint32_t>(i), stageSummary);
        }

        summary.Set("stages", stagesArray);

        return summary;
    }

    /**
     * Dispose of the pipeline and free all resources
     * This method ensures safe cleanup and prevents further use of the pipeline
     *
     * Behavior:
     * - Blocks disposal if async processing is currently running
     * - Clears all stages (triggers RAII cleanup of all stage resources)
     * - Marks pipeline as disposed to prevent future operations
     * - Safe to call multiple times (idempotent)
     */
    Napi::Value DspPipeline::Dispose(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] DspPipeline::Dispose - this=" << this
                      << ", stages=" << m_stages.size() << ", disposed=" << m_disposed << std::endl;
        }

        // Already disposed - silently succeed (idempotent behavior)
        if (m_disposed)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] Dispose - already disposed, this=" << this << std::endl;
            }
            return env.Undefined();
        }

        // Cannot dispose while processing is in progress
        if (*m_isBusy)
        {
            if (isDebugEnabled())
            {
                std::cout << "[DEBUG] Dispose - pipeline busy, cannot dispose, this=" << this << std::endl;
            }
            Napi::Error::New(env, "Cannot dispose pipeline: process() is still running.")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] Dispose - clearing " << m_stages.size()
                      << " stages, this=" << this << std::endl;
        }
        // Clear all stages - triggers RAII cleanup of all stage resources
        // This will:
        // - Free all stage internal buffers
        // - Free all filter state memory
        // - Free all adaptive filter memory arenas
        // - Free all detachable buffers
        // - Free timestamp and resize buffers
        m_stages.clear();
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] Dispose - stages cleared, this=" << this << std::endl;
        }

        // Reset busy flag (defensive programming)
        *m_isBusy = false;

        // Mark as disposed to prevent further operations
        m_disposed = true;
        if (isDebugEnabled())
        {
            std::cout << "[DEBUG] Dispose - complete, this=" << this << std::endl;
        }

        return env.Undefined();
    }

} // namespace dsp

// Forward declare FFT bindings init
namespace dsp
{
    void InitFftBindings(Napi::Env env, Napi::Object exports);
    Napi::Object InitMatrixBindings(Napi::Env env, Napi::Object exports);
    void RegisterFilterBankDesignBindings(Napi::Env env, Napi::Object exports);
    namespace bindings
    {
        Napi::Object InitUtilityBindings(Napi::Env env, Napi::Object exports);
    }
}

// This function is called by Node.js when the addon is loaded
Napi::Object InitAll(Napi::Env env, Napi::Object exports)
{
    // Initialize DspPipeline class
    dsp::DspPipeline::Init(env, exports);

    // Initialize FFT/DFT bindings
    dsp::InitFftBindings(env, exports);

    // Initialize FIR/IIR filter bindings
    dsp::InitFilterBindings(env, exports);

    // Initialize filter bank design utilities
    dsp::RegisterFilterBankDesignBindings(env, exports);

    // Initialize matrix analysis bindings (PCA, ICA, Whitening)
    dsp::InitMatrixBindings(env, exports);

    // Initialize utility functions (dot product, etc.)
    dsp::bindings::InitUtilityBindings(env, exports);

    return exports;
}

// This line registers the module
NODE_API_MODULE(dsp_addon, InitAll)