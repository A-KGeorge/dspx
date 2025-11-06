#ifndef DSPX_FFTBATCHPROCESSOR_H
#define DSPX_FFTBATCHPROCESSOR_H

#include "FftEngine.h"
#include "FftCache.h"
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <unordered_map>

namespace dsp
{
    namespace core
    {

        /**
         * Parallel batch FFT processor with result caching
         * Processes multiple FFTs concurrently using a thread pool
         *
         * Template parameter T is the numeric type (float or double)
         */
        template <typename T>
        class FftBatchProcessor
        {
        public:
            using Complex = std::complex<T>;

            struct BatchJob
            {
                const T *input;   // Input signal
                Complex *output;  // Output buffer (caller allocates)
                size_t length;    // Length of input
                bool isRealInput; // true for RFFT, false for complex FFT
                bool forward;     // true for forward FFT, false for inverse
            };

            /**
             * Constructor
             * @param numThreads Number of worker threads (0 = auto-detect physical cores)
             * @param enableCache Enable FFT result caching
             * @param cacheSize Maximum cache entries
             */
            explicit FftBatchProcessor(size_t numThreads = 0, bool enableCache = true,
                                       size_t cacheSize = 128)
                : m_running(false), m_hits(0), m_misses(0)
            {

                // Auto-detect physical cores (hardware_concurrency typically returns logical cores)
                if (numThreads == 0)
                {
                    unsigned int hwThreads = std::thread::hardware_concurrency();
                    // Assume 2 logical cores per physical core (hyperthreading)
                    numThreads = hwThreads > 0 ? (hwThreads + 1) / 2 : 4;
                    if (numThreads < 1)
                        numThreads = 1;
                }

                m_numThreads = numThreads;

                // Create cache if enabled
                if (enableCache)
                {
                    m_cache = std::make_unique<FftCache<T>>(cacheSize);
                }

                // Start worker threads
                start();
            }

            ~FftBatchProcessor()
            {
                stop();
            }

            /**
             * Process a batch of FFTs in parallel
             * @param jobs Array of batch jobs
             * @param numJobs Number of jobs
             * @param wait If true, wait for all jobs to complete before returning
             */
            void processBatch(BatchJob *jobs, size_t numJobs, bool wait = true)
            {
                if (numJobs == 0)
                    return;

                // Submit jobs to queue
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    for (size_t i = 0; i < numJobs; ++i)
                    {
                        m_jobQueue.push(&jobs[i]);
                    }
                    m_jobsSubmitted += numJobs;
                }

                // Wake up worker threads
                m_queueCondition.notify_all();

                // Wait for completion if requested
                if (wait)
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_completionCondition.wait(lock, [this, numJobs]()
                                               { return m_jobsCompleted >= m_jobsSubmitted; });
                    m_jobsSubmitted = 0;
                    m_jobsCompleted = 0;
                }
            }

            /**
             * Convenience method: process batch of real-valued FFTs
             * @param inputs Array of input signals
             * @param outputs Array of output buffers (caller allocates)
             * @param lengths Array of input lengths
             * @param numSignals Number of signals
             */
            void processRfftBatch(const T **inputs, Complex **outputs,
                                  const size_t *lengths, size_t numSignals)
            {
                std::vector<BatchJob> jobs(numSignals);
                for (size_t i = 0; i < numSignals; ++i)
                {
                    jobs[i].input = inputs[i];
                    jobs[i].output = outputs[i];
                    jobs[i].length = lengths[i];
                    jobs[i].isRealInput = true;
                    jobs[i].forward = true;
                }
                processBatch(jobs.data(), numSignals, true);
            }

            /**
             * Get cache statistics
             */
            double getCacheHitRate() const
            {
                if (m_cache)
                {
                    return m_cache->hitRate();
                }
                return 0.0;
            }

            size_t getCacheHits() const
            {
                return m_cache ? m_cache->hits() : 0;
            }

            size_t getCacheMisses() const
            {
                return m_cache ? m_cache->misses() : 0;
            }

