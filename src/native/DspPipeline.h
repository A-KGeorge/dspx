#pragma once
#include <napi.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <atomic>
#include "IDspStage.h"

namespace dsp
{
    class DspPipeline : public Napi::ObjectWrap<DspPipeline>
    {
    public:
        // N-API boilerplate
        static Napi::Object Init(Napi::Env env, Napi::Object exports);
        DspPipeline(const Napi::CallbackInfo &info);

    private:
        // This is the "factory" method called by the TS builder
        Napi::Value AddStage(const Napi::CallbackInfo &info);
        Napi::Value AddFilterStage(const Napi::CallbackInfo &info);

        // This is the async "process" method called by the TS processor
        Napi::Value ProcessAsync(const Napi::CallbackInfo &info);

        // The Synchronous method for JS workers/clusters
        Napi::Value ProcessSync(const Napi::CallbackInfo &info);

        // Dispose method for explicit teardown
        Napi::Value Dispose(const Napi::CallbackInfo &info);

        // State management methods (for Redis persistence from TypeScript)
        Napi::Value SaveState(const Napi::CallbackInfo &info);
        Napi::Value LoadState(const Napi::CallbackInfo &info);
        Napi::Value ClearState(const Napi::CallbackInfo &info);
        Napi::Value ListState(const Napi::CallbackInfo &info);

        // Initialize the stage factory map
        void InitializeStageFactories();

        // Type alias for stage factory functions
        using StageFactory = std::function<std::unique_ptr<IDspStage>(const Napi::Object &)>;

        // Map of stage names to factory functions
        std::unordered_map<std::string, StageFactory> m_stageFactories;

        // This is the "pipeline": a vector of our abstract filter stages
        std::vector<std::unique_ptr<IDspStage>> m_stages;

        // uses shared_ptr so the lock survices if the JS object is GC'd early
        std::shared_ptr<std::atomic<bool>> m_isBusy;

        // Flag to track if pipeline has been disposed
        bool m_disposed = false;
    };

} // namespace dsp