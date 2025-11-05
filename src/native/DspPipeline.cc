#include "DspPipeline.h"
#include "adapters/MovingAverageStage.h"     // Moving Average method
#include "adapters/RmsStage.h"               // RMS method
#include "adapters/RectifyStage.h"           // Rectify method
#include "adapters/VarianceStage.h"          // Variance method
#include "adapters/ZScoreNormalizeStage.h"   // Z-Score Normalize method
#include "adapters/MeanAbsoluteValueStage.h" // Mean Absolute Value method
#include "adapters/WaveformLengthStage.h"    // Waveform Length method
#include "adapters/SscStage.h"               // Slope Sign Change method
#include "adapters/WampStage.h"              // Willison Amplitude method
#include "adapters/FilterStage.h"            // Filter stage (FIR/IIR)
#include "adapters/InterpolatorStage.h"      // Interpolator (upsample)
#include "adapters/DecimatorStage.h"         // Decimator (downsample)
#include "adapters/ResamplerStage.h"         // Resampler (rational rate conversion)
#include "adapters/ConvolutionStage.h"       // Convolution stage
#include "adapters/LinearRegressionStage.h"  // Linear Regression stage
#include "adapters/LmsStage.h"               // LMS Adaptive Filter stage
#include "adapters/WaveletTransformStage.h"  // Wavelet Transform stage
#include "adapters/HilbertEnvelopeStage.h"   // Hilbert Envelope stage

namespace dsp
{
    // Forward declarations for bindings
    extern void InitFftBindings(Napi::Env env, Napi::Object exports);
    extern void InitFilterBindings(Napi::Env env, Napi::Object exports);
}