            void clearCache()
            {
                if (m_cache)
                {
                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                    m_cache->clear();
                }
            }

            size_t getNumThreads() const
            {
                return m_numThreads;
            }

        private:
            void start()
            {
                m_running = true;
                m_jobsSubmitted = 0;
                m_jobsCompleted = 0;

                for (size_t i = 0; i < m_numThreads; ++i)
                {
                    m_workers.emplace_back(&FftBatchProcessor::workerThread, this);
                }
            }

            void stop()
            {
                // Signal workers to stop
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_running = false;
                }
                m_queueCondition.notify_all();

                // Wait for workers to finish
                for (auto &worker : m_workers)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }
                m_workers.clear();
            }

            void workerThread()
            {
                // Each worker has its own FFT engines (one per FFT size)
                // This avoids locking during computation
                std::unordered_map<size_t, std::unique_ptr<FftEngine<T>>> engines;

                while (true)
                {
                    BatchJob *job = nullptr;

                    // Get next job from queue
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_queueCondition.wait(lock, [this]()
                                              { return !m_running || !m_jobQueue.empty(); });

                        if (!m_running && m_jobQueue.empty())
                        {
                            break; // Exit thread
                        }

                        if (!m_jobQueue.empty())
                        {
                            job = m_jobQueue.front();
                            m_jobQueue.pop();
                        }
                    }

                    if (!job)
                        continue;

                    // Check cache (with cache mutex)
                    bool cacheHit = false;
                    if (m_cache && job->isRealInput && job->forward)
                    {
                        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
                        cacheHit = m_cache->lookup(job->input, job->length,
                                                   job->isRealInput, job->output);
                    }

                    // Compute FFT if cache miss
                    if (!cacheHit)
                    {
                        // Get or create FFT engine for this size
                        auto &engine = engines[job->length];
                        if (!engine)
                        {
                            engine = std::make_unique<FftEngine<T>>(job->length);
                        }

                        // Compute FFT (no locking needed - each worker has its own engines)
                        if (job->isRealInput)
                        {
                            if (job->forward)
                            {
                                engine->rfft(job->input, job->output);
                            }
                            else
                            {
                                engine->irfft(reinterpret_cast<const typename FftEngine<T>::Complex *>(job->input),
                                              reinterpret_cast<T *>(job->output));
                            }
                        }
                        else
                        {
                            if (job->forward)
                            {
                                engine->fft(reinterpret_cast<const typename FftEngine<T>::Complex *>(job->input),
                                            reinterpret_cast<typename FftEngine<T>::Complex *>(job->output));
                            }
                            else
                            {
                                engine->ifft(reinterpret_cast<const typename FftEngine<T>::Complex *>(job->input),
                                             reinterpret_cast<typename FftEngine<T>::Complex *>(job->output));
                            }
                        }

                        // Store result in cache
                        if (m_cache && job->isRealInput && job->forward)
                        {
                            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
                            size_t outputSize = job->length / 2 + 1; // RFFT output size
                            m_cache->store(job->input, job->length, job->isRealInput,
                                           job->output, outputSize);
                        }
                    }

                    // Mark job as completed
                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_jobsCompleted++;
                        if (m_jobsCompleted >= m_jobsSubmitted)
                        {
                            m_completionCondition.notify_all();
                        }
                    }
                }
            }

            size_t m_numThreads;
            std::vector<std::thread> m_workers;

            std::queue<BatchJob *> m_jobQueue;
            std::mutex m_queueMutex;
            std::condition_variable m_queueCondition;
            std::condition_variable m_completionCondition;

            bool m_running;
            size_t m_jobsSubmitted;
            size_t m_jobsCompleted;

            // FFT result cache (shared by all workers)
            std::unique_ptr<FftCache<T>> m_cache;
            std::mutex m_cacheMutex; // Protects cache access

            // Statistics
            size_t m_hits;
            size_t m_misses;
        };

    } // namespace core
} // namespace dsp

#endif // DSPX_FFTBATCHPROCESSOR_H