#include <iostream>
#include <ctime>

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

                                                                  // State management (for Redis persistence from TypeScript)
                                                                  InstanceMethod("saveState", &DspPipeline::SaveState),
                                                                  InstanceMethod("loadState", &DspPipeline::LoadState),
                                                                  InstanceMethod("clearState", &DspPipeline::ClearState),
                                                                  InstanceMethod("listState", &DspPipeline::ListState),
                                                              });

        exports.Set("DspPipeline", func);
        return exports;
    }

    // N-API Boilerplate: Constructor
    DspPipeline::DspPipeline(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<DspPipeline>(info)
    {
        // Config logic from TS (redis, stateKey) would go here
        InitializeStageFactories();
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
    }

    /**
     * This is the "Factory" method.
     * TS calls: native.addStage("movingAverage", { windowSize: 100 })
     */
    Napi::Value DspPipeline::AddStage(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // 1. Get arguments from TypeScript
        std::string stageName = info[0].As<Napi::String>();
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
                      size_t numSamples,
                      int channels,
                      Napi::Reference<Napi::Float32Array> &&bufferRef,
                      Napi::Reference<Napi::Float32Array> &&timestampRef)
            : Napi::AsyncWorker(env),
              m_deferred(std::move(deferred)),
              m_stages(stages),
              m_data(data),
              m_timestamps(timestamps),
              m_numSamples(numSamples),
              m_channels(channels),
              m_bufferRef(std::move(bufferRef)),
              m_timestampRef(std::move(timestampRef))
        {
        }

    protected:
        // This runs on a worker thread (not blocking the event loop)
        void Execute() override
        {
            try
            {
                // Process the buffer through all stages
                // Handle both in-place and resizing stages
                float *currentBuffer = m_data;
                size_t currentSize = m_numSamples;
                float *tempBuffer = nullptr;
                bool usingTempBuffer = false;

                for (const auto &stage : m_stages)
                {
                    if (stage->isResizing())
                    {
                        // Resizing stage: allocate new buffer
                        size_t outputSize = stage->calculateOutputSize(currentSize);
                        float *outputBuffer = new float[outputSize];

                        size_t actualOutputSize = 0;
                        stage->processResizing(currentBuffer, currentSize,
                                               outputBuffer, actualOutputSize,
                                               m_channels, m_timestamps);

                        // Free the previous temporary buffer if we allocated one
                        if (usingTempBuffer)
                        {
                            delete[] currentBuffer;
                        }

                        currentBuffer = outputBuffer;
                        currentSize = actualOutputSize;
                        usingTempBuffer = true;

                        // Adjust timestamps for resampled data
                        if (m_timestamps != nullptr)
                        {
                            double timeScale = stage->getTimeScaleFactor();
                            size_t numOutputSamples = actualOutputSize / m_channels;

                            // Allocate new timestamp buffer
                            float *newTimestamps = new float[actualOutputSize];

                            // Interpolate timestamps based on time scale
                            // For upsampling (timeScale < 1): more samples, smaller time steps
                            // For downsampling (timeScale > 1): fewer samples, larger time steps
                            for (size_t i = 0; i < numOutputSamples; ++i)
                            {
                                // Map output sample index to input time domain
                                double inputTime = i * timeScale;
                                size_t inputIdx = static_cast<size_t>(inputTime);
                                double frac = inputTime - inputIdx;

                                float timestamp;
                                if (inputIdx >= (m_numSamples / m_channels))
                                {
                                    // Beyond input range, extrapolate
                                    timestamp = m_timestamps[(m_numSamples / m_channels - 1) * m_channels] +
                                                static_cast<float>((inputTime - (m_numSamples / m_channels - 1)) * timeScale);
                                }
                                else if (inputIdx + 1 >= (m_numSamples / m_channels))
                                {
                                    // At boundary, use last timestamp
                                    timestamp = m_timestamps[inputIdx * m_channels];
                                }
                                else
                                {
                                    // Interpolate between two timestamps
                                    float t0 = m_timestamps[inputIdx * m_channels];
                                    float t1 = m_timestamps[(inputIdx + 1) * m_channels];
                                    timestamp = t0 + static_cast<float>(frac) * (t1 - t0);
                                }

                                // Replicate timestamp for all channels
                                for (int ch = 0; ch < m_channels; ++ch)
                                {
                                    newTimestamps[i * m_channels + ch] = timestamp;
                                }
                            }

                            // Replace old timestamps
                            // Note: We don't own the original m_timestamps, so don't delete it
                            m_timestamps = newTimestamps;
                            m_timestampBuffer.reset(newTimestamps);
                        }
                    }
                    else
                    {
                        // In-place stage: process directly
                        // If we're using a temp buffer, process it; otherwise process original
                        if (usingTempBuffer)
                        {
                            stage->process(currentBuffer, currentSize, m_channels, m_timestamps);
                        }
                        else
                        {
                            // First in-place stage on original buffer
                            stage->process(currentBuffer, currentSize, m_channels, m_timestamps);
                        }
                    }
                }

                // Store final result
                m_finalBuffer = currentBuffer;
                m_finalSize = currentSize;
                m_ownsBuffer = usingTempBuffer;
            }
            catch (const std::exception &e)
            {
                SetError(e.what());
            }
        }

        // This runs on the main thread after Execute() completes
        void OnOK() override
        {
            Napi::Env env = Env();

            // Create a new Float32Array with the final buffer size
            Napi::Float32Array outputArray = Napi::Float32Array::New(env, m_finalSize);
            float *outputData = outputArray.Data();

            // Copy final data to the output array
            std::memcpy(outputData, m_finalBuffer, m_finalSize * sizeof(float));

            // Clean up temporary buffer if we allocated one
            if (m_ownsBuffer)
            {
                delete[] m_finalBuffer;
            }

            // Resolve the promise with the processed buffer
            m_deferred.Resolve(outputArray);
        }

        void OnError(const Napi::Error &error) override
        {
            m_deferred.Reject(error.Value());
        }

    private:
        Napi::Promise::Deferred m_deferred;
        std::vector<std::unique_ptr<IDspStage>> &m_stages;
        float *m_data;
        float *m_timestamps;
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

        // 1. Get buffer from TypeScript (zero-copy)
        Napi::Float32Array jsBuffer = info[0].As<Napi::Float32Array>();
        float *data = jsBuffer.Data();
        size_t numSamples = jsBuffer.ElementLength();

        // 2. Get timestamps and options
        // TypeScript can pass either:
        //   process(buffer, timestamps, options) - new time-based API
        //   process(buffer, options) - legacy sample-based API (timestamps = nullptr)
        Napi::Float32Array jsTimestamps;
        float *timestamps = nullptr;
        Napi::Object options;

        if (info.Length() >= 2 && info[1].IsTypedArray())
        {
            // New API: timestamps provided
            jsTimestamps = info[1].As<Napi::Float32Array>();
            timestamps = jsTimestamps.Data();
            options = info[2].As<Napi::Object>();

            // Validate timestamp length matches sample length
            if (jsTimestamps.ElementLength() != numSamples)
            {
                Napi::TypeError::New(env, "Timestamp array length must match sample array length")
                    .ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        else
        {
            // Legacy API: no timestamps (will use sample indices)
            options = info[1].As<Napi::Object>();
        }

        int channels = options.Get("channels").As<Napi::Number>().Uint32Value();
        // int sampleRate = options.Get("sampleRate").As<Napi::Number>().Uint32Value();

        // 3. Create a deferred promise and get the promise before moving
        Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
        Napi::Promise promise = deferred.Promise();

        // 4. Create references to keep buffers alive during async operation
        Napi::Reference<Napi::Float32Array> bufferRef = Napi::Reference<Napi::Float32Array>::New(jsBuffer, 1);
        Napi::Reference<Napi::Float32Array> timestampRef;
        if (timestamps != nullptr)
        {
            timestampRef = Napi::Reference<Napi::Float32Array>::New(jsTimestamps, 1);
        }

        // 5. Create and queue the worker
        ProcessWorker *worker = new ProcessWorker(env, std::move(deferred), m_stages, data, timestamps, numSamples, channels, std::move(bufferRef), std::move(timestampRef));
        worker->Queue();

        // 6. Return the promise immediately
        return promise;
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

    /**
     * Load pipeline state from JSON string
     * TypeScript retrieves this from Redis and passes it here
     *
     * Accepts: JSON string with pipeline configuration
     */
    Napi::Value DspPipeline::LoadState(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate input
        if (info.Length() < 1 || !info[0].IsString())
        {
            Napi::TypeError::New(env, "Expected state JSON string as first argument")
                .ThrowAsJavaScriptException();
            return env.Undefined();
        }

        std::string stateJson = info[0].As<Napi::String>().Utf8Value();

        try
        {
            // Parse JSON string using JavaScript's JSON.parse
            Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
            Napi::Function parse = JSON.Get("parse").As<Napi::Function>();
            Napi::Object stateObj = parse.Call(JSON, {Napi::String::New(env, stateJson)}).As<Napi::Object>();

            // Validate state object has required fields
            if (!stateObj.Has("stages"))
            {
                Napi::Error::New(env, "Invalid state: missing 'stages' field")
                    .ThrowAsJavaScriptException();
                return Napi::Boolean::New(env, false);
            }

            // Get stages array
            Napi::Array stagesArray = stateObj.Get("stages").As<Napi::Array>();
            uint32_t stageCount = stagesArray.Length();

            // Validate stage count matches
            if (stageCount != m_stages.size())
            {
                Napi::Error::New(env, "Stage count mismatch: expected " +
                                          std::to_string(m_stages.size()) + " but got " + std::to_string(stageCount))
                    .ThrowAsJavaScriptException();
                return Napi::Boolean::New(env, false);
            }

            // Log restoration
            std::cout << "Restoring pipeline state with " << stageCount << " stages" << std::endl;

            // Restore each stage's state
            for (uint32_t i = 0; i < stageCount; ++i)
            {
                Napi::Object stageConfig = stagesArray.Get(i).As<Napi::Object>();
                if (stageConfig.Has("state"))
                {
                    Napi::Object stageState = stageConfig.Get("state").As<Napi::Object>();
                    m_stages[i]->deserializeState(stageState);
                }
            }

            std::cout << "State restoration complete!" << std::endl;

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

        // Reset all stages
        for (auto &stage : m_stages)
        {
            stage->reset();
        }

        std::cout << "Pipeline state cleared (" << m_stages.size() << " stages reset)" << std::endl;

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

} // namespace dsp

// Forward declare FFT bindings init
namespace dsp
{
    void InitFftBindings(Napi::Env env, Napi::Object exports);
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

    // Initialize utility functions (dot product, etc.)
    dsp::bindings::InitUtilityBindings(env, exports);

    return exports;
}

// This line registers the module
NODE_API_MODULE(dsp_addon, InitAll)